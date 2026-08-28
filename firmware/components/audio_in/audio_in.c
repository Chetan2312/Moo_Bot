#include "audio_in.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "audio_in";

#define FRAME_SAMPLES   256     /* 16 ms at 16 kHz */

static i2s_chan_handle_t s_rx;
static int32_t *s_raw;          /* 32-bit slot scratch, reused per read */
static SemaphoreHandle_t s_lock;

static float s_level_dbfs = -90.0f;
static int16_t s_peak;

/* The bringup monitor and audio_in_record() are both plain readers of the
 * same DMA stream: whoever calls audio_in_read() first gets those samples
 * and the other never sees them. Left alone, the monitor silently steals
 * about half of every recording. The recorder claims exclusivity while it
 * runs rather than relying on the monitor being configured off. */
static volatile bool s_recording;

/* One-pole DC blocker: y[n] = x[n] - x[n-1] + R*y[n-1].
 * The INMP441 carries a real DC offset. Without this the frame RMS is
 * dominated by that offset instead of the audio, and any VAD threshold
 * built on top of it never moves. */
#define HP_R 0.995f
static float s_hp_x1, s_hp_y1;

esp_err_t audio_in_init(void)
{
    ESP_RETURN_ON_FALSE(!s_rx, ESP_ERR_INVALID_STATE, TAG, "already init");

    s_raw = malloc(FRAME_SAMPLES * sizeof(int32_t));
    ESP_RETURN_ON_FALSE(s_raw, ESP_ERR_NO_MEM, TAG, "scratch buf");

    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = FRAME_SAMPLES;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &s_rx), TAG, "chan create");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_MOO_MIC_SAMPLE_RATE),
        /* 32-bit slots, not 24: the INMP441 clocks 24 data bits out
         * MSB-first and then idles: a 24-bit slot width mistimes the frame
         * and you get garbage. */
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_MOO_PIN_I2S_MIC_SCK,
            .ws = CONFIG_MOO_PIN_I2S_MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = CONFIG_MOO_PIN_I2S_MIC_SD,
        },
    };
    /* Follows the L/R pin: tied to GND the part talks in the left slot.
     * A Kconfig bool left at 'n' is not defined at all, so the name can
     * only be tested by the preprocessor — hence the mirrored literal. */
#if CONFIG_MOO_MIC_SLOT_RIGHT
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    const char *slot_name = "right";
#else
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    const char *slot_name = "left";
#endif

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &std_cfg), TAG, "std init");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx), TAG, "chan enable");

    ESP_LOGI(TAG, "INMP441 up: sck=%d ws=%d sd=%d @ %d Hz, %s slot, gain +%d dB",
             CONFIG_MOO_PIN_I2S_MIC_SCK, CONFIG_MOO_PIN_I2S_MIC_WS,
             CONFIG_MOO_PIN_I2S_MIC_SD, CONFIG_MOO_MIC_SAMPLE_RATE,
             slot_name, CONFIG_MOO_MIC_GAIN_SHIFT * 6);
    return ESP_OK;
}

