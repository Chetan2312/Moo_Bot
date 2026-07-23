#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Demo-1 procedural idle face: eyes + mouth drawn with LVGL primitives,
 * randomized blink and eye saccade so the device reads as "alive".
 * Replaced by anim_engine sprite playback in milestone 2.
 */
void face_ui_start(lv_display_t *disp);

/* Head-pat reaction: happy squint + wide smile for ~1.2 s, then idle.
 * Safe to call from any task (takes the LVGL lock). */
void face_ui_pat(void);

#ifdef __cplusplus
}
#endif
