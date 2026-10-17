/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_lan.h"
#include "lwip/inet.h"
#include "mdns.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#endif

static atomic_uint network_epoch = 1;
uint32_t actell_lan_epoch(void) { return atomic_load(&network_epoch); }
void actell_lan_network_changed(void) { atomic_fetch_add(&network_epoch, 1); }
#ifdef ESP_PLATFORM
static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)data;
    if ((base == WIFI_EVENT && (id == WIFI_EVENT_STA_START || id == WIFI_EVENT_STA_STOP ||
         id == WIFI_EVENT_STA_CONNECTED || id == WIFI_EVENT_STA_DISCONNECTED)) ||
        (base == IP_EVENT && (id == IP_EVENT_STA_GOT_IP || id == IP_EVENT_STA_LOST_IP)))
        actell_lan_network_changed();
}
#endif
bool actell_lan_network(actell_network_stamp *out) {
#ifdef ESP_PLATFORM
    /* Worker-only registration; network generation changes even if DHCP gives
     * the same address after roaming or disconnect/reconnect. */
    static bool wifi_events, ip_events;
    if (!wifi_events) wifi_events = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event, NULL) == ESP_OK;
    if (!ip_events) ip_events = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, network_event, NULL) == ESP_OK;
    if (!wifi_events || !ip_events) return false;
#endif
    uint32_t epoch = actell_lan_epoch();
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info;
    if (!sta || !esp_netif_is_netif_up(sta) || esp_netif_get_ip_info(sta, &info) != ESP_OK ||
        !slan_private_ipv4(ntohl(info.ip.addr)) || epoch != actell_lan_epoch()) return false;
    *out = (actell_network_stamp){epoch, info.ip.addr, info.netmask.addr, info.gw.addr};
    return true;
}

bool actell_lan_interface(struct ifreq *iface) {
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (!sta || !esp_netif_is_netif_up(sta) || esp_netif_get_ip_info(sta, &ip) != ESP_OK ||
        !slan_private_ipv4(ntohl(ip.ip.addr)))
        return false;
    memset(iface, 0, sizeof(*iface));
    return esp_netif_get_netif_impl_name(sta, iface->ifr_name) == ESP_OK;
}
bool actell_lan_address(uint32_t network_ip) {
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info;
    return sta && esp_netif_get_ip_info(sta, &info) == ESP_OK &&
           slan_lan_ipv4(ntohl(network_ip), ntohl(info.ip.addr), ntohl(info.netmask.addr));
}
esp_err_t actell_lan_discover(actell_candidate out[SLAN_CANDIDATES], unsigned *count,
                              const actell_config *pinned) {
    *count = 0;
    struct ifreq iface;
    if (!actell_lan_interface(&iface))
        return ESP_ERR_INVALID_STATE;
    static bool initialized;
    if (!initialized) {
        esp_err_t e = mdns_init();
        if (e != ESP_OK)
            return e;
        initialized = true;
    }
    mdns_result_t *found = NULL;
    esp_err_t e = mdns_query_ptr("_actell-muse", "_tcp", 2000, SLAN_CANDIDATES, &found);
    char trusted_host[254] = {0};
    unsigned trusted_port = 443;
    if (pinned) {
        const char *begin = pinned->origin + 8, *colon = strrchr(begin, ':');
        size_t host_len = colon ? (size_t)(colon - begin) : strlen(begin);
        if (!host_len || host_len >= sizeof(trusted_host)) {
            mdns_query_results_free(found);
            return ESP_ERR_INVALID_ARG;
        }
        memcpy(trusted_host, begin, host_len);
        if (colon) {
            char *end = NULL;
            unsigned long n = strtoul(colon + 1, &end, 10);
            if (!colon[1] || *end || !n || n > 65535) {
                mdns_query_results_free(found);
                return ESP_ERR_INVALID_ARG;
            }
            trusted_port = (unsigned)n;
        }
    }
    bool incompatible = false;
    unsigned seen = 0, eligible = 0, identity_fields = 0, identity_matches = 0, host_matches = 0;
    for (mdns_result_t *c = found; e == ESP_OK && c && *count < SLAN_CANDIDATES; c = c->next) {
        seen++;
        if (!c->hostname || !c->port ||
            c->esp_netif != esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"))
            continue;
        eligible++;
        const char *host = NULL;
        bool match = false, duplicate_host = false;
        for (size_t i = 0; i < c->txt_count; i++) {
            if (!strcmp(c->txt[i].key, "host")) {
                if (host)
                    duplicate_host = true;
                host = c->txt[i].value ? c->txt[i].value : "";
            }
            if (!c->txt[i].value)
                continue;
            if (!strcmp(c->txt[i].key, "identity")) identity_fields++;
            if (!pinned && !strcmp(c->txt[i].key, "pair") && !strcmp(c->txt[i].value, "1"))
                match = true;
            if (pinned && !strcmp(c->txt[i].key, "identity") &&
                !strcmp(c->txt[i].value, pinned->fingerprint))
                match = true;
        }
        if (!match)
            continue;
        identity_matches++;
        if (!host || duplicate_host || !slan_host(host)) {
            incompatible = true;
            continue;
        }
        // SRV is a locator. Matching discovery hints never replace saved trust;
        // TLS still uses the stored name and exact certificate.
        if (pinned && (strcmp(host, trusted_host) || c->port != trusted_port))
            continue;
        host_matches++;
        for (mdns_ip_addr_t *a = c->addr; a; a = a->next) {
            if (a->addr.type != ESP_IPADDR_TYPE_V4 || !actell_lan_address(a->addr.u_addr.ip4.addr))
                continue;
            actell_candidate *d = &out[*count];
            memset(d, 0, sizeof(*d));
            strcpy(d->host, host);
            d->port = c->port;
            snprintf(d->address, sizeof(d->address), IPSTR, IP2STR(&a->addr.u_addr.ip4));
            const char *name = c->instance_name;
            if (!slan_name(name))
                name = "Actell";
            strcpy(d->name, name);
            (*count)++;
            break;
        }
    }
    mdns_query_results_free(found);
#ifdef ESP_PLATFORM
    /* Counts only: no phone names, addresses, saved host, pin or client IDs. */
    if (pinned && !*count)
        ESP_LOGW("actell.rpc", "discovery seen=%u eligible=%u identity_fields=%u identity=%u host_port=%u error=%s",
                 seen, eligible, identity_fields, identity_matches, host_matches, esp_err_to_name(e));
#else
    (void)seen; (void)eligible; (void)identity_fields; (void)identity_matches; (void)host_matches;
#endif
    return *count         ? ESP_OK
           : e != ESP_OK  ? e
           : incompatible ? ESP_ERR_NOT_SUPPORTED
                          : ESP_ERR_NOT_FOUND;
}
