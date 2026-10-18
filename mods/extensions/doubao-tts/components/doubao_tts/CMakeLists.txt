idf_component_register(SRCS "doubao_tts.c" "doubao_ws.c" "doubao_protocol.c" "tts_stream.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_http_client tcp_transport mbedtls esp_timer esp_psram)
