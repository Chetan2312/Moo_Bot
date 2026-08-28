#include "google_oauth.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "ghttp.h"

static const char *TAG = "google_oauth";

#define NVS_NS     "moo"
#define NVS_RT_KEY "g_rt"

#define DEVICE_CODE_URL "https://oauth2.googleapis.com/device/code"
#define TOKEN_URL       "https://oauth2.googleapis.com/token"
/* NOTE: Google's OAuth *device flow* only allows a fixed whitelist of scopes.
 * calendar.readonly is allowed; tasks.readonly is NOT (returns invalid_scope).
 * Tasks would require a backend-mediated auth-code flow — see MOO_GSYNC_ENABLE_TASKS. */
#define SCOPES          "https://www.googleapis.com/auth/calendar.readonly"
#define FORM_CT         "application/x-www-form-urlencoded"
#define DEVICE_GRANT    "urn:ietf:params:oauth:grant-type:device_code"

static char s_refresh_token[256];
static char s_access_token[2048];
static time_t s_access_expiry;             /* epoch when access token dies */

static const char *j_str(cJSON *o, const char *k)
{
    cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static void save_refresh_token(const char *rt)
{
    strlcpy(s_refresh_token, rt, sizeof(s_refresh_token));
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, NVS_RT_KEY, rt);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "refresh token stored");
}

static void load_refresh_token(void)
{
    if (s_refresh_token[0]) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_refresh_token);
        nvs_get_str(h, NVS_RT_KEY, s_refresh_token, &len);
        nvs_close(h);
    }
}

bool google_oauth_have_token(void)
{
    load_refresh_token();
    return s_refresh_token[0] != '\0';
}

void google_oauth_forget(void)
{
    s_refresh_token[0] = '\0';
    s_access_token[0] = '\0';
    s_access_expiry = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_RT_KEY);
        nvs_commit(h);
        nvs_close(h);
    }
}

