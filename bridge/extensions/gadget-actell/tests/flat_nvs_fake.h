/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "nvs.h"
typedef struct {unsigned ns;char key[16];size_t n;unsigned char *data;} kv;
static kv blobs[80],pending;static unsigned count,commits,fail_commit;static bool fail_after,fail_set;
static size_t free_entries=1000;
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *out){(void)mode;assert(!strcmp(name,"actell")||!strcmp(name,"actell_flat"));*out=!strcmp(name,"actell")?1:2;return ESP_OK;}
static kv *find(unsigned ns,const char *key){for(unsigned i=0;i<count;i++)if(blobs[i].ns==ns&&!strcmp(blobs[i].key,key))return &blobs[i];return NULL;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *n){kv *v=find(h,key);if(!v)return ESP_ERR_NVS_NOT_FOUND;if(!out){*n=v->n;return ESP_OK;}if(*n<v->n)return ESP_ERR_INVALID_SIZE;*n=v->n;memcpy(out,v->data,v->n);return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *p,size_t n){if(fail_set)return ESP_FAIL;assert(!pending.data);pending.ns=h;snprintf(pending.key,sizeof(pending.key),"%s",key);pending.data=malloc(n);assert(pending.data);memcpy(pending.data,p,n);pending.n=n;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;commits++;bool fail=fail_commit&&commits==fail_commit;if(!fail||fail_after){kv *v=find(pending.ns,pending.key);if(!v){assert(count<80);v=&blobs[count++];}else free(v->data);*v=pending;memset(&pending,0,sizeof(pending));}else{free(pending.data);memset(&pending,0,sizeof(pending));}return fail?ESP_FAIL:ESP_OK;}
void nvs_close(nvs_handle_t h){(void)h;if(pending.data){free(pending.data);memset(&pending,0,sizeof(pending));}}
esp_err_t nvs_get_stats(const char *name,nvs_stats_t *s){(void)name;memset(s,0,sizeof(*s));s->free_entries=free_entries;return ESP_OK;}
static void reset(void){for(unsigned i=0;i<count;i++)free(blobs[i].data);count=commits=fail_commit=0;fail_after=fail_set=false;free_entries=1000;memset(blobs,0,sizeof(blobs));}
