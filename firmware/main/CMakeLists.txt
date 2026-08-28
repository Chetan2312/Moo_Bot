idf_component_register(SRCS "main.c"
                       REQUIRES hal_board face_ui audio_out audio_in net_sync
                                sfx gemini nvs_flash
                       EMBED_FILES "assets/moo_test.pcm")
