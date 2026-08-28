/* Host-side streak engine tests. Build & run: make -C host_test */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "streak_engine.h"

#define DAILY 0x7F
#define MWF   ((1u << 1) | (1u << 3) | (1u << 5))   /* Mon Wed Fri */

static int s_pass;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); return 1; } \
    s_pass++; \
} while (0)

static moo_habit_t mk(uint8_t mask)
{
    moo_habit_t h = { .id = 1, .days_mask = mask, .enabled = true };
    strcpy(h.name, "t");
    return h;
}

int main(void)
{
    /* date math sanity: 2026-07-24 is a Friday */
    CHECK(ymd_dow(20260724) == 5);
    CHECK(ymd_add_days(20260724, 1) == 20260725);
    CHECK(ymd_add_days(20260731, 1) == 20260801);
    CHECK(ymd_add_days(20261231, 1) == 20270101);
    CHECK(ymd_add_days(20280228, 1) == 20280229);   /* leap */
    CHECK(ymd_add_days(20260801, -1) == 20260731);

    /* daily: start then extend */
    moo_habit_t h = mk(DAILY);
    CHECK(streak_check_in(&h, 20260720) == CHECK_STREAK_STARTED);
    CHECK(h.streak == 1);
    CHECK(streak_check_in(&h, 20260721) == CHECK_STREAK_EXTENDED);
    CHECK(h.streak == 2);
    CHECK(streak_check_in(&h, 20260721) == CHECK_ALREADY_DONE);
    CHECK(h.streak == 2);

    /* gap without rollover: restart on check-in */
    CHECK(streak_check_in(&h, 20260724) == CHECK_STREAK_STARTED);
    CHECK(h.streak == 1);
    CHECK(h.best_streak == 2);

    /* rollover: anchor, extend, then miss a day */
    h = mk(DAILY);
    CHECK(streak_rollover(&h, 20260720) == false);          /* anchor */
    streak_check_in(&h, 20260720);
    streak_check_in(&h, 20260721);
    CHECK(streak_rollover(&h, 20260722) == false);          /* nothing missed */
    CHECK(h.streak == 2);
    /* 22nd, 23rd not checked; rollover on 24th must break */
    CHECK(streak_rollover(&h, 20260724) == true);
    CHECK(h.streak == 0);
    /* re-rollover same day: no double break */
    CHECK(streak_rollover(&h, 20260724) == false);

    /* multi-day offline gap evaluated in one shot */
    h = mk(DAILY);
    streak_rollover(&h, 20260701);
    streak_check_in(&h, 20260701);
    CHECK(streak_rollover(&h, 20260710) == true);
    CHECK(h.streak == 0);

    /* Mon/Wed/Fri: Mon->Wed extends (Tue not scheduled) */
    h = mk(MWF);
    /* 2026-07-20 = Monday */
    CHECK(ymd_dow(20260720) == 1);
    CHECK(streak_check_in(&h, 20260720) == CHECK_STREAK_STARTED);
    CHECK(streak_check_in(&h, 20260722) == CHECK_STREAK_EXTENDED);   /* Wed */
    CHECK(h.streak == 2);
    /* Tuesday not scheduled */
    CHECK(streak_check_in(&h, 20260721) == CHECK_NOT_SCHEDULED);
    /* rollover across Tue must NOT break */
    CHECK(streak_rollover(&h, 20260723) == false);
    CHECK(h.streak == 2);
    /* skip Fri 24th; rollover Mon 27th breaks */
    CHECK(streak_rollover(&h, 20260727) == true);
    CHECK(h.streak == 0);

    /* checked today, rollover later same day: earlier missed day must not
     * re-break the fresh streak */
    h = mk(DAILY);
    streak_rollover(&h, 20260720);
    streak_check_in(&h, 20260720);                 /* streak 1 */
    /* miss 21st; check in on 22nd first (restarts streak), THEN rollover */
    CHECK(streak_check_in(&h, 20260722) == CHECK_STREAK_STARTED);
    CHECK(streak_rollover(&h, 20260722) == false); /* 21st missed but streak already restarted */
    CHECK(h.streak == 1);

    /* disabled habit: never scheduled */
    h = mk(DAILY);
    h.enabled = false;
    CHECK(streak_check_in(&h, 20260724) == CHECK_NOT_SCHEDULED);

    printf("all %d checks passed\n", s_pass);
    return 0;
}
