/* SPDX-License-Identifier: Apache-2.0. Host test platform; no network. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <pthread.h>
typedef int esp_err_t;
typedef int portMUX_TYPE;
typedef void *QueueHandle_t;
typedef void *esp_http_client_handle_t;
typedef struct {
    const char *url;
    int method, timeout_ms;
    void *crt_bundle_attach;
    bool disable_auto_redirect;
    int buffer_size, buffer_size_tx;
} esp_http_client_config_t;
#define CONFIG_DOUBAO_TTS 1
#ifndef CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
#define CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY 1
#endif
#define CONFIG_DOUBAO_TTS_API_KEY "SYNTHETIC_API_KEY"
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdMS_TO_TICKS(n) (n)
#define portMAX_DELAY 0xffffffffu
#define pdPASS 1
#define pdTRUE 1
#define ESP_OK 0
#define ESP_ERR_HTTP_EAGAIN 0x7007
#define HTTP_METHOD_POST 1
#define ESP_LOG_WARN 2
#define ESP_LOG_NONE 0
#define CONFIG_WS_BUFFER_SIZE 4096
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
#define ESP_LOGI(...) fake_log(__VA_ARGS__)
void fake_log(const char *, const char *, ...);
void esp_log_level_set(const char *, int);
int64_t esp_timer_get_time(void);
void esp_fill_random(void *, size_t);
extern void *esp_crt_bundle_attach;
void *heap_caps_malloc(size_t, unsigned);
size_t heap_caps_get_free_size(unsigned);
QueueHandle_t xQueueCreate(unsigned, unsigned);
void vQueueDelete(QueueHandle_t);
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, int, void *);
int xQueueOverwrite(QueueHandle_t, const void *);
int xQueueReceive(QueueHandle_t, void *, unsigned);
typedef void *esp_transport_handle_t;
typedef enum {
    WS_TRANSPORT_OPCODES_CONT = 0, WS_TRANSPORT_OPCODES_BINARY = 2,
    WS_TRANSPORT_OPCODES_FIN = 0x80,
} ws_transport_opcodes_t;
typedef struct { const char *ws_path; const char *headers; bool propagate_control_frames; } esp_transport_ws_config_t;
esp_transport_handle_t esp_transport_ssl_init(void);
void esp_transport_ssl_crt_bundle_attach(esp_transport_handle_t, void *);
esp_transport_handle_t esp_transport_ws_init(esp_transport_handle_t);
int esp_transport_ws_set_config(esp_transport_handle_t, const esp_transport_ws_config_t *);
int esp_transport_connect(esp_transport_handle_t, const char *, int, int);
int esp_transport_ws_get_upgrade_request_status(esp_transport_handle_t);
int esp_transport_ws_send_raw(esp_transport_handle_t, ws_transport_opcodes_t, const char *, int, int);
int esp_transport_poll_read(esp_transport_handle_t, int);
int esp_transport_read(esp_transport_handle_t, char *, int, int);
int esp_transport_ws_get_read_opcode(esp_transport_handle_t);
int esp_transport_ws_get_read_payload_len(esp_transport_handle_t);
bool esp_transport_ws_get_fin_flag(esp_transport_handle_t);
int esp_transport_close(esp_transport_handle_t);
int esp_transport_destroy(esp_transport_handle_t);
int xTaskCreateWithCaps(void (*)(void *), const char *, unsigned, void *, int, void *, unsigned);
void vTaskDelay(unsigned);
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
int esp_http_client_cleanup(esp_http_client_handle_t);
int esp_http_client_set_header(esp_http_client_handle_t, const char *, const char *);
int esp_http_client_open(esp_http_client_handle_t, int);
int esp_http_client_write(esp_http_client_handle_t, const char *, int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t, char *, int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
