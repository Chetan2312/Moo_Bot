#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * On-device HTTP server: serves the habit-config webapp from flash and a
 * small JSON REST API over habit_store. Start after the device has an IP.
 * Phone must be on the same LAN.
 */
esp_err_t web_ui_start(void);

/* "http://<ip>/" for the current STA IP, into buf. Empty string if no IP.
 * Used to build the on-screen QR. */
void web_ui_url(char *buf, size_t len);

#ifdef __cplusplus
}
#endif