esp_err_t audio_in_read(int16_t *dst, size_t max_samples, size_t *out_samples,
                        uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_rx, ESP_ERR_INVALID_STATE, TAG, "not init");
    ESP_RETURN_ON_FALSE(dst && max_samples, ESP_ERR_INVALID_ARG, TAG, "bad dst");
    if (out_samples) *out_samples = 0;

    const size_t want = max_samples < FRAME_SAMPLES ? max_samples : FRAME_SAMPLES;

    xSemaphoreTake(s_lock, portMAX_DELAY);

    size_t got = 0;
    esp_err_t err = i2s_channel_read(s_rx, s_raw, want * sizeof(int32_t), &got,
                                     pdMS_TO_TICKS(timeout_ms));
    if (err != ESP_OK) {
        xSemaphoreGive(s_lock);
        return err;
    }

    const size_t n = got / sizeof(int32_t);
    const float gain = (float)(1 << CONFIG_MOO_MIC_GAIN_SHIFT);
    double sq = 0.0;
    int16_t peak = 0;

    for (size_t i = 0; i < n; i++) {
        /* 24 bits MSB-aligned in 32 -> sign-extended +/-2^23 */
        const float x = (float)(s_raw[i] >> 8);

        const float y = x - s_hp_x1 + HP_R * s_hp_y1;
        s_hp_x1 = x;
        s_hp_y1 = y;

        /* 24-bit down to 16, then the user gain. Clamp: at high gain a
         * loud transient will run past full scale. */
        float s = y * (1.0f / 256.0f) * gain;
        if (s > 32767.0f) s = 32767.0f;
        else if (s < -32768.0f) s = -32768.0f;

        const int16_t v = (int16_t)s;
        dst[i] = v;

        sq += (double)v * (double)v;
        const int16_t a = (v < 0) ? (int16_t)-(v + 1) : v;
        if (a > peak) peak = a;
    }

    if (n) {
        const float rms = sqrtf((float)(sq / (double)n));
        s_level_dbfs = (rms > 1.0f) ? 20.0f * log10f(rms / 32768.0f) : -90.0f;
        s_peak = peak;
    }

    xSemaphoreGive(s_lock);

    if (out_samples) *out_samples = n;
    return ESP_OK;
}

float audio_in_level_dbfs(void) { return s_level_dbfs; }
int16_t audio_in_peak(void)     { return s_peak; }

/* dBFS of one frame, computed here rather than read back from
 * audio_in_level_dbfs() so the VAD does not depend on shared state that
 * another reader could overwrite between calls. */
static float frame_dbfs(const int16_t *s, size_t n)
{
    if (!n) {
        return -90.0f;
    }
    double sq = 0.0;
    for (size_t i = 0; i < n; i++) {
        sq += (double)s[i] * (double)s[i];
    }
    const float rms = sqrtf((float)(sq / (double)n));
    return (rms > 1.0f) ? 20.0f * log10f(rms / 32768.0f) : -90.0f;
}

static esp_err_t record_impl(int16_t *dst, size_t max_samples, size_t *out_samples,
                             uint32_t wait_ms, uint32_t max_ms, uint32_t hang_ms)
{
    const uint32_t frame_ms = 1000u * FRAME_SAMPLES / CONFIG_MOO_MIC_SAMPLE_RATE;

    /* Noise floor: average a few hundred ms of whatever the room is doing.
     * A fixed threshold is useless — the same margin has to work on a quiet
     * desk and next to a fan. */
    float floor_db = 0.0f;
    int floor_n = 0;
    for (int i = 0; i < 20; i++) {
        size_t n = 0;
        if (audio_in_read(dst, FRAME_SAMPLES, &n, 200) == ESP_OK && n) {
            floor_db += frame_dbfs(dst, n);
            floor_n++;
        }
    }
    floor_db = floor_n ? (floor_db / floor_n) : -70.0f;

    float threshold = floor_db + (float)CONFIG_MOO_MIC_VAD_MARGIN_DB;
    if (threshold < -60.0f) {
        threshold = -60.0f;      /* don't trigger on the mic's own hiss */
    }
    ESP_LOGI(TAG, "vad: floor %.1f dBFS, threshold %.1f dBFS", floor_db, threshold);

    size_t total = 0, onset = 0;
    bool speaking = false;
    uint32_t waited = 0, quiet = 0, spoken = 0;
    esp_err_t err = ESP_OK;

    while (total + FRAME_SAMPLES <= max_samples) {
        size_t n = 0;
        err = audio_in_read(dst + total, FRAME_SAMPLES, &n, 500);
        if (err != ESP_OK) {
            return err;
        }
        if (!n) {
            continue;
        }
        const float db = frame_dbfs(dst + total, n);
        total += n;

        if (!speaking) {
            if (db > threshold) {
                speaking = true;
                onset = (total > n) ? (total - n) : 0;
                ESP_LOGI(TAG, "vad: onset at %.1f dBFS", db);
            } else {
                waited += frame_ms;
                if (waited >= wait_ms) {
                    ESP_LOGI(TAG, "vad: nobody spoke");
                    return ESP_ERR_TIMEOUT;
                }
            }
            continue;
        }

        spoken += frame_ms;
        if (db <= threshold) {
            quiet += frame_ms;
            if (quiet >= hang_ms) {
                break;
            }
        } else {
            quiet = 0;
        }
        if (spoken >= max_ms) {
            ESP_LOGI(TAG, "vad: hit the %u ms cap", (unsigned)max_ms);
            break;
        }
    }

    if (!speaking) {
        return ESP_ERR_TIMEOUT;
    }

    /* Keep ~200 ms ahead of the onset. The frame that crosses the threshold
     * is already partway into the first syllable, and starting exactly there
     * chops the beginning off short words. */
    const size_t preroll = (CONFIG_MOO_MIC_SAMPLE_RATE / 5);
    const size_t start = (onset > preroll) ? (onset - preroll) : 0;
    const size_t len = total - start;

    if (start) {
        memmove(dst, dst + start, len * sizeof(int16_t));
    }
    if (out_samples) {
        *out_samples = len;
    }
    ESP_LOGI(TAG, "recorded %u samples (%.1f s)", (unsigned)len,
             (float)len / CONFIG_MOO_MIC_SAMPLE_RATE);
    return ESP_OK;
}

