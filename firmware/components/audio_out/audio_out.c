#include "audio_out.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#if CONFIG_MOO_AUDIO_BACKEND_LEDC
#include "driver/gptimer.h"
#include "driver/ledc.h"
#include "hal/ledc_ll.h"
#elif CONFIG_MOO_AUDIO_BACKEND_PDM
#include "driver/i2s_pdm.h"
#else
#include "driver/i2s_std.h"
#endif

static const char *TAG = "audio_out";

#define NVS_NS          "moo"
#define NVS_VOL_KEY     "vol"

static bool s_ready;
static float s_volume = 1.0f;       /* default max; NVS overrides if set */
static uint8_t s_vol_pct = 100;
static SemaphoreHandle_t s_lock;    /* serialize concurrent tone callers */

void audio_out_set_volume(float volume)
{
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    s_volume = volume;
    s_vol_pct = (uint8_t)(volume * 100.0f + 0.5f);
}

uint8_t audio_out_volume_get(void)
{
    return s_vol_pct;
}

uint8_t audio_out_volume_adjust(int delta)
{
    int v = (int)s_vol_pct + delta;
    if (v < 1) v = 1;
    if (v > 100) v = 100;
    s_vol_pct = (uint8_t)v;
    s_volume = v / 100.0f;
    return s_vol_pct;
}

void audio_out_volume_commit(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_VOL_KEY, s_vol_pct);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void volume_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t v;
        if (nvs_get_u8(h, NVS_VOL_KEY, &v) == ESP_OK && v >= 1 && v <= 100) {
            s_vol_pct = v;
            s_volume = v / 100.0f;
        }
        nvs_close(h);
    }
}

/* ---------------------------------------------------------------------------
 * Backends. Each provides backend_init() and tone_locked(); everything above
 * and below this block is shared. tone_locked() is always called with s_lock
 * held and with `amplitude` already scaled by the global volume.
 * ------------------------------------------------------------------------ */

#if !CONFIG_MOO_AUDIO_BACKEND_LEDC
/* --- I2S: real 16-bit PCM. Two flavours behind one channel handle --------
 *
 *   MOO_AUDIO_BACKEND_I2S  standard 3-wire I2S into a MAX98357A-class
 *                          DAC + amp.
 *   MOO_AUDIO_BACKEND_PDM  hardware sigma-delta on ONE wire into the
 *                          single-signal analog speaker module. The S3 has
 *                          no DAC, so this is the only way to get true PCM
 *                          out of a 3-wire module.
 *
 * Only backend_init() differs — below it both are an i2s_chan_handle_t fed
 * int16 mono samples by i2s_channel_write().
 * ------------------------------------------------------------------------ */

#define SAMPLE_RATE_HZ  22050
#define CHUNK_SAMPLES   256

static i2s_chan_handle_t s_tx;
static uint32_t s_rate_hz = SAMPLE_RATE_HZ;   /* clock the channel runs at now */

static esp_err_t backend_init(void)
{
    /* Pinned to I2S0 on purpose: audio_in owns I2S1. I2S_NUM_AUTO used to
     * work only because nothing else claimed a controller. PDM is I2S0-only
     * on the S3 anyway (i2s_pdm.c rejects I2S1). */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;   /* zero DMA bufs after send, else last chunk loops forever */
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, NULL), TAG, "chan create");

#if CONFIG_MOO_AUDIO_BACKEND_PDM
    /* DAC line mode, not codec: one line carrying one slot, which is what an
     * analog amp input wants. Codec mode interleaves both slots on the wire
     * and an amp reproduces that as noise. */
    const i2s_pdm_tx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_TX_CLK_DAC_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        .slot_cfg = I2S_PDM_TX_SLOT_DAC_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                       I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = I2S_GPIO_UNUSED,        /* single wire — nothing clocks us */
            .dout = CONFIG_MOO_PIN_SPEAKER_PWM,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_pdm_tx_mode(s_tx, &pdm_cfg), TAG, "pdm init");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "chan enable");

    ESP_LOGI(TAG, "PDM speaker up: sig=%d @ %d Hz (RC low-pass 1k+10nF on SIGNAL)",
             CONFIG_MOO_PIN_SPEAKER_PWM, SAMPLE_RATE_HZ);
