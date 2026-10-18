#pragma once
#include "esp_netif.h"
#include <stddef.h>
#define ESP_IPADDR_TYPE_V4 0
#define ESP_IPADDR_TYPE_V6 6
typedef struct mdns_ip_addr { struct mdns_ip_addr *next; struct { int type; union { test_ip4 ip4; } u_addr; } addr; } mdns_ip_addr_t;
typedef struct { const char *key, *value; } test_txt;
typedef struct mdns_result { struct mdns_result *next; const char *hostname, *instance_name; unsigned short port; esp_netif_t *esp_netif; size_t txt_count; test_txt *txt; mdns_ip_addr_t *addr; } mdns_result_t;
esp_err_t mdns_init(void);
esp_err_t mdns_query_ptr(const char *, const char *, unsigned, unsigned, mdns_result_t **);
void mdns_query_results_free(mdns_result_t *);
