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
 */
esp_err_t net_sync_start(void);

#ifdef __cplusplus
}
#endif
