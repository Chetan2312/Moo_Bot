#include "face_ui.h"

#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "face_ui";

/* DIAG: full-screen redraw every 3 s, wipes any accumulated transfer
 * corruption. Remove once the panel is trusted. */
static void diag_redraw_timer_cb(lv_timer_t *t)
{
    (void)t;
    lv_obj_invalidate(lv_screen_active());
}

/* ================= touch input ================= */

static void (*s_tap_cb)(void);
static void (*s_long_press_cb)(void);

void face_ui_set_tap_cb(void (*cb)(void))
{
    s_tap_cb = cb;
}

void face_ui_set_long_press_cb(void (*cb)(void))
{
    s_long_press_cb = cb;
}

/* LVGL still sends CLICKED on release after a LONG_PRESSED, so without this
 * every story request would also fire a tap — a check-in or, in play mode,
 * a card advance right as the mic opens. */
static bool s_long_fired;

static void face_tapped_cb(lv_event_t *e)
{
    (void)e;
    if (s_long_fired) {
        s_long_fired = false;
        return;
    }
    lv_indev_t *indev = lv_indev_active();
    if (indev) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        ESP_LOGI(TAG, "touch @ (%d, %d)", (int)p.x, (int)p.y);
    }
    if (s_tap_cb) {
        s_tap_cb();
    }
}

/* A held screen is a separate gesture from a held head button, which the
 * volume ramp and the play-mode toggle already own. */
static void face_long_press_cb(lv_event_t *e)
{
    (void)e;
    s_long_fired = true;
    ESP_LOGI(TAG, "screen long-press");
    if (s_long_press_cb) {
        s_long_press_cb();
    }
}

/* Full-screen transparent hit area behind the face art so a tap anywhere
 * registers (the sprite/eyes aren't clickable and pass the touch through). */
