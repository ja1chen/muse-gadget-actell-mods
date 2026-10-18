#pragma once
/* Consume arguments, but emit no test contents. */
void test_http_log(const char *,const char *,...);
#define ESP_LOGW(tag,...) test_http_log(tag,__VA_ARGS__)
#define ESP_LOGI(tag,...) test_http_log(tag,__VA_ARGS__)
const char *esp_err_to_name(int);