#else
    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_MOO_PIN_I2S_AMP_BCLK,
            .ws = CONFIG_MOO_PIN_I2S_AMP_LRCLK,
            .dout = CONFIG_MOO_PIN_I2S_AMP_DIN,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std_cfg), TAG, "std init");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "chan enable");

    ESP_LOGI(TAG, "MAX98357A up: bclk=%d lrclk=%d din=%d @ %d Hz",
             CONFIG_MOO_PIN_I2S_AMP_BCLK, CONFIG_MOO_PIN_I2S_AMP_LRCLK,
             CONFIG_MOO_PIN_I2S_AMP_DIN, SAMPLE_RATE_HZ);
#endif
    return ESP_OK;
}

/* Clips arrive at whatever rate they were made at (16 kHz mic captures,
 * 24 kHz from a TTS service, 22050 for our tones). Retune the channel
 * clock instead of resampling. The channel must be stopped to reconfigure. */
static esp_err_t set_rate_locked(uint32_t hz)
{
    if (hz == s_rate_hz) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_tx), TAG, "disable");
#if CONFIG_MOO_AUDIO_BACKEND_PDM
    const i2s_pdm_tx_clk_config_t clk = I2S_PDM_TX_CLK_DAC_DEFAULT_CONFIG(hz);
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_pdm_tx_clock(s_tx, &clk), TAG, "pdm clk");
#else
    const i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(hz);
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(s_tx, &clk), TAG, "std clk");
#endif
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "enable");
    s_rate_hz = hz;
    return ESP_OK;
}

static esp_err_t pcm_begin_locked(uint32_t rate_hz)
{
    return set_rate_locked(rate_hz);
}

static esp_err_t pcm_feed_locked(const uint8_t *pcm, size_t bytes, float amp)
{
    int16_t chunk[CHUNK_SAMPLES];

    for (size_t done = 0; done < bytes; ) {
        size_t n = bytes - done;
        if (n > sizeof(chunk)) {
            n = sizeof(chunk);
        }
        /* memcpy rather than casting `pcm` to int16_t*: EMBED_FILES emits
         * the blob with no .align directive, so the source may sit on an
         * odd address and a 16-bit load off it traps. Volume has to be
         * applied into RAM anyway. */
        memcpy(chunk, pcm + done, n);
        if (amp < 1.0f) {
            for (size_t i = 0; i < n / sizeof(int16_t); i++) {
                chunk[i] = (int16_t)(chunk[i] * amp);
            }
        }

        size_t written = 0;
        const esp_err_t err = i2s_channel_write(s_tx, chunk, n, &written, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "pcm write failed: %s", esp_err_to_name(err));
            return err;
        }
        done += n;
    }
    return ESP_OK;
}

/* Back to the tone rate, else every chirp after a clip plays at the clip's
 * pitch. i2s_channel_write() already blocked until the DMA took everything,
 * so there is nothing left to drain. */
static esp_err_t pcm_end_locked(void)
{
    return set_rate_locked(SAMPLE_RATE_HZ);
}

/* Overdriven (near-square) wave: hard clipping pushes RMS toward full
 * scale, ~2x louder than a sine at the same peak — needed to get real
 * volume out of the little speaker. DRIVE trades loudness vs harshness. */
#define TONE_DRIVE 3.0f

static esp_err_t tone_locked(float f0_hz, float f1_hz, uint32_t duration_ms, float amplitude)
{
    const uint32_t total = (uint32_t)((uint64_t)SAMPLE_RATE_HZ * duration_ms / 1000);
    int16_t chunk[CHUNK_SAMPLES];
    float phase = 0.0f;

    for (uint32_t done = 0; done < total; ) {
        const uint32_t n = (total - done) < CHUNK_SAMPLES ? (total - done) : CHUNK_SAMPLES;
        for (uint32_t i = 0; i < n; i++) {
            const float t = (float)(done + i) / (float)total;
            const float f = f0_hz + (f1_hz - f0_hz) * t;
            phase += 2.0f * (float)M_PI * f / (float)SAMPLE_RATE_HZ;

            /* overdrive then hard-clip -> loud near-square */
            float s = sinf(phase) * TONE_DRIVE;
            if (s > 1.0f) s = 1.0f;
            else if (s < -1.0f) s = -1.0f;

            float env = 1.0f;
            const uint32_t k = done + i;
            const uint32_t ramp = SAMPLE_RATE_HZ / 200;  /* 5 ms */
            if (k < ramp) env = (float)k / ramp;
            else if (total - k < ramp) env = (float)(total - k) / ramp;

            chunk[i] = (int16_t)(s * amplitude * env * 32767.0f);
        }
        size_t written = 0;
        const esp_err_t err = i2s_channel_write(s_tx, chunk, n * sizeof(int16_t),
                                                &written, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2s write failed: %s", esp_err_to_name(err));
            return err;
        }
        done += n;
    }
    return ESP_OK;
}