static void add_face_hitbox(lv_obj_t *scr)
{
    lv_obj_t *hit = lv_obj_create(scr);
    lv_obj_remove_style_all(hit);
    lv_obj_set_size(hit, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hit, face_tapped_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(hit, face_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_move_background(hit);
}

/* ================= screens: face <-> clock ================= */

#define CLOCK_RETURN_MS 30000

static lv_obj_t *s_scr_face;      /* set by face_ui_start */
static lv_obj_t *s_scr_clock;
static lv_obj_t *s_lbl_time;
static lv_obj_t *s_lbl_date;
static lv_timer_t *s_return_timer;

static void clock_update_cb(lv_timer_t *t)
{
    (void)t;
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char buf[16];
    strftime(buf, sizeof(buf), "%H:%M", &tm_now);
    lv_label_set_text(s_lbl_time, buf);
    char date[32];
    strftime(date, sizeof(date), "%a  %d %b %Y", &tm_now);
    lv_label_set_text(s_lbl_date, date);
}

static void clock_screen_ensure(void)
{
    if (s_scr_clock) {
        return;
    }
    s_scr_clock = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_clock, lv_color_hex(0x000000), 0);

    s_lbl_time = lv_label_create(s_scr_clock);
    lv_obj_set_style_text_font(s_lbl_time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_lbl_time, lv_color_hex(0xEAF6FF), 0);
    lv_obj_align(s_lbl_time, LV_ALIGN_CENTER, 0, -20);

    s_lbl_date = lv_label_create(s_scr_clock);
    lv_obj_set_style_text_font(s_lbl_date, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_lbl_date, lv_color_hex(0x8FA3B0), 0);
    lv_obj_align(s_lbl_date, LV_ALIGN_CENTER, 0, 28);

    lv_timer_t *t = lv_timer_create(clock_update_cb, 1000, NULL);
    clock_update_cb(t);
}

static void clock_return_cb(lv_timer_t *t)
{
    (void)t;
    s_return_timer = NULL;
    lv_screen_load(s_scr_face);
}

/* ================= QR screen ================= */

static lv_obj_t *s_scr_qr;
static lv_obj_t *s_qr;
static lv_obj_t *s_qr_caption;   /* top caption */
static lv_obj_t *s_qr_code;      /* bottom code line (device flow) */
static lv_timer_t *s_qr_timer;

static void qr_return_cb(lv_timer_t *t)
{
    (void)t;
    s_qr_timer = NULL;
    lv_screen_load(s_scr_face);
}

/* Shared QR renderer. `code` non-NULL shows a big code line beneath. */
static void qr_show(const char *data, const char *caption, const char *code, uint32_t ms)
{
    if (!data) {
        return;
    }
    lvgl_port_lock(0);

    if (!s_scr_qr) {
        s_scr_qr = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(s_scr_qr, lv_color_hex(0xFFFFFF), 0);
        lv_obj_clear_flag(s_scr_qr, LV_OBJ_FLAG_SCROLLABLE);

        s_qr_caption = lv_label_create(s_scr_qr);
        lv_obj_set_style_text_color(s_qr_caption, lv_color_hex(0x202020), 0);
        lv_obj_set_style_text_font(s_qr_caption, &lv_font_montserrat_14, 0);
        lv_obj_align(s_qr_caption, LV_ALIGN_TOP_MID, 0, 8);

        s_qr = lv_qrcode_create(s_scr_qr);
        lv_qrcode_set_size(s_qr, 150);
        lv_qrcode_set_dark_color(s_qr, lv_color_hex(0x000000));
        lv_qrcode_set_light_color(s_qr, lv_color_hex(0xFFFFFF));
        lv_obj_align(s_qr, LV_ALIGN_CENTER, 0, -6);

        s_qr_code = lv_label_create(s_scr_qr);
        lv_obj_set_style_text_color(s_qr_code, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(s_qr_code, &lv_font_montserrat_20, 0);
        lv_obj_align(s_qr_code, LV_ALIGN_BOTTOM_MID, 0, -10);
    }

    lv_qrcode_update(s_qr, data, strlen(data));
    lv_label_set_text(s_qr_caption, caption);
    if (code) {
        lv_label_set_text(s_qr_code, code);
        lv_obj_clear_flag(s_qr_code, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_qr_code, LV_OBJ_FLAG_HIDDEN);
    }

    lv_screen_load(s_scr_qr);
    if (s_qr_timer) {
        lv_timer_delete(s_qr_timer);
    }
    s_qr_timer = lv_timer_create(qr_return_cb, ms, NULL);
    lv_timer_set_repeat_count(s_qr_timer, 1);

    lvgl_port_unlock();
}

void face_ui_show_qr(const char *url, uint32_t ms)
{
    qr_show(url, "Scan to set up habits", NULL, ms);
}

void face_ui_show_device_code(const char *url, const char *code, uint32_t ms)
{
    qr_show(url, "Scan to connect Google", code, ms);
}

/* ================= volume overlay ================= */

static lv_obj_t *s_vol_box;
static lv_obj_t *s_vol_bar;
static lv_obj_t *s_vol_lbl;
static lv_timer_t *s_vol_timer;

static void vol_hide_cb(lv_timer_t *t)
{
    (void)t;
    s_vol_timer = NULL;
    if (s_vol_box) {
        lv_obj_add_flag(s_vol_box, LV_OBJ_FLAG_HIDDEN);
    }
}

void face_ui_show_volume(uint8_t pct)
{
    if (pct > 100) {
        pct = 100;
    }
    lvgl_port_lock(0);

    if (!s_vol_box) {
        /* top layer floats over whatever screen is active */
        lv_obj_t *top = lv_layer_top();
        s_vol_box = lv_obj_create(top);
        lv_obj_remove_style_all(s_vol_box);
        lv_obj_set_size(s_vol_box, 40, 200);
        lv_obj_align(s_vol_box, LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_set_style_bg_color(s_vol_box, lv_color_hex(0x151b23), 0);
        lv_obj_set_style_bg_opa(s_vol_box, LV_OPA_90, 0);
        lv_obj_set_style_radius(s_vol_box, 12, 0);
        lv_obj_set_style_pad_all(s_vol_box, 6, 0);
        lv_obj_clear_flag(s_vol_box, LV_OBJ_FLAG_SCROLLABLE);

        s_vol_bar = lv_bar_create(s_vol_box);
        lv_bar_set_range(s_vol_bar, 0, 100);
        lv_obj_set_size(s_vol_bar, 12, 150);
        lv_obj_align(s_vol_bar, LV_ALIGN_TOP_MID, 0, 4);
        lv_obj_set_style_bg_color(s_vol_bar, lv_color_hex(0x2a3542), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_vol_bar, lv_color_hex(0xFF5A8A), LV_PART_INDICATOR);

        s_vol_lbl = lv_label_create(s_vol_box);
        lv_obj_set_style_text_font(s_vol_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_vol_lbl, lv_color_hex(0xEAF6FF), 0);
        lv_obj_align(s_vol_lbl, LV_ALIGN_BOTTOM_MID, 0, 0);
    }

    lv_bar_set_value(s_vol_bar, pct, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_vol_lbl, "%d", pct);
    lv_obj_clear_flag(s_vol_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_vol_box);

    if (s_vol_timer) {
        lv_timer_reset(s_vol_timer);
    } else {
        s_vol_timer = lv_timer_create(vol_hide_cb, 1200, NULL);
        lv_timer_set_repeat_count(s_vol_timer, 1);
    }

    lvgl_port_unlock();
}

/* ================= generic list screen ================= */

static lv_obj_t *s_scr_list;
static lv_obj_t *s_list_title;
static lv_obj_t *s_list_body;
static lv_timer_t *s_list_timer;

static void list_return_cb(lv_timer_t *t)
{
    (void)t;
    s_list_timer = NULL;
    lv_screen_load(s_scr_face);
}

void face_ui_show_list(const char *title, const char *body, uint32_t ms)
{
    lvgl_port_lock(0);

    if (!s_scr_list) {
        s_scr_list = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(s_scr_list, lv_color_hex(0x000000), 0);
        lv_obj_set_style_pad_all(s_scr_list, 12, 0);

        s_list_title = lv_label_create(s_scr_list);
        lv_obj_set_style_text_font(s_list_title, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(s_list_title, lv_color_hex(0xFF5A8A), 0);
        lv_obj_align(s_list_title, LV_ALIGN_TOP_MID, 0, 0);

        s_list_body = lv_label_create(s_scr_list);
        lv_obj_set_style_text_font(s_list_body, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_list_body, lv_color_hex(0xEAF6FF), 0);
        lv_label_set_long_mode(s_list_body, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(s_list_body, CONFIG_MOO_LCD_H_RES - 24);
        lv_obj_align(s_list_body, LV_ALIGN_TOP_LEFT, 0, 34);
    }

    lv_label_set_text(s_list_title, title ? title : "");
    lv_label_set_text(s_list_body, body ? body : "");

    lv_screen_load(s_scr_list);
    if (s_list_timer) {
        lv_timer_delete(s_list_timer);
    }
    s_list_timer = lv_timer_create(list_return_cb, ms, NULL);
    lv_timer_set_repeat_count(s_list_timer, 1);

    lvgl_port_unlock();
}

/* ================= AI screen ================= */

static lv_obj_t *s_scr_ai;
static lv_obj_t *s_lbl_ai_status;
static lv_obj_t *s_lbl_ai_detail;
static lv_obj_t *s_ai_meter;
static lv_timer_t *s_ai_meter_timer;
static void (*s_ai_tap_cb)(void);
static uint8_t (*s_level_src)(void);
static ai_state_t s_ai_state = AI_IDLE;

void face_ui_set_ai_tap_cb(void (*cb)(void))   { s_ai_tap_cb = cb; }
void face_ui_set_level_source(uint8_t (*fn)(void)) { s_level_src = fn; }

bool face_ui_on_ai_screen(void)
{
    return s_scr_ai && lv_screen_active() == s_scr_ai;
}

static void ai_tapped_cb(lv_event_t *e)
{
    (void)e;
    if (s_ai_tap_cb) {
        s_ai_tap_cb();
    }
}

/* Only runs while listening — the meter is the one thing on this screen
 * that tells you the mic is alive. */
static void ai_meter_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_ai_meter) {
        return;
    }
    const bool live = (s_ai_state == AI_LISTENING) && s_level_src;
    lv_bar_set_value(s_ai_meter, live ? s_level_src() : 0, LV_ANIM_OFF);
}

static void ai_screen_ensure(void)
{
    if (s_scr_ai) {
        return;
    }
    s_scr_ai = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_ai, lv_color_hex(0x050A14), 0);
    lv_obj_add_flag(s_scr_ai, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_scr_ai, ai_tapped_cb, LV_EVENT_CLICKED, NULL);

    s_lbl_ai_status = lv_label_create(s_scr_ai);
    lv_obj_set_style_text_font(s_lbl_ai_status, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_lbl_ai_status, lv_color_hex(0xEAF6FF), 0);
    lv_label_set_text(s_lbl_ai_status, "Tap to talk");
    lv_obj_align(s_lbl_ai_status, LV_ALIGN_CENTER, 0, -34);

    s_lbl_ai_detail = lv_label_create(s_scr_ai);
    lv_obj_set_style_text_font(s_lbl_ai_detail, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_ai_detail, lv_color_hex(0x7F92A3), 0);
    lv_label_set_text(s_lbl_ai_detail, "ask me for a story");
    lv_obj_align(s_lbl_ai_detail, LV_ALIGN_CENTER, 0, -6);

    s_ai_meter = lv_bar_create(s_scr_ai);
    lv_obj_set_size(s_ai_meter, 200, 14);
    lv_obj_align(s_ai_meter, LV_ALIGN_CENTER, 0, 40);
    lv_bar_set_range(s_ai_meter, 0, 100);
    lv_bar_set_value(s_ai_meter, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ai_meter, lv_color_hex(0x16202E), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ai_meter, lv_color_hex(0x50A0FF), LV_PART_INDICATOR);

    s_ai_meter_timer = lv_timer_create(ai_meter_cb, 100, NULL);
}

void face_ui_ai_set_state(ai_state_t state, const char *detail)
{
    lvgl_port_lock(0);
    ai_screen_ensure();
    s_ai_state = state;

    static const char *TEXT[] = {
        [AI_IDLE]      = "Tap to talk",
        [AI_LISTENING] = "Listening...",
        [AI_THINKING]  = "Thinking...",
        [AI_SPEAKING]  = "Telling a story",
        [AI_FAILED]    = "Let's try again",
    };
    static const uint32_t COLOUR[] = {
        [AI_IDLE]      = 0xEAF6FF,
        [AI_LISTENING] = 0x50A0FF,
        [AI_THINKING]  = 0xB478FF,
        [AI_SPEAKING]  = 0x78FF96,
        [AI_FAILED]    = 0xFF8080,
    };
    lv_label_set_text(s_lbl_ai_status, TEXT[state]);
    lv_obj_set_style_text_color(s_lbl_ai_status, lv_color_hex(COLOUR[state]), 0);
    lv_label_set_text(s_lbl_ai_detail, detail ? detail :
                      (state == AI_IDLE ? "ask me for a story" : ""));
    lvgl_port_unlock();
}

/* ================= screen cycle ================= */

void face_ui_next_screen(void)
{
    lvgl_port_lock(0);
    clock_screen_ensure();
    ai_screen_ensure();

    lv_obj_t *active = lv_screen_active();

    if (s_return_timer) {
        lv_timer_delete(s_return_timer);
        s_return_timer = NULL;
    }

    if (active == s_scr_face) {
        lv_screen_load_anim(s_scr_clock, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
        s_return_timer = lv_timer_create(clock_return_cb, CLOCK_RETURN_MS, NULL);
        lv_timer_set_repeat_count(s_return_timer, 1);
    } else if (active == s_scr_clock) {
        /* No auto-return here: walking away mid-story to the face screen
         * would be worse than leaving it up. */
        lv_screen_load_anim(s_scr_ai, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
    } else {
        lv_screen_load_anim(s_scr_face, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
    }
    lvgl_port_unlock();
}

#if CONFIG_MOO_FACE_SPRITES
/* ================= sprite face ================= */

#include "anim_engine.h"
#include "cow_anims.h"

#define BG_COLOR        0x000000   /* must match sheet background */
#define BLINK_MIN_MS    3000
#define BLINK_JITTER_MS 5000

typedef struct {
    lv_obj_t *player;
    lv_timer_t *idle_timer;
    bool busy;                     /* reaction anim in flight */
} face_t;

static face_t s_face;

static void idle_timer_cb(lv_timer_t *t)
{
    if (!s_face.busy) {
        const moo_anim_t *a = &moo_anim_idle_blink;
#ifdef MOO_HAS_ANIM_IDLE_LOOK
        if (lv_rand(0, 3) == 0) {          /* 1-in-4 idles glance instead of blink */
            a = &moo_anim_idle_look;
        }
#endif
        anim_engine_play(s_face.player, a, NULL, NULL);
    }
    lv_timer_set_period(t, BLINK_MIN_MS + lv_rand(0, BLINK_JITTER_MS));
}

static void reaction_done_cb(void *ctx)
{
    (void)ctx;
    /* back to neutral open-eyes frame */
    lv_image_set_src(s_face.player, moo_anim_idle_blink.frames[0]);
    s_face.busy = false;
}

void face_ui_start(lv_display_t *disp)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    s_scr_face = scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(BG_COLOR), 0);

    s_face.player = anim_engine_create(scr);
    lv_image_set_src(s_face.player, moo_anim_idle_blink.frames[0]);
    add_face_hitbox(scr);

    s_face.idle_timer = lv_timer_create(idle_timer_cb, BLINK_MIN_MS, NULL);
    lv_timer_create(diag_redraw_timer_cb, 3000, NULL);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "sprite face started (cow)");
}

static const moo_anim_t *react_anim(face_react_t r)
{
    switch (r) {
#ifdef MOO_HAS_ANIM_PAT
    case FACE_REACT_PAT:       return &moo_anim_pat;
#endif
#ifdef MOO_HAS_ANIM_HAPPY
    case FACE_REACT_HAPPY:     return &moo_anim_happy;
#endif
#ifdef MOO_HAS_ANIM_CELEBRATE
    case FACE_REACT_CELEBRATE: return &moo_anim_celebrate;
#endif
#ifdef MOO_HAS_ANIM_SAD
    case FACE_REACT_SAD:       return &moo_anim_sad;
#endif
#ifdef MOO_HAS_ANIM_SLEEPY
    case FACE_REACT_SLEEPY:    return &moo_anim_sleepy;
#endif
#ifdef MOO_HAS_ANIM_ALERT
    case FACE_REACT_ALERT:     return &moo_anim_alert;
#endif
#ifdef MOO_HAS_ANIM_TASK_DONE
    case FACE_REACT_TASK_DONE: return &moo_anim_task_done;
#endif
#ifdef MOO_HAS_ANIM_WAKE
    case FACE_REACT_WAKE:      return &moo_anim_wake;
#endif
#ifdef MOO_HAS_ANIM_EAT
    case FACE_REACT_EAT:       return &moo_anim_eat;
#endif
    default:                   return NULL;
    }
}

/* Looping anims (sleepy, eat) have no natural end; until the real state
 * machine drives enter/exit, time them out back to idle. */
#define LOOP_REACT_TIMEOUT_MS 3000

static void loop_stop_timer_cb(lv_timer_t *t)
{
    (void)t;
    anim_engine_stop(s_face.player);
    reaction_done_cb(NULL);
}

bool face_ui_react(face_react_t reaction)
{
    const moo_anim_t *anim = react_anim(reaction);
    if (!anim) {
        return false;
    }

    bool played = false;
    lvgl_port_lock(0);
    if (!s_face.busy) {
        s_face.busy = true;
        anim_engine_play(s_face.player, anim, reaction_done_cb, NULL);
        if (anim->loop) {
            lv_timer_t *t = lv_timer_create(loop_stop_timer_cb, LOOP_REACT_TIMEOUT_MS, NULL);
            lv_timer_set_repeat_count(t, 1);
        }
        played = true;
    }
    lvgl_port_unlock();
    return played;
}

#else
/* ================= procedural fallback ================= */

#define BG_COLOR      0x0B0F14
#define EYE_COLOR     0xEAF6FF
#define MOUTH_COLOR   0xEAF6FF

#define EYE_W         64
#define EYE_H         84
#define EYE_OFFS_X    52
#define EYE_OFFS_Y    (-36)
#define EYE_H_CLOSED  8

#define BLINK_MIN_MS      3000
#define BLINK_JITTER_MS   5000
#define SACCADE_MIN_MS    4000
#define SACCADE_JITTER_MS 5000

typedef struct {
    lv_obj_t *eye_l;
    lv_obj_t *eye_r;
    lv_obj_t *mouth;
    lv_timer_t *blink_timer;
    bool pat_active;
} face_t;

static face_t s_face;

static void eye_height_anim_cb(void *obj, int32_t v)
{
    lv_obj_set_height(obj, v);
}

static void eye_shift_anim_cb(void *obj, int32_t v)
{
    lv_obj_set_style_translate_x(obj, v, 0);
}

static void blink_eye(lv_obj_t *eye)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, eye);
    lv_anim_set_exec_cb(&a, eye_height_anim_cb);
    lv_anim_set_values(&a, EYE_H, EYE_H_CLOSED);
    lv_anim_set_duration(&a, 70);
    lv_anim_set_playback_duration(&a, 90);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void blink_timer_cb(lv_timer_t *t)
{
    blink_eye(s_face.eye_l);
    blink_eye(s_face.eye_r);
    lv_timer_set_period(t, BLINK_MIN_MS + lv_rand(0, BLINK_JITTER_MS));
}

static void saccade_eye(lv_obj_t *eye, int32_t dx)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, eye);
    lv_anim_set_exec_cb(&a, eye_shift_anim_cb);
    lv_anim_set_values(&a, 0, dx);
    lv_anim_set_duration(&a, 120);
    lv_anim_set_playback_delay(&a, 450);
    lv_anim_set_playback_duration(&a, 150);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void saccade_timer_cb(lv_timer_t *t)
{
    const int32_t dx = (int32_t)lv_rand(0, 20) - 10;
    saccade_eye(s_face.eye_l, dx);
    saccade_eye(s_face.eye_r, dx);
    lv_timer_set_period(t, SACCADE_MIN_MS + lv_rand(0, SACCADE_JITTER_MS));
}

