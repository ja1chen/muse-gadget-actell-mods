/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "cJSON.h"
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Public leaf certificate and hostname must be obtained over trusted OOB.
 * Secret stays in private NVS, never registration/commands/logs. */
esp_err_t actell_bridge_provision(const char *origin,const char *client_id,
 const unsigned char secret[32],const char *leaf_pem,const char *sha256_hex);
void actell_bridge_usb_setup(void);
void actell_bridge_register(cJSON *commands);
void actell_bridge_session(uint64_t generation);
cJSON *actell_bridge_invoke(const char *command,const cJSON *params,const char *request_id,uint64_t generation);
#ifdef __cplusplus
}
#endif
