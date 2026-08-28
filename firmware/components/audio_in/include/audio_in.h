#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * INMP441 MEMS microphone on I2S1 (audio_out pins its TX to I2S0).
 *
 * The part emits 24-bit samples MSB-aligned inside 32-bit slots, so the
 * channel is configured 32-bit even though only 24 are meaningful. Reads
 * sign-extend, run a DC blocker, apply CONFIG_MOO_MIC_GAIN_SHIFT and hand
 * back plain int16 mono PCM ready for VAD or an ASR frontend.
 *
 * Wiring: VDD -> 3V3 (5 V destroys it), L/R -> GND for the LEFT slot.
 */

esp_err_t audio_in_init(void);

/* Blocking read of up to max_samples int16 mono samples. Returns however
 * many the DMA had ready (never more than 256 per call). Thread-safe. */
esp_err_t audio_in_read(int16_t *dst, size_t max_samples, size_t *out_samples,
                        uint32_t timeout_ms);

/* RMS of the most recent frame any reader pulled, in dBFS. -90.0 means
 * silence or nothing read yet. */
float audio_in_level_dbfs(void);

/* Absolute peak of the most recent frame, 0..32767. Watch this while
 * tuning CONFIG_MOO_MIC_GAIN_SHIFT — pinned at 32767 means clipping. */
int16_t audio_in_peak(void);

/* Bringup helper: background task that reads continuously and logs level
 * and peak every ~200 ms. Gate it off once the mic is verified. */
esp_err_t audio_in_start_monitor(void);

/* Record one utterance, endpointed by level.
 *
 * Measures the room's noise floor first, then waits up to wait_ms for
 * something louder than floor + CONFIG_MOO_MIC_VAD_MARGIN_DB, records until
 * hang_ms of quiet or max_ms total, and shifts the result to the start of
 * dst. Roughly 200 ms of lead-in before the onset is kept — without it the
 * first syllable is clipped, which matters a lot for a short word.
 *
 * dst must hold (wait_ms + max_ms) worth of samples: everything is captured
 * and then trimmed, so a long silent wait still consumes buffer. At 16 kHz
 * that is 32 KB per second — use PSRAM.
 *
 * Returns ESP_ERR_TIMEOUT if nobody spoke inside wait_ms. Blocking; only
 * one recorder at a time. */
esp_err_t audio_in_record(int16_t *dst, size_t max_samples, size_t *out_samples,
                          uint32_t wait_ms, uint32_t max_ms, uint32_t hang_ms);

#ifdef __cplusplus
}
#endif
