/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_flat_store.h"
#include "psa/crypto.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define EXT_RAM_BSS_ATTR
#endif

/* Only the serialized bridge worker accesses this cache. Each execute finishes
 * before another prepare can recycle its slot. No NVS, crash journal or sentinel.
 * The admission guard separately retains write IDs for the control generation. */
typedef struct { unsigned char invoke[16], arguments[16], owner[16]; double deadline; } entry;
/* Ordinary worker data; keep the internal/DMA heap available for transport. */
static EXT_RAM_BSS_ATTR entry records[ACTELL_FLAT_CACHE_CAPACITY];
static unsigned used, next_slot;

void actell_flat_store_reset(void) {
    memset(records, 0, sizeof(records));
    used = next_slot = 0;
}

static bool hash(const void *data,size_t size,unsigned char out[32]){size_t n=0;return psa_crypto_init()==PSA_SUCCESS&&psa_hash_compute(PSA_ALG_SHA_256,data,size,out,32,&n)==PSA_SUCCESS&&n==32;}
static void hex(const unsigned char in[32],char out[129]){for(unsigned i=0;i<32;i++)snprintf(out+i*2,3,"%02x",in[i]);}
static bool unhex(const char *s,unsigned char out[32]){if(!s||strlen(s)!=64)return false;for(unsigned i=0;i<32;i++){unsigned n=0;for(unsigned k=0;k<2;k++){char c=s[i*2+k];if(c>='0'&&c<='9')n=n*16+(unsigned)(c-'0');else if(c>='a'&&c<='f')n=n*16+(unsigned)(c-'a'+10);else return false;}out[i]=(unsigned char)n;}return true;}

static void record(const entry *e, const unsigned char full[32], actell_flat_record *r) {
    memset(r, 0, sizeof(*r));
    hex(full, r->operation_id);
    strcpy(r->run_id, r->operation_id);
    strcpy(r->call_id, r->operation_id);
    r->deadline_ms = e->deadline;
}
bool actell_flat_store_owner(actell_flat_store *s, const char *client, const char *pin) {
    memset(s, 0, sizeof(*s));
    if (!client || !pin || strlen(client) > 128 || strlen(pin) != 64) return false;
    char input[194];
    snprintf(input, sizeof(input), "%s|%s", client, pin);
    unsigned char digest[32];
    if (!hash(input, strlen(input), digest)) return false;
    memcpy(s->owner, digest, 16);
    s->capacity = ACTELL_FLAT_CACHE_CAPACITY;
    s->used = used;
    return true;
}
bool actell_flat_arguments_digest(const char *command,const cJSON *args,unsigned char out[32]){
 cJSON *canonical=cJSON_CreateObject();if(!canonical)return false;
 /* Flat schemas contain only scalar fields; sort keys, normalize JSON numbers. */
 const char *previous=NULL;for(;;){const cJSON *next=NULL;for(const cJSON *v=args->child;v;v=v->next)if((!previous||strcmp(v->string,previous)>0)&&(!next||strcmp(v->string,next->string)<0))next=v;if(!next)break;cJSON *copy=cJSON_Duplicate(next,true);if(!copy||!cJSON_AddItemToObject(canonical,next->string,copy)){cJSON_Delete(copy);cJSON_Delete(canonical);return false;}previous=next->string;}
 char *raw=cJSON_PrintUnformatted(canonical);cJSON_Delete(canonical);if(!raw)return false;
 size_t len=strlen(command)+strlen(raw)+2;char *input=malloc(len);if(!input){free(raw);return false;}snprintf(input,len,"%s|%s",command,raw);free(raw);bool success=hash(input,strlen(input),out);free(input);return success;
}

int actell_flat_store_prepare(void *ctx, const char *invoke, const char *command,
                              const cJSON *args, actell_flat_record *r) {
    actell_flat_store *s = ctx;
    unsigned char full[32], arguments[32];
    s->error = NULL;
    s->capacity = ACTELL_FLAT_CACHE_CAPACITY;
    s->used = used;
    if (!hash(invoke, strlen(invoke), full) || !actell_flat_arguments_digest(command, args, arguments)) {
        s->error = "identity_unavailable";
        return -1;
    }
    for (unsigned i = 0; i < used; i++) {
        const entry *e = &records[i];
        if (memcmp(e->invoke, full, 16)) continue;
        if (memcmp(e->arguments, arguments, 16) || memcmp(e->owner, s->owner, 16)) {
            s->error = "idempotency_conflict";
            return -1;
        }
        record(e, full, r);
        return 0;
    }
    if (!isfinite(s->deadline_ms) || s->deadline_ms <= 0 ||
        floor(s->deadline_ms) != s->deadline_ms || s->deadline_ms > 9007199254740991.0) {
        s->error = "deadline_exceeded";
        return -1;
    }
    entry *e = &records[next_slot];
    memset(e, 0, sizeof(*e));
    memcpy(e->invoke, full, 16);
    memcpy(e->arguments, arguments, 16);
    memcpy(e->owner, s->owner, 16);
    e->deadline = s->deadline_ms;
    next_slot = (next_slot + 1) % ACTELL_FLAT_CACHE_CAPACITY;
    if (used < ACTELL_FLAT_CACHE_CAPACITY) used++;
    s->used = used;
    record(e, full, r);
    return 1;
}
bool actell_flat_store_lookup(void *ctx, const char *operation, actell_flat_record *r) {
    actell_flat_store *s = ctx;
    unsigned char full[32];
    s->error = NULL;
    s->used = used;
    if (!unhex(operation, full)) return false;
    for (unsigned i = 0; i < used; i++) {
        const entry *e = &records[i];
        if (!memcmp(e->invoke, full, 16) && !memcmp(e->owner, s->owner, 16)) {
            record(e, full, r);
            return true;
        }
    }
    return false;
}
