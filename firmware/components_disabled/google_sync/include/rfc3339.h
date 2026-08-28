#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parse an RFC3339 timestamp (as returned by Google Calendar/Tasks) to a
 * UTC epoch. Handles:
 *   2026-07-24T14:30:00+05:30    (offset)
 *   2026-07-24T14:30:00Z         (UTC)
 *   2026-07-24T14:30:00.000Z     (fractional seconds)
 *   2026-07-24                   (date-only -> midnight UTC, *date_only=true)
 * Returns false on malformed input. date_only may be NULL.
 */
bool rfc3339_to_epoch(const char *s, int64_t *out_epoch, bool *date_only);

/* Civil date -> days since 1970-01-01 (Howard Hinnant). Exposed for tests
 * and for building timeMin/timeMax query bounds. */
int64_t days_from_civil(int y, unsigned m, unsigned d);

#ifdef __cplusplus
}
#endif
