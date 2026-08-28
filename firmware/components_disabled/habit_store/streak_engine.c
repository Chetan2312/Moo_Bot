#include "streak_engine.h"

/* Howard Hinnant civil-calendar algorithms; epoch 1970-01-01. */

static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int yr = (int)yoe + (int)era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = yr + (*m <= 2);
}

static int64_t ymd_to_days(uint32_t ymd)
{
    return days_from_civil((int)(ymd / 10000), (ymd / 100) % 100, ymd % 100);
}

static uint32_t days_to_ymd(int64_t z)
{
    int y;
    unsigned m, d;
    civil_from_days(z, &y, &m, &d);
    return (uint32_t)y * 10000 + m * 100 + d;
}

int ymd_dow(uint32_t ymd)
{
    const int64_t z = ymd_to_days(ymd);
    return (int)((z >= -4 ? (z + 4) % 7 : (z + 5) % 7 + 6));
}

uint32_t ymd_add_days(uint32_t ymd, int delta)
{
    return days_to_ymd(ymd_to_days(ymd) + delta);
}

bool habit_scheduled_on(const moo_habit_t *h, uint32_t ymd)
{
    return h->enabled && (h->days_mask & (1u << ymd_dow(ymd)));
}

/* Most recent scheduled day strictly before `ymd`; 0 if mask empty. */
static uint32_t prev_scheduled_day(const moo_habit_t *h, uint32_t ymd)
{
    for (int i = 1; i <= 7; i++) {
        const uint32_t d = ymd_add_days(ymd, -i);
        if (habit_scheduled_on(h, d)) {
            return d;
        }
    }
    return 0;
}

check_result_t streak_check_in(moo_habit_t *h, uint32_t today_ymd)
{
    if (!habit_scheduled_on(h, today_ymd)) {
        return CHECK_NOT_SCHEDULED;
    }
    if (h->last_checked_ymd == today_ymd) {
        return CHECK_ALREADY_DONE;
    }

    const uint32_t prev = prev_scheduled_day(h, today_ymd);
    check_result_t res;
    if (prev != 0 && h->last_checked_ymd == prev) {
        h->streak++;
        res = CHECK_STREAK_EXTENDED;
    } else {
        h->streak = 1;
        res = CHECK_STREAK_STARTED;
    }

    h->last_checked_ymd = today_ymd;
    if (h->streak > h->best_streak) {
        h->best_streak = h->streak;
    }
    return res;
}

bool streak_rollover(moo_habit_t *h, uint32_t today_ymd)
{
    const uint32_t yesterday = ymd_add_days(today_ymd, -1);

    if (h->last_evaluated_ymd == 0) {          /* first run: anchor only */
        h->last_evaluated_ymd = yesterday;
        return false;
    }

    bool broke = false;
    uint32_t d = ymd_add_days(h->last_evaluated_ymd, 1);
    for (int guard = 0; guard < 3700 && d < today_ymd; guard++) {
        if (habit_scheduled_on(h, d) && h->last_checked_ymd != d &&
            h->last_checked_ymd < d && h->streak > 0) {
            h->streak = 0;
            broke = true;
        }
        d = ymd_add_days(d, 1);
    }
    h->last_evaluated_ymd = yesterday;
    return broke;
}
