#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "audio_in.h"
#include "audio_out.h"
#include "face_ui.h"
#include "gemini.h"
#include "hal_board.h"
#include "net_sync.h"
#include "sfx.h"

static const char *TAG = "main";

/* Bump on every flashed release. Printed at boot so a device in hand can be
 * identified without reading its flash. */
#define MOO_FW_VERSION "0.1.0"

/*
 * Three screens, cycled with a double-tap of the head button:
 *
 *   face   Shivaansh's screen. Every touch plays the next cow animation and
 *          its sound, straight away. No modes, no counting taps.
 *   clock  time and date, auto-returns to the face after 30 s.
 *   AI     tap to talk; ask for a story and the cow tells one.
 *
 * The touchscreen is deliberately instant while the head button keeps the
 * multi-tap grouping. A toddler mashes the screen and gets a response per
 * touch; navigation lives on the button where a 500 ms listen window costs
 * nothing. Same input, same output, every time — that is the whole game at
 * 18 months.
 */

#define MULTI_TAP_MS    500         /* grouping window, head button only */
#define MAX_TAPS        2
#define VOL_STEP        4

typedef enum {
    GEST_BUTTON_TAP,     /* head button — may be grouped into a double */
    GEST_SCREEN_FACE,    /* touchscreen on the face screen — immediate */
    GEST_HOLD_TICK,
    GEST_HOLD_END,
} gesture_kind_t;

typedef struct { gesture_kind_t kind; uint32_t held; } gesture_msg_t;

static QueueHandle_t s_gesture_q;

/* "Hello Shivaansh", raw headerless mono s16le, embedded from
 * main/assets/moo_test.pcm. The symbol is the filename with dots turned
 * into underscores. MOO_CLIP_RATE_HZ must match the `ffmpeg -ar` it was
 * made with — there is no header to read it from. */
#define MOO_CLIP_RATE_HZ 22050

extern const uint8_t moo_clip_start[] asm("_binary_moo_test_pcm_start");
extern const uint8_t moo_clip_end[]   asm("_binary_moo_test_pcm_end");

/* ---- the play cards ----------------------------------------------------
 *
 * Every frame here is already in flash: 11 cow animations x 9 frames. The
 * greeting clip is card 0 so the first touch says his name. */

typedef enum { CARD_CLIP, CARD_SFX } card_kind_t;

typedef struct {
    face_react_t react;
    card_kind_t  kind;
    sfx_id_t     sfx;
    uint8_t      r, g, b;
} play_card_t;

static const play_card_t s_cards[] = {
    { FACE_REACT_HAPPY,     CARD_CLIP, 0,             255, 200, 120 },
    { FACE_REACT_PAT,       CARD_SFX,  SFX_MOO,       120, 255, 120 },
    { FACE_REACT_EAT,       CARD_SFX,  SFX_MUNCH,     255, 200,  60 },
    { FACE_REACT_CELEBRATE, CARD_SFX,  SFX_GIGGLE,    255, 120, 200 },
    { FACE_REACT_TASK_DONE, CARD_SFX,  SFX_MOO_SHORT, 120, 200, 255 },
    { FACE_REACT_WAKE,      CARD_SFX,  SFX_YAWN,      255, 160,  80 },
};
#define CARD_COUNT (sizeof(s_cards) / sizeof(s_cards[0]))

static void play_card_next(void)
{
    static size_t idx;
    const play_card_t *c = &s_cards[idx];

    board_led_set_all(c->r, c->g, c->b);
    face_ui_react(c->react);            /* ignored if one is still running */

    if (c->kind == CARD_CLIP) {
        const size_t bytes = (size_t)(moo_clip_end - moo_clip_start);
        if (audio_out_play_pcm(moo_clip_start, bytes, MOO_CLIP_RATE_HZ) != ESP_OK) {
            audio_out_play(SND_HAPPY);  /* backend with no PCM path */
        }
    } else {
        sfx_play(c->sfx);
    }

    board_led_off();
    idx = (idx + 1) % CARD_COUNT;
}

/* ---- AI screen ---------------------------------------------------------- */

/* Feeds the listening meter. -55 dBFS reads as silence and -15 as loud,
 * which is roughly the span of a person talking at a desk mic. */
static uint8_t mic_level_pct(void)
{
    const float db = audio_in_level_dbfs();
    float pct = (db + 55.0f) * (100.0f / 40.0f);
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return (uint8_t)pct;
}

