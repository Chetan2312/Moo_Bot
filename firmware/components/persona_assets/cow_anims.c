#include "cow_anims.h"

extern const lv_image_dsc_t img_cow_01_idle_blink_f00;
extern const lv_image_dsc_t img_cow_01_idle_blink_f01;
extern const lv_image_dsc_t img_cow_01_idle_blink_f02;
extern const lv_image_dsc_t img_cow_01_idle_blink_f03;
extern const lv_image_dsc_t img_cow_01_idle_blink_f04;
extern const lv_image_dsc_t img_cow_01_idle_blink_f05;
extern const lv_image_dsc_t img_cow_01_idle_blink_f06;
extern const lv_image_dsc_t img_cow_01_idle_blink_f07;
extern const lv_image_dsc_t img_cow_01_idle_blink_f08;
static const lv_image_dsc_t *frames_idle_blink[] = {
    &img_cow_01_idle_blink_f00,
    &img_cow_01_idle_blink_f01,
    &img_cow_01_idle_blink_f02,
    &img_cow_01_idle_blink_f03,
    &img_cow_01_idle_blink_f04,
    &img_cow_01_idle_blink_f05,
    &img_cow_01_idle_blink_f06,
    &img_cow_01_idle_blink_f07,
    &img_cow_01_idle_blink_f08,
};
const moo_anim_t moo_anim_idle_blink = {
    .frames = frames_idle_blink,
    .count = 9,
    .fps = 12,
    .loop = false,
};

extern const lv_image_dsc_t img_cow_02_idle_look_f00;
extern const lv_image_dsc_t img_cow_02_idle_look_f01;
extern const lv_image_dsc_t img_cow_02_idle_look_f02;
extern const lv_image_dsc_t img_cow_02_idle_look_f03;
extern const lv_image_dsc_t img_cow_02_idle_look_f04;
extern const lv_image_dsc_t img_cow_02_idle_look_f05;
extern const lv_image_dsc_t img_cow_02_idle_look_f06;
extern const lv_image_dsc_t img_cow_02_idle_look_f07;
extern const lv_image_dsc_t img_cow_02_idle_look_f08;
static const lv_image_dsc_t *frames_idle_look[] = {
    &img_cow_02_idle_look_f00,
    &img_cow_02_idle_look_f01,
    &img_cow_02_idle_look_f02,
    &img_cow_02_idle_look_f03,
    &img_cow_02_idle_look_f04,
    &img_cow_02_idle_look_f05,
    &img_cow_02_idle_look_f06,
    &img_cow_02_idle_look_f07,
    &img_cow_02_idle_look_f08,
};
const moo_anim_t moo_anim_idle_look = {
    .frames = frames_idle_look,
    .count = 9,
    .fps = 10,
    .loop = false,
};

extern const lv_image_dsc_t img_cow_03_pat_f00;
extern const lv_image_dsc_t img_cow_03_pat_f01;
extern const lv_image_dsc_t img_cow_03_pat_f02;
extern const lv_image_dsc_t img_cow_03_pat_f03;
extern const lv_image_dsc_t img_cow_03_pat_f04;
extern const lv_image_dsc_t img_cow_03_pat_f05;
extern const lv_image_dsc_t img_cow_03_pat_f06;
extern const lv_image_dsc_t img_cow_03_pat_f07;
extern const lv_image_dsc_t img_cow_03_pat_f08;
static const lv_image_dsc_t *frames_pat[] = {
    &img_cow_03_pat_f00,
    &img_cow_03_pat_f01,
    &img_cow_03_pat_f02,
    &img_cow_03_pat_f03,
    &img_cow_03_pat_f04,
    &img_cow_03_pat_f05,
    &img_cow_03_pat_f06,
    &img_cow_03_pat_f07,
    &img_cow_03_pat_f08,
};
const moo_anim_t moo_anim_pat = {
    .frames = frames_pat,
    .count = 9,
    .fps = 12,
    .loop = false,
};

extern const lv_image_dsc_t img_cow_04_happy_f00;
extern const lv_image_dsc_t img_cow_04_happy_f01;
extern const lv_image_dsc_t img_cow_04_happy_f02;
extern const lv_image_dsc_t img_cow_04_happy_f03;
extern const lv_image_dsc_t img_cow_04_happy_f04;
extern const lv_image_dsc_t img_cow_04_happy_f05;
extern const lv_image_dsc_t img_cow_04_happy_f06;
extern const lv_image_dsc_t img_cow_04_happy_f07;
extern const lv_image_dsc_t img_cow_04_happy_f08;
static const lv_image_dsc_t *frames_happy[] = {
    &img_cow_04_happy_f00,
    &img_cow_04_happy_f01,
    &img_cow_04_happy_f02,
    &img_cow_04_happy_f03,
    &img_cow_04_happy_f04,
    &img_cow_04_happy_f05,
    &img_cow_04_happy_f06,
    &img_cow_04_happy_f07,
    &img_cow_04_happy_f08,
};
const moo_anim_t moo_anim_happy = {
    .frames = frames_happy,
    .count = 9,
    .fps = 12,
    .loop = false,
};

