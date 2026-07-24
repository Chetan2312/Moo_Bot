#include "hal_board.h"

#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_ili9341.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "led_strip.h"

static const char *TAG = "hal_board";

#define LCD_SPI_HOST      SPI2_HOST

#ifdef CONFIG_MOO_LCD_MIRROR_X
#define LCD_MIRROR_X true
#else
#define LCD_MIRROR_X false
#endif
#ifdef CONFIG_MOO_LCD_MIRROR_Y
#define LCD_MIRROR_Y true
#else
#define LCD_MIRROR_Y false
#endif
#define BL_LEDC_TIMER     LEDC_TIMER_0
#define BL_LEDC_CHANNEL   LEDC_CHANNEL_0
#define BL_LEDC_RES       LEDC_TIMER_10_BIT
#define BL_LEDC_FREQ_HZ   5000

#if CONFIG_MOO_LCD_ILI9342_INIT
/* ILI9342C analog init (VCOM/power/gamma), from LovyanGFX Panel_ILI9342.
 * Replaces the driver's ILI9341 defaults, which render dark fields with
 * crosstalk lines on this glass. */
static const ili9341_lcd_init_cmd_t s_ili9342_init_cmds[] = {
    {0xC8, (uint8_t[]){0xFF, 0x93, 0x42}, 3, 0},
    {0xC0, (uint8_t[]){0x12, 0x12}, 2, 0},
    {0xC1, (uint8_t[]){0x03}, 1, 0},
    {0xC5, (uint8_t[]){0xF2}, 1, 0},
    {0xB0, (uint8_t[]){0xE0}, 1, 0},
    {0xF6, (uint8_t[]){0x01, 0x00, 0x00}, 3, 0},
    {0xE0, (uint8_t[]){0x00, 0x0C, 0x11, 0x04, 0x11, 0x08, 0x37, 0x89,
                       0x4C, 0x06, 0x0C, 0x0A, 0x2E, 0x34, 0x0F}, 15, 0},
    {0xE1, (uint8_t[]){0x00, 0x0B, 0x11, 0x05, 0x13, 0x09, 0x33, 0x67,
                       0x48, 0x07, 0x0E, 0x0B, 0x2E, 0x33, 0x0F}, 15, 0},
    {0xB6, (uint8_t[]){0x08, 0x82, 0x1D, 0x04}, 4, 0},
};

static const ili9341_vendor_config_t s_ili9342_vendor_cfg = {
    .init_cmds = s_ili9342_init_cmds,
    .init_cmds_size = sizeof(s_ili9342_init_cmds) / sizeof(s_ili9342_init_cmds[0]),
};
#endif

esp_err_t board_lcd_init(board_lcd_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "out is NULL");

    /* XPT2046 shares the SPI bus; a floating T_CS lets it fight the LCD
     * and corrupt frames. Park it deasserted until touch is brought up. */
    const gpio_config_t tcs_cfg = {
        .pin_bit_mask = 1ULL << CONFIG_MOO_PIN_TOUCH_CS,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&tcs_cfg), TAG, "touch cs cfg failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(CONFIG_MOO_PIN_TOUCH_CS, 1), TAG, "touch cs set failed");

    const spi_bus_config_t bus_cfg = {
        .sclk_io_num = CONFIG_MOO_PIN_LCD_SCLK,
        .mosi_io_num = CONFIG_MOO_PIN_LCD_MOSI,
        .miso_io_num = CONFIG_MOO_PIN_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CONFIG_MOO_LCD_H_RES * 80 * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "spi bus init failed");

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = CONFIG_MOO_PIN_LCD_DC,
        .cs_gpio_num = CONFIG_MOO_PIN_LCD_CS,
        .pclk_hz = CONFIG_MOO_LCD_PCLK_MHZ * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &io_cfg, &out->io),
                        TAG, "panel io failed");

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = CONFIG_MOO_PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
#if CONFIG_MOO_LCD_ILI9342_INIT
        .vendor_config = (void *)&s_ili9342_vendor_cfg,
#endif
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(out->io, &panel_cfg, &out->panel),
                        TAG, "ili9341 create failed");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(out->panel), TAG, "reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(out->panel), TAG, "init failed");
