idf_component_register(SRCS "web_ui.c"
                       INCLUDE_DIRS "include"
                       REQUIRES habit_store
                       PRIV_REQUIRES esp_http_server esp_netif json
                       EMBED_FILES "index.html")
