#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * "Tell me a story" over the Gemini REST API.
 *
 *   hold the screen -> record until you stop talking
 *                   -> Gemini hears the audio and writes a short story
 *                   -> Gemini speaks it
 *                   -> the cow plays it
 *
 * Two round trips. Gemini takes audio natively, so there is no separate
 * speech-to-text service in the chain; the first call both hears the
 * request and writes the answer.
 *
 * Expect 8-13 s from the end of your sentence to the first word out of the
 * speaker. That is fine for a story an adult asks for and a child listens
 * to. It is far too slow to be a conversation, and a toddler will not wait
 * for it — this is deliberately a "press for a story" feature, not a chat.
 *
 * Needs WiFi, a valid clock (TLS), and CONFIG_MOO_GEMINI_API_KEY.
 */

typedef enum {
    GEMINI_STAGE_LISTENING,   /* mic open, waiting for you to speak */
    GEMINI_STAGE_THINKING,    /* uploading audio, waiting for the story */
    GEMINI_STAGE_SPEAKING,    /* audio coming back, playing */
    GEMINI_STAGE_DONE,
    GEMINI_STAGE_FAILED,
} gemini_stage_t;

/* Progress callback, so the face can show what is happening through a
 * ten-second wait. Runs on the gemini task — keep it light. `detail` is a
 * short human-readable string, or NULL. */
typedef void (*gemini_stage_cb_t)(gemini_stage_t stage, const char *detail, void *ctx);

esp_err_t gemini_init(gemini_stage_cb_t cb, void *ctx);

/* Kick off one story. Returns immediately; the work runs on its own task
 * because the round trip is far too long to hold up the gesture task.
 * Returns ESP_ERR_INVALID_STATE if one is already running. */
esp_err_t gemini_ask_story(void);

bool gemini_busy(void);

/* True when an API key was compiled in. */
bool gemini_configured(void);

#ifdef __cplusplus
}
#endif