#if CONFIG_MOO_LCD_INVERT_COLOR
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(out->panel, true), TAG, "invert failed");
#endif
#if CONFIG_MOO_LCD_SWAP_XY
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(out->panel, true), TAG, "swap failed");
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(out->panel, LCD_MIRROR_X, LCD_MIRROR_Y),
                        TAG, "mirror failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(out->panel, true), TAG, "disp on failed");

    ESP_LOGI(TAG, "ILI9341 up: %dx%d @ %d MHz",
             CONFIG_MOO_LCD_H_RES, CONFIG_MOO_LCD_V_RES, CONFIG_MOO_LCD_PCLK_MHZ);
    return ESP_OK;
}

esp_err_t board_backlight_set(uint8_t percent)
{
#if CONFIG_MOO_PIN_LCD_BL < 0
    (void)percent;
    return ESP_OK;
#else
    static bool s_bl_init;

    if (percent > 100) {
        percent = 100;
    }

    if (!s_bl_init) {
        const ledc_timer_config_t tcfg = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .timer_num = BL_LEDC_TIMER,
            .duty_resolution = BL_LEDC_RES,
            .freq_hz = BL_LEDC_FREQ_HZ,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ESP_RETURN_ON_ERROR(ledc_timer_config(&tcfg), TAG, "ledc timer failed");

        const ledc_channel_config_t ccfg = {
            .gpio_num = CONFIG_MOO_PIN_LCD_BL,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = BL_LEDC_CHANNEL,
            .timer_sel = BL_LEDC_TIMER,
            .duty = 0,
            .hpoint = 0,
        };
        ESP_RETURN_ON_ERROR(ledc_channel_config(&ccfg), TAG, "ledc channel failed");
        s_bl_init = true;
    }

    const uint32_t duty = ((1u << BL_LEDC_RES) - 1) * percent / 100;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, duty),
                        TAG, "set duty failed");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
#endif
}

/* ---------------- TTP223 pat sensor ---------------- */

static QueueHandle_t s_pat_queue;
static board_pat_cb_t s_pat_cb;
static void *s_pat_ctx;
static int64_t s_pat_press_us;

static void IRAM_ATTR pat_isr(void *arg)
{
    (void)arg;
    const int64_t now = esp_timer_get_time();
    if (gpio_get_level(CONFIG_MOO_PIN_PAT)) {
        s_pat_press_us = now;                       /* press edge */
        return;
    }
    /* release edge: report hold duration */
    const uint32_t held_ms = (uint32_t)((now - s_pat_press_us) / 1000);
    BaseType_t hp_woken = pdFALSE;
    xQueueSendFromISR(s_pat_queue, &held_ms, &hp_woken);
    portYIELD_FROM_ISR(hp_woken);
}

static void pat_task(void *arg)
{
    (void)arg;
    uint32_t held_ms;
    int64_t last_us = 0;

    for (;;) {
        if (xQueueReceive(s_pat_queue, &held_ms, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (held_ms < 30) {                          /* contact bounce */
            continue;
        }
        const int64_t now = esp_timer_get_time();
        if (now - last_us < (int64_t)CONFIG_MOO_PAT_COOLDOWN_MS * 1000) {
            continue;
        }
        last_us = now;
        if (s_pat_cb) {
            s_pat_cb(s_pat_ctx, held_ms);
        }
    }
}

esp_err_t board_pat_init(board_pat_cb_t cb, void *ctx)
{
    ESP_RETURN_ON_FALSE(cb, ESP_ERR_INVALID_ARG, TAG, "cb is NULL");
    s_pat_cb = cb;
    s_pat_ctx = ctx;

    s_pat_queue = xQueueCreate(4, sizeof(uint32_t));
    ESP_RETURN_ON_FALSE(s_pat_queue, ESP_ERR_NO_MEM, TAG, "pat queue");

    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << CONFIG_MOO_PIN_PAT,
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,   /* TTP223 idle low, active high */
        .intr_type = GPIO_INTR_ANYEDGE,         /* press + release for duration */
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "pat gpio cfg");

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {  /* already installed is fine */
        return err;
    }
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(CONFIG_MOO_PIN_PAT, pat_isr, NULL),
                        TAG, "pat isr add");

    ESP_RETURN_ON_FALSE(
        xTaskCreate(pat_task, "pat", 4096, NULL, 5, NULL) == pdPASS,
        ESP_ERR_NO_MEM, TAG, "pat task");

    ESP_LOGI(TAG, "pat sensor on GPIO%d", CONFIG_MOO_PIN_PAT);
    return ESP_OK;
}

