#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REMINDER_MAX       16
#define REMINDER_TEXT_LEN  32

typedef struct {
    uint8_t hh;                     /* 0-23 local */
    uint8_t mm;                     /* 0-59 */
    uint8_t days_mask;              /* bit0=Sun .. bit6=Sat, 0x7F daily */
    bool enabled;
    char text[REMINDER_TEXT_LEN];
} moo_reminder_t;

/* Fires from the reminder task when local time matches an enabled slot. */
typedef void (*reminder_fire_cb_t)(const moo_reminder_t *r, void *ctx);

/* Loads NVS, starts minute-tick task. System time must be valid. */
esp_err_t reminder_store_init(reminder_fire_cb_t cb, void *ctx);

esp_err_t reminder_store_add(const moo_reminder_t *r);
esp_err_t reminder_store_clear_all(void);
size_t reminder_store_count(void);

/* Pending = fired, not yet acknowledged (long-press). */
bool reminder_store_pending(void);
void reminder_store_ack(void);

#ifdef __cplusplus
}
#endif
