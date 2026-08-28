#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Google Calendar + Tasks sync via OAuth 2.0 Device Flow. First run shows a
 * QR/code on screen for the user to authorize; the refresh token is stored in
 * NVS so later boots refresh silently. Polls every MOO_GSYNC_POLL_MIN minutes
 * and pushes dated reminders into ext_reminders.
 *
 * Call after WiFi has an IP and system time is (or will be) valid.
 */
esp_err_t google_sync_start(void);

#ifdef __cplusplus
}
#endif
