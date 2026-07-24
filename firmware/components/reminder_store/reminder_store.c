#include "reminder_store.h"

#include <string.h>
#include <time.h>

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "reminder_store";

#define NVS_NS   "moo"
#define NVS_KEY  "reminders"
#define TICK_MS  10000

static moo_reminder_t s_list[REMINDER_MAX];
static size_t s_count;
static reminder_fire_cb_t s_cb;
static void *s_cb_ctx;
static volatile bool s_pending;

static esp_err_t save(void)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "nvs open");
    esp_err_t err = nvs_set_blob(h, NVS_KEY, s_list, s_count * sizeof(moo_reminder_t));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static void load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;                                  /* first boot */
    }
    size_t len = sizeof(s_list);
    if (nvs_get_blob(h, NVS_KEY, s_list, &len) == ESP_OK) {
        s_count = len / sizeof(moo_reminder_t);
    }
    nvs_close(h);
}

static void reminder_task(void *arg)
{
    (void)arg;
    int last_min = -1;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));

        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);
        if (t.tm_year < 124 || t.tm_min == last_min) {   /* time unset or same minute */
            continue;
        }
        last_min = t.tm_min;

        for (size_t i = 0; i < s_count; i++) {
            const moo_reminder_t *r = &s_list[i];
            if (r->enabled && r->hh == t.tm_hour && r->mm == t.tm_min &&
                (r->days_mask & (1u << t.tm_wday))) {
                ESP_LOGI(TAG, "fire: %02d:%02d '%s'", r->hh, r->mm, r->text);
                s_pending = true;
                if (s_cb) {
                    s_cb(r, s_cb_ctx);
                }
            }
        }
    }
}

esp_err_t reminder_store_init(reminder_fire_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;
    load();
    ESP_RETURN_ON_FALSE(
        xTaskCreate(reminder_task, "reminder", 3072, NULL, 4, NULL) == pdPASS,
        ESP_ERR_NO_MEM, TAG, "task");
    ESP_LOGI(TAG, "loaded %u reminders", (unsigned)s_count);
    return ESP_OK;
}

esp_err_t reminder_store_add(const moo_reminder_t *r)
{
    ESP_RETURN_ON_FALSE(r && r->hh < 24 && r->mm < 60, ESP_ERR_INVALID_ARG, TAG, "bad arg");
    ESP_RETURN_ON_FALSE(s_count < REMINDER_MAX, ESP_ERR_NO_MEM, TAG, "full");
    s_list[s_count++] = *r;
    return save();
}

esp_err_t reminder_store_clear_all(void)
{
    s_count = 0;
    return save();
}

size_t reminder_store_count(void)
{
    return s_count;
}

bool reminder_store_pending(void)
{
    return s_pending;
}

void reminder_store_ack(void)
{
    s_pending = false;
}
