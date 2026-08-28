/* Host test: make -C host_test run */

#include <stdio.h>

#include "rfc3339.h"

static int s_pass;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); return 1; } \
    s_pass++; \
} while (0)

int main(void)
{
    int64_t ep;
    bool dateonly;

    /* 2026-07-24 00:00:00 UTC (verified via `date -u -d @<n>`) */
    const int64_t base = 1784851200;

    CHECK(rfc3339_to_epoch("2026-07-24", &ep, &dateonly));
    CHECK(ep == base);
    CHECK(dateonly == true);

    /* UTC time */
    CHECK(rfc3339_to_epoch("2026-07-24T00:00:00Z", &ep, &dateonly));
    CHECK(ep == base);
    CHECK(dateonly == false);

    CHECK(rfc3339_to_epoch("2026-07-24T01:00:00Z", &ep, NULL));
    CHECK(ep == base + 3600);

    /* fractional seconds */
    CHECK(rfc3339_to_epoch("2026-07-24T00:00:00.000Z", &ep, NULL));
    CHECK(ep == base);

    /* +05:30 offset -> subtract 5h30m to get UTC */
    CHECK(rfc3339_to_epoch("2026-07-24T05:30:00+05:30", &ep, NULL));
    CHECK(ep == base);

    /* -08:00 offset */
    CHECK(rfc3339_to_epoch("2026-07-23T16:00:00-08:00", &ep, NULL));
    CHECK(ep == base);

    /* a concrete afternoon IST event: 14:30 +05:30 == 09:00 UTC */
    CHECK(rfc3339_to_epoch("2026-07-24T14:30:00+05:30", &ep, NULL));
    CHECK(ep == base + 9 * 3600);

    /* malformed */
    CHECK(rfc3339_to_epoch("garbage", &ep, NULL) == false);

    /* civil helper: 1970-01-01 == day 0 */
    CHECK(days_from_civil(1970, 1, 1) == 0);
    CHECK(days_from_civil(2026, 7, 24) == base / 86400);

    printf("all %d checks passed\n", s_pass);
    return 0;
}
