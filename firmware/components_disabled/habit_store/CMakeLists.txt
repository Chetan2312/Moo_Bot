idf_component_register(SRCS "habit_store.c" "streak_engine.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES nvs_flash)
