#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "streak_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HABIT_MAX 12

typedef enum {
    HABIT_EVT_CHECKED,        /* one habit checked in (streak in habit->streak) */
    HABIT_EVT_ALL_DONE,       /* every habit scheduled today is checked */
    HABIT_EVT_STREAK_BROKEN,  /* midnight rollover found a miss */
    HABIT_EVT_REMIND,         /* habit's reminder time reached today */
} habit_evt_t;

/* Fires from habit_store's task (or the caller's task for check-ins). */
typedef void (*habit_evt_cb_t)(habit_evt_t evt, const moo_habit_t *habit, void *ctx);

/* Loads NVS, starts the reminder + midnight-rollover task. Needs valid
 * system time eventually; both wait until time is sane. */
esp_err_t habit_store_init(habit_evt_cb_t cb, void *ctx);

/* remind_hh = HABIT_NO_REMIND for a habit with no time reminder. */
esp_err_t habit_store_add(const char *name, uint8_t days_mask,
                          uint8_t remind_hh, uint8_t remind_mm);
esp_err_t habit_store_remove(uint8_t id);
size_t habit_store_count(void);
const moo_habit_t *habit_store_get(size_t idx);

/* Reminder acknowledge state (long-press on device). */
bool habit_store_reminder_pending(void);
void habit_store_reminder_ack(void);

/* Check in a habit for today. Fires HABIT_EVT_CHECKED (+ ALL_DONE). */
esp_err_t habit_store_check_in(uint8_t id);

/* Convenience: check in the first habit scheduled today and not yet done.
 * Returns false if none left. */
bool habit_store_check_in_next(void);

esp_err_t habit_store_clear_all(void);

#ifdef __cplusplus
}
#endif
