#include "sfx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "audio_out.h"
#include "esp_check.h"
#include "esp_heap_caps.h"

static const char *TAG = "sfx";

/* 16 kHz divides the GPTimer's 10 MHz tick exactly (625), so the PWM-DAC
 * runs with zero rate error. Everything synthesized here lives below
 * 4 kHz anyway, so there is nothing to gain from a higher rate. */
#define SFX_RATE_HZ     16000

/* ---- moo shape — all of this is meant to be tuned by ear ----------------
 *
 * Pitched as a calf rather than a bull on purpose. A real cow's fundamental
 * sits near 100 Hz, which a small mylar speaker cannot move at all; the
 * fundamental would simply vanish. Starting near 200 Hz puts the whole
 * harmonic stack inside the range this speaker is actually efficient at.
 */
#define MOO_F_START     190.0f     /* the "mmm" onset */
#define MOO_F_PEAK      255.0f     /* the "OOO" body */
#define MOO_F_END       150.0f     /* falling tail */
#define MOO_VIB_HZ      5.5f
#define MOO_VIB_DEPTH   0.030f
#define MOO_FORMANT     720.0f     /* nasal peak — this is what reads as
                                    * "animal throat" instead of "buzzer" */
#define MOO_FORMANT_BW  420.0f
#define MOO_HARMONICS   9

static int16_t *s_cache[SFX_COUNT];
static size_t   s_cache_samples[SFX_COUNT];

/* ------------------------------------------------------------------------ */

static uint32_t rnd(void)
{
    static uint32_t s = 0x13579bdfu;
    s = s * 1664525u + 1013904223u;
    return s;
}

/* -1..1 white noise */
static float noise(void)
{
    return (float)(int32_t)(rnd() >> 8) / 8388608.0f - 1.0f;
}

/* Linear attack/release over a 0..1 progress value. Without a release
 * every sound ends on a step, and the speaker reproduces that as a click. */
static float envelope(float t, float attack, float release)
{
    if (attack > 0.0f && t < attack)        return t / attack;
    if (release > 0.0f && t > 1.0f - release) return (1.0f - t) / release;
    return 1.0f;
}

static int16_t clamp16(float v)
{
    if (v > 32767.0f)  return 32767;
    if (v < -32768.0f) return -32768;
    return (int16_t)v;
}

/* ---- generators --------------------------------------------------------- */

static void gen_moo(int16_t *out, size_t n, float f_peak, float dur_s)
{
    float phase = 0.0f;

    for (size_t i = 0; i < n; i++) {
        const float t = (float)i / (float)n;

        /* Quick rise into the body, long fall out of it — "mmMOOoo". */
        float f0;
        if (t < 0.15f) {
            f0 = MOO_F_START + (f_peak - MOO_F_START) * (t / 0.15f);
        } else if (t < 0.55f) {
            f0 = f_peak;
        } else {
            f0 = f_peak + (MOO_F_END - f_peak) * ((t - 0.55f) / 0.45f);
        }
        f0 *= 1.0f + MOO_VIB_DEPTH * sinf(2.0f * (float)M_PI * MOO_VIB_HZ * t * dur_s);

        phase += 2.0f * (float)M_PI * f0 / SFX_RATE_HZ;
        if (phase > 2.0f * (float)M_PI) {
            phase -= 2.0f * (float)M_PI;   /* keep float precision usable at h=9 */
        }

        /* Additive stack. A plain 1/h rolloff sounds like a doorbell; the
         * fixed formant bump is what turns it into a voice. */
        float s = 0.0f, norm = 0.0f;
        for (int h = 1; h <= MOO_HARMONICS; h++) {
            const float d = (f0 * h - MOO_FORMANT) / MOO_FORMANT_BW;
            const float a = (1.0f / h) * (1.0f + 2.5f * expf(-d * d));
            s += a * sinf(phase * h);
            norm += a;
        }
        s /= (norm > 0.0f) ? norm : 1.0f;

        out[i] = clamp16(s * envelope(t, 0.10f, 0.28f) * 0.92f * 32767.0f);
    }
}

/* Four chews: short bursts of low-passed noise with a soft thump under each. */
static void gen_munch(int16_t *out, size_t n)
{
    const int chews = 4;
    const size_t span = n / chews;
    float lp = 0.0f, phase = 0.0f;

    for (size_t i = 0; i < n; i++) {
        const size_t k = i % span;
        const float ct = (float)k / (float)span;

        /* Each chew is a burst in the first 55% of its slot, then silence. */
        float env = 0.0f;
        if (ct < 0.55f) {
            env = envelope(ct / 0.55f, 0.15f, 0.55f);
        }

        lp += 0.22f * (noise() - lp);                 /* ~1.2 kHz one-pole */
        phase += 2.0f * (float)M_PI * 95.0f / SFX_RATE_HZ;
        const float thump = sinf(phase) * 0.45f;

        const float body = envelope((float)i / (float)n, 0.02f, 0.10f);
        out[i] = clamp16((lp * 1.5f + thump) * env * body * 0.75f * 32767.0f);
    }
}

