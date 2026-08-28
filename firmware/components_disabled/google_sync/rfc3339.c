#include "rfc3339.h"

#include <stdio.h>
#include <string.h>

int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

bool rfc3339_to_epoch(const char *s, int64_t *out_epoch, bool *date_only)
{
    if (!s || !out_epoch) {
        return false;
    }
    int y, mo, d, hh = 0, mi = 0, se = 0;
    if (sscanf(s, "%d-%d-%d", &y, &mo, &d) != 3) {
        return false;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > 31) {
        return false;
    }

    const char *tp = strchr(s, 'T');
    const bool has_time = (tp != NULL);
    int off_sec = 0;

    if (has_time) {
        tp++;
        if (sscanf(tp, "%d:%d:%d", &hh, &mi, &se) < 2) {
            return false;
        }
        /* timezone suffix: Z, +HH:MM, or -HH:MM (date '-' are before T) */
        if (strchr(tp, 'Z') == NULL) {
            const char *plus = strchr(tp, '+');
            const char *minus = strrchr(tp, '-');
            const char *sign = plus ? plus : minus;
            if (sign) {
                int oh = 0, om = 0;
                if (sscanf(sign + 1, "%d:%d", &oh, &om) >= 1) {
                    off_sec = (oh * 3600 + om * 60) * (*sign == '+' ? 1 : -1);
                }
            }
        }
    }

    const int64_t days = days_from_civil(y, (unsigned)mo, (unsigned)d);
    *out_epoch = days * 86400LL + hh * 3600 + mi * 60 + se - off_sec;
    if (date_only) {
        *date_only = !has_time;
    }
    return true;
}
