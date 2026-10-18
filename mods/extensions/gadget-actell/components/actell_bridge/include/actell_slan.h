/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define SLAN_CANDIDATES 8
#define SLAN_REPLY_MAX 4096
#define SLAN_LOCAL_GENERATION (UINT64_MAX)
typedef enum { SLAN_REQUEST, SLAN_CLAIM, SLAN_PENDING, SLAN_APPROVED, SLAN_ERROR } slan_kind;
typedef struct {
    char request_id[37], device_name[65], state[16], client_id[37], secret[44], host[254], pin[65],
        error[32];
    unsigned port, remaining;
} slan_message;
void slan_zero(void *, size_t);
bool slan_uuid(const char *);
bool slan_host(const char *);
bool slan_name(const char *);
bool slan_private_ipv4(uint32_t host_order);
bool slan_lan_ipv4(uint32_t remote,uint32_t local,uint32_t mask);
bool slan_secret_decode(const char *, uint8_t out[32]);
void slan_secret_encode(const uint8_t in[32], char out[44]);
bool slan_decode(slan_kind, const char *, size_t, slan_message *);
/* Pinned approval cannot replace selected host/port/pin. */
bool slan_claim_matches(const slan_message *, const char *request, const char *host, unsigned port,
                        const char *pin);
