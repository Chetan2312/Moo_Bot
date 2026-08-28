#include "habit_store.h"

#include <string.h>
#include <time.h>

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "habit_store";

#define NVS_NS   "moo"
#define NVS_KEY  "habits"
#define TICK_MS  15000

static moo_habit_t s_list[HABIT_MAX];
static size_t s_count;
static habit_evt_cb_t s_cb;
static void *s_cb_ctx;
static SemaphoreHandle_t s_lock;
static uint8_t s_next_id = 1;
static volatile bool s_remind_pending;

static uint32_t today_ymd(void)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year < 124) {
        return 0;                                /* time not set yet */
    }
    return (uint32_t)(t.tm_year + 1900) * 10000 +
           (uint32_t)(t.tm_mon + 1) * 100 + (uint32_t)t.tm_mday;
}

static esp_err_t save_locked(void)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "nvs open");
    esp_err_t err = nvs_set_blob(h, NVS_KEY, s_list, s_count * sizeof(moo_habit_t));
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
        return;
    }
    size_t len = sizeof(s_list);
    if (nvs_get_blob(h, NVS_KEY, s_list, &len) == ESP_OK) {
        s_count = len / sizeof(moo_habit_t);
        for (size_t i = 0; i < s_count; i++) {
            if (s_list[i].id >= s_next_id) {
                s_next_id = s_list[i].id + 1;
            }
        }
    }
    nvs_close(h);
}

static bool all_done_locked(uint32_t today)
{
    bool any_scheduled = false;
    for (size_t i = 0; i < s_count; i++) {
        if (habit_scheduled_on(&s_list[i], today)) {
            any_scheduled = true;
            if (s_list[i].last_checked_ymd != today) {
                return false;
            }
        }
    }
    return any_scheduled;
}

/* One task: fires time reminders every minute + evaluates streak breaks
 * at day change. */
static void habit_task(void *arg)
{
    (void)arg;
    uint32_t last_roll_ymd = 0;
    int last_min = -1;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));

        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);
        if (t.tm_year < 124) {
            continue;                            /* time not set */
        }
        const uint32_t today = today_ymd();
        const int cur_min = t.tm_hour * 60 + t.tm_min;

        /* --- day rollover (streak breaks) --- */
        if (today != last_roll_ymd) {
            last_roll_ymd = today;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            const moo_habit_t *first_broken = NULL;
            for (size_t i = 0; i < s_count; i++) {
                if (streak_rollover(&s_list[i], today) && !first_broken) {
                    first_broken = &s_list[i];
                }
            }
            save_locked();
            xSemaphoreGive(s_lock);
            if (first_broken && s_cb) {
                s_cb(HABIT_EVT_STREAK_BROKEN, first_broken, s_cb_ctx);
            }
        }

        /* --- time reminders (once per minute boundary) --- */
        if (cur_min != last_min) {
            last_min = cur_min;
            for (size_t i = 0; i < s_count; i++) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                const moo_habit_t h = s_list[i];
                xSemaphoreGive(s_lock);
                if (h.remind_hh == t.tm_hour && h.remind_mm == t.tm_min &&
                    h.remind_hh != HABIT_NO_REMIND &&
                    habit_scheduled_on(&h, today) &&
                    h.last_checked_ymd != today) {     /* skip if already done */
                    ESP_LOGI(TAG, "remind: '%s' @ %02d:%02d", h.name, h.remind_hh, h.remind_mm);
                    s_remind_pending = true;
                    if (s_cb) {
                        s_cb(HABIT_EVT_REMIND, &s_list[i], s_cb_ctx);
                    }
                }
            }
        }
    }
}

esp_err_t habit_store_init(habit_evt_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");
    load();
    ESP_RETURN_ON_FALSE(
        xTaskCreate(habit_task, "habit", 3584, NULL, 4, NULL) == pdPASS,
        ESP_ERR_NO_MEM, TAG, "task");
    ESP_LOGI(TAG, "loaded %u habits", (unsigned)s_count);
    return ESP_OK;
}

esp_err_t habit_store_add(const char *name, uint8_t days_mask,
                          uint8_t remind_hh, uint8_t remind_mm)
{
    ESP_RETURN_ON_FALSE(name && days_mask, ESP_ERR_INVALID_ARG, TAG, "bad arg");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_ERR_NO_MEM;
    if (s_count < HABIT_MAX) {
        moo_habit_t *h = &s_list[s_count++];
        memset(h, 0, sizeof(*h));
        h->id = s_next_id++;
        strlcpy(h->name, name, sizeof(h->name));
        h->days_mask = days_mask;
        h->enabled = true;
        h->remind_hh = (remind_hh <= 23) ? remind_hh : HABIT_NO_REMIND;
        h->remind_mm = (remind_mm <= 59) ? remind_mm : 0;
        err = save_locked();
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t habit_store_remove(uint8_t id)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_ERR_NOT_FOUND;
    for (size_t i = 0; i < s_count; i++) {
        if (s_list[i].id == id) {
            for (size_t j = i; j + 1 < s_count; j++) {
                s_list[j] = s_list[j + 1];
            }
            s_count--;
            err = save_locked();
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

bool habit_store_reminder_pending(void)
{
    return s_remind_pending;
}

void habit_store_reminder_ack(void)
{
    s_remind_pending = false;
}

size_t habit_store_count(void)
{
    return s_count;
}

const moo_habit_t *habit_store_get(size_t idx)
{
    return idx < s_count ? &s_list[idx] : NULL;
}

esp_err_t habit_store_check_in(uint8_t id)
{
    const uint32_t today = today_ymd();
    ESP_RETURN_ON_FALSE(today, ESP_ERR_INVALID_STATE, TAG, "time not set");

    xSemaphoreTake(s_lock, portMAX_DELAY);
    moo_habit_t *h = NULL;
    for (size_t i = 0; i < s_count; i++) {
        if (s_list[i].id == id) {
            h = &s_list[i];
            break;
        }
    }
    if (!h) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }

    const check_result_t res = streak_check_in(h, today);
    bool all_done = false;
    if (res == CHECK_STREAK_STARTED || res == CHECK_STREAK_EXTENDED) {
        save_locked();
        all_done = all_done_locked(today);
        ESP_LOGI(TAG, "'%s' checked, streak %u", h->name, h->streak);
    }
    xSemaphoreGive(s_lock);

    if (res == CHECK_STREAK_STARTED || res == CHECK_STREAK_EXTENDED) {
        if (s_cb) {
            s_cb(HABIT_EVT_CHECKED, h, s_cb_ctx);
            if (all_done) {
                s_cb(HABIT_EVT_ALL_DONE, h, s_cb_ctx);
            }
        }
        return ESP_OK;
    }
    return res == CHECK_ALREADY_DONE ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_SUPPORTED;
}

bool habit_store_check_in_next(void)
{
    const uint32_t today = today_ymd();
    if (!today) {
        return false;
    }
    uint8_t id = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < s_count; i++) {
        if (habit_scheduled_on(&s_list[i], today) &&
            s_list[i].last_checked_ymd != today) {
            id = s_list[i].id;
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return id != 0 && habit_store_check_in(id) == ESP_OK;
}

esp_err_t habit_store_clear_all(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_count = 0;
    esp_err_t err = save_locked();
    xSemaphoreGive(s_lock);
    return err;
}