/* Runs on the gemini task. The round trip is ~10 s, so showing nothing
 * would read as a dead toy. */
static void on_gemini_stage(gemini_stage_t st, const char *detail, void *ctx)
{
    (void)ctx;
    switch (st) {
    case GEMINI_STAGE_LISTENING:
        ESP_LOGI(TAG, "story: listening");
        board_led_set_all(80, 160, 255);
        face_ui_ai_set_state(AI_LISTENING, "say something");
        break;
    case GEMINI_STAGE_THINKING:
        ESP_LOGI(TAG, "story: thinking");
        board_led_set_all(180, 120, 255);
        face_ui_ai_set_state(AI_THINKING, "writing your story");
        break;
    case GEMINI_STAGE_SPEAKING:
        ESP_LOGI(TAG, "story: speaking - %.120s", detail ? detail : "");
        board_led_set_all(120, 255, 120);
        face_ui_ai_set_state(AI_SPEAKING, NULL);
        break;
    case GEMINI_STAGE_DONE:
        board_led_off();
        face_ui_ai_set_state(AI_IDLE, NULL);
        break;
    case GEMINI_STAGE_FAILED:
        ESP_LOGW(TAG, "story failed: %s", detail ? detail : "?");
        board_led_off();
        face_ui_ai_set_state(AI_FAILED, detail);
        audio_out_play(SND_SAD);
        break;
    }
}

/* Tap on the AI screen. Runs in the LVGL task; gemini_ask_story() only
 * spawns a task, so this stays light. */
static void on_ai_tap(void)
{
    if (!gemini_configured()) {
        ESP_LOGW(TAG, "story: no API key (menuconfig -> MooBot Gemini)");
        face_ui_ai_set_state(AI_FAILED, "no API key");
        return;
    }
    if (gemini_busy()) {
        return;                          /* already running; ignore */
    }
    const esp_err_t err = gemini_ask_story();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "story: %s", esp_err_to_name(err));
        face_ui_ai_set_state(AI_FAILED, esp_err_to_name(err));
    }
}

/* ---- gestures ----------------------------------------------------------- */

