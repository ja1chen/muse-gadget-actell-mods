/* SPDX-License-Identifier: Apache-2.0 */
/* Actual production discovery, synthetic mDNS + Wi-Fi. No DNS lookup exists here. */
#include "actell_lan.h"
#include "lwip/inet.h"
#include "mdns.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static esp_netif_t wifi;
static test_txt txt[] = {
    {"pair", "1"},
    {"host", "actell-fixture.local"},
    {"identity", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
    {"host", "other.local"}};
static mdns_ip_addr_t ip;
static mdns_result_t result;
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key) {
    assert(!strcmp(key, "WIFI_STA_DEF"));
    return &wifi;
}
bool esp_netif_is_netif_up(esp_netif_t *p) { return p == &wifi; }
esp_err_t esp_netif_get_ip_info(esp_netif_t *p, esp_netif_ip_info_t *out) {
    assert(p == &wifi);
    out->ip.addr = inet_addr("192.168.1.10");
    out->netmask.addr = inet_addr("255.255.255.0");
    return ESP_OK;
}
esp_err_t esp_netif_get_netif_impl_name(esp_netif_t *p, char *out) {
    assert(p == &wifi);
    strcpy(out, "en0");
    return ESP_OK;
}
esp_err_t mdns_init(void) { return ESP_OK; }
esp_err_t mdns_query_ptr(const char *type, const char *proto, unsigned timeout, unsigned max,
                         mdns_result_t **out) {
    assert(!strcmp(type, "_actell-muse") && !strcmp(proto, "_tcp") && timeout == 2000 &&
           max == SLAN_CANDIDATES);
    *out = &result;
    return ESP_OK;
}
void mdns_query_results_free(mdns_result_t *p) { assert(p == &result); }
int main(void) {
    ip.addr.type = ESP_IPADDR_TYPE_V4;
    ip.addr.u_addr.ip4.addr = inet_addr("192.168.1.20");
    result = (mdns_result_t){.hostname = "system-iphone",
                             .instance_name = "Actell",
                             .port = 8765,
                             .esp_netif = &wifi,
                             .txt_count = 3,
                             .txt = txt,
                             .addr = &ip};
    actell_candidate out[SLAN_CANDIDATES];
    unsigned count;
    assert(actell_lan_discover(out, &count, NULL) == ESP_OK && count == 1);
    assert(!strcmp(out[0].host, "actell-fixture.local") && !strcmp(out[0].address, "192.168.1.20"));
    actell_config saved = {0};
    strcpy(saved.origin, "https://actell-fixture.local:8765");
    strcpy(saved.fingerprint, txt[2].value);
    actell_config before = saved;
    // DHCP address and SRV hostname may both change; persistent identity never changes.
    ip.addr.u_addr.ip4.addr = inet_addr("192.168.1.77");
    result.hostname = "renamed-system-iphone";
    assert(actell_lan_discover(out, &count, &saved) == ESP_OK && count == 1);
    assert(!strcmp(out[0].host, "actell-fixture.local") && !strcmp(out[0].address, "192.168.1.77"));
    assert(!memcmp(&before, &saved, sizeof(saved)));
    txt[1].value = "attacker.local";
    assert(actell_lan_discover(out, &count, &saved) == ESP_ERR_NOT_FOUND && count == 0);
    assert(!memcmp(&before, &saved, sizeof(saved)));
    txt[1].value = "actell-fixture.local";
    txt[2].value = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    assert(actell_lan_discover(out, &count, &saved) == ESP_ERR_NOT_FOUND && count == 0);
    txt[2].value = saved.fingerprint;
    result.txt_count = 1;
    assert(actell_lan_discover(out, &count, NULL) == ESP_ERR_NOT_SUPPORTED && count == 0);
    result.txt_count = 3;
    txt[1].value = "INVALID.local";
    assert(actell_lan_discover(out, &count, NULL) == ESP_ERR_NOT_SUPPORTED && count == 0);
    txt[1].value = "actell-fixture.local";
    result.txt_count = 4;
    assert(actell_lan_discover(out, &count, NULL) == ESP_ERR_NOT_SUPPORTED && count == 0);
    result.txt_count = 3;
    ip.addr.u_addr.ip4.addr = inet_addr("8.8.8.8");
    assert(actell_lan_discover(out, &count, NULL) == ESP_ERR_NOT_FOUND && count == 0);
    puts("SLAN discovery: distinct SRV/SAN, DHCP, immutable identity, missing/invalid/duplicate "
         "host passed");
}
