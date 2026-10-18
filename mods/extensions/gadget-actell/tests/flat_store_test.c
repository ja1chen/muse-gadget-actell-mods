/* SPDX-License-Identifier: Apache-2.0 */
/* Actual RAM cache and a synthetic digest; no NVS symbols are linked. */
#include "actell_flat_store.h"
#include "psa/crypto.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int psa_crypto_init(void){return PSA_SUCCESS;}
int psa_hash_compute(int algorithm,const unsigned char *p,size_t n,unsigned char *out,size_t cap,size_t *len){
 (void)algorithm;assert(cap==32);uint64_t h=1469598103934665603ULL;for(size_t i=0;i<n;i++)h=(h^p[i])*1099511628211ULL;
 for(unsigned i=0;i<4;i++)memcpy(out+i*8,&h,8);*len=32;return PSA_SUCCESS;
}
static actell_flat_store owner(void){actell_flat_store s;assert(actell_flat_store_owner(&s,"synthetic-client","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));s.deadline_ms=1234567890000.0;return s;}
static int prepare(actell_flat_store *s,const char *id,const char *json,actell_flat_record *r){cJSON *a=cJSON_Parse(json);assert(a);int n=actell_flat_store_prepare(s,id,"actell.tap",a,r);cJSON_Delete(a);return n;}
int main(void){
 actell_flat_store_reset();actell_flat_store s=owner();actell_flat_record first,r;
 assert(prepare(&s,"invoke-fixture","{\"x\":250,\"y\":750,\"purpose\":\"Open\"}",&first)==1);
 assert(s.used==1&&s.capacity==ACTELL_FLAT_CACHE_CAPACITY);
 s=owner();s.deadline_ms+=999;assert(prepare(&s,"invoke-fixture","{\"purpose\":\"Open\",\"y\":750,\"x\":250}",&r)==0);
 assert(!strcmp(r.operation_id,first.operation_id)&&r.deadline_ms==first.deadline_ms);
 assert(prepare(&s,"invoke-fixture","{\"x\":251,\"y\":750,\"purpose\":\"Open\"}",&r)==-1&&!strcmp(s.error,"idempotency_conflict"));
 assert(actell_flat_store_lookup(&s,first.operation_id,&r));s.owner[0]^=1;
 assert(!actell_flat_store_lookup(&s,first.operation_id,&r));assert(prepare(&s,"invoke-fixture","{}",&r)==-1);
 s=owner();
 /* Recycle through many wraps; reads cannot allocate or refresh cache age. */
 for(unsigned i=1;i<ACTELL_FLAT_CACHE_CAPACITY*10;i++){
  char id[64];snprintf(id,sizeof(id),"invoke-%u",i);assert(prepare(&s,id,"{}",&r)==1);
  assert(s.used<=ACTELL_FLAT_CACHE_CAPACITY);assert(actell_flat_store_lookup(&s,r.operation_id,&first));
 }
 assert(s.used==ACTELL_FLAT_CACHE_CAPACITY);
 assert(prepare(&s,"newest","{}",&r)==1);first=r;
 for(unsigned i=0;i<1000;i++)assert(actell_flat_store_lookup(&s,first.operation_id,&r));
 assert(prepare(&s,"newest","{}",&r)==0);
 s.deadline_ms=-1;assert(prepare(&s,"invalid-deadline","{}",&r)==-1&&!strcmp(s.error,"deadline_exceeded"));
 assert(prepare(&s,"newest","{}",&r)==0); /* Duplicate preserves original deadline. */
 s=owner();
 for(unsigned i=0;i<ACTELL_FLAT_CACHE_CAPACITY;i++){
  char id[64];snprintf(id,sizeof(id),"replacement-%u",i);assert(prepare(&s,id,"{}",&r)==1);
 }
 assert(!actell_flat_store_lookup(&s,first.operation_id,&r));
 actell_flat_store_reset();s=owner();assert(s.used==0);
 assert(!actell_flat_store_lookup(&s,first.operation_id,&r));
 assert(prepare(&s,"newest","{}",&r)==1); /* Reboot has no remembered history. */
 puts("RAM cache: canonical duplicates, ownership, bounded recycling, lookup and reboot reset passed");return 0;
}
