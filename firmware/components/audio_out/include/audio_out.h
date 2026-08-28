#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Synthesized tones for persona chirps — no assets. Blocking; call from a
 * non-UI task. Mic input lives in the separate audio_in component.
 *
 * Three interchangeable backends, picked at build time under
 * "MooBot Board -> Audio out" — the tone API below is identical for all:
 *
 *   MOO_AUDIO_BACKEND_I2S   MAX98357A-class DAC + amp, 3 wires. Real
 *                           16-bit PCM, cleanest output.
 *   MOO_AUDIO_BACKEND_PDM   Single-wire analog speaker module fed by the
 *                           S3's hardware sigma-delta modulator. Also real
 *                           16-bit PCM, on one pin, no extra hardware.
 *   MOO_AUDIO_BACKEND_LEDC  Same single wire driven by a PWM carrier.
 *                           Tones are square waves, harsher timbre. PCM
 *                           plays as a PWM-DAC: 8-bit duty at a 312.5 kHz
 *                           carrier, so no external filter is needed.
 *
 * All three can play PCM. LEDC is 8-bit and costs an interrupt at the
 * sample rate; the other two are 16-bit and DMA-fed.
 */

esp_err_t audio_out_init(void);

/* Global output volume 0.0..1.0, scales every tone. Default from NVS. */
void audio_out_set_volume(float volume);

/* Volume as 1..100. adjust() steps in RAM (for live ramping); commit()
 * persists the current level to NVS. get() reads the current level. */
uint8_t audio_out_volume_get(void);
uint8_t audio_out_volume_adjust(int delta);
void audio_out_volume_commit(void);

/* Linear frequency sweep f0→f1 Hz, amplitude 0.0..1.0. Richer than a pure
 * sine (adds harmonics) so it carries louder on the small speaker. */
esp_err_t audio_out_tone(float f0_hz, float f1_hz, uint32_t duration_ms, float amplitude);

/* Distinct signature per event. Whole melody plays atomically. */
typedef enum {
    SND_PAT,          /* head pat — soft coo */
    SND_HAPPY,        /* habit checked — bright ding-ding */
    SND_CELEBRATE,    /* all habits done — ascending fanfare */
    SND_SAD,          /* streak broken — gentle descending */
    SND_ALERT,        /* reminder fired — attention chime (repeats) */
    SND_ACK,          /* reminder acknowledged — short blip up */
    SND_SCREEN,       /* screen change — tick */
} moo_sound_t;

esp_err_t audio_out_play(moo_sound_t sound);

/* Play raw mono 16-bit little-endian PCM, scaled by the global volume.
 * Blocks for the length of the clip.
 *
 * Takes bytes rather than an int16_t* on purpose: the usual source is an
 * EMBED_FILES blob, and IDF's embed script emits it with no .align
 * directive — the symbol can land on an odd address, where a 16-bit load
 * traps on Xtensa. An odd trailing byte is dropped.
 *
 * rate_hz retunes the output clock for the clip and restores the tone rate
 * afterwards, so no resampling is needed. Valid 8000..48000. */
esp_err_t audio_out_play_pcm(const void *pcm_s16le, size_t bytes, uint32_t rate_hz);

/* Streaming form, for audio that arrives faster than it is played and is
 * too big to hold — a TTS story is megabytes of PCM. write() blocks while
 * the buffer is full, which is what stops the network side racing ahead of
 * the speaker; no separate flow control is needed.
 *
 * The output is locked from begin() to end(), so tones and other clips wait.
 * EVERY begin() must be matched with an end(), error paths included, or
 * audio stays locked for the rest of the session.
 *
 * write() may be handed any byte count: an odd trailing byte is carried
 * into the next call rather than dropped, so base64-sized chunks are fine. */
esp_err_t audio_out_pcm_begin(uint32_t rate_hz);
esp_err_t audio_out_pcm_write(const void *pcm_s16le, size_t bytes);
esp_err_t audio_out_pcm_end(void);

/* Back-compat aliases. */
esp_err_t audio_out_pat_sound(void);
esp_err_t audio_out_alert_sound(void);

#ifdef __cplusplus
}
#endif
