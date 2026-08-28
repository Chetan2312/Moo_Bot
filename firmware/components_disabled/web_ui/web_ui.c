#include "web_ui.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_netif.h"

#include "habit_store.h"

static const char *TAG = "web_ui";

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

static httpd_handle_t s_server;

void web_ui_url(char *buf, size_t len)
{
    buf[0] = '\0';
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr) {
        char ipstr[16];
        esp_ip4addr_ntoa(&ip.ip, ipstr, sizeof(ipstr));
        snprintf(buf, len, "http://%s/", ipstr);
    }
}

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html_start,
                           index_html_end - index_html_start - 1);
}

static esp_err_t habits_get(httpd_req_t *req)
{
    cJSON *arr = cJSON_CreateArray();
    for (size_t i = 0; i < habit_store_count(); i++) {
        const moo_habit_t *h = habit_store_get(i);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "id", h->id);
        cJSON_AddStringToObject(o, "name", h->name);
        cJSON_AddNumberToObject(o, "days", h->days_mask);
        cJSON_AddNumberToObject(o, "hh", h->remind_hh);
        cJSON_AddNumberToObject(o, "mm", h->remind_mm);
        cJSON_AddNumberToObject(o, "streak", h->streak);
        cJSON_AddItemToArray(arr, o);
    }
    char *out = cJSON_PrintUnformatted(arr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out);
    cJSON_free(out);
    cJSON_Delete(arr);
    return ESP_OK;
}

/* Reads the request body into a stack buffer, returns parsed cJSON or NULL. */
static cJSON *read_body(httpd_req_t *req)
{
    char buf[256];
    int total = req->content_len;
    if (total <= 0 || total >= (int)sizeof(buf)) {
        return NULL;
    }
    int got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, buf + got, total - got);
        if (r <= 0) {
            return NULL;
        }
        got += r;
    }
    buf[got] = '\0';
    return cJSON_Parse(buf);
}

static esp_err_t habits_post(httpd_req_t *req)
{
    cJSON *j = read_body(req);
    if (!j) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        return ESP_OK;
    }
    const cJSON *name = cJSON_GetObjectItem(j, "name");
    const cJSON *days = cJSON_GetObjectItem(j, "days_mask");
    const cJSON *hh = cJSON_GetObjectItem(j, "remind_hh");
    const cJSON *mm = cJSON_GetObjectItem(j, "remind_mm");

    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (cJSON_IsString(name) && cJSON_IsNumber(days)) {
        err = habit_store_add(name->valuestring, (uint8_t)days->valueint,
                              hh ? (uint8_t)hh->valueint : HABIT_NO_REMIND,
                              mm ? (uint8_t)mm->valueint : 0);
    }
    cJSON_Delete(j);

    if (err == ESP_OK) {
        httpd_resp_sendstr(req, "{\"ok\":true}");
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "add failed");
    }
    return ESP_OK;
}

static esp_err_t habits_delete(httpd_req_t *req)
{
    cJSON *j = read_body(req);
    if (!j) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        return ESP_OK;
    }
    const cJSON *id = cJSON_GetObjectItem(j, "id");
    if (cJSON_IsNumber(id)) {
        habit_store_remove((uint8_t)id->valueint);
    }
    cJSON_Delete(j);
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t clear_post(httpd_req_t *req)
{
    habit_store_clear_all();
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t reg(const char *uri, httpd_method_t m, esp_err_t (*fn)(httpd_req_t *))
{
    const httpd_uri_t u = { .uri = uri, .method = m, .handler = fn };
    return httpd_register_uri_handler(s_server, &u);
}

esp_err_t web_ui_start(void)
{
    if (s_server) {
        return ESP_OK;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 8;
    cfg.lru_purge_enable = true;
    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &cfg), TAG, "httpd start");

    reg("/", HTTP_GET, root_get);
    reg("/api/habits", HTTP_GET, habits_get);
    reg("/api/habits", HTTP_POST, habits_post);
    reg("/api/habits/delete", HTTP_POST, habits_delete);
    reg("/api/clear", HTTP_POST, clear_post);

    char url[40];
    web_ui_url(url, sizeof(url));
    ESP_LOGI(TAG, "web ui at %s", url[0] ? url : "(no ip yet)");
    return ESP_OK;
}
