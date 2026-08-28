#include "google_sync.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ext_reminders.h"
#include "face_ui.h"
#include "ghttp.h"
#include "google_oauth.h"
#include "rfc3339.h"

static const char *TAG = "google_sync";

#define CAL_URL_FMT                                                            \
    "https://www.googleapis.com/calendar/v3/calendars/primary/events"         \
    "?timeMin=%s&timeMax=%s&singleEvents=true&orderBy=startTime"              \
    "&maxResults=20&fields=items(summary,start)"
#define TASKS_URL                                                             \
    "https://tasks.googleapis.com/tasks/v1/lists/@default/tasks"             \
    "?showCompleted=false&fields=items(title,due,status)"

static void iso_utc(time_t t, char *buf, size_t len)
{
    struct tm g;
    gmtime_r(&t, &g);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", &g);
}

/* Append calendar events to list; returns new count. */
static size_t parse_calendar(const char *json, moo_ext_reminder_t *list,
                             size_t n, size_t max, int64_t day_end)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return n;
    }
    cJSON *items = cJSON_GetObjectItem(root, "items");
    cJSON *it;
    cJSON_ArrayForEach(it, items) {
        if (n >= max) break;
        cJSON *summary = cJSON_GetObjectItem(it, "summary");
        cJSON *start = cJSON_GetObjectItem(it, "start");
        if (!start) continue;
        cJSON *dt = cJSON_GetObjectItem(start, "dateTime");
        cJSON *donly = cJSON_GetObjectItem(start, "date");
        const char *ts = cJSON_IsString(dt) ? dt->valuestring
                       : cJSON_IsString(donly) ? donly->valuestring : NULL;
        if (!ts) continue;

        int64_t epoch;
        if (!rfc3339_to_epoch(ts, &epoch, NULL)) continue;
        if (epoch > day_end) continue;                 /* keep to ~today */
        epoch -= CONFIG_MOO_CAL_LEAD_MIN * 60;         /* alert this early */

        moo_ext_reminder_t *r = &list[n++];
        r->due_epoch = epoch;
        r->source = EXT_SRC_CALENDAR;
        r->fired = false;
        strlcpy(r->title, cJSON_IsString(summary) ? summary->valuestring : "(event)",
                sizeof(r->title));
    }
    cJSON_Delete(root);
    return n;
}

#if CONFIG_MOO_GSYNC_ENABLE_TASKS
/* Append due tasks (date-only -> remind at MOO_TASK_REMIND_HOUR local). */
static size_t parse_tasks(const char *json, moo_ext_reminder_t *list,
                          size_t n, size_t max, int64_t day_end)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return n;
    }
    cJSON *items = cJSON_GetObjectItem(root, "items");
    cJSON *it;
    cJSON_ArrayForEach(it, items) {
        if (n >= max) break;
        cJSON *title = cJSON_GetObjectItem(it, "title");
        cJSON *due = cJSON_GetObjectItem(it, "due");
        cJSON *status = cJSON_GetObjectItem(it, "status");
        if (!cJSON_IsString(due)) continue;
        if (cJSON_IsString(status) && strcmp(status->valuestring, "completed") == 0)
            continue;

        int64_t epoch;
        if (!rfc3339_to_epoch(due->valuestring, &epoch, NULL)) continue;
        /* Tasks 'due' is date-only at 00:00 UTC; nudge to the configured
         * local reminder hour so it doesn't fire at midnight. */
        epoch += CONFIG_MOO_TASK_REMIND_HOUR * 3600;
        if (epoch > day_end) continue;

        moo_ext_reminder_t *r = &list[n++];
        r->due_epoch = epoch;
        r->source = EXT_SRC_TASK;
        r->fired = false;
        strlcpy(r->title, cJSON_IsString(title) ? title->valuestring : "(task)",
                sizeof(r->title));
    }
    cJSON_Delete(root);
    return n;
}
#endif /* CONFIG_MOO_GSYNC_ENABLE_TASKS */

static void poll_once(void)
{
    static char token[2048];        /* off-stack; task is single-threaded */
    if (google_oauth_access_token(token, sizeof(token)) != ESP_OK) {
        ESP_LOGW(TAG, "no access token, skipping poll");
        return;
    }

    const time_t now = time(NULL);
    const int64_t day_end = now + 24 * 3600;           /* next 24h window */
    char tmin[24], tmax[24];
    iso_utc(now, tmin, sizeof(tmin));
    iso_utc((time_t)day_end, tmax, sizeof(tmax));

    static moo_ext_reminder_t list[EXT_REMINDER_MAX];
    size_t n = 0;

    /* calendar */
    char url[512];
    snprintf(url, sizeof(url), CAL_URL_FMT, tmin, tmax);
    ghttp_resp_t r = { 0 };
    if (ghttp_request(HTTP_METHOD_GET, url, token, NULL, NULL, &r) == ESP_OK &&
        r.status == 200 && r.body) {
        n = parse_calendar(r.body, list, n, EXT_REMINDER_MAX, day_end);
    } else {
        ESP_LOGW(TAG, "calendar GET status %d", r.status);
    }
    ghttp_free(&r);

#if CONFIG_MOO_GSYNC_ENABLE_TASKS
    /* tasks (only if a backend granted the tasks scope — device flow can't) */
    memset(&r, 0, sizeof(r));
    if (ghttp_request(HTTP_METHOD_GET, TASKS_URL, token, NULL, NULL, &r) == ESP_OK &&
        r.status == 200 && r.body) {
        n = parse_tasks(r.body, list, n, EXT_REMINDER_MAX, day_end);
    } else {
        ESP_LOGW(TAG, "tasks GET status %d", r.status);
    }
    ghttp_free(&r);
#endif

    ESP_LOGI(TAG, "poll: %u upcoming reminders", (unsigned)n);
    ext_reminders_replace(list, n);
}

static void show_device_code_cb(const char *url, const char *code)
{
    /* Reuse the QR screen; verification URL as QR, code as caption. */
    face_ui_show_device_code(url, code, 120000);
}

static void sync_task(void *arg)
{
    (void)arg;

    /* wait for system time to be valid (SNTP) before anything time-based */
    for (int i = 0; i < 60; i++) {
        if (time(NULL) > 1735689600) break;            /* > 2025-01-01 */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    while (!google_oauth_have_token()) {
        if (strlen(CONFIG_MOO_GOOGLE_CLIENT_ID) == 0) {
            ESP_LOGW(TAG, "no client id configured (menuconfig -> MooBot Google) — disabled");
            vTaskDelete(NULL);
            return;
        }
        ESP_LOGI(TAG, "not authorized — starting device flow");
        if (google_oauth_device_flow(show_device_code_cb) == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "authorization failed (network/DNS?); retrying in 60 s");
        vTaskDelay(pdMS_TO_TICKS(60000));
    }

    for (;;) {
        poll_once();
        vTaskDelay(pdMS_TO_TICKS((uint32_t)CONFIG_MOO_GSYNC_POLL_MIN * 60 * 1000));
    }
}

esp_err_t google_sync_start(void)
{
    ESP_LOGI(TAG, "start (free heap %u, internal %u)",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    /* TLS handshake + cJSON need a generous stack. */
    if (xTaskCreate(sync_task, "gsync", 12288, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "gsync task create failed — not enough internal RAM");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
