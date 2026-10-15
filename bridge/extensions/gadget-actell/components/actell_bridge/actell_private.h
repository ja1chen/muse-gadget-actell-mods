/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "actell_bridge.h"
#include "actell_guard.h"
typedef struct {char origin[256],client[129],pem[4096],fingerprint[65];unsigned char secret[32];} actell_config;
typedef struct {unsigned char *body;size_t length;char mime[64],request_id[129],image_id[129],generation[32],width[16],height[16],digest[65],revision[129],contract[32];} actell_response;
esp_err_t actell_config_load(actell_config *config);
esp_err_t actell_http(const actell_config *config,const char *request_id,const char *body,bool image,uint64_t generation,int64_t expires,actell_response *response);
bool actell_bridge_current(uint64_t generation);

void actell_activity_observe(uint64_t generation,const char *op,const cJSON *params,const cJSON *result);
