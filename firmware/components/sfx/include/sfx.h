#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Synthesized creature noises — no audio assets, no flash cost.
 *
 * Everything is generated from oscillators and shaped noise into a PSRAM
 * buffer, then handed to audio_out_play_pcm(). Needs a backend with a real
 * PCM path (LEDC PWM-DAC, PDM, or I2S); returns ESP_ERR_NOT_SUPPORTED on a
 * tones-only build.
 *
 * Each sound is synthesized on first use and cached, so the first play
 * costs ~60 ms of maths and every play after it is free. That matters:
 * these fire on a toddler's tap, and a response later than ~300 ms stops
 * reading as something he caused.
 *
 * Blocking for the length of the sound. Call from a normal task.
 */

typedef enum {
    SFX_MOO,          /* the full hello — ~0.9 s */
    SFX_MOO_SHORT,    /* a little answering moo */
    SFX_MUNCH,        /* grazing, four chews */
    SFX_YAWN,         /* sleepy stretch */
    SFX_GIGGLE,       /* delighted, four rising blips */
    SFX_COUNT,
} sfx_id_t;

esp_err_t sfx_play(sfx_id_t id);

/* Synthesize every sound up front (~300 ms total) so the first tap of each
 * is as fast as the rest. Optional — sfx_play() generates on demand. */
esp_err_t sfx_prewarm(void);

#ifdef __cplusplus
}
#endif