esp_err_t google_oauth_device_flow(oauth_show_cb_t show_cb)
{
    /* 1. request a device + user code.
     * NOTE: this buffer is reused below for the token-poll request, which is
     * much longer (client_id + client_secret + ~100-char device_code +
     * grant_type ≈ 300 B). It MUST be >= ~460 B or snprintf silently truncates
     * grant_type and Google rejects the poll → token never saved → re-auth loop. */
    char body[512];
    snprintf(body, sizeof(body), "client_id=%s&scope=%s",
             CONFIG_MOO_GOOGLE_CLIENT_ID, SCOPES);

    ghttp_resp_t r = { 0 };
    esp_err_t err = ghttp_request(HTTP_METHOD_POST, DEVICE_CODE_URL,
                                  NULL, FORM_CT, body, &r);
    if (err != ESP_OK || r.status != 200 || !r.body) {
        ESP_LOGE(TAG, "device/code failed (status %d): %s", r.status,
                 r.body ? r.body : "(no body)");
        ghttp_free(&r);
        return ESP_FAIL;
    }

    cJSON *j = cJSON_Parse(r.body);
    ghttp_free(&r);
    if (!j) {
        return ESP_FAIL;
    }
    char device_code[256] = { 0 }, user_code[64] = { 0 }, verify_url[128] = { 0 };
    int interval = 5;
    const char *s;
    if ((s = j_str(j, "device_code"))) strlcpy(device_code, s, sizeof(device_code));
    if ((s = j_str(j, "user_code")))   strlcpy(user_code, s, sizeof(user_code));
    if ((s = j_str(j, "verification_url")) || (s = j_str(j, "verification_uri")))
        strlcpy(verify_url, s, sizeof(verify_url));
    cJSON *iv = cJSON_GetObjectItem(j, "interval");
    if (cJSON_IsNumber(iv)) interval = iv->valueint;
    cJSON_Delete(j);

    if (!device_code[0] || !user_code[0]) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "authorize at %s  code: %s", verify_url, user_code);
    if (show_cb) {
        show_cb(verify_url, user_code);
    }

    /* 2. poll the token endpoint until the user authorizes (or ~5 min) */
    snprintf(body, sizeof(body),
             "client_id=%s&client_secret=%s&device_code=%s&grant_type=%s",
             CONFIG_MOO_GOOGLE_CLIENT_ID, CONFIG_MOO_GOOGLE_CLIENT_SECRET,
             device_code, DEVICE_GRANT);

    for (int elapsed = 0; elapsed < 300; elapsed += interval) {
        vTaskDelay(pdMS_TO_TICKS(interval * 1000));

        ghttp_resp_t t = { 0 };
        if (ghttp_request(HTTP_METHOD_POST, TOKEN_URL, NULL, FORM_CT, body, &t) != ESP_OK) {
            ghttp_free(&t);
            continue;
        }
        cJSON *tj = t.body ? cJSON_Parse(t.body) : NULL;
        const int status = t.status;
        ghttp_free(&t);
        if (!tj) {
            continue;
        }

        const char *rt = j_str(tj, "refresh_token");
        const char *at = j_str(tj, "access_token");
        const char *e = j_str(tj, "error");
        cJSON *exp = cJSON_GetObjectItem(tj, "expires_in");

        if (status == 200 && rt && at) {
            save_refresh_token(rt);
            strlcpy(s_access_token, at, sizeof(s_access_token));
            s_access_expiry = time(NULL) + (cJSON_IsNumber(exp) ? exp->valueint : 3600) - 60;
            cJSON_Delete(tj);
            ESP_LOGI(TAG, "authorized");
            return ESP_OK;
        }
        if (e && strcmp(e, "authorization_pending") != 0 && strcmp(e, "slow_down") != 0) {
            ESP_LOGE(TAG, "device flow error: %s", e);
            cJSON_Delete(tj);
            return ESP_FAIL;
        }
        if (e && strcmp(e, "slow_down") == 0) {
            interval += 5;
        }
        cJSON_Delete(tj);
    }
    ESP_LOGE(TAG, "device flow timed out");
    return ESP_ERR_TIMEOUT;
}

esp_err_t google_oauth_access_token(char *buf, size_t len)
{
    load_refresh_token();
    if (!s_refresh_token[0]) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_access_token[0] && time(NULL) < s_access_expiry) {
        strlcpy(buf, s_access_token, len);
        return ESP_OK;
    }

    char body[512];
    snprintf(body, sizeof(body),
             "client_id=%s&client_secret=%s&refresh_token=%s&grant_type=refresh_token",
             CONFIG_MOO_GOOGLE_CLIENT_ID, CONFIG_MOO_GOOGLE_CLIENT_SECRET,
             s_refresh_token);

    ghttp_resp_t r = { 0 };
    esp_err_t err = ghttp_request(HTTP_METHOD_POST, TOKEN_URL, NULL, FORM_CT, body, &r);
    if (err != ESP_OK || !r.body) {
        ghttp_free(&r);
        return ESP_FAIL;
    }
    cJSON *j = cJSON_Parse(r.body);
    const int status = r.status;
    ghttp_free(&r);
    if (!j) {
        return ESP_FAIL;
    }

    const char *at = j_str(j, "access_token");
    cJSON *exp = cJSON_GetObjectItem(j, "expires_in");
    if (status == 200 && at) {
        strlcpy(s_access_token, at, sizeof(s_access_token));
        s_access_expiry = time(NULL) + (cJSON_IsNumber(exp) ? exp->valueint : 3600) - 60;
        strlcpy(buf, s_access_token, len);
        cJSON_Delete(j);
        return ESP_OK;
    }
    /* refresh_token rejected (revoked/expired) -> forget so we re-auth */
    ESP_LOGW(TAG, "refresh failed (status %d), forgetting token", status);
    cJSON_Delete(j);
    google_oauth_forget();
    return ESP_FAIL;
}
