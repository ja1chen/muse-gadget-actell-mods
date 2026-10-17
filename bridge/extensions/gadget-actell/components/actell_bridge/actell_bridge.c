/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_lan.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"
#include "psa/crypto.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
/* Kept at the official ABI boundary without depending back on main component. */
extern void noise_ctrl_send_command_result(uint64_t generation,const char *id,cJSON *result);

typedef struct {uint64_t generation;
int64_t expires,queued_at;
char id[129],op[32];
unsigned local_action,local_index;
cJSON *params;
} job;

static actell_guard *guard;

static QueueHandle_t queue;

static SemaphoreHandle_t mutex;

static bool started;

bool actell_bridge_current(uint64_t generation){
 if(generation==SLAN_LOCAL_GENERATION)return actell_pair_current();
 if(!actell_pair_rpc_allowed()||!mutex||!guard)return false;

 xSemaphoreTake(mutex,portMAX_DELAY);
bool current=generation&&generation==guard->generation;
xSemaphoreGive(mutex);
return current;

}
static cJSON *error(const char*code,const char*message){cJSON*r=cJSON_CreateObject();
cJSON_AddBoolToObject(r,"ok",false);
cJSON*e=cJSON_AddObjectToObject(r,"error");
cJSON_AddStringToObject(e,"code",code);
cJSON_AddStringToObject(e,"message",message);
return r;
}
static const char *str(const cJSON*j,const char*k){const cJSON*v=cJSON_GetObjectItemCaseSensitive(j,k);
return cJSON_IsString(v)?v->valuestring:NULL;
}
static bool unique(const cJSON*j,unsigned depth){if(depth>12||(cJSON_IsNumber(j)&&!isfinite(j->valuedouble)))return false;
for(const cJSON*a=j?j->child:NULL;
a;
a=a->next){if(cJSON_IsObject(j)){for(const cJSON*b=a->next;
b;
b=b->next)if(a->string&&b->string&&!strcmp(a->string,b->string))return false;
}if(!unique(a,depth+1))return false;
}return true;
}
static cJSON *parse(const char*s){const char*end=NULL;
cJSON*j=cJSON_ParseWithOpts(s,&end,true);
if(!j||!unique(j,0)){cJSON_Delete(j);
return NULL;
}return j;
}
static bool same(const cJSON*a,const cJSON*b,const char*k){const char*x=str(a,k),*y=str(b,k);
return x&&y&&!strcmp(x,y);
}
static cJSON *image_result(const actell_response*r,const cJSON*p,const char*id){
#if CONFIG_ACTELL_BRIDGE_IMAGES
 const char *image_id=str(p,"image_id");
const cJSON*g=cJSON_GetObjectItemCaseSensitive(p,"generation");

 if(!actell_id_valid(image_id)||!cJSON_IsNumber(g)||!isfinite(g->valuedouble)||g->valuedouble<0||g->valuedouble>9007199254740991.0||floor(g->valuedouble)!=g->valuedouble)return error("bad_request","Invalid image identity/generation");
 char gen[32];
snprintf(gen,sizeof(gen),"%.0f",cJSON_IsNumber(g)?g->valuedouble:-1.0);

 if(strcmp(r->contract,"thumbnail-v2")||strcmp(r->mime,"image/jpeg")||strcmp(r->request_id,id)||!image_id||strcmp(r->image_id,image_id)||strcmp(r->generation,gen)||!r->revision[0]||r->length<4||r->body[0]!=255||r->body[1]!=216||r->body[r->length-2]!=255||r->body[r->length-1]!=217)return error("image_invalid","Image identity or JPEG metadata mismatch");

 char *end;
unsigned long w=strtoul(r->width,&end,10);
if(*end||!w||w>1280)return error("image_invalid","Invalid thumbnail width");
unsigned long h=strtoul(r->height,&end,10);
if(*end||!h||h>1280)return error("image_invalid","Invalid thumbnail height");

 unsigned actual_w=0,actual_h=0;
if(!actell_jpeg_dimensions(r->body,r->length,&actual_w,&actual_h)||actual_w!=w||actual_h!=h)return error("image_invalid","JPEG dimensions differ from metadata");

 unsigned char digest[32];
size_t n=0;
char hex[65];
if(psa_hash_compute(PSA_ALG_SHA_256,r->body,r->length,digest,32,&n)!=PSA_SUCCESS||n!=32)return error("image_invalid","Image hash unavailable");
for(unsigned i=0;
i<32;
i++)snprintf(hex+2*i,3,"%02x",digest[i]);
if(strcmp(hex,r->digest))return error("image_invalid","Image hash mismatch");

 size_t cap=4*((r->length+2)/3)+1;
unsigned char*b64=malloc(cap);
if(!b64)return error("memory","Image buffer unavailable");
if(mbedtls_base64_encode(b64,cap,&n,r->body,r->length)!=0){free(b64);
return error("image_invalid","Image encoding failed");
}b64[n]=0;

 cJSON*out=cJSON_CreateObject();
cJSON_AddBoolToObject(out,"ok",true);
cJSON*payload=cJSON_AddObjectToObject(out,"payload");
cJSON_AddStringToObject(payload,"format","jpeg-base64");
cJSON_AddStringToObject(payload,"data_base64",(char*)b64);
free(b64);
cJSON_AddStringToObject(payload,"image_id",r->image_id);
cJSON_AddNumberToObject(payload,"generation",g->valuedouble);
cJSON_AddStringToObject(payload,"geometry_revision",r->revision);
cJSON_AddNumberToObject(payload,"width",w);
cJSON_AddNumberToObject(payload,"height",h);
cJSON_AddStringToObject(payload,"coordinate_space","delivered-image-pixels");
cJSON_AddBoolToObject(payload,"model_visibility_verified",false);
return out;

#else
 (void)r;
(void)p;
(void)id;
return error("model_visibility_gate","Experimental image output is disabled");

#endif
}
static cJSON *execute(job*j){
 actell_config *c=calloc(1,sizeof(*c));
if(!c)return error("memory","Configuration buffer unavailable");

 if(actell_config_load(c)!=ESP_OK){free(c);
return error("pairing_required","Connect Actell in device settings");
}
 cJSON*envelope=cJSON_CreateObject();
cJSON_AddNumberToObject(envelope,"v",1);
cJSON_AddStringToObject(envelope,"client_id",c->client);
cJSON_AddStringToObject(envelope,"request_id",j->id);
cJSON_AddStringToObject(envelope,"op",j->op);
cJSON_AddItemToObject(envelope,"params",cJSON_Duplicate(j->params,true));
char*body=cJSON_PrintUnformatted(envelope);
cJSON_Delete(envelope);

 actell_response r={0};
esp_err_t e=body?actell_http(c,j->id,body,!strcmp(j->op,"images.get"),j->generation,j->expires,&r):ESP_ERR_NO_MEM;

 volatile unsigned char*z=(volatile unsigned char*)c;
for(size_t i=0;
i<sizeof(*c);
i++)z[i]=0;
free(c);
free(body);

 if(e!=ESP_OK){
 if(e==ESP_ERR_INVALID_SIZE)
     return error(actell_op_mutates(j->op)?"unknown":"response_too_large",
                  "Actell response exceeds the gadget result limit. This is not a connection failure. Keep original run/call IDs; do not replay the action.");
 if(!actell_op_mutates(j->op)&&e==ESP_ERR_NOT_FOUND)
     return error("phone_unavailable","Saved Actell identity was not found on this Wi-Fi. Keep Actell open with Muse connections enabled on the same Wi-Fi. Do not reset pairing.");
 return error(actell_op_mutates(j->op)?"unknown":"transport_error","Connection unavailable. Do not replay actions; reconcile original run/call IDs. not_found is not proof of nonexecution.");
 }

 cJSON*out=NULL;

 if(!strcmp(j->op,"images.get")&&!strcmp(r.mime,"image/jpeg"))out=image_result(&r,j->params,j->id);

 else {
  cJSON*reply=!strncmp(r.mime,"application/json",16)&&(r.mime[16]==0||r.mime[16]==';')&&r.length<=ACTELL_JSON_MAX?parse((char*)r.body):NULL;

  const cJSON*v=cJSON_GetObjectItemCaseSensitive(reply,"v"),*ok=cJSON_GetObjectItemCaseSensitive(reply,"ok");
const char*rid=str(reply,"request_id");

  cJSON*result=cJSON_GetObjectItemCaseSensitive(reply,"result");

  bool valid=cJSON_IsNumber(v)&&v->valuedouble==1&&cJSON_IsBool(ok)&&rid&&!strcmp(rid,j->id);

  if(valid&&cJSON_IsTrue(ok)) {valid=cJSON_IsObject(result);
if(!strncmp(j->op,"runs.",5)||!strcmp(j->op,"tools.call")||!strcmp(j->op,"calls.get"))valid=valid&&same(result,j->params,"run_id");
if(!strcmp(j->op,"tools.call")||!strcmp(j->op,"calls.get"))valid=valid&&same(result,j->params,"call_id");
}
  if(valid&&cJSON_IsFalse(ok)){
   const cJSON *remote_error=cJSON_GetObjectItemCaseSensitive(reply,"error");
   valid=cJSON_IsObject(remote_error)&&str(remote_error,"code")&&str(remote_error,"message")&&cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(remote_error,"retryable"));
  }
  if(valid){if(cJSON_IsTrue(ok)){
 xSemaphoreTake(mutex,portMAX_DELAY);
 if(guard->generation==j->generation)actell_activity_observe(j->generation,j->op,j->params,result);
 xSemaphoreGive(mutex);
}
out=cJSON_CreateObject();
cJSON_AddBoolToObject(out,"ok",cJSON_IsTrue(ok));
cJSON_AddItemToObject(out,cJSON_IsTrue(ok)?"payload":"error",cJSON_Duplicate(cJSON_IsTrue(ok)?result:cJSON_GetObjectItemCaseSensitive(reply,"error"),true));
}
  else out=error(actell_op_mutates(j->op)?"unknown":"protocol_error","Invalid response; do not replay writes");

  cJSON_Delete(reply);

 }
 /* Audit only fixed protocol words: never parameters, IDs, content or secrets. */
 const char *observed = str(cJSON_GetObjectItemCaseSensitive(out,"payload"),"state");
 const char *safe_state = "-";
 const char *states[] = {"pending_approval","queued","running","succeeded","failed","cancelled","expired","unknown"};
 for(unsigned i=0;i<sizeof(states)/sizeof(*states);i++)
     if(observed&&!strcmp(observed,states[i]))safe_state=states[i];
 const char *tool = str(j->params,"tool");
 ESP_LOGI("actell.rpc","op=%s outcome=%s state=%s kind=%s",j->op,
          cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(out,"ok"))?"ok":"error",safe_state,
          tool&&!strcmp(tool,"list_apps")?"list_apps":"other");
 free(r.body);
