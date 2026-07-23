#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
} board_lcd_t;

/* SPI bus + ILI9341 panel bringup. Panel is left ON, blank. */
esp_err_t board_lcd_init(board_lcd_t *out);

/* 0..100. No-op when MOO_PIN_LCD_BL = -1 (backlight hardwired). */
esp_err_t board_backlight_set(uint8_t percent);

/* TTP223 head-pat sensor. cb runs in a dedicated task (LVGL calls OK),
 * debounced by MOO_PAT_COOLDOWN_MS. */
typedef void (*board_pat_cb_t)(void *ctx);
esp_err_t board_pat_init(board_pat_cb_t cb, void *ctx);

/* WS2812 strip. */
esp_err_t board_led_init(void);
esp_err_t board_led_set_all(uint8_t r, uint8_t g, uint8_t b);
esp_err_t board_led_off(void);

#ifdef __cplusplus
}
#endif
