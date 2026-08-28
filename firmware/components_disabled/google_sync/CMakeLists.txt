idf_component_register(SRCS "google_sync.c" "google_oauth.c" "ghttp.c" "rfc3339.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_http_client esp-tls mbedtls json
                                     nvs_flash ext_reminders face_ui esp_timer)
