#include "net_sync.h"

#include <string.h>
#include <time.h>

#include "esp_check.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"

#include "hal_board.h"

static const char *TAG = "net_sync";

#define WIFI_MAX_RETRY 8

static int s_retries;
static net_got_ip_cb_t s_got_ip_cb;

/* Debug aid on give-up: list what the radio actually sees. */
static void scan_task(void *arg)
{
    (void)arg;
    wifi_scan_config_t scan_cfg = { .show_hidden = true };
    if (esp_wifi_scan_start(&scan_cfg, true) == ESP_OK) {
        uint16_t n = 20;
        wifi_ap_record_t recs[20];
        if (esp_wifi_scan_get_ap_records(&n, recs) == ESP_OK) {
            ESP_LOGI(TAG, "visible networks (%u):", n);
            for (int i = 0; i < n; i++) {
                ESP_LOGI(TAG, "  '%s' ch=%d rssi=%d auth=%d",
                         (char *)recs[i].ssid, recs[i].primary,
                         recs[i].rssi, recs[i].authmode);
            }
        }
    }
    vTaskDelete(NULL);
}

static void on_time_sync(struct timeval *tv)
{
    (void)tv;
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    ESP_LOGI(TAG, "SNTP synced: %04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);

    /* keep DS3231 authoritative for offline boots; no-op if not wired */
    if (board_rtc_set(&t) == ESP_OK) {
        ESP_LOGI(TAG, "DS3231 updated from SNTP");
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *evt = data;
        if (s_retries < WIFI_MAX_RETRY) {
            s_retries++;
            ESP_LOGW(TAG, "wifi disconnected (reason %d), retry %d/%d",
                     evt->reason, s_retries, WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "wifi gave up after %d retries (last reason %d)",
                     WIFI_MAX_RETRY, evt->reason);
            xTaskCreate(scan_task, "wifi_scan", 4096, NULL, 3, NULL);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *evt = data;
        s_retries = 0;
        ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&evt->ip_info.ip));
        if (s_got_ip_cb) {
            s_got_ip_cb();
        }
    }
}

esp_err_t net_sync_start(net_got_ip_cb_t on_got_ip)
{
    s_got_ip_cb = on_got_ip;
    if (strlen(CONFIG_MOO_WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "no WiFi SSID configured (menuconfig -> MooBot Network) — offline");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "wifi init");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   wifi_event_handler, NULL),
                        TAG, "wifi handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                   wifi_event_handler, NULL),
                        TAG, "ip handler");

    wifi_config_t wifi_cfg = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,   /* WPA3 hotspots (modern phones) */
        },
    };
    strlcpy((char *)wifi_cfg.sta.ssid, CONFIG_MOO_WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, CONFIG_MOO_WIFI_PASS, sizeof(wifi_cfg.sta.password));

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG, "wifi cfg");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");

    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_MOO_SNTP_SERVER);
    sntp_cfg.sync_cb = on_time_sync;
    ESP_RETURN_ON_ERROR(esp_netif_sntp_init(&sntp_cfg), TAG, "sntp init");

    ESP_LOGI(TAG, "connecting to '%s'", CONFIG_MOO_WIFI_SSID);
    return ESP_OK;
}
