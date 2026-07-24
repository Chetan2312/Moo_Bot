idf_component_register(SRCS "net_sync.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_event esp_netif
                       PRIV_REQUIRES hal_board)
