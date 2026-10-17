/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Legacy development USB bootstrap only. Normal pairing uses SLAN/1.
 * Public leaf certificate and hostname must be obtained over trusted OOB.
 * Secret stays in private NVS, never registration/commands/logs. */
esp_err_t actell_bridge_provision(const char *origin,const char *client_id,
 const unsigned char secret[32],const char *leaf_pem,const char *sha256_hex);
void actell_bridge_usb_setup(void);
void actell_bridge_init(void);
/* Snapshot and commands are nonblocking; only the existing RPC worker does I/O. */
/* SELECT retains its old numeric slot but is rejected; pairing now auto-requests. */
typedef enum { ACTELL_PAIR_SCAN=1,ACTELL_PAIR_SELECT,ACTELL_PAIR_RETRY,ACTELL_PAIR_FORGET,ACTELL_PAIR_AUTO_CONNECT } actell_pair_action;
typedef struct {
 unsigned revision,count; /* count/names reserved, no user candidate selection. */
 bool busy,paired,pending,auto_connect;
 bool connected,connection_checked; /* Authenticated peer reachability, not execution readiness. */
 char message[96],names[8][65];
} actell_pair_status;
void actell_bridge_pair_status(actell_pair_status *);
const char *actell_bridge_pair_summary(const actell_pair_status *);
bool actell_bridge_pair_action(actell_pair_action,unsigned index);
void actell_bridge_pair_cancel(void);
void actell_bridge_register(cJSON *commands);
bool actell_bridge_handles(const char *command);
bool actell_bridge_json_safe(const char *data,size_t length);
size_t actell_bridge_result_limit(void);
void actell_bridge_session(uint64_t generation);
cJSON *actell_bridge_invoke(const char *command,const cJSON *params,const char *request_id,uint64_t generation);
#ifdef __cplusplus
}
#endif
