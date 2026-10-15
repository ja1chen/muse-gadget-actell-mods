idf_component_register(SRCS "doubao_tts.c" "tts_stream.c" INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_http_client mbedtls esp_timer esp_psram)
