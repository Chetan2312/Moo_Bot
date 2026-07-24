#pragma once

#include <stdbool.h>

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

typedef enum {
    FACE_REACT_PAT,        /* head touched */
    FACE_REACT_HAPPY,      /* habit checked */
    FACE_REACT_CELEBRATE,  /* all habits done / streak milestone */
    FACE_REACT_SAD,        /* streak broken at day rollover */
    FACE_REACT_SLEEPY,     /* night hours (looping; auto-timeout for now) */
    FACE_REACT_ALERT,      /* reminder fired */
    FACE_REACT_TASK_DONE,  /* task completed */
    FACE_REACT_WAKE,       /* morning */
    FACE_REACT_EAT,        /* random idle graze (looping; auto-timeout) */
    FACE_REACT_COUNT,
} face_react_t;

/* Play a reaction, then auto-return to idle. Returns false if a reaction
 * is already in flight or the asset is missing (call ignored).
 * Safe from any task (takes the LVGL lock). */
bool face_ui_react(face_react_t reaction);

/* Cycle face → clock → face. Clock auto-returns to face after 30 s.
 * Safe from any task. */
void face_ui_next_screen(void);

#ifdef __cplusplus
}
#endif