#else
/* --- LEDC: PWM carrier into a single-wire analog speaker module ----------- */

#define PWM_MODE        LEDC_LOW_SPEED_MODE   /* S3 has no high-speed mode */
#define PWM_TIMER       LEDC_TIMER_0
#define PWM_CHANNEL     LEDC_CHANNEL_0
#define PWM_RES_BITS    LEDC_TIMER_10_BIT
#define PWM_DUTY_SQUARE 512                   /* 50 % of 1<<10 = max RMS */

/* 10-bit resolution on the 80 MHz APB clock: the fractional divider bottoms
 * out near 76 Hz. Every note we play is >= 523 Hz, so this only guards
 * against a caller passing nonsense. */
#define PWM_FREQ_MIN_HZ 80.0f
#define PWM_FREQ_MAX_HZ 20000.0f

#define STEP_MS         2     /* sweep + envelope granularity (tick = 1 ms) */
#define RAMP_MS         6     /* de-click attack / release */

static esp_err_t backend_init(void)
{
    const ledc_timer_config_t tcfg = {
        .speed_mode      = PWM_MODE,
        .timer_num       = PWM_TIMER,
        .duty_resolution = PWM_RES_BITS,
        .freq_hz         = 1000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&tcfg), TAG, "ledc timer");

    const ledc_channel_config_t ccfg = {
        .gpio_num   = CONFIG_MOO_PIN_SPEAKER_PWM,
        .speed_mode = PWM_MODE,
        .channel    = PWM_CHANNEL,
        .timer_sel  = PWM_TIMER,
        .duty       = 0,           /* silent until a tone asks otherwise */
        .hpoint     = 0,
        .intr_type  = LEDC_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ccfg), TAG, "ledc channel");

    ESP_LOGI(TAG, "PWM speaker up: sig=%d (LEDC 10-bit, low-speed)",
             CONFIG_MOO_PIN_SPEAKER_PWM);
    return ESP_OK;
}

/* Duty is the only loudness knob a square wave gives us: 50 % is a full
 * square (maximum acoustic power), narrower pulses carry less energy.
 * Monotonic and click-free, but not a linear dB taper — the module's
 * trimpot is still the coarse control. */
static void duty_set(float amp)
{
    if (amp < 0.0f) amp = 0.0f;
    if (amp > 1.0f) amp = 1.0f;
    ledc_set_duty(PWM_MODE, PWM_CHANNEL, (uint32_t)(PWM_DUTY_SQUARE * amp + 0.5f));
    ledc_update_duty(PWM_MODE, PWM_CHANNEL);
}

static void freq_set(float hz)
{
    if (hz < PWM_FREQ_MIN_HZ) hz = PWM_FREQ_MIN_HZ;
    if (hz > PWM_FREQ_MAX_HZ) hz = PWM_FREQ_MAX_HZ;
    ledc_set_freq(PWM_MODE, PWM_TIMER, (uint32_t)hz);
}

static esp_err_t tone_locked(float f0_hz, float f1_hz, uint32_t duration_ms, float amplitude)
{
    if (duration_ms == 0) return ESP_OK;

    uint32_t steps = duration_ms / STEP_MS;
    if (steps == 0) steps = 1;

    freq_set(f0_hz);
    for (uint32_t i = 0; i < steps; i++) {
        if (f1_hz != f0_hz) {
            const float t = (steps > 1) ? (float)i / (float)(steps - 1) : 0.0f;
            freq_set(f0_hz + (f1_hz - f0_hz) * t);
        }

        /* Linear attack/release. Without it every note starts and ends on a
         * duty step change, which the speaker reproduces as a click. */
        const uint32_t ms = i * STEP_MS;
        float env = 1.0f;
        if (ms < RAMP_MS)                    env = (float)ms / RAMP_MS;
        else if (duration_ms - ms < RAMP_MS) env = (float)(duration_ms - ms) / RAMP_MS;

        duty_set(amplitude * env);
        vTaskDelay(pdMS_TO_TICKS(STEP_MS));
    }

    duty_set(0.0f);
    return ESP_OK;
}

