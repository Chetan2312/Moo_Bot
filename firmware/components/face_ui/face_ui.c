#include "face_ui.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "face_ui";

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

/* DIAG: full-screen redraw every 3 s. Accumulated transfer corruption gets
 * wiped each pass; panel-level defects survive it. Remove after bringup. */
static void diag_redraw_timer_cb(lv_timer_t *t)
{
    (void)t;
    lv_obj_invalidate(lv_screen_active());
}

/* ---------------- pat reaction ---------------- */

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

void face_ui_pat(void)
{
    lvgl_port_lock(0);
    if (!s_face.pat_active) {
        s_face.pat_active = true;
        lv_timer_pause(s_face.blink_timer);   /* blink would fight the squint */
        eye_squint(s_face.eye_l, 30);
        eye_squint(s_face.eye_r, 30);
        lv_arc_set_bg_angles(s_face.mouth, 20, 160);
        lv_timer_t *t = lv_timer_create(pat_end_timer_cb, 1200, NULL);
        lv_timer_set_repeat_count(t, 1);      /* one-shot, auto-deletes */
    }
    lvgl_port_unlock();
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
    lv_obj_set_style_bg_color(scr, lv_color_hex(BG_COLOR), 0);

    s_face.eye_l = eye_create(scr, -EYE_OFFS_X);
    s_face.eye_r = eye_create(scr, EYE_OFFS_X);
    s_face.mouth = mouth_create(scr);

    s_face.blink_timer = lv_timer_create(blink_timer_cb, BLINK_MIN_MS, NULL);
    lv_timer_create(saccade_timer_cb, SACCADE_MIN_MS + 1500, NULL);
    lv_timer_create(diag_redraw_timer_cb, 3000, NULL);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "idle face started");
}