static void gesture_task(void *arg)
{
    (void)arg;
    static int vol_dir = +1;             /* first hold increases */
    gesture_msg_t m;

    for (;;) {
        if (xQueueReceive(s_gesture_q, &m, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (m.kind) {
        case GEST_HOLD_TICK: {
            const uint8_t pct = audio_out_volume_adjust(vol_dir * VOL_STEP);
            face_ui_show_volume(pct);
            continue;
        }
        case GEST_HOLD_END:
            audio_out_volume_commit();
            ESP_LOGI(TAG, "volume %d%%", audio_out_volume_get());
            vol_dir = -vol_dir;          /* next hold goes the other way */
            continue;

        case GEST_SCREEN_FACE:
            /* No grouping, no waiting — this is the toddler path. */
            play_card_next();
            xQueueReset(s_gesture_q);    /* mashing must not build a backlog */
            continue;

        case GEST_BUTTON_TAP:
            break;
        }

        /* Head button: group a double-tap for screen changes. */
        int taps = 1;
        audio_out_tone(1500.0f, 1500.0f, 25, 0.4f);
        while (taps < MAX_TAPS &&
               xQueueReceive(s_gesture_q, &m, pdMS_TO_TICKS(MULTI_TAP_MS)) == pdTRUE) {
            if (m.kind != GEST_BUTTON_TAP) {
                break;
            }
            taps++;
        }

        if (taps >= 2) {
            ESP_LOGI(TAG, "gesture: next screen");
            audio_out_play(SND_SCREEN);
            face_ui_next_screen();
        } else if (!face_ui_on_ai_screen()) {
            play_card_next();
        }
    }
}

/* Runs in hal_board's pat task. */
static void on_pat(void *ctx, pat_event_t evt, uint32_t held_ms)
{
    (void)ctx;
    gesture_msg_t m = { .held = held_ms };
    switch (evt) {
    case PAT_TAP:       m.kind = GEST_BUTTON_TAP; break;
    case PAT_HOLD_TICK: m.kind = GEST_HOLD_TICK;  break;
    case PAT_HOLD_END:  m.kind = GEST_HOLD_END;   break;
    default: return;
    }
    xQueueSend(s_gesture_q, &m, 0);
}

/* Touchscreen on the face screen. Runs in the LVGL task, so hand the
 * blocking audio to the gesture task. */
static void on_face_tap(void)
{
    const gesture_msg_t m = { .kind = GEST_SCREEN_FACE, .held = 0 };
    xQueueSend(s_gesture_q, &m, 0);
}

static void on_got_ip(void)
{
    ESP_LOGI(TAG, "network up — stories available");
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
        /* Fresh/battery-dead RTC: seed from the build timestamp. A 1970
         * clock breaks TLS cert validation, which kills Gemini. */
        struct tm bt = { 0 };
        char mon[4] = { 0 };
        sscanf(__DATE__, "%3s %d %d", mon, &bt.tm_mday, &bt.tm_year);
        sscanf(__TIME__, "%d:%d:%d", &bt.tm_hour, &bt.tm_min, &bt.tm_sec);
        static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
        bt.tm_mon = (int)(strstr(months, mon) - months) / 3;
        bt.tm_year -= 1900;
        const time_t bt_time = mktime(&bt);
        const struct timeval tv = { .tv_sec = bt_time };
        settimeofday(&tv, NULL);
        struct tm lt;
        localtime_r(&bt_time, &lt);
        mktime(&lt);                          /* normalize tm_wday */
        board_rtc_set(&lt);                   /* best-effort */
        ESP_LOGW(TAG, "no valid RTC — seeded system time from build time %s %s",
                 __DATE__, __TIME__);
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_LOGI(TAG, "MooBot firmware v%s (built %s %s)",
             MOO_FW_VERSION, __DATE__, __TIME__);

    board_lcd_t lcd;
    ESP_ERROR_CHECK(board_lcd_init(&lcd));

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd.io,
        .panel_handle = lcd.panel,
        .buffer_size = CONFIG_MOO_LCD_H_RES * 20,
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
            /* Draw buffers stay in internal DMA RAM (stable SPI-LCD path);
             * the halved buffer_size frees the ~25 KB internal RAM the
             * WiFi/TLS path needs. PSRAM-DMA for the LCD caused intermittent
             * "spi transmit (queue) color failed" + LVGL hangs. */
            .buff_dma = true,
            .swap_bytes = true,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    ESP_ERROR_CHECK(disp ? ESP_OK : ESP_FAIL);

    ESP_ERROR_CHECK(board_backlight_set(100));
    face_ui_start(disp);

    /* Must exist before touch or button can post to it. */
    s_gesture_q = xQueueCreate(12, sizeof(gesture_msg_t));
    ESP_ERROR_CHECK(s_gesture_q ? ESP_OK : ESP_ERR_NO_MEM);
    xTaskCreate(gesture_task, "gesture", 4096, NULL, 5, NULL);

    esp_lcd_touch_handle_t tp;
    if (board_touch_init(&tp) == ESP_OK) {
        const lvgl_port_touch_cfg_t touch_cfg = { .disp = disp, .handle = tp };
        if (lvgl_port_add_touch(&touch_cfg) == NULL) {
            ESP_LOGW(TAG, "touch indev add failed");
        }
        face_ui_set_tap_cb(on_face_tap);
        face_ui_set_ai_tap_cb(on_ai_tap);
        face_ui_set_level_source(mic_level_pct);
    } else {
        ESP_LOGW(TAG, "touch init failed (unwired?)");
    }

    system_time_from_rtc();

    /* Peripherals may be unwired mid-bringup — log and carry on. */
    err = board_led_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "led init failed: %s", esp_err_to_name(err));
    }
    err = audio_out_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "audio init failed: %s", esp_err_to_name(err));
    } else if (sfx_prewarm() != ESP_OK) {
        ESP_LOGW(TAG, "sfx prewarm incomplete");
    }
#if CONFIG_MOO_MIC_ENABLE
    err = audio_in_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mic init failed: %s", esp_err_to_name(err));
    }
#if CONFIG_MOO_MIC_MONITOR
    else if (audio_in_start_monitor() != ESP_OK) {
        ESP_LOGW(TAG, "mic monitor task not started");
    }
#endif
#endif

    /* Not fatal without a key — the other two screens still work. */
    gemini_init(on_gemini_stage, NULL);

    ESP_ERROR_CHECK(board_pat_init(on_pat, NULL));

    err = net_sync_start(on_got_ip);     /* WiFi + SNTP; Gemini needs both */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "net_sync not started: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "moo: touch face = play, button x2 = next screen "
                  "(face / clock / AI), hold = volume");
}
