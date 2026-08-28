#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EXT_REMINDER_MAX      32
#define EXT_REMINDER_TITLE_LEN 48

typedef enum {
    EXT_SRC_CALENDAR = 0,
    EXT_SRC_TASK,
} ext_source_t;

typedef struct {
    int64_t due_epoch;                        /* fire at/after this UTC epoch */
    char title[EXT_REMINDER_TITLE_LEN];
    ext_source_t source;
    bool fired;                               /* already alerted */
} moo_ext_reminder_t;

/* Fires from the ext_reminders task when a reminder's time is reached. */
typedef void (*ext_reminder_cb_t)(const moo_ext_reminder_t *r, void *ctx);

/* Starts the minute-tick task. Needs valid system time (waits for it). */
esp_err_t ext_reminders_init(ext_reminder_cb_t cb, void *ctx);

/* Replace the whole set (called after each Google poll). Preserves the
 * `fired` flag for entries that match an existing title+due_epoch, so a
 * re-poll doesn't re-alert something already shown. */
esp_err_t ext_reminders_replace(const moo_ext_reminder_t *list, size_t n);

size_t ext_reminders_count(void);
const moo_ext_reminder_t *ext_reminders_get(size_t idx);

/* Pending = fired, not yet acknowledged (tap). */
bool ext_reminders_pending(void);
void ext_reminders_ack(void);

#ifdef __cplusplus
}
#endif
