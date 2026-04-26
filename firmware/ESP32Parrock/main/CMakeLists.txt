idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES board_config sd_card_mgr audio_player audio_recorder nvs_flash esp_wifi mqtt)