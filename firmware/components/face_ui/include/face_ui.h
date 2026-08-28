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

/* Cycle face → clock → AI → face. The clock auto-returns to the face after
 * 30 s; the AI screen stays until you move off it. Safe from any task. */
void face_ui_next_screen(void);

/* ---- AI screen ---------------------------------------------------------
 *
 * Status only, in English. The story itself is audio: LVGL ships Montserrat,
 * which has no Devanagari glyphs, and Devanagari needs conjunct/matra
 * shaping that LVGL does not do — so Marathi text on this display would
 * come out as broken boxes rather than words. */

typedef enum {
    AI_IDLE,        /* "Tap to talk" */
    AI_LISTENING,   /* mic open; the level meter is live */
    AI_THINKING,    /* uploaded, waiting on the model */
    AI_SPEAKING,
    AI_FAILED,
} ai_state_t;

/* `detail` is an optional short line under the status, or NULL. */
void face_ui_ai_set_state(ai_state_t state, const char *detail);

/* Called when the AI screen is tapped. Runs in the LVGL task — keep it
 * light. Taps are delivered only while that screen is showing. */
void face_ui_set_ai_tap_cb(void (*cb)(void));

/* Feeds the listening level meter. face_ui polls this ~10x/s while the AI
 * screen shows AI_LISTENING; return 0..100. Registering it here rather than
 * calling audio_in directly keeps face_ui free of an audio dependency. */
void face_ui_set_level_source(uint8_t (*fn)(void));

/* True while the AI screen is the active one, so a tap can be routed to
 * the right place. */
bool face_ui_on_ai_screen(void);

/* Full-screen QR of `url` with a caption, shown for `ms` then auto-returns
 * to the face. Safe from any task. */
void face_ui_show_qr(const char *url, uint32_t ms);

/* Like show_qr but for the Google device-flow: QR of the verification URL
 * plus the short user code shown large beneath. */
void face_ui_show_device_code(const char *url, const char *code, uint32_t ms);

/* Full-screen title + multi-line body (e.g. habit list), shown for `ms`
 * then auto-returns to the face. Safe from any task. */
void face_ui_show_list(const char *title, const char *body, uint32_t ms);

/* Volume bar overlay on the left edge (0..100), floats over the current
 * screen and auto-hides ~1 s after the last update. Safe from any task. */
void face_ui_show_volume(uint8_t pct);

/* Registers a callback fired when the face screen is tapped (touchscreen).
 * The callback runs in the LVGL task — keep it light (e.g. post to a queue).
 * Touch coordinates are logged for calibration. */
void face_ui_set_tap_cb(void (*cb)(void));

/* Fired when the face screen is held (LVGL's long-press, ~400 ms). The tap
 * that would otherwise follow on release is suppressed. Runs in the LVGL
 * task — keep it light. */
void face_ui_set_long_press_cb(void (*cb)(void));

#ifdef __cplusplus
}
#endif
