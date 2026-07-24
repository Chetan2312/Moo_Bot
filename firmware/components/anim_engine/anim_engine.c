#include "anim_engine.h"

#include <stdlib.h>

#include "esp_log.h"

static const char *TAG = "anim_engine";

typedef struct {
    const moo_anim_t *anim;
    anim_engine_done_cb_t done_cb;
    void *done_ctx;
    lv_timer_t *timer;
    uint8_t idx;
} player_state_t;

static void frame_timer_cb(lv_timer_t *t)
{
    lv_obj_t *player = lv_timer_get_user_data(t);
    player_state_t *st = lv_obj_get_user_data(player);

    st->idx++;
    if (st->idx >= st->anim->count) {
        if (st->anim->loop) {
            st->idx = 0;
        } else {
            st->idx = st->anim->count - 1;
            lv_timer_pause(st->timer);
            if (st->done_cb) {
                st->done_cb(st->done_ctx);
            }
            return;
        }
    }
    lv_image_set_src(player, st->anim->frames[st->idx]);
}

static void player_delete_cb(lv_event_t *e)
{
    player_state_t *st = lv_obj_get_user_data(lv_event_get_target(e));
    if (st) {
        if (st->timer) {
            lv_timer_delete(st->timer);
        }
        free(st);
    }
}

lv_obj_t *anim_engine_create(lv_obj_t *parent)
{
    lv_obj_t *player = lv_image_create(parent);

    player_state_t *st = calloc(1, sizeof(player_state_t));
    LV_ASSERT_MALLOC(st);
    lv_obj_set_user_data(player, st);

    st->timer = lv_timer_create(frame_timer_cb, 100, player);
    lv_timer_pause(st->timer);

    lv_obj_add_event_cb(player, player_delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_center(player);
    return player;
}

void anim_engine_play(lv_obj_t *player, const moo_anim_t *anim,
                      anim_engine_done_cb_t on_done, void *ctx)
{
    player_state_t *st = lv_obj_get_user_data(player);
    if (!st || !anim || anim->count == 0) {
        ESP_LOGW(TAG, "play: bad player/anim");
        return;
    }
    st->anim = anim;
    st->done_cb = on_done;
    st->done_ctx = ctx;
    st->idx = 0;

    lv_image_set_src(player, anim->frames[0]);
    lv_timer_set_period(st->timer, 1000 / (anim->fps ? anim->fps : 12));
    lv_timer_reset(st->timer);
    lv_timer_resume(st->timer);
}

void anim_engine_stop(lv_obj_t *player)
{
    player_state_t *st = lv_obj_get_user_data(player);
    if (st && st->timer) {
        lv_timer_pause(st->timer);
    }
}
