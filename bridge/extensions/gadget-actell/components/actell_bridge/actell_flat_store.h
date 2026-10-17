/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "actell_flat.h"
#include <stdint.h>
/* No token, private key, user text or image bytes are stored here. */
typedef struct {unsigned char owner[16];unsigned capacity,used;double deadline_ms;const char *error;} actell_flat_store;
bool actell_flat_arguments_digest(const char *,const cJSON *,unsigned char out[32]);
bool actell_flat_store_owner(actell_flat_store *,const char *client,const char *pin);
int actell_flat_store_prepare(void *,const char *,const char *,const cJSON *,actell_flat_record *);
bool actell_flat_store_lookup(void *,const char *,actell_flat_record *);