extern const lv_image_dsc_t img_cow_05_celebrate_f00;
extern const lv_image_dsc_t img_cow_05_celebrate_f01;
extern const lv_image_dsc_t img_cow_05_celebrate_f02;
extern const lv_image_dsc_t img_cow_05_celebrate_f03;
extern const lv_image_dsc_t img_cow_05_celebrate_f04;
extern const lv_image_dsc_t img_cow_05_celebrate_f05;
extern const lv_image_dsc_t img_cow_05_celebrate_f06;
extern const lv_image_dsc_t img_cow_05_celebrate_f07;
extern const lv_image_dsc_t img_cow_05_celebrate_f08;
static const lv_image_dsc_t *frames_celebrate[] = {
    &img_cow_05_celebrate_f00,
    &img_cow_05_celebrate_f01,
    &img_cow_05_celebrate_f02,
    &img_cow_05_celebrate_f03,
    &img_cow_05_celebrate_f04,
    &img_cow_05_celebrate_f05,
    &img_cow_05_celebrate_f06,
    &img_cow_05_celebrate_f07,
    &img_cow_05_celebrate_f08,
};
const moo_anim_t moo_anim_celebrate = {
    .frames = frames_celebrate,
    .count = 9,
    .fps = 14,
    .loop = false,
};

extern const lv_image_dsc_t img_cow_06_sad_f00;
extern const lv_image_dsc_t img_cow_06_sad_f01;
extern const lv_image_dsc_t img_cow_06_sad_f02;
extern const lv_image_dsc_t img_cow_06_sad_f03;
extern const lv_image_dsc_t img_cow_06_sad_f04;
extern const lv_image_dsc_t img_cow_06_sad_f05;
extern const lv_image_dsc_t img_cow_06_sad_f06;
extern const lv_image_dsc_t img_cow_06_sad_f07;
extern const lv_image_dsc_t img_cow_06_sad_f08;
static const lv_image_dsc_t *frames_sad[] = {
    &img_cow_06_sad_f00,
    &img_cow_06_sad_f01,
    &img_cow_06_sad_f02,
    &img_cow_06_sad_f03,
    &img_cow_06_sad_f04,
    &img_cow_06_sad_f05,
    &img_cow_06_sad_f06,
    &img_cow_06_sad_f07,
    &img_cow_06_sad_f08,
};
const moo_anim_t moo_anim_sad = {
    .frames = frames_sad,
    .count = 9,
    .fps = 8,
    .loop = false,
};

extern const lv_image_dsc_t img_cow_07_sleepy_f00;
extern const lv_image_dsc_t img_cow_07_sleepy_f01;
extern const lv_image_dsc_t img_cow_07_sleepy_f02;
extern const lv_image_dsc_t img_cow_07_sleepy_f03;
extern const lv_image_dsc_t img_cow_07_sleepy_f04;
extern const lv_image_dsc_t img_cow_07_sleepy_f05;
extern const lv_image_dsc_t img_cow_07_sleepy_f06;
extern const lv_image_dsc_t img_cow_07_sleepy_f07;
extern const lv_image_dsc_t img_cow_07_sleepy_f08;
static const lv_image_dsc_t *frames_sleepy[] = {
    &img_cow_07_sleepy_f00,
    &img_cow_07_sleepy_f01,
    &img_cow_07_sleepy_f02,
    &img_cow_07_sleepy_f03,
    &img_cow_07_sleepy_f04,
    &img_cow_07_sleepy_f05,
    &img_cow_07_sleepy_f06,
    &img_cow_07_sleepy_f07,
    &img_cow_07_sleepy_f08,
};
const moo_anim_t moo_anim_sleepy = {
    .frames = frames_sleepy,
    .count = 9,
    .fps = 6,
    .loop = true,
};

