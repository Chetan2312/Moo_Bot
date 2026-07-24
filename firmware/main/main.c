#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "audio_out.h"
#include "face_ui.h"
#include "hal_board.h"
#include "net_sync.h"
#include "reminder_store.h"

static const char *TAG = "main";

#define LONG_PRESS_MS 600

/* Runs in the pat task from hal_board — blocking audio is fine here.
 * Short tap: cycle face <-> clock screen.
 * Long press: ack pending reminder, else TEST-cycle emotions. */
static void on_pat(void *ctx, uint32_t held_ms)
{
    (void)ctx;

    if (held_ms >= LONG_PRESS_MS) {
        if (reminder_store_pending()) {
            ESP_LOGI(TAG, "long press -> reminder ack");
            reminder_store_ack();
            board_led_off();
            audio_out_tone(900.0f, 1300.0f, 80, 0.6f);
        } else {
            ESP_LOGI(TAG, "long press -> next screen");
            face_ui_next_screen();
        }
        return;
    }

    /* Short tap = pat. TEST: cycles all reactions until real triggers exist;
     * product behavior is FACE_REACT_PAT only. */
    static const face_react_t order[] = {
        FACE_REACT_PAT, FACE_REACT_HAPPY, FACE_REACT_CELEBRATE,
        FACE_REACT_SAD, FACE_REACT_SLEEPY, FACE_REACT_ALERT,
        FACE_REACT_TASK_DONE, FACE_REACT_WAKE, FACE_REACT_EAT,
    };
    static const char *names[] = {
        "pat", "happy", "celebrate", "sad", "sleepy",
        "alert", "task_done", "wake", "eat",
    };
    static uint8_t ri;

    if (face_ui_react(order[ri])) {
        ESP_LOGI(TAG, "pat -> %s", names[ri]);
        ri = (ri + 1) % (sizeof(order) / sizeof(order[0]));
        board_led_set_all(255, 70, 130);
        audio_out_pat_sound();
        vTaskDelay(pdMS_TO_TICKS(400));
        if (!reminder_store_pending()) {
            board_led_off();
        }
    } else {
        ESP_LOGI(TAG, "pat ignored (reaction in flight)");
    }
}

/* Runs in the reminder task. */
static void on_reminder(const moo_reminder_t *r, void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "REMINDER: %s", r->text);
    face_ui_react(FACE_REACT_ALERT);
    board_led_set_all(255, 140, 0);        /* amber until acked */
    audio_out_alert_sound();
}

static void system_time_from_rtc(void)
{
    /* IST; make Kconfig when personas/regions multiply */
    setenv("TZ", "IST-5:30", 1);
    tzset();

    esp_err_t err = board_rtc_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "rtc init failed: %s", esp_err_to_name(err));
        return;
    }

    struct tm t;
    if (board_rtc_get(&t) == ESP_OK && t.tm_year >= 124) {
        const time_t now = mktime(&t);
        const struct timeval tv = { .tv_sec = now };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "time from DS3231: %04d-%02d-%02d %02d:%02d:%02d",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                 t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        /* Fresh/battery-dead RTC: seed from build timestamp (close enough,
         * SNTP takes over once WiFi exists). __DATE__ = "Jul 23 2026". */
        struct tm bt = { 0 };
        char mon[4] = { 0 };
        sscanf(__DATE__, "%3s %d %d", mon, &bt.tm_mday, &bt.tm_year);
        sscanf(__TIME__, "%d:%d:%d", &bt.tm_hour, &bt.tm_min, &bt.tm_sec);
        static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
        bt.tm_mon = (int)(strstr(months, mon) - months) / 3;
        bt.tm_year -= 1900;
        const time_t bt_time = mktime(&bt);
        struct tm lt;
        localtime_r(&bt_time, &lt);
        mktime(&lt);                          /* normalize tm_wday */
        if (board_rtc_set(&lt) == ESP_OK) {
            const struct timeval tv = { .tv_sec = bt_time };
            settimeofday(&tv, NULL);
            ESP_LOGW(TAG, "DS3231 was unset — seeded from build time %s %s", __DATE__, __TIME__);
        }
    }
}

/* Waits for valid time (RTC at boot or SNTP async), then seeds the demo
 * reminder once. Self-deletes. */
static void seed_demo_reminder_task(void *arg)
{
    (void)arg;
    for (int i = 0; i < 90; i++) {
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);
        if (t.tm_year >= 124) {
            if (reminder_store_count() == 0) {
                const int total = t.tm_hour * 60 + t.tm_min + 2;
                const moo_reminder_t demo = {
                    .hh = (total / 60) % 24,
                    .mm = total % 60,
                    .days_mask = 0x7F,
                    .enabled = true,
                    .text = "demo: drink water",
                };
                reminder_store_add(&demo);
                ESP_LOGI(TAG, "demo reminder at %02d:%02d (2 min from now)",
                         demo.hh, demo.mm);
            }
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    board_lcd_t lcd;
    ESP_ERROR_CHECK(board_lcd_init(&lcd));

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd.io,
        .panel_handle = lcd.panel,
        .buffer_size = CONFIG_MOO_LCD_H_RES * 40,
        .double_buffer = true,
        .hres = CONFIG_MOO_LCD_H_RES,
        .vres = CONFIG_MOO_LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    ESP_ERROR_CHECK(disp ? ESP_OK : ESP_FAIL);

    ESP_ERROR_CHECK(board_backlight_set(100));
    face_ui_start(disp);

    system_time_from_rtc();

    /* Peripherals may be unwired mid-bringup — log and carry on. */
    err = board_led_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "led init failed: %s", esp_err_to_name(err));
    }
    err = audio_out_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "audio init failed: %s", esp_err_to_name(err));
    }
    ESP_ERROR_CHECK(board_pat_init(on_pat, NULL));
    ESP_ERROR_CHECK(reminder_store_init(on_reminder, NULL));

    err = net_sync_start();          /* SNTP fills system time async */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "net_sync not started: %s", esp_err_to_name(err));
    }
    xTaskCreate(seed_demo_reminder_task, "seed", 3072, NULL, 3, NULL);

    ESP_LOGI(TAG, "demo3: face + clock + reminders (tap=screen, hold=emotion/ack)");
}
