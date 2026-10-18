/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "actell_bridge.h"
#include "actell_guard.h"
typedef struct {char origin[272],client[129],pem[4096],fingerprint[65];unsigned char secret[32];} actell_config;
#define ACTELL_DEVICE_NAME "Muse Gadget"
typedef struct {uint32_t epoch,ip,netmask,gateway;} actell_network_stamp;
/* One serialized worker owns locators/sessions, including one idle connection.
 * Event callbacks only advance generation/epoch and wake that worker. */
typedef struct {
 char address[16],origin[272],client[129],fingerprint[65],operation_id[65];
 actell_network_stamp network;
 int64_t located_at;
 void *session;
} actell_http_locator;
typedef struct {const char *transport_stage;long long expected_bytes;size_t received_bytes;int read_result;int http_status;unsigned char *body;size_t length;char mime[64],request_id[129],image_id[129],generation[32],width[16],height[16],digest[65],revision[129],contract[32];} actell_response;
esp_err_t actell_config_load(actell_config *config);
esp_err_t actell_http(const actell_config *config,const char *request_id,const char *body,bool image,uint64_t generation,int64_t expires,actell_response *response);
esp_err_t actell_http_with_locator(const actell_config *,const char *,const char *,bool,uint64_t,int64_t,actell_response *,actell_http_locator *);
void actell_http_locator_close(actell_http_locator *);
void actell_http_invalidate(actell_http_locator *);
/* Worker-only: transfer a completed connection into the idle slot; reap expired
 * or invalid idle state and return the maximum queue wait before the next reap. */
void actell_http_locator_release(actell_http_locator *);
unsigned actell_http_idle_wait_ms(void);
void actell_http_close_idle(void);
bool actell_bridge_current(uint64_t generation);

void actell_activity_observe(uint64_t generation,const char *op,const cJSON *params,const cJSON *result);

/* Presentation only: serialized bridge worker, bounded to the command budget. */
void actell_activity_begin(uint64_t generation,const char *command,uint32_t ttl_ms);
void actell_activity_end(uint64_t generation,bool success);
