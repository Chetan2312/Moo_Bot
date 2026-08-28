#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* True once a refresh token is available (from NVS or a completed flow). */
bool google_oauth_have_token(void);

/*
 * Run the OAuth device flow. show_cb is called once with the verification URL
 * and short user code so the UI can display them; blocks (polling Google)
 * until the user authorizes or it times out. On success the refresh token is
 * persisted to NVS.
 */
typedef void (*oauth_show_cb_t)(const char *verification_url, const char *user_code);
esp_err_t google_oauth_device_flow(oauth_show_cb_t show_cb);

/*
 * Return a valid bearer access token in `buf`, refreshing via the stored
 * refresh token if the cached one is expired. Fails if not authorized.
 */
esp_err_t google_oauth_access_token(char *buf, size_t len);

/* Drop the stored refresh token (e.g. after a hard 401 / revoked grant). */
void google_oauth_forget(void);

#ifdef __cplusplus
}
#endif
