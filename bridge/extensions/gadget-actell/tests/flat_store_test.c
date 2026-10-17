/* SPDX-License-Identifier: Apache-2.0 */
/* Real store algorithm; fake atomic NVS and noncryptographic synthetic hash.
 * These are power-loss state-machine tests, not a physical flash wear test. */
#include "actell_flat_store.h"
#include "nvs.h"
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
#include "flat_nvs_fake.h"
static actell_flat_store owner(void){actell_flat_store s;assert(actell_flat_store_owner(&s,"synthetic-client","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));s.deadline_ms=1234567890000.0;return s;}
static int prepare(actell_flat_store *s,const char *id,const char *json,actell_flat_record *r){cJSON *a=cJSON_Parse(json);assert(a);int n=actell_flat_store_prepare(s,id,"actell.tap",a,r);cJSON_Delete(a);return n;}
int main(void){
 reset();actell_flat_store s=owner();actell_flat_record first,r;
 assert(prepare(&s,"invoke-fixture","{\"x\":250,\"y\":750,\"purpose\":\"Open\"}",&first)==1);assert(s.used==1&&s.capacity>100);
 s=owner();s.deadline_ms+=999;assert(prepare(&s,"invoke-fixture","{\"purpose\":\"Open\",\"y\":750,\"x\":250}",&r)==0);assert(!strcmp(r.operation_id,first.operation_id)&&r.deadline_ms==first.deadline_ms);
 assert(prepare(&s,"invoke-fixture","{\"x\":251,\"y\":750,\"purpose\":\"Open\"}",&r)==-1&&!strcmp(s.error,"idempotency_conflict"));
 assert(actell_flat_store_lookup(&s,first.operation_id,&r));s.owner[0]^=1;assert(!actell_flat_store_lookup(&s,first.operation_id,&r));assert(prepare(&s,"invoke-fixture","{\"x\":250,\"y\":750,\"purpose\":\"Open\"}",&r)==-1);
 for(unsigned after=0;after<2;after++)for(unsigned fault=1;fault<=5;fault++){
  reset();s=owner();fail_commit=fault;fail_after=after!=0;
  int original=prepare(&s,"crash-fixture","{\"x\":0,\"y\":0,\"purpose\":\"Open\"}",&r);
  fail_commit=0;s=owner();int recovered=prepare(&s,"crash-fixture","{\"x\":0,\"y\":0,\"purpose\":\"Open\"}",&r);
  assert(original<0||original==1);assert(recovered==0||recovered==1);
  /* A checkpoint that could have permitted I/O never becomes a fresh invoke. */
  if(original==1||fault>=4||(fault==3&&after))assert(recovered==0);
 }
 reset();s=owner();fail_set=true;assert(prepare(&s,"write-failure","{}",&r)==-1);fail_set=false;
 reset();s=owner();assert(prepare(&s,"invoke-fixture","{}",&first)==1);
 kv *h=find(2,"header");assert(h);free(h->data);*h=blobs[--count];s=owner();assert(prepare(&s,"new-invoke","{}",&r)==-1); /* sentinel prevents silent ledger reset */
 reset();s=owner();assert(prepare(&s,"saved","{}",&r)==1);kv *c=find(2,"chunk000");assert(c);free(c->data);*c=blobs[--count];s=owner();assert(prepare(&s,"new-after-loss","{}",&r)==-1);
 reset();s=owner();free_entries=100000;assert(prepare(&s,"maximum","{}",&r)==1);assert(s.capacity==512);
 reset();s=owner();free_entries=200;assert(prepare(&s,"first","{}",&r)==1);unsigned capacity=s.capacity;assert(capacity>0&&capacity<100);
 for(unsigned i=1;i<capacity;i++){char id[64];snprintf(id,sizeof(id),"invoke-%u",i);assert(prepare(&s,id,"{}",&r)==1);}
 assert(prepare(&s,"full","{}",&r)==-1&&!strcmp(s.error,"storage_full"));assert(prepare(&s,"first","{}",&r)==0);
 reset();s=owner();free_entries=1;assert(prepare(&s,"no-space","{}",&r)==-1&&!strcmp(s.error,"storage_full"));
 reset();puts("flat NVS store: canonical conflicts, restart, commit crash windows, missing ledger, capacity and ownership passed");return 0;
}