/* --- PWM-DAC: real PCM out of the same single wire ------------------------
 *
 * The S3 has no DAC, but a fast PWM carrier with a per-sample duty is one.
 * At 8-bit duty the 80 MHz APB caps the carrier at 312.5 kHz — ~20x above
 * the audio band, and low enough that the module's own amplifier bandwidth
 * does the reconstruction filtering. That is the whole reason to do this
 * here rather than in PDM: PDM's 6.14 MHz carrier needs an external RC,
 * this does not, and the module is documented to accept PWM.
 *
 * Cost vs PDM: 8 bits instead of 16 (~48 dB dynamic range), and an
 * interrupt at the sample rate instead of DMA.
 *
 * The ISR pops one duty byte per tick from a ring in internal DRAM. It must
 * never read flash — a concurrent NVS write disables the cache and a
 * flash-resident load would fault — hence the ring rather than reading the
 * embedded clip directly. Same reason it uses ledc_ll_* rather than
 * ledc_set_duty(): that one takes a semaphore and is not ISR-safe.
 * ------------------------------------------------------------------------ */

#define PCM_CARRIER_HZ  312500          /* 80 MHz APB / 2^8 */
#define PCM_RES_BITS    LEDC_TIMER_8_BIT
#define PCM_MID_DUTY    128             /* silence sits at half rail */
#define PCM_RING_LEN    4096            /* pow2; 186 ms of slack at 22 kHz */
#define PCM_FADE_MS     5               /* de-click ramp to and from mid-rail */

static uint8_t s_ring[PCM_RING_LEN];    /* DRAM only — the ISR reads this */
static volatile uint32_t s_ring_r, s_ring_w;   /* SPSC, 32-bit = atomic here */
static gptimer_handle_t s_gpt;

static bool pcm_tick(gptimer_handle_t timer, const gptimer_alarm_event_data_t *ev, void *arg)
{
    (void)timer; (void)ev; (void)arg;
    if (s_ring_r != s_ring_w) {
        const uint8_t d = s_ring[s_ring_r & (PCM_RING_LEN - 1)];
        s_ring_r++;
        /* duty_direction/num/cycle/scale were set once by the ledc_set_duty()
         * in pcm_ramp(); per sample only these three registers move. */
        ledc_ll_set_duty_int_part(&LEDC, PWM_MODE, PWM_CHANNEL, d);
        ledc_ll_set_duty_start(&LEDC, PWM_MODE, PWM_CHANNEL, true);
        ledc_ll_ls_channel_update(&LEDC, PWM_MODE, PWM_CHANNEL);
    }
    return false;                       /* no task woken */
}

/* Step the duty over `ms` at 1 ms per step. Jumping straight to mid-rail
 * pops; the speaker reproduces the step. */
