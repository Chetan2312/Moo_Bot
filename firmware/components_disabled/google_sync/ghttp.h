#pragma once

#include "esp_err.h"
#include "esp_http_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *body;         /* heap, NUL-terminated; caller frees (may be NULL) */
    int status;         /* HTTP status code */
} ghttp_resp_t;

/* Full HTTPS request to Google. bearer/content_type/post_field may be NULL.
 * Body accumulated into out->body. Uses the esp_crt_bundle root CAs. */
esp_err_t ghttp_request(esp_http_client_method_t method, const char *url,
                        const char *bearer, const char *content_type,
                        const char *post_field, ghttp_resp_t *out);

static inline void ghttp_free(ghttp_resp_t *r)
{
    if (r && r->body) {
        free(r->body);
        r->body = NULL;
    }
}

#ifdef __cplusplus
}
#endif
