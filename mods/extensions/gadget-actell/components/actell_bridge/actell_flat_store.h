/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "actell_flat.h"
#include <stdint.h>
/* Worker-only RAM cache; reset at process/firmware startup, never per request.
 * Recycled/missing records return unavailable; callers must not infer nonexecution.
 * No token, private key, user text or image bytes are stored here. */
#define ACTELL_FLAT_CACHE_CAPACITY 64u
void actell_flat_store_reset(void);
typedef struct {unsigned char owner[16];unsigned capacity,used;double deadline_ms;const char *error;} actell_flat_store;
bool actell_flat_arguments_digest(const char *,const cJSON *,unsigned char out[32]);
bool actell_flat_store_owner(actell_flat_store *,const char *client,const char *pin);
int actell_flat_store_prepare(void *,const char *,const char *,const cJSON *,actell_flat_record *);
bool actell_flat_store_lookup(void *,const char *,actell_flat_record *);