static void pcm_ramp(int from, int to, uint32_t ms)
{
    for (uint32_t i = 1; i <= ms; i++) {
        const int d = from + (to - from) * (int)i / (int)ms;
        ledc_set_duty(PWM_MODE, PWM_CHANNEL, (uint32_t)d);
        ledc_update_duty(PWM_MODE, PWM_CHANNEL);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static bool s_gpt_enabled;

static esp_err_t pcm_begin_locked(uint32_t rate_hz)
{
    /* Tones run 10-bit at the note frequency; PCM wants 8-bit at the
     * carrier. APB is named explicitly because LEDC_AUTO_CLK may settle on
     * a slower source, and 312.5 kHz at 8 bits is then unreachable. */
    const ledc_timer_config_t pcm_timer = {
        .speed_mode      = PWM_MODE,
        .timer_num       = PWM_TIMER,
        .duty_resolution = PCM_RES_BITS,
        .freq_hz         = PCM_CARRIER_HZ,
        .clk_cfg         = LEDC_USE_APB_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&pcm_timer), TAG, "pcm timer");

    s_ring_r = s_ring_w = 0;
    pcm_ramp(0, PCM_MID_DUTY, PCM_FADE_MS);

    const gptimer_config_t gcfg = {
        .clk_src      = GPTIMER_CLK_SRC_DEFAULT,
        .direction    = GPTIMER_COUNT_UP,
        .resolution_hz = 10 * 1000 * 1000,   /* 0.1 us tick -> <0.1% rate error */
    };
    esp_err_t err = gptimer_new_timer(&gcfg, &s_gpt);

    if (err == ESP_OK) {
        const gptimer_event_callbacks_t cbs = { .on_alarm = pcm_tick };
        err = gptimer_register_event_callbacks(s_gpt, &cbs, NULL);
    }
    if (err == ESP_OK) {
        const gptimer_alarm_config_t acfg = {
            .alarm_count  = 10000000ULL / rate_hz,
            .reload_count = 0,
            .flags.auto_reload_on_alarm = true,
        };
        err = gptimer_set_alarm_action(s_gpt, &acfg);
    }
    if (err == ESP_OK) {
        err = gptimer_enable(s_gpt);
        s_gpt_enabled = (err == ESP_OK);
    }
    if (err == ESP_OK) {
        err = gptimer_start(s_gpt);
    }
    return err;
}

/* Blocks until every sample has been handed to the ISR's ring. The caller is
 * already blocked for the length of the audio, so a feeder task buys
 * nothing — and for a streamed story this doubles as the backpressure that
 * stops the network side running ahead of the speaker. */
static esp_err_t pcm_feed_locked(const uint8_t *pcm, size_t bytes, float amp)
{
    const size_t total = bytes / sizeof(int16_t);
    size_t done = 0;

    while (done < total) {
        while (done < total && (s_ring_w - s_ring_r) < PCM_RING_LEN) {
            int16_t v;
            /* memcpy: the source may be an odd-aligned EMBED_FILES blob or a
             * base64 decode boundary. */
            memcpy(&v, pcm + done * sizeof(int16_t), sizeof(v));
            const int scaled = (int)(v * amp);
            s_ring[s_ring_w & (PCM_RING_LEN - 1)] = (uint8_t)((scaled + 32768) >> 8);
            s_ring_w++;
            done++;
        }
        if (done < total) {
            vTaskDelay(pdMS_TO_TICKS(5));    /* ring full — let the ISR drain */
        }
    }
    return ESP_OK;
}

static esp_err_t pcm_end_locked(void)
{
    /* Let the ISR finish what is already queued before killing its clock. */
    while (s_gpt && s_ring_r != s_ring_w) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (s_gpt) {
        gptimer_stop(s_gpt);
        if (s_gpt_enabled) {
            gptimer_disable(s_gpt);
            s_gpt_enabled = false;
        }
        gptimer_del_timer(s_gpt);
        s_gpt = NULL;
    }

    pcm_ramp(PCM_MID_DUTY, 0, PCM_FADE_MS);

    const ledc_timer_config_t tone_timer = {
        .speed_mode      = PWM_MODE,
        .timer_num       = PWM_TIMER,
        .duty_resolution = PWM_RES_BITS,
        .freq_hz         = 1000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&tone_timer);
    duty_set(0.0f);
    return ESP_OK;
}

#endif /* backend */

/* ------------------------------------------------------------------------ */

esp_err_t audio_out_init(void)
{
    ESP_RETURN_ON_ERROR(backend_init(), TAG, "backend init");

    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");

    volume_load();
    s_ready = true;
    return ESP_OK;
}

esp_err_t audio_out_tone(float f0_hz, float f1_hz, uint32_t duration_ms, float amplitude)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not init");
    if (amplitude < 0.0f) amplitude = 0.0f;
    if (amplitude > 1.0f) amplitude = 1.0f;
    amplitude *= s_volume;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    const esp_err_t err = tone_locked(f0_hz, f1_hz, duration_ms, amplitude);
    xSemaphoreGive(s_lock);
    return err;
}

/* Streaming playback. The lock is held from begin() to end() so a stream and
 * a chirp can never interleave on the same pin; every begin() must be
 * matched by an end(), including on the error path. */
static bool s_streaming;
static uint8_t s_carry;          /* odd byte left over from the last write */
static bool s_have_carry;

esp_err_t audio_out_pcm_begin(uint32_t rate_hz)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not init");
    ESP_RETURN_ON_FALSE(!s_streaming, ESP_ERR_INVALID_STATE, TAG, "already streaming");
    /* PDM upsampling needs rate/100 <= 480; 48 kHz is the ceiling either way. */
    ESP_RETURN_ON_FALSE(rate_hz >= 8000 && rate_hz <= 48000, ESP_ERR_INVALID_ARG,
                        TAG, "rate %u out of range", (unsigned)rate_hz);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_have_carry = false;

    const esp_err_t err = pcm_begin_locked(rate_hz);
    if (err != ESP_OK) {
        pcm_end_locked();                /* tolerant of a half-built state */
        xSemaphoreGive(s_lock);
        return err;
    }
    s_streaming = true;
    return ESP_OK;
}

