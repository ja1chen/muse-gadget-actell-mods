#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct fake_http *esp_http_client_handle_t;
typedef struct { int event_id; void *user_data; const char *header_key,*header_value; } esp_http_client_event_t;
typedef struct {
    const char *url,*cert_pem,*common_name;
    struct ifreq *if_name;
    int timeout_ms;
    bool disable_auto_redirect,keep_alive_enable,skip_cert_common_name_check;
    esp_err_t (*event_handler)(esp_http_client_event_t *);
    void *user_data;
} esp_http_client_config_t;
#define HTTP_EVENT_ON_HEADER 1
#define HTTP_METHOD_POST 1
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
int esp_http_client_set_header(esp_http_client_handle_t,const char *,const char *);
int esp_http_client_set_method(esp_http_client_handle_t,int);
esp_err_t esp_http_client_open(esp_http_client_handle_t,int);
int esp_http_client_write(esp_http_client_handle_t,const char *,int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t,char *,int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
int esp_http_client_close(esp_http_client_handle_t);
int esp_http_client_cleanup(esp_http_client_handle_t);
