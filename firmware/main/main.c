#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio_out.h"
#include "face_ui.h"
#include "hal_board.h"

static const char *TAG = "main";

/* Runs in the pat task from hal_board — blocking audio is fine here. */
static void on_pat(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "pat!");
    face_ui_pat();
    board_led_set_all(255, 70, 130);   /* pink glow */
    audio_out_pat_sound();
    vTaskDelay(pdMS_TO_TICKS(400));
    board_led_off();
}

void app_main(void)
{
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

    /* Peripherals may be unwired mid-bringup — log and carry on, face still runs. */
    esp_err_t err = board_led_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "led init failed: %s", esp_err_to_name(err));
    }
    err = audio_out_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "audio init failed: %s", esp_err_to_name(err));
    }
    ESP_ERROR_CHECK(board_pat_init(on_pat, NULL));

    ESP_LOGI(TAG, "demo2 running: face + pat + led + chirp");
}
