idf_component_register(SRCS "audio_recorder.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver board_config esp_http_client)