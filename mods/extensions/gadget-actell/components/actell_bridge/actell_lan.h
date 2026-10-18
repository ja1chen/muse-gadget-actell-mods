/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "actell_private.h"
#include "actell_slan.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
typedef struct {
    char name[65], host[254], address[16];
    uint16_t port;
} actell_candidate;
typedef enum {
    ACTELL_PEER_PENDING = 1,
    ACTELL_PEER_ACTIVE = 2
} actell_peer_phase;
esp_err_t actell_peer_load(actell_config *, actell_peer_phase *);
esp_err_t actell_peer_save(const actell_config *, actell_peer_phase);
esp_err_t actell_peer_forget(void);
esp_err_t actell_auto_connect_load(bool *);
esp_err_t actell_auto_connect_save(bool);
esp_err_t actell_lan_discover(actell_candidate out[SLAN_CANDIDATES], unsigned *count,
                              const actell_config *pinned);
bool actell_lan_interface(struct ifreq *iface);
bool actell_lan_address(uint32_t network_ip);
bool actell_lan_network(actell_network_stamp *);
uint32_t actell_lan_epoch(void);
void actell_lan_network_changed(void);
bool actell_clock_ready(void);
struct mbedtls_x509_crt;
bool actell_verify_self_leaf(const char *,actell_config *,struct mbedtls_x509_crt *);
void actell_random_uuid(char out[37]);
/* Only pairing worker uses this transport. First request captures a verified
 * self-issued P-256 leaf; subsequent calls require exactly the same DER pin. */
esp_err_t actell_pair_post(const actell_candidate *, actell_config *anchor, const char *path,
                           const char *token, const char *body, int *status, char response[4097]);
bool actell_pair_current(void);
bool actell_pair_rpc_allowed(void);
void actell_pair_process(unsigned action, unsigned index);
void actell_pair_boot(void);
void actell_pair_poll(void);
void actell_pair_clock_synchronized(void);
void actell_bridge_pair_wake(void);
bool actell_bridge_local_job(unsigned action, unsigned index);
esp_err_t actell_pair_hello(const actell_config *, bool *rejected);