return out;

}
static void worker(void*unused){(void)unused;
job*j;

 actell_pair_boot();
 for(;;){
  actell_pair_poll();
  if(xQueueReceive(queue,&j,pdMS_TO_TICKS(30000))!=pdTRUE)continue;

  if(!j)continue; /* Clock wakeup: poll saved pairing on the next iteration. */
  if(j->local_action){actell_pair_process(j->local_action,j->local_index);free(j);continue;}
  xSemaphoreTake(mutex,portMAX_DELAY);
bool current=j->generation==guard->generation;
xSemaphoreGive(mutex);

  int64_t began=esp_timer_get_time();
  ESP_LOGI("actell.rpc","phase=worker_begin op=%s wait_ms=%lld current=%d",j->op,
           (long long)((began-j->queued_at)/1000),current);
  cJSON*r=!current?error("unknown","Session changed; queued request was not sent"):esp_timer_get_time()>j->expires?error("deadline_exceeded","Request expired in local queue"):execute(j);

  ESP_LOGI("actell.rpc","phase=worker_done op=%s elapsed_ms=%lld result=%d",j->op,
           (long long)((esp_timer_get_time()-began)/1000),r!=NULL);
  /* Exactly one completion attempt. Upstream owns result; disconnected/old-generation
     responses can be dropped by its documented bounded result queue. */
  xSemaphoreTake(mutex,portMAX_DELAY);
bool finish=actell_guard_finish(guard,j->generation,j->id);
xSemaphoreGive(mutex);

  ESP_LOGI("actell.rpc","phase=result_dispatch op=%s current=%d",j->op,finish);
  if(finish)noise_ctrl_send_command_result(j->generation,j->id,r);
else cJSON_Delete(r);
  ESP_LOGI("actell.rpc","phase=result_dispatch_done op=%s",j->op);

  cJSON_Delete(j->params);
free(j);

 }
}
void actell_bridge_init(void){
 if(!mutex)mutex=xSemaphoreCreateMutex();
 if(!mutex)return;
 xSemaphoreTake(mutex,portMAX_DELAY);
 if(!guard)guard=heap_caps_calloc(1,sizeof(*guard),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!guard)guard=calloc(1,sizeof(*guard));
 if(!guard){xSemaphoreGive(mutex);return;}
 if(!started){queue=xQueueCreate(3,sizeof(job*));
 if(queue&&xTaskCreate(worker,"actell_rpc",12288,NULL,3,NULL)==pdPASS)started=true;
 else if(queue){vQueueDelete(queue);queue=NULL;}}
 xSemaphoreGive(mutex);
}
void actell_bridge_session(uint64_t generation){
 actell_bridge_init();if(!mutex||!guard)return;
 xSemaphoreTake(mutex,portMAX_DELAY);actell_guard_session(guard,generation);xSemaphoreGive(mutex);
}
bool actell_bridge_local_job(unsigned action,unsigned index){
 actell_bridge_init();if(!started)return false;
 job*j=calloc(1,sizeof(*j));if(!j)return false;j->local_action=action;j->local_index=index;
 if(xQueueSend(queue,&j,0)!=pdTRUE){free(j);return false;}return true;
}
void actell_bridge_pair_wake(void){
 /* A full queue already wakes the worker. The atomic clock event is retained. */
 job *wake=NULL;
 if(queue)xQueueSend(queue,&wake,0);
}
esp_err_t actell_pair_hello(const actell_config*c,bool*rejected){
 *rejected=false;char id[37];actell_random_uuid(id);
 cJSON*envelope=cJSON_CreateObject();cJSON_AddNumberToObject(envelope,"v",1);cJSON_AddStringToObject(envelope,"client_id",c->client);cJSON_AddStringToObject(envelope,"request_id",id);cJSON_AddStringToObject(envelope,"op","companion.hello");cJSON*p=cJSON_AddObjectToObject(envelope,"params");cJSON_AddStringToObject(p,"device_name",actell_device_name(c));cJSON_AddNumberToObject(p,"protocol",1);
 char*body=cJSON_PrintUnformatted(envelope);cJSON_Delete(envelope);if(!body)return ESP_ERR_NO_MEM;
 actell_response r={0};esp_err_t e=actell_http(c,id,body,false,SLAN_LOCAL_GENERATION,esp_timer_get_time()+10000000,&r);free(body);
 if(e!=ESP_OK){*rejected=r.http_status==401||r.http_status==403||r.http_status==410;return e;}
 cJSON*reply=r.length<=ACTELL_JSON_MAX?parse((char*)r.body):NULL;
 const cJSON*v=cJSON_GetObjectItemCaseSensitive(reply,"v"),*ok=cJSON_GetObjectItemCaseSensitive(reply,"ok"),*result=cJSON_GetObjectItemCaseSensitive(reply,"result");
 const char*rid=str(reply,"request_id"),*phase=str(result,"pairing_state");
 bool active=cJSON_IsNumber(v)&&v->valuedouble==1&&cJSON_IsTrue(ok)&&rid&&!strcmp(rid,id)&&phase&&!strcmp(phase,"active");
 if(cJSON_IsFalse(ok)){const char*code=str(cJSON_GetObjectItemCaseSensitive(reply,"error"),"code");*rejected=code&&(!strcmp(code,"unauthorized")||!strcmp(code,"revoked")||!strcmp(code,"pairing_expired"));}
 cJSON_Delete(reply);free(r.body);return active?ESP_OK:ESP_FAIL;
}
cJSON *actell_bridge_invoke(const char*command,const cJSON*params,const char*id,uint64_t generation){
 bool image=!strcmp(command,"actell.image");
if(!image&&strcmp(command,"actell.rpc"))return NULL;

#if !CONFIG_ACTELL_BRIDGE_IMAGES
 if(image)return error("model_visibility_gate","Experimental image output is disabled");

#endif
 const char*op=image?"images.get":str(params,"op"),*raw=str(params,"params_json");

 if(!actell_id_valid(id)||!op||!actell_op_valid(op)||(!image&&!strcmp(op,"images.get"))||!raw||strlen(raw)>4096)return error("bad_request","Invalid operation or bounded params_json");

 cJSON*p=parse(raw);
if(!cJSON_IsObject(p)){cJSON_Delete(p);
return error("bad_request","params_json must be a unique-key JSON object");
}
 if(!strcmp(op,"runs.open")){cJSON_DeleteItemFromObjectCaseSensitive(p,"image_contract");
cJSON_AddStringToObject(p,"image_contract","thumbnail-v2");
}
 job*j=calloc(1,sizeof(*j));
if(!j){cJSON_Delete(p);
return error("memory","Request unavailable");
}j->params=p;
j->generation=generation;
j->queued_at=esp_timer_get_time();
j->expires=j->queued_at+40000000;
strcpy(j->id,id);
strcpy(j->op,op);

 if(!started||!mutex){cJSON_Delete(p);
free(j);
return error("unavailable","Bridge worker unavailable");
}
 unsigned char digest[ACTELL_ID_DIGEST]; size_t digest_len=0;
 if(psa_crypto_init()!=PSA_SUCCESS ||
    psa_hash_compute(PSA_ALG_SHA_256,(const unsigned char*)id,strlen(id),digest,sizeof(digest),&digest_len)!=PSA_SUCCESS ||
    digest_len!=sizeof(digest)) {
     cJSON_Delete(p);free(j);
     return error("unavailable","Invoke identity protection unavailable; this attempt was not queued. An earlier invoke may still exist; reconcile original run/call IDs and do not replay actions");
 }
 xSemaphoreTake(mutex,portMAX_DELAY);
 actell_admission admitted=actell_guard_admit(guard,generation,id,op,digest);
 bool queued=false;
 if(admitted==ACTELL_ADMIT_NEW) {
     queued=xQueueSend(queue,&j,0)==pdTRUE;
     if(!queued)actell_guard_unsent(guard,generation,id,digest);
 }
 xSemaphoreGive(mutex);
 if(admitted!=ACTELL_ADMIT_NEW || !queued) {
     cJSON_Delete(p);free(j);
     if(admitted==ACTELL_ADMIT_PENDING) {
         /* Original job still owns the one eventual completion attempt. */
         cJSON *r=cJSON_CreateObject();cJSON_AddBoolToObject(r,"_async",true);return r;
     }
     if(admitted==ACTELL_ADMIT_FINISHED) {
         ESP_LOGW("actell.rpc","phase=duplicate_completed op=%s",op);
         return error("invoke_result_unavailable",
             "This invoke was already handled; its result delivery and outcome are not retained. Do not replay actions. Reconcile original run/call IDs with a new read-only invoke ID. This is not proof of nonexecution.");
     }
     if(admitted==ACTELL_ADMIT_WRITE_LIMIT)
         return error("write_receipt_limit",
             "This control session reached its protected write-invoke limit. This new request was not queued. Read-only status queries remain available. A reserved runs.cancel budget may still be available to stop the original run. Do not replay actions or reconnect to bypass protection.");
     return error(admitted==ACTELL_ADMIT_INVALID?"session_changed":"busy",
                  "Request was not queued in this control session; preserve original run/call IDs and do not replay actions");
 }
 ESP_LOGI("actell.rpc","submitted op=%s",op);
 cJSON*r=cJSON_CreateObject();
cJSON_AddBoolToObject(r,"_async",true);
return r;

}