/* ---------------- WS2812 strip ---------------- */

static led_strip_handle_t s_strip;

esp_err_t board_led_init(void)
{
    const led_strip_config_t strip_cfg = {
        .strip_gpio_num = CONFIG_MOO_PIN_WS2812,
        .max_leds = CONFIG_MOO_WS2812_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    const led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip),
                        TAG, "led strip create");
    ESP_RETURN_ON_ERROR(led_strip_clear(s_strip), TAG, "led clear");
    ESP_LOGI(TAG, "ws2812 x%d on GPIO%d", CONFIG_MOO_WS2812_COUNT, CONFIG_MOO_PIN_WS2812);
    return ESP_OK;
}

esp_err_t board_led_set_all(uint8_t r, uint8_t g, uint8_t b)
{
    ESP_RETURN_ON_FALSE(s_strip, ESP_ERR_INVALID_STATE, TAG, "led not init");
    for (int i = 0; i < CONFIG_MOO_WS2812_COUNT; i++) {
        ESP_RETURN_ON_ERROR(led_strip_set_pixel(s_strip, i, r, g, b), TAG, "set pixel");
    }
    return led_strip_refresh(s_strip);
}

esp_err_t board_led_off(void)
{
    ESP_RETURN_ON_FALSE(s_strip, ESP_ERR_INVALID_STATE, TAG, "led not init");
    return led_strip_clear(s_strip);
}

/* ---------------- DS3231 RTC ---------------- */

#define DS3231_ADDR 0x68

static i2c_master_dev_handle_t s_rtc_dev;

static uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t dec2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

esp_err_t board_rtc_init(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CONFIG_MOO_PIN_I2C_SDA,
        .scl_io_num = CONFIG_MOO_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,   /* module has its own too */
    };
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &bus), TAG, "i2c bus");

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS3231_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_rtc_dev),
                        TAG, "rtc add dev");
    ESP_LOGI(TAG, "DS3231 on sda=%d scl=%d", CONFIG_MOO_PIN_I2C_SDA, CONFIG_MOO_PIN_I2C_SCL);
    return ESP_OK;
}

esp_err_t board_rtc_get(struct tm *out)
{
    ESP_RETURN_ON_FALSE(s_rtc_dev && out, ESP_ERR_INVALID_STATE, TAG, "rtc get");
    const uint8_t reg = 0x00;
    uint8_t b[7];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_rtc_dev, &reg, 1, b, sizeof(b),
                                                    pdMS_TO_TICKS(100)),
                        TAG, "rtc read");
    out->tm_sec = bcd2dec(b[0] & 0x7F);
    out->tm_min = bcd2dec(b[1] & 0x7F);
    out->tm_hour = bcd2dec(b[2] & 0x3F);        /* 24h mode assumed */
    out->tm_wday = (b[3] & 0x07) - 1;           /* DS3231 dow 1-7 */
    out->tm_mday = bcd2dec(b[4] & 0x3F);
    out->tm_mon = bcd2dec(b[5] & 0x1F) - 1;
    out->tm_year = bcd2dec(b[6]) + 100;         /* years since 1900, base 2000 */
    out->tm_isdst = 0;
    return ESP_OK;
}

esp_err_t board_rtc_set(const struct tm *t)
{
    ESP_RETURN_ON_FALSE(s_rtc_dev && t, ESP_ERR_INVALID_STATE, TAG, "rtc set");
    const uint8_t buf[8] = {
        0x00,
        dec2bcd(t->tm_sec),
        dec2bcd(t->tm_min),
        dec2bcd(t->tm_hour),
        dec2bcd((uint8_t)(t->tm_wday + 1)),
        dec2bcd(t->tm_mday),
        dec2bcd((uint8_t)(t->tm_mon + 1)),
        dec2bcd((uint8_t)(t->tm_year - 100)),
    };
    return i2c_master_transmit(s_rtc_dev, buf, sizeof(buf), pdMS_TO_TICKS(100));
}
