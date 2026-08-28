#pragma once

#include <time.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"

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

/* TTP223 head-pat sensor. cb runs in a dedicated task (LVGL calls OK).
 *   PAT_TAP:       short press released (held_ms < hold threshold)
 *   PAT_HOLD_TICK: fired repeatedly while held past the threshold
 *   PAT_HOLD_END:  release after a hold (held_ms = total) */
typedef enum {
    PAT_TAP,
    PAT_HOLD_TICK,
    PAT_HOLD_END,
} pat_event_t;
typedef void (*board_pat_cb_t)(void *ctx, pat_event_t evt, uint32_t held_ms);
esp_err_t board_pat_init(board_pat_cb_t cb, void *ctx);

/* XPT2046 resistive touch on the shared LCD SPI bus. Call after
 * board_lcd_init (needs the bus up). */
esp_err_t board_touch_init(esp_lcd_touch_handle_t *out);

/* DS3231 RTC. get/set use struct tm in local wall time. */
esp_err_t board_rtc_init(void);
esp_err_t board_rtc_get(struct tm *out);
esp_err_t board_rtc_set(const struct tm *t);

/* WS2812 strip. */
esp_err_t board_led_init(void);
esp_err_t board_led_set_all(uint8_t r, uint8_t g, uint8_t b);
esp_err_t board_led_off(void);

#ifdef __cplusplus
}
#endif