esp_err_t audio_out_pcm_write(const void *pcm_s16le, size_t bytes)
{
    ESP_RETURN_ON_FALSE(s_streaming, ESP_ERR_INVALID_STATE, TAG, "not streaming");
    if (!pcm_s16le || !bytes) {
        return ESP_OK;
    }
    const uint8_t *p = (const uint8_t *)pcm_s16le;
    esp_err_t err = ESP_OK;

    /* Chunks arriving from a base64 decoder are multiples of 3, not 2, so
     * roughly half of them end mid-sample. Carrying the odd byte across
     * writes is what keeps the stream from desyncing by one byte and
     * turning the rest of the story into noise. */
    if (s_have_carry) {
        const uint8_t pair[2] = { s_carry, p[0] };
        err = pcm_feed_locked(pair, sizeof(pair), s_volume);
        s_have_carry = false;
        p++;
        bytes--;
    }
    if (err == ESP_OK && (bytes & 1)) {
        s_carry = p[bytes - 1];
        s_have_carry = true;
        bytes--;
    }
    if (err == ESP_OK && bytes) {
        err = pcm_feed_locked(p, bytes, s_volume);
    }
    return err;
}

esp_err_t audio_out_pcm_end(void)
{
    ESP_RETURN_ON_FALSE(s_streaming, ESP_ERR_INVALID_STATE, TAG, "not streaming");
    const esp_err_t err = pcm_end_locked();
    s_streaming = false;
    s_have_carry = false;
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t audio_out_play_pcm(const void *pcm_s16le, size_t bytes, uint32_t rate_hz)
{
    ESP_RETURN_ON_FALSE(pcm_s16le && bytes >= sizeof(int16_t), ESP_ERR_INVALID_ARG,
                        TAG, "bad pcm");
    ESP_RETURN_ON_ERROR(audio_out_pcm_begin(rate_hz), TAG, "begin");

    const esp_err_t err = audio_out_pcm_write(pcm_s16le, bytes);
    const esp_err_t end = audio_out_pcm_end();
    return (err != ESP_OK) ? err : end;
}

/* note: {freq_hz, ms}; freq 0 = rest. Played atomically under s_lock. */
typedef struct { float hz; uint16_t ms; } note_t;

#define C5 523.f
#define D5 587.f
#define E5 659.f
#define G5 784.f
#define A5 880.f
#define C6 1047.f
#define E6 1319.f
#define G6 1568.f

static esp_err_t play_notes(const note_t *seq, size_t n, float amp)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not init");
    amp *= s_volume;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    for (size_t i = 0; i < n && err == ESP_OK; i++) {
        if (seq[i].hz <= 0.0f) {
            vTaskDelay(pdMS_TO_TICKS(seq[i].ms));
        } else {
            err = tone_locked(seq[i].hz, seq[i].hz, seq[i].ms, amp);
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t audio_out_play(moo_sound_t sound)
{
    switch (sound) {
    case SND_PAT: {
        static const note_t s[] = {{G5, 90}, {C6, 110}};
        return play_notes(s, 2, 0.9f);
    }
    case SND_HAPPY: {
        static const note_t s[] = {{E5, 90}, {G5, 90}, {C6, 130}};
        return play_notes(s, 3, 1.0f);
    }
    case SND_CELEBRATE: {
        static const note_t s[] = {{C5, 90}, {E5, 90}, {G5, 90}, {C6, 120},
                                   {0, 40}, {G5, 90}, {C6, 200}};
        return play_notes(s, 7, 1.0f);
    }
    case SND_SAD: {
        static const note_t s[] = {{A5, 160}, {E5, 160}, {C5, 260}};
        return play_notes(s, 3, 0.85f);
    }
    case SND_ALERT: {
        static const note_t s[] = {{C6, 120}, {G5, 120}, {C6, 120}, {0, 120},
                                   {C6, 120}, {G5, 120}, {C6, 160}};
        return play_notes(s, 7, 1.0f);
    }
    case SND_ACK: {
        static const note_t s[] = {{G5, 70}, {C6, 100}};
        return play_notes(s, 2, 0.9f);
    }
    case SND_SCREEN: {
        static const note_t s[] = {{C6, 35}};
        return play_notes(s, 1, 0.7f);
    }
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t audio_out_pat_sound(void)   { return audio_out_play(SND_PAT); }
esp_err_t audio_out_alert_sound(void) { return audio_out_play(SND_ALERT); }