extern const lv_image_dsc_t img_cow_08_alert_f00;
extern const lv_image_dsc_t img_cow_08_alert_f01;
extern const lv_image_dsc_t img_cow_08_alert_f02;
extern const lv_image_dsc_t img_cow_08_alert_f03;
extern const lv_image_dsc_t img_cow_08_alert_f04;
extern const lv_image_dsc_t img_cow_08_alert_f05;
extern const lv_image_dsc_t img_cow_08_alert_f06;
extern const lv_image_dsc_t img_cow_08_alert_f07;
extern const lv_image_dsc_t img_cow_08_alert_f08;
static const lv_image_dsc_t *frames_alert[] = {
    &img_cow_08_alert_f00,
    &img_cow_08_alert_f01,
    &img_cow_08_alert_f02,
    &img_cow_08_alert_f03,
    &img_cow_08_alert_f04,
    &img_cow_08_alert_f05,
    &img_cow_08_alert_f06,
    &img_cow_08_alert_f07,
    &img_cow_08_alert_f08,
};
const moo_anim_t moo_anim_alert = {
    .frames = frames_alert,
    .count = 9,
    .fps = 12,
    .loop = false,
};

extern const lv_image_dsc_t img_cow_09_task_done_f00;
extern const lv_image_dsc_t img_cow_09_task_done_f01;
extern const lv_image_dsc_t img_cow_09_task_done_f02;
extern const lv_image_dsc_t img_cow_09_task_done_f03;
extern const lv_image_dsc_t img_cow_09_task_done_f04;
extern const lv_image_dsc_t img_cow_09_task_done_f05;
extern const lv_image_dsc_t img_cow_09_task_done_f06;
extern const lv_image_dsc_t img_cow_09_task_done_f07;
extern const lv_image_dsc_t img_cow_09_task_done_f08;
static const lv_image_dsc_t *frames_task_done[] = {
    &img_cow_09_task_done_f00,
    &img_cow_09_task_done_f01,
    &img_cow_09_task_done_f02,
    &img_cow_09_task_done_f03,
    &img_cow_09_task_done_f04,
    &img_cow_09_task_done_f05,
    &img_cow_09_task_done_f06,
    &img_cow_09_task_done_f07,
    &img_cow_09_task_done_f08,
};
const moo_anim_t moo_anim_task_done = {
    .frames = frames_task_done,
    .count = 9,
    .fps = 12,
    .loop = false,
};

extern const lv_image_dsc_t img_cow_10_wake_f00;
extern const lv_image_dsc_t img_cow_10_wake_f01;
extern const lv_image_dsc_t img_cow_10_wake_f02;
extern const lv_image_dsc_t img_cow_10_wake_f03;
extern const lv_image_dsc_t img_cow_10_wake_f04;
extern const lv_image_dsc_t img_cow_10_wake_f05;
extern const lv_image_dsc_t img_cow_10_wake_f06;
extern const lv_image_dsc_t img_cow_10_wake_f07;
extern const lv_image_dsc_t img_cow_10_wake_f08;
static const lv_image_dsc_t *frames_wake[] = {
    &img_cow_10_wake_f00,
    &img_cow_10_wake_f01,
    &img_cow_10_wake_f02,
    &img_cow_10_wake_f03,
    &img_cow_10_wake_f04,
    &img_cow_10_wake_f05,
    &img_cow_10_wake_f06,
    &img_cow_10_wake_f07,
    &img_cow_10_wake_f08,
};
const moo_anim_t moo_anim_wake = {
    .frames = frames_wake,
    .count = 9,
    .fps = 10,
    .loop = false,
};

extern const lv_image_dsc_t img_cow_11_eat_f00;
extern const lv_image_dsc_t img_cow_11_eat_f01;
extern const lv_image_dsc_t img_cow_11_eat_f02;
extern const lv_image_dsc_t img_cow_11_eat_f03;
extern const lv_image_dsc_t img_cow_11_eat_f04;
extern const lv_image_dsc_t img_cow_11_eat_f05;
extern const lv_image_dsc_t img_cow_11_eat_f06;
extern const lv_image_dsc_t img_cow_11_eat_f07;
extern const lv_image_dsc_t img_cow_11_eat_f08;
static const lv_image_dsc_t *frames_eat[] = {
    &img_cow_11_eat_f00,
    &img_cow_11_eat_f01,
    &img_cow_11_eat_f02,
    &img_cow_11_eat_f03,
    &img_cow_11_eat_f04,
    &img_cow_11_eat_f05,
    &img_cow_11_eat_f06,
    &img_cow_11_eat_f07,
    &img_cow_11_eat_f08,
};
const moo_anim_t moo_anim_eat = {
    .frames = frames_eat,
    .count = 9,
    .fps = 8,
    .loop = true,
};