esp_err_t audio_in_record(int16_t *dst, size_t max_samples, size_t *out_samples,
                          uint32_t wait_ms, uint32_t max_ms, uint32_t hang_ms)
{
    ESP_RETURN_ON_FALSE(s_rx, ESP_ERR_INVALID_STATE, TAG, "not init");
    ESP_RETURN_ON_FALSE(dst && max_samples > FRAME_SAMPLES, ESP_ERR_INVALID_ARG,
                        TAG, "bad dst");
    ESP_RETURN_ON_FALSE(!s_recording, ESP_ERR_INVALID_STATE, TAG, "already recording");
    if (out_samples) {
        *out_samples = 0;
    }

    /* Wrapper purely so the flag is cleared on every exit path — the body
     * returns from half a dozen places. */
    s_recording = true;
    const esp_err_t err = record_impl(dst, max_samples, out_samples,
                                      wait_ms, max_ms, hang_ms);
    s_recording = false;
    return err;
}

static void monitor_task(void *arg)
{
    (void)arg;
    int16_t *buf = malloc(FRAME_SAMPLES * sizeof(int16_t));
    if (!buf) {
        ESP_LOGE(TAG, "monitor: no mem");
        vTaskDelete(NULL);
        return;
    }

    uint32_t frames = 0;
    for (;;) {
        /* Back off entirely while a recording is running: every frame this
         * task reads is a frame the recorder never sees. */
        if (s_recording) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        size_t n = 0;
        if (audio_in_read(buf, FRAME_SAMPLES, &n, 200) != ESP_OK || n == 0) {
            ESP_LOGW(TAG, "monitor: no samples (check SCK/WS/SD and L/R)");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        /* 256 samples = 16 ms at 16 kHz; every 12th frame is ~200 ms. */
        if (++frames % 12 == 0) {
            ESP_LOGI(TAG, "level %6.1f dBFS   peak %5d", audio_in_level_dbfs(),
                     audio_in_peak());
        }
    }
}

esp_err_t audio_in_start_monitor(void)
{
    ESP_RETURN_ON_FALSE(s_rx, ESP_ERR_INVALID_STATE, TAG, "not init");
    const BaseType_t ok = xTaskCreate(monitor_task, "mic_mon", 3072, NULL, 4, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
