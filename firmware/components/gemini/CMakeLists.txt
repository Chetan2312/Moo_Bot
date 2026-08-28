idf_component_register(SRCS "gemini.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES audio_in audio_out esp_http_client
                                     esp-tls mbedtls)
