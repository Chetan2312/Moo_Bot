#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Dev connectivity: WiFi STA from Kconfig credentials + SNTP.
 * On time sync the DS3231 (if present) is updated automatically.
 * Non-blocking; returns after starting the connect state machine.
 * BLE provisioning + MQTT land here in milestone 4.
 *
 * on_got_ip (may be NULL) fires from the WiFi event task each time the
 * device obtains an IP — good place to start the on-device web server.
 */
typedef void (*net_got_ip_cb_t)(void);
esp_err_t net_sync_start(net_got_ip_cb_t on_got_ip);

#ifdef __cplusplus
}
#endif
