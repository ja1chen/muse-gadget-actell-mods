/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_flat_store.h"
#include "nvs.h"
#include "psa/crypto.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#define SLOTS 8u
#define MAX_RECORDS 512u
#define MAGIC 0x464c5431u
/* Pair ownership + exact truncated SHA-256 fingerprints. Fingerprint collisions
 * reject/reconcile, never execute. IDs on the phone retain the full SHA-256. */
typedef struct {unsigned char invoke[16],arguments[16],owner[16];double deadline;} entry;
typedef struct {uint32_t magic,capacity,used,pending;entry reserved;} header;
typedef struct {uint32_t magic,used;entry records[SLOTS];} chunk;
_Static_assert(sizeof(entry)==56&&sizeof(header)==72&&sizeof(chunk)==456,"Flat journal v1 layout changed");
static bool hash(const void *data,size_t size,unsigned char out[32]){size_t n=0;return psa_crypto_init()==PSA_SUCCESS&&psa_hash_compute(PSA_ALG_SHA_256,data,size,out,32,&n)==PSA_SUCCESS&&n==32;}
static void hex(const unsigned char in[32],char out[129]){for(unsigned i=0;i<32;i++)snprintf(out+i*2,3,"%02x",in[i]);}
static bool unhex(const char *s,unsigned char out[32]){if(!s||strlen(s)!=64)return false;for(unsigned i=0;i<32;i++){unsigned n=0;for(unsigned k=0;k<2;k++){char c=s[i*2+k];if(c>='0'&&c<='9')n=n*16+(unsigned)(c-'0');else if(c>='a'&&c<='f')n=n*16+(unsigned)(c-'a'+10);else return false;}out[i]=(unsigned char)n;}return true;}
static bool write_blob(nvs_handle_t h,const char *key,const void *p,size_t n){return nvs_set_blob(h,key,p,n)==ESP_OK&&nvs_commit(h)==ESP_OK;}
static void key_for(unsigned index,char key[16]){snprintf(key,16,"chunk%03u",index);}
static bool load_chunk(nvs_handle_t h,unsigned index,chunk *c,unsigned expected){char key[16];key_for(index,key);memset(c,0,sizeof(*c));size_t n=sizeof(*c);esp_err_t e=nvs_get_blob(h,key,c,&n);if(e==ESP_ERR_NVS_NOT_FOUND&&expected==0){c->magic=MAGIC;return true;}return e==ESP_OK&&n==sizeof(*c)&&c->magic==MAGIC&&c->used==expected&&expected<=SLOTS;}
static bool commit_chunk(nvs_handle_t h,unsigned index,const chunk *c){char key[16];key_for(index,key);return write_blob(h,key,c,sizeof(*c));}
static bool open_store(actell_flat_store *s,nvs_handle_t *out,header *head){
 s->error="storage_unavailable";
 if(nvs_open("actell_flat",NVS_READWRITE,out)!=ESP_OK)return false;
 size_t n=sizeof(*head);esp_err_t e=nvs_get_blob(*out,"header",head,&n);
 if(e==ESP_ERR_NVS_NOT_FOUND){
  /* Independent sentinel detects a deleted/missing ledger without touching the
   * pairing blob. Never reset either namespace automatically. */
  nvs_handle_t marker;unsigned char initialized=0;size_t marker_n=1;
  if(nvs_open("actell",NVS_READWRITE,&marker)!=ESP_OK){nvs_close(*out);return false;}
  esp_err_t found=nvs_get_blob(marker,"flat_v1",&initialized,&marker_n);
  if(found!=ESP_ERR_NVS_NOT_FOUND){nvs_close(marker);nvs_close(*out);return false;}
  nvs_stats_t stats;
  if(nvs_get_stats(NULL,&stats)!=ESP_OK){nvs_close(marker);nvs_close(*out);return false;}
  /* Retain 128 entries for other config/GC and two chunk writes plus header.
   * Allocation/fragmentation may still fail: writes then fail closed. */
  size_t chunk_entries=(sizeof(chunk)+31)/32+2;
  size_t reserve=128+2*chunk_entries+2*((sizeof(header)+31)/32+2);
  size_t chunks=stats.free_entries>reserve?(stats.free_entries-reserve)/chunk_entries:0;
  if(chunks>MAX_RECORDS/SLOTS)chunks=MAX_RECORDS/SLOTS;
  if(!chunks){s->error="storage_full";nvs_close(marker);nvs_close(*out);return false;}
  memset(head,0,sizeof(*head));head->magic=MAGIC;head->capacity=(uint32_t)(chunks*SLOTS);
  bool written=write_blob(*out,"header",head,sizeof(*head));initialized=1;
  if(written)written=write_blob(marker,"flat_v1",&initialized,1);
  nvs_close(marker);if(!written){nvs_close(*out);return false;}
 }else if(e!=ESP_OK||n!=sizeof(*head)){nvs_close(*out);return false;}
 if(head->magic!=MAGIC||!head->capacity||head->capacity>MAX_RECORDS||head->capacity%SLOTS||head->used>head->capacity||head->pending>1|| (head->pending&&head->used>=head->capacity)){nvs_close(*out);return false;}
 /* A reserved identity is durable in header before its chunk. Recover exactly
  * that entry; a crash cannot turn the ID into an unrecorded fresh request. */
 if(head->pending){
  unsigned ci=head->used/SLOTS,slot=head->used%SLOTS;chunk c;char key[16];key_for(ci,key);size_t len=sizeof(c);esp_err_t got=nvs_get_blob(*out,key,&c,&len);
  if(got==ESP_ERR_NVS_NOT_FOUND&&slot==0){memset(&c,0,sizeof(c));c.magic=MAGIC;}
  else if(got!=ESP_OK||len!=sizeof(c)||c.magic!=MAGIC){nvs_close(*out);return false;}
  if(c.used==slot){c.records[slot]=head->reserved;c.used++;if(!commit_chunk(*out,ci,&c)){nvs_close(*out);return false;}}
  else if(c.used!=slot+1||memcmp(&c.records[slot],&head->reserved,sizeof(entry))){nvs_close(*out);return false;}
  head->used++;head->pending=0;memset(&head->reserved,0,sizeof(head->reserved));
  if(!write_blob(*out,"header",head,sizeof(*head))){nvs_close(*out);return false;}
 }
 nvs_handle_t marker;unsigned char initialized=0;size_t marker_n=1;
 if(nvs_open("actell",NVS_READWRITE,&marker)!=ESP_OK){nvs_close(*out);return false;}
 esp_err_t mark=nvs_get_blob(marker,"flat_v1",&initialized,&marker_n);
 bool marked=mark==ESP_OK&&marker_n==1&&initialized==1;
 if(mark==ESP_ERR_NVS_NOT_FOUND){initialized=1;marked=write_blob(marker,"flat_v1",&initialized,1);}
 nvs_close(marker);if(!marked){nvs_close(*out);return false;}
 s->capacity=head->capacity;s->used=head->used;s->error=NULL;return true;
}
static bool find(nvs_handle_t h,const header *head,const unsigned char digest[16],entry *found,bool *exists){
 *exists=false;for(unsigned ci=0;ci*SLOTS<head->used;ci++){unsigned count=head->used-ci*SLOTS;if(count>SLOTS)count=SLOTS;chunk c;if(!load_chunk(h,ci,&c,count))return false;for(unsigned i=0;i<count;i++)if(!memcmp(c.records[i].invoke,digest,16)){*found=c.records[i];*exists=true;return true;}}
 return true;
}
static void record(const entry *e,const unsigned char full[32],actell_flat_record *r){memset(r,0,sizeof(*r));hex(full,r->operation_id);strcpy(r->run_id,r->operation_id);strcpy(r->call_id,r->operation_id);r->deadline_ms=e->deadline;}
bool actell_flat_store_owner(actell_flat_store *s,const char *client,const char *pin){
 memset(s,0,sizeof(*s));if(!client||!pin||strlen(client)>128||strlen(pin)!=64)return false;
 char input[194];snprintf(input,sizeof(input),"%s|%s",client,pin);unsigned char digest[32];if(!hash(input,strlen(input),digest))return false;memcpy(s->owner,digest,16);return true;
}
bool actell_flat_arguments_digest(const char *command,const cJSON *args,unsigned char out[32]){
 cJSON *canonical=cJSON_CreateObject();if(!canonical)return false;
 /* Flat schemas contain only scalar fields; sort keys, normalize JSON numbers. */
 const char *previous=NULL;for(;;){const cJSON *next=NULL;for(const cJSON *v=args->child;v;v=v->next)if((!previous||strcmp(v->string,previous)>0)&&(!next||strcmp(v->string,next->string)<0))next=v;if(!next)break;cJSON *copy=cJSON_Duplicate(next,true);if(!copy||!cJSON_AddItemToObject(canonical,next->string,copy)){cJSON_Delete(copy);cJSON_Delete(canonical);return false;}previous=next->string;}
 char *raw=cJSON_PrintUnformatted(canonical);cJSON_Delete(canonical);if(!raw)return false;
 size_t len=strlen(command)+strlen(raw)+2;char *input=malloc(len);if(!input){free(raw);return false;}snprintf(input,len,"%s|%s",command,raw);free(raw);bool success=hash(input,strlen(input),out);free(input);return success;
}
int actell_flat_store_prepare(void *ctx,const char *invoke,const char *command,const cJSON *args,actell_flat_record *r){
 actell_flat_store *s=ctx;unsigned char full[32],arguments[32];
 if(!hash(invoke,strlen(invoke),full)||!actell_flat_arguments_digest(command,args,arguments)){s->error="identity_unavailable";return -1;}
 nvs_handle_t h;header head;if(!open_store(s,&h,&head))return -1;entry e;bool exists;
 if(!find(h,&head,full,&e,&exists)){s->error="storage_unavailable";nvs_close(h);return -1;}
 if(exists){nvs_close(h);if(memcmp(e.arguments,arguments,16)||memcmp(e.owner,s->owner,16)){s->error="idempotency_conflict";return -1;}record(&e,full,r);return 0;}
 if(head.used>=head.capacity){s->error="storage_full";nvs_close(h);return -1;}
 memset(&e,0,sizeof(e));memcpy(e.invoke,full,16);memcpy(e.arguments,arguments,16);memcpy(e.owner,s->owner,16);
 /* Explicit per-call platform budget; no idle/run-sharing heuristic. */
 if(!isfinite(s->deadline_ms)||s->deadline_ms<=0||floor(s->deadline_ms)!=s->deadline_ms||s->deadline_ms>9007199254740991.0){s->error="deadline_exceeded";nvs_close(h);return -1;}
 e.deadline=s->deadline_ms;
 head.reserved=e;head.pending=1;
 bool saved=write_blob(h,"header",&head,sizeof(head));nvs_close(h);
 if(!saved){s->error="storage_unavailable";return -1;}
 if(!open_store(s,&h,&head)){return -1;}
 nvs_close(h);record(&e,full,r);return 1;
}
bool actell_flat_store_lookup(void *ctx,const char *operation,actell_flat_record *r){
 actell_flat_store *s=ctx;unsigned char full[32];if(!unhex(operation,full))return false;
 nvs_handle_t h;header head;if(!open_store(s,&h,&head))return false;entry e;bool exists;
 bool success=find(h,&head,full,&e,&exists)&&exists&&!memcmp(e.owner,s->owner,16);nvs_close(h);
 if(success){record(&e,full,r);}
 return success;
}
