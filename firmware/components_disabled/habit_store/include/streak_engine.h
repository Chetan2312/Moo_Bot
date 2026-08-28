#pragma once

/*
 * Pure streak logic — no ESP-IDF dependencies, unit-tested host-side
 * (host_test/). Dates are uint32_t YYYYMMDD in device-local wall time;
 * numeric comparison == chronological comparison.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HABIT_NAME_LEN 24

#define HABIT_NO_REMIND 0xFF           /* remind_hh sentinel: no reminder */

typedef struct {
    uint8_t id;
    char name[HABIT_NAME_LEN];
    uint8_t days_mask;              /* bit0=Sun .. bit6=Sat */
    bool enabled;
    uint8_t remind_hh;             /* 0-23, or HABIT_NO_REMIND */
    uint8_t remind_mm;             /* 0-59 */
    uint16_t streak;
    uint16_t best_streak;
    uint32_t last_checked_ymd;      /* 0 = never */
    uint32_t last_evaluated_ymd;    /* last day judged by rollover, 0 = never */
} moo_habit_t;

typedef enum {
    CHECK_STREAK_STARTED = 0,       /* checked; streak reset to 1 */
    CHECK_STREAK_EXTENDED,          /* checked; streak grew */
    CHECK_ALREADY_DONE,             /* no-op, was checked today */
    CHECK_NOT_SCHEDULED,            /* no-op, not scheduled today / disabled */
} check_result_t;

/* 0=Sun .. 6=Sat */
int ymd_dow(uint32_t ymd);
uint32_t ymd_add_days(uint32_t ymd, int delta);

bool habit_scheduled_on(const moo_habit_t *h, uint32_t ymd);

/* Check in today. Streak extends iff the previous *scheduled* day was
 * checked, else restarts at 1. */
check_result_t streak_check_in(moo_habit_t *h, uint32_t today_ymd);

/* Judge every unjudged day before today (handles multi-day offline gaps).
 * Returns true if the streak broke. First call just anchors evaluation. */
bool streak_rollover(moo_habit_t *h, uint32_t today_ymd);

#ifdef __cplusplus
}
#endif
