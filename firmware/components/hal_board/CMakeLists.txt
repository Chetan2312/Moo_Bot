idf_component_register(SRCS "hal_board.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_lcd esp_lcd_touch
                       PRIV_REQUIRES driver esp_timer)
