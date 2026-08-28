#include "ext_reminders.h"

#include <string.h>
#include <time.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "ext_reminders";

#define TICK_MS       15000
#define FIRE_WINDOW_S 150          /* fire if now within [due, due+window) */

static moo_ext_reminder_t s_list[EXT_REMINDER_MAX];
static size_t s_count;
static ext_reminder_cb_t s_cb;
static void *s_cb_ctx;
static SemaphoreHandle_t s_lock;
static volatile bool s_pending;

static bool same_reminder(const moo_ext_reminder_t *a, const moo_ext_reminder_t *b)
{
    return a->due_epoch == b->due_epoch && strcmp(a->title, b->title) == 0;
}

static void ext_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));

        const time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);
        if (t.tm_year < 124) {
            continue;                          /* time not set yet */
        }

        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (size_t i = 0; i < s_count; i++) {
            moo_ext_reminder_t *r = &s_list[i];
            if (!r->fired && now >= r->due_epoch &&
                now < r->due_epoch + FIRE_WINDOW_S) {
                r->fired = true;
                s_pending = true;
                ESP_LOGI(TAG, "fire: '%s' (%s)", r->title,
                         r->source == EXT_SRC_TASK ? "task" : "calendar");
                if (s_cb) {
                    s_cb(r, s_cb_ctx);
                }
            }
        }
        xSemaphoreGive(s_lock);
    }
}

esp_err_t ext_reminders_init(ext_reminder_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");
    /* callback fires face_ui + blocking audio, so give it room */
    ESP_RETURN_ON_FALSE(
        xTaskCreate(ext_task, "ext_rem", 4096, NULL, 4, NULL) == pdPASS,
        ESP_ERR_NO_MEM, TAG, "task");
    ESP_LOGI(TAG, "started");
    return ESP_OK;
}

esp_err_t ext_reminders_replace(const moo_ext_reminder_t *list, size_t n)
{
    if (n > EXT_REMINDER_MAX) {
        n = EXT_REMINDER_MAX;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);

    moo_ext_reminder_t next[EXT_REMINDER_MAX];
    for (size_t i = 0; i < n; i++) {
        next[i] = list[i];
        next[i].fired = false;
        /* carry over fired state from the current set */
        for (size_t j = 0; j < s_count; j++) {
            if (same_reminder(&next[i], &s_list[j])) {
                next[i].fired = s_list[j].fired;
                break;
            }
        }
    }
    memcpy(s_list, next, n * sizeof(moo_ext_reminder_t));
    s_count = n;

    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "replaced: %u reminders", (unsigned)n);
    return ESP_OK;
}

size_t ext_reminders_count(void)
{
    return s_count;
}

const moo_ext_reminder_t *ext_reminders_get(size_t idx)
{
    return idx < s_count ? &s_list[idx] : NULL;
}

bool ext_reminders_pending(void)
{
    return s_pending;
}

void ext_reminders_ack(void)
{
    s_pending = false;
}
