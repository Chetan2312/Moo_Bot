#pragma once

#include "anim_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOO_HAS_ANIM_IDLE_BLINK 1
#define MOO_HAS_ANIM_IDLE_LOOK 1
#define MOO_HAS_ANIM_PAT 1
#define MOO_HAS_ANIM_HAPPY 1
#define MOO_HAS_ANIM_CELEBRATE 1
#define MOO_HAS_ANIM_SAD 1
#define MOO_HAS_ANIM_SLEEPY 1
#define MOO_HAS_ANIM_ALERT 1
#define MOO_HAS_ANIM_TASK_DONE 1
#define MOO_HAS_ANIM_WAKE 1
#define MOO_HAS_ANIM_EAT 1

extern const moo_anim_t moo_anim_idle_blink;
extern const moo_anim_t moo_anim_idle_look;
extern const moo_anim_t moo_anim_pat;
extern const moo_anim_t moo_anim_happy;
extern const moo_anim_t moo_anim_celebrate;
extern const moo_anim_t moo_anim_sad;
extern const moo_anim_t moo_anim_sleepy;
extern const moo_anim_t moo_anim_alert;
extern const moo_anim_t moo_anim_task_done;
extern const moo_anim_t moo_anim_wake;
extern const moo_anim_t moo_anim_eat;

#ifdef __cplusplus
}
#endif
