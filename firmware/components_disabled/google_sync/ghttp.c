#include "ghttp.h"

#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_log.h"

static const char *TAG = "ghttp";

typedef struct {
    char *buf;
    int len;
    int cap;
} accum_t;

static esp_err_t on_event(esp_http_client_event_t *e)
{
    if (e->event_id != HTTP_EVENT_ON_DATA) {
        return ESP_OK;
    }
    accum_t *a = e->user_data;
    if (a->len + e->data_len + 1 > a->cap) {
        int nc = a->cap * 2 + e->data_len + 256;
        char *nb = realloc(a->buf, nc);
        if (!nb) {
            ESP_LOGE(TAG, "oom accumulating %d bytes", nc);
            return ESP_FAIL;
        }
        a->buf = nb;
        a->cap = nc;
    }
    memcpy(a->buf + a->len, e->data, e->data_len);
    a->len += e->data_len;
    a->buf[a->len] = '\0';
    return ESP_OK;
}

esp_err_t ghttp_request(esp_http_client_method_t method, const char *url,
                        const char *bearer, const char *content_type,
                        const char *post_field, ghttp_resp_t *out)
{
    accum_t a = { 0 };
    const esp_http_client_config_t cfg = {
        .url = url,
        .method = method,
        .event_handler = on_event,
        .user_data = &a,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        return ESP_FAIL;
    }

    if (bearer) {
        char hdr[600];
        snprintf(hdr, sizeof(hdr), "Bearer %s", bearer);
        esp_http_client_set_header(c, "Authorization", hdr);
    }
    if (content_type) {
        esp_http_client_set_header(c, "Content-Type", content_type);
    }
    if (post_field) {
        esp_http_client_set_post_field(c, post_field, strlen(post_field));
    }

    const esp_err_t err = esp_http_client_perform(c);
    out->status = esp_http_client_get_status_code(c);
    out->body = a.buf;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s -> %s", url, esp_err_to_name(err));
    }
    esp_http_client_cleanup(c);
    return err;
}