static void eye_squint(lv_obj_t *eye, int32_t to_h)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, eye);
    lv_anim_set_exec_cb(&a, eye_height_anim_cb);
    lv_anim_set_values(&a, lv_obj_get_height(eye), to_h);
    lv_anim_set_duration(&a, 100);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void pat_end_timer_cb(lv_timer_t *t)
{
    (void)t;
    eye_squint(s_face.eye_l, EYE_H);
    eye_squint(s_face.eye_r, EYE_H);
    lv_arc_set_bg_angles(s_face.mouth, 35, 145);
    lv_timer_resume(s_face.blink_timer);
    s_face.pat_active = false;
}

static lv_obj_t *eye_create(lv_obj_t *parent, int32_t x_offs)
{
    lv_obj_t *eye = lv_obj_create(parent);
    lv_obj_remove_style_all(eye);
    lv_obj_set_size(eye, EYE_W, EYE_H);
    lv_obj_align(eye, LV_ALIGN_CENTER, x_offs, EYE_OFFS_Y);
    lv_obj_set_style_bg_color(eye, lv_color_hex(EYE_COLOR), 0);
    lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(eye, 26, 0);
    return eye;
}

static lv_obj_t *mouth_create(lv_obj_t *parent)
{
    lv_obj_t *mouth = lv_arc_create(parent);
    lv_obj_remove_style(mouth, NULL, LV_PART_KNOB);
    lv_obj_remove_style(mouth, NULL, LV_PART_INDICATOR);
    lv_obj_remove_flag(mouth, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(mouth, 96, 96);
    lv_obj_align(mouth, LV_ALIGN_CENTER, 0, 56);
    lv_arc_set_bg_angles(mouth, 35, 145);
    lv_arc_set_value(mouth, 0);
    lv_obj_set_style_arc_width(mouth, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(mouth, true, LV_PART_MAIN);
    lv_obj_set_style_arc_color(mouth, lv_color_hex(MOUTH_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(mouth, LV_OPA_TRANSP, 0);
    return mouth;
}

void face_ui_start(lv_display_t *disp)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    s_scr_face = scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(BG_COLOR), 0);

    s_face.eye_l = eye_create(scr, -EYE_OFFS_X);
    s_face.eye_r = eye_create(scr, EYE_OFFS_X);
    s_face.mouth = mouth_create(scr);
    add_face_hitbox(scr);

    s_face.blink_timer = lv_timer_create(blink_timer_cb, BLINK_MIN_MS, NULL);
    lv_timer_create(saccade_timer_cb, SACCADE_MIN_MS + 1500, NULL);
    lv_timer_create(diag_redraw_timer_cb, 3000, NULL);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "procedural face started");
}

bool face_ui_react(face_react_t reaction)
{
    (void)reaction;   /* procedural fallback has one reaction only */
    bool played = false;
    lvgl_port_lock(0);
    if (!s_face.pat_active) {
        s_face.pat_active = true;
        lv_timer_pause(s_face.blink_timer);
        eye_squint(s_face.eye_l, 30);
        eye_squint(s_face.eye_r, 30);
        lv_arc_set_bg_angles(s_face.mouth, 20, 160);
        lv_timer_t *t = lv_timer_create(pat_end_timer_cb, 1200, NULL);
        lv_timer_set_repeat_count(t, 1);
        played = true;
    }
    lvgl_port_unlock();
    return played;
}

#endif /* CONFIG_MOO_FACE_SPRITES */
