#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MAX98357A over I2S. Synthesized tones for persona chirps — no assets.
 * Blocking; call from a non-UI task. Mic (ICS-43434) intentionally not
 * handled yet.
 */

esp_err_t audio_out_init(void);

/* Global output volume 0.0..1.0, scales every tone. Default 1.0.
 * Will be driven by device settings (webapp) later. */
void audio_out_set_volume(float volume);

/* Linear frequency sweep f0→f1 Hz, sine, amplitude 0.0..1.0. */
esp_err_t audio_out_tone(float f0_hz, float f1_hz, uint32_t duration_ms, float amplitude);

/* Two-chirp "happy coo" used on head pat. */
esp_err_t audio_out_pat_sound(void);

/* Two falling chirps — reminder alert. */
esp_err_t audio_out_alert_sound(void);

#ifdef __cplusplus
}
#endif
