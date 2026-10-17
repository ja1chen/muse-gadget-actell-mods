#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct { uint32_t addr; } test_ip4;
typedef struct { test_ip4 ip, netmask, gw; } esp_netif_ip_info_t;
typedef struct esp_netif { int unused; } esp_netif_t;
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *);
bool esp_netif_is_netif_up(esp_netif_t *);
esp_err_t esp_netif_get_ip_info(esp_netif_t *, esp_netif_ip_info_t *);
esp_err_t esp_netif_get_netif_impl_name(esp_netif_t *, char *);