/* Breathy stretch: a gliding tone buried in a lot of noise, slow either end. */
static void gen_yawn(int16_t *out, size_t n)
{
    float phase = 0.0f, lp = 0.0f;

    for (size_t i = 0; i < n; i++) {
        const float t = (float)i / (float)n;

        const float f0 = (t < 0.55f) ? 170.0f + 120.0f * (t / 0.55f)
                                     : 290.0f - 140.0f * ((t - 0.55f) / 0.45f);
        phase += 2.0f * (float)M_PI * f0 / SFX_RATE_HZ;
        if (phase > 2.0f * (float)M_PI) {
            phase -= 2.0f * (float)M_PI;
        }

        float tone = 0.0f;
        for (int h = 1; h <= 4; h++) {
            tone += sinf(phase * h) / (float)h;
        }
        tone /= 2.08f;                                /* 1 + 1/2 + 1/3 + 1/4 */

        lp += 0.10f * (noise() - lp);                 /* breath */

        out[i] = clamp16((tone * 0.6f + lp * 2.2f) *
                         envelope(t, 0.30f, 0.35f) * 0.8f * 32767.0f);
    }
}

/* Four short rising blips. */
static void gen_giggle(int16_t *out, size_t n)
{
    const int blips = 4;
    const size_t span = n / blips;
    float phase = 0.0f;

    for (size_t i = 0; i < n; i++) {
        const size_t b = i / span;
        const size_t k = i % span;
        const float bt = (float)k / (float)span;

        const float f0 = 520.0f + 110.0f * (float)b;
        phase += 2.0f * (float)M_PI * f0 / SFX_RATE_HZ;
        if (phase > 2.0f * (float)M_PI) {
            phase -= 2.0f * (float)M_PI;
        }

        float env = 0.0f;
        if (bt < 0.45f) {
            env = envelope(bt / 0.45f, 0.20f, 0.50f);
        }

        const float s = sinf(phase) + 0.35f * sinf(phase * 2.0f);
        out[i] = clamp16(s * env * 0.62f * 32767.0f);
    }
}

/* Scale each finished sound to a fixed peak.
 *
 * Two reasons. The output is an 8-bit PWM-DAC, so a sound that only reaches
 * 60% of full scale is throwing away most of a bit it cannot spare. And the
 * noise-based generators have peaks that depend on the RNG, so hand-tuned
 * gain constants either clip or leave headroom on the table. Normalizing
 * afterwards makes all five equally loud and removes the guesswork. */
static void normalize(int16_t *buf, size_t n, float target)
{
    int32_t peak = 0;
    for (size_t i = 0; i < n; i++) {
        const int32_t a = (buf[i] < 0) ? -(int32_t)buf[i] : buf[i];
        if (a > peak) {
            peak = a;
        }
    }
    if (peak <= 0) {
        return;
    }
    const float g = target * 32767.0f / (float)peak;
    for (size_t i = 0; i < n; i++) {
        buf[i] = clamp16((float)buf[i] * g);
    }
}

/* ---- table -------------------------------------------------------------- */

static const struct { uint32_t ms; } s_spec[SFX_COUNT] = {
    [SFX_MOO]       = { 900 },
    [SFX_MOO_SHORT] = { 450 },
    [SFX_MUNCH]     = { 560 },
    [SFX_YAWN]      = { 800 },
    [SFX_GIGGLE]    = { 560 },
};

static esp_err_t sfx_build(sfx_id_t id)
{
    const size_t n = (size_t)((uint64_t)SFX_RATE_HZ * s_spec[id].ms / 1000);

    /* PSRAM explicitly: ~100 KB across all five, and internal RAM is
     * already tight with LVGL and the WiFi/TLS path. */
    int16_t *buf = heap_caps_malloc(n * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!buf) {
        buf = malloc(n * sizeof(int16_t));
    }
    ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "no mem for %d samples", (int)n);

    switch (id) {
    case SFX_MOO:       gen_moo(buf, n, MOO_F_PEAK, s_spec[id].ms / 1000.0f); break;
    case SFX_MOO_SHORT: gen_moo(buf, n, MOO_F_PEAK + 45.0f, s_spec[id].ms / 1000.0f); break;
    case SFX_MUNCH:     gen_munch(buf, n); break;
    case SFX_YAWN:      gen_yawn(buf, n); break;
    case SFX_GIGGLE:    gen_giggle(buf, n); break;
    default:            free(buf); return ESP_ERR_INVALID_ARG;
    }
    normalize(buf, n, 0.94f);

    s_cache[id] = buf;
    s_cache_samples[id] = n;
    ESP_LOGI(TAG, "built sfx %d: %u samples (%u ms)", (int)id, (unsigned)n,
             (unsigned)s_spec[id].ms);
    return ESP_OK;
}

esp_err_t sfx_play(sfx_id_t id)
{
    ESP_RETURN_ON_FALSE(id < SFX_COUNT, ESP_ERR_INVALID_ARG, TAG, "bad id");

    if (!s_cache[id]) {
        ESP_RETURN_ON_ERROR(sfx_build(id), TAG, "build %d", (int)id);
    }
    return audio_out_play_pcm(s_cache[id], s_cache_samples[id] * sizeof(int16_t),
                              SFX_RATE_HZ);
}

esp_err_t sfx_prewarm(void)
{
    esp_err_t err = ESP_OK;
    for (int i = 0; i < SFX_COUNT; i++) {
        if (!s_cache[i]) {
            const esp_err_t e = sfx_build((sfx_id_t)i);
            if (e != ESP_OK) {
                err = e;
            }
        }
    }
    return err;
}
