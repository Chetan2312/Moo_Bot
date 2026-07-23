idf_component_register(SRCS "hal_board.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_lcd
                       PRIV_REQUIRES driver esp_timer)
