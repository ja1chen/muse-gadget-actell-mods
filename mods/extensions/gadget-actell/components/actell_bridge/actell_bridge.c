/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_lan.h"
#include "actell_flat.h"
#include "actell_flat_store.h"
#include <time.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"
#include "actell_image_stream.h"
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
extern void noise_ctrl_send_image_result(uint64_t generation,const char *id,cJSON *result,actell_image_blob image);

typedef struct {uint64_t generation;
int64_t expires,queued_at,began_at;
char id[129],op[32];
unsigned local_action,local_index;
actell_image_blob *image_out;
actell_http_locator *locator;
cJSON *params;
} job;

size_t actell_bridge_result_limit(void){return ACTELL_RESULT_MAX;}

static actell_guard *guard;

enum { ACTELL_QUEUE_CAPACITY = 20 };
static QueueHandle_t queue;

static SemaphoreHandle_t mutex;

static bool started;
/* Guarded with admission; obsolete-generation jobs also drain before cleanup. */
static unsigned pending_jobs;
static uint64_t idle_hint_generation;
/* Worker-owned capability and opportunity, never cloud task/run identities. */
static uint64_t peer_idle_release_generation, idle_candidate_generation;
static int64_t idle_candidate_until;

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
void actell_bridge_observe_event(const cJSON *event){
 const char *type=str(event,"type"),*name=str(event,"event");
 const char *status=str(cJSON_GetObjectItemCaseSensitive(event,"payload"),"status");
 if(!type||strcmp(type,"event")||!name||strcmp(name,"task.status")||
    !status||strcmp(status,"completed")||!mutex||!guard)return;
 xSemaphoreTake(mutex,portMAX_DELAY);
 bool post=started&&guard->generation&&!pending_jobs&&!idle_hint_generation;
 if(post)idle_hint_generation=guard->generation;
 xSemaphoreGive(mutex);
 if(post)actell_bridge_pair_wake();
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
static cJSON *image_result(actell_response*r,const cJSON*p,const char*id,actell_image_blob *image){
#if CONFIG_ACTELL_BRIDGE_IMAGES
 const char *image_id=str(p,"image_id");
const cJSON*g=cJSON_GetObjectItemCaseSensitive(p,"generation");

 if(r->length>ACTELL_IMAGE_MAX)return error("image_invalid","JPEG exceeds the image budget");
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

 unsigned char *b64=NULL;
 if(!image){
  size_t cap=4*((r->length+2)/3)+1;
  b64=malloc(cap);
  if(!b64)return error("memory","Image buffer unavailable");
  if(mbedtls_base64_encode(b64,cap,&n,r->body,r->length)!=0){free(b64);return error("image_invalid","Image encoding failed");}
  b64[n]=0;
 }
 cJSON *out=cJSON_CreateObject();
 cJSON *payload=out?cJSON_AddObjectToObject(out,"payload"):NULL;
 if(!payload||!cJSON_AddBoolToObject(out,"ok",true)||
    !cJSON_AddStringToObject(payload,"format","jpeg-base64")||
    !cJSON_AddStringToObject(payload,"data_base64",b64?(char*)b64:"")){
  free(b64);cJSON_Delete(out);return error("memory","Image metadata unavailable");
 }
 free(b64);
 if(image){
  if(image->data||!cJSON_AddBoolToObject(payload,"_actell_stream_image",true)){
   cJSON_Delete(out);return error("memory","Image transfer slot unavailable");
  }
  image->data=r->body;image->length=r->length;r->body=NULL;
 }
 if(!cJSON_AddStringToObject(payload,"image_id",r->image_id)||
   !cJSON_AddNumberToObject(payload,"generation",g->valuedouble)||
   !cJSON_AddStringToObject(payload,"geometry_revision",r->revision)||
   !cJSON_AddNumberToObject(payload,"width",w)||
   !cJSON_AddNumberToObject(payload,"height",h)||
   !cJSON_AddStringToObject(payload,"coordinate_space","delivered-image-pixels")||
   !cJSON_AddBoolToObject(payload,"model_visibility_verified",false)){
 cJSON_Delete(out);return error("memory","Image metadata unavailable");
}
return out;

#else
 (void)r;
(void)p;
(void)id;
(void)image;
return error("model_visibility_gate","Experimental image output is disabled");

#endif
}
static cJSON *execute(job*j);
static cJSON *rpc_step(void *context,const char *op,const cJSON *params){
 job nested=*(job*)context;snprintf(nested.op,sizeof(nested.op),"%s",op);
 actell_random_uuid(nested.id);nested.params=(cJSON*)params;
 return execute(&nested);
}
typedef struct {job *work;actell_flat_store store;} flat_context;
static cJSON *flat_step(void *ctx,const char *op,const cJSON *p){return rpc_step(((flat_context*)ctx)->work,op,p);}
static bool flat_current(void *ctx){return actell_bridge_current(((flat_context*)ctx)->work->generation);}
static long long flat_remaining(void *ctx){return (((flat_context*)ctx)->work->expires-esp_timer_get_time())/1000;}
static void flat_pause(void *ctx,unsigned ms){
 (void)ctx;
 while(ms){
  unsigned next=actell_http_idle_wait_ms(),chunk=ms<next?ms:next;
  unsigned ticks=pdMS_TO_TICKS(chunk);
  vTaskDelay(ticks?ticks:1);ms-=chunk;
 }
 (void)actell_http_idle_wait_ms(); /* Local actell.wait also observes idle expiry. */
}
static void flat_trace(flat_context *ctx,const actell_flat_record *r){
 if(ctx->work->locator){
  ctx->work->locator->operation_id[0]=0;
  if(strlen(r->operation_id)==64)memcpy(ctx->work->locator->operation_id,r->operation_id,65);
 }
 ESP_LOGI("actell.rpc","phase=operation_begin operation_id=%s queue_ms=%lld",r->operation_id,(long long)((ctx->work->began_at-ctx->work->queued_at)/1000));
}
static int flat_prepare(void *ctx,const char *id,const char *command,const cJSON *p,actell_flat_record *r){
 int result=actell_flat_store_prepare(&((flat_context*)ctx)->store,id,command,p,r);
 if(result>=0)flat_trace(ctx,r);
 return result;
}
static bool flat_lookup(void *ctx,const char *id,actell_flat_record *r){
 bool found=actell_flat_store_lookup(&((flat_context*)ctx)->store,id,r);
 if(found)flat_trace(ctx,r);
 return found;
}
static cJSON *execute(job*j){
 if(actell_flat_command(j->op)){
  if(!actell_clock_ready())return error("clock_unavailable","Waiting for clock synchronization; no phone action submitted");
  actell_config *config=calloc(1,sizeof(*config));if(!config)return error("memory","Configuration buffer unavailable");
  flat_context context={.work=j};
  bool loaded=actell_config_load(config)==ESP_OK;
  bool owner=loaded&&actell_flat_store_owner(&context.store,config->client,config->fingerprint);
  volatile unsigned char *wipe=(volatile unsigned char*)config;for(size_t i=0;i<sizeof(*config);i++)wipe[i]=0;free(config);
  if(!loaded)return error("pairing_required","Connect Actell in device settings");
  if(!owner)return error("identity_unavailable","Saved Actell identity cannot initialize the operation store");
  long long budget=flat_remaining(&context)-5000;
  if(budget<=0)return error("deadline_exceeded","Request expired before phone submission");
  context.store.deadline_ms=(double)time(NULL)*1000+budget;
  actell_flat_executor executor={flat_prepare,flat_lookup,flat_step,flat_current,flat_remaining,flat_pause,&context};
  cJSON *out=actell_flat_execute(j->op,j->params,j->id,&executor);
  if(context.store.error){
   cJSON *e=cJSON_GetObjectItemCaseSensitive(out,"error");if(cJSON_IsObject(e)){
    cJSON_DeleteItemFromObjectCaseSensitive(e,"code");cJSON_AddStringToObject(e,"code",context.store.error);
    cJSON_DeleteItemFromObjectCaseSensitive(e,"message");
    cJSON_AddStringToObject(e,"message",!strcmp(context.store.error,"idempotency_conflict")?"This invoke ID has different arguments or pairing ownership. No new action was submitted.":"Actell operation identity is unavailable. No new action was submitted. Preserve the original operation; do not replay.");
   }
  }
  ESP_LOGI("actell.rpc","phase=operation_store used=%u capacity=%u",context.store.used,context.store.capacity);
  return out;
 }
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
int64_t rpc_started=esp_timer_get_time();
esp_err_t e=body?actell_http_with_locator(c,j->id,body,!strcmp(j->op,"images.get"),j->generation,j->expires,&r,j->locator):ESP_ERR_NO_MEM;

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
 char message[384];
 snprintf(message,sizeof(message),"Actell %s failed at %s: %s; HTTP=%d; received=%u/%lld bytes; read_result=%d. Query original status; do not replay.",
          j->op,r.transport_stage?r.transport_stage:"transport",esp_err_to_name(e),r.http_status,(unsigned)r.received_bytes,r.expected_bytes,r.read_result);
 return error(actell_op_mutates(j->op)?"unknown":e==ESP_ERR_NO_MEM?"memory":"transport_error",message);
 }

 cJSON*out=NULL;

 if(!strcmp(j->op,"images.get")&&!strcmp(r.mime,"image/jpeg")){
 int64_t validation_started=esp_timer_get_time();
 out=image_result(&r,j->params,j->id,j->image_out);
 ESP_LOGI("actell.rpc","phase=image_validation operation_id=%s request_id=%s elapsed_ms=%lld bytes=%u ok=%d",j->locator?j->locator->operation_id:"-",j->id,(long long)((esp_timer_get_time()-validation_started)/1000),(unsigned)r.length,cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(out,"ok")));
 const char *image_error=str(cJSON_GetObjectItemCaseSensitive(out,"error"),"code");
 if(image_error&&!strcmp(image_error,"image_invalid"))actell_http_invalidate(j->locator);
 }

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
 if(!strcmp(j->op,"capabilities"))
     peer_idle_release_generation=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(
         cJSON_GetObjectItemCaseSensitive(result,"performance"),"idle_release"))?j->generation:0;
 xSemaphoreTake(mutex,portMAX_DELAY);
 if(guard->generation==j->generation)actell_activity_observe(j->generation,j->op,j->params,result);
 xSemaphoreGive(mutex);
}
out=cJSON_CreateObject();
cJSON_AddBoolToObject(out,"ok",cJSON_IsTrue(ok));
cJSON_AddItemToObject(out,cJSON_IsTrue(ok)?"payload":"error",cJSON_Duplicate(cJSON_IsTrue(ok)?result:cJSON_GetObjectItemCaseSensitive(reply,"error"),true));
}
  else {actell_http_invalidate(j->locator);out=error(actell_op_mutates(j->op)?"unknown":"protocol_error","Invalid response; do not replay writes");}

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
 ESP_LOGI("actell.rpc","phase=rpc_done operation_id=%s request_id=%s op=%s elapsed_ms=%lld",j->locator?j->locator->operation_id:"-",j->id,j->op,(long long)((esp_timer_get_time()-rpc_started)/1000));
 free(r.body);
return out;

}
/* Busy hints are discarded, not saved to terminate an unrelated later run. */
static void reclaim_idle_hint(void){
 xSemaphoreTake(mutex,portMAX_DELAY);
 uint64_t generation=idle_hint_generation;
 idle_hint_generation=0;
 bool idle_now=generation&&guard->generation==generation&&!pending_jobs;
 xSemaphoreGive(mutex);
 if(!idle_now||!actell_bridge_current(generation))return;
 bool notify=peer_idle_release_generation==generation&&idle_candidate_generation==generation&&
     esp_timer_get_time()<idle_candidate_until;
 idle_candidate_generation=0;idle_candidate_until=0;
 bool ok=false;
 if(notify){
  /* New invokes may queue now, but cannot use resources until this bounded
   * worker operation finishes. Actell independently checks ALL media users. */
  actell_http_locator locator={0};
  job request={.generation=generation,.expires=esp_timer_get_time()+2000000,.locator=&locator};
  strcpy(request.op,"companion.release_idle");actell_random_uuid(request.id);
  request.params=cJSON_CreateObject();
  cJSON *response=request.params?execute(&request):NULL;
  ok=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response,"ok"));
  cJSON_Delete(response);cJSON_Delete(request.params);
  actell_http_locator_close(&locator);
 }
 actell_http_close_idle();
 ESP_LOGI("actell.rpc","phase=idle_release reason=completion_hint notified=%d ok=%d",notify,ok);
}
static void worker(void*unused){(void)unused;
job*j;

 actell_pair_boot();
 for(;;){
  (void)actell_http_idle_wait_ms();
  reclaim_idle_hint();
  actell_pair_poll();
  /* Wake at the idle connection deadline even when no new tool arrives. */
  unsigned wait_ms=actell_http_idle_wait_ms();
  if(xQueueReceive(queue,&j,pdMS_TO_TICKS(wait_ms)+1)!=pdTRUE)continue;

  if(!j)continue; /* Clock/network/session wake: recheck state on the next iteration. */
  if(j->local_action){
   actell_http_invalidate(NULL);peer_idle_release_generation=idle_candidate_generation=0;
   actell_pair_process(j->local_action,j->local_index);free(j);
   xSemaphoreTake(mutex,portMAX_DELAY);pending_jobs--;xSemaphoreGive(mutex);
   continue;
  }
  xSemaphoreTake(mutex,portMAX_DELAY);
bool current=j->generation==guard->generation;
xSemaphoreGive(mutex);

  int64_t began=esp_timer_get_time();j->began_at=began;
  ESP_LOGI("actell.rpc","phase=worker_begin op=%s wait_ms=%lld current=%d",j->op,
           (long long)((began-j->queued_at)/1000),current);
  xSemaphoreTake(mutex,portMAX_DELAY);
  if(current&&guard->generation==j->generation&&j->expires>began)
      actell_activity_begin(j->generation,j->op,(uint32_t)((j->expires-began+999)/1000));
  xSemaphoreGive(mutex);
  actell_image_blob image={0};j->image_out=&image;
  actell_http_locator locator={0};j->locator=&locator;
  cJSON*r=!current?error("unknown","Session changed; queued request was not sent"):esp_timer_get_time()>j->expires?error("deadline_exceeded","Request expired in local queue"):execute(j);
  if(current&&strcmp(j->op,"actell.wait")&&strcmp(j->op,"actell.call_status")){
   idle_candidate_generation=j->generation;idle_candidate_until=esp_timer_get_time()+600000000LL;
  }

  if(image.data){
   snprintf(image.operation_id,sizeof(image.operation_id),"%s",locator.operation_id);
   image.invoked_at_us=j->queued_at;image.queued_at_us=esp_timer_get_time();
  }
  ESP_LOGI("actell.rpc","phase=worker_done operation_id=%s op=%s elapsed_ms=%lld ok=%d",locator.operation_id,j->op,
           (long long)((esp_timer_get_time()-began)/1000),cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r,"ok")));
  /* Exactly one completion attempt. Upstream owns result; disconnected/old-generation
     responses can be dropped by its documented bounded result queue. */
  xSemaphoreTake(mutex,portMAX_DELAY);
bool finish=actell_guard_finish(guard,j->generation,j->id);
  actell_activity_end(j->generation,finish&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r,"ok")));
xSemaphoreGive(mutex);

  if(finish&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r,"ok"))&&esp_timer_get_time()<j->expires)
      actell_http_locator_release(&locator);
  else actell_http_locator_close(&locator);

  ESP_LOGI("actell.rpc","phase=result_dispatch op=%s current=%d",j->op,finish);
  if(r)cJSON_AddBoolToObject(r,"_actell",true);
  cJSON *payload=cJSON_GetObjectItemCaseSensitive(r,"payload");
  cJSON_DeleteItemFromObjectCaseSensitive(payload,"_actell_stream_image");
  if(finish&&image.data&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r,"ok")))
      noise_ctrl_send_image_result(j->generation,j->id,r,image);
  else{
   free(image.data);
   if(finish)noise_ctrl_send_command_result(j->generation,j->id,r);
   else cJSON_Delete(r);
  }
  ESP_LOGI("actell.rpc","phase=result_dispatch_done op=%s",j->op);

  cJSON_Delete(j->params);
free(j);
  xSemaphoreTake(mutex,portMAX_DELAY);pending_jobs--;xSemaphoreGive(mutex);

 }
}
void actell_bridge_init(void){
 if(!mutex)mutex=xSemaphoreCreateMutex();
 if(!mutex)return;
 xSemaphoreTake(mutex,portMAX_DELAY);
 if(!guard)guard=heap_caps_calloc(1,sizeof(*guard),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!guard)guard=calloc(1,sizeof(*guard));
 if(!guard){xSemaphoreGive(mutex);return;}
 if(!started){actell_flat_store_reset();queue=xQueueCreate(ACTELL_QUEUE_CAPACITY,sizeof(job*));
 if(queue&&xTaskCreate(worker,"actell_rpc",12288,NULL,3,NULL)==pdPASS){
 started=true;ESP_LOGI("actell.rpc","worker_ready queue_capacity=%u",(unsigned)ACTELL_QUEUE_CAPACITY);}
 else if(queue){vQueueDelete(queue);queue=NULL;}}
 xSemaphoreGive(mutex);
}
void actell_bridge_session(uint64_t generation){
 actell_bridge_init();if(!mutex||!guard)return;
 xSemaphoreTake(mutex,portMAX_DELAY);
 bool changed=guard->generation&&guard->generation!=generation;
 if(guard->generation!=generation)idle_hint_generation=0;
 actell_guard_session(guard,generation);xSemaphoreGive(mutex);
 if(changed)actell_bridge_pair_wake(); /* Only the worker closes idle TLS. */
}
bool actell_bridge_local_job(unsigned action,unsigned index){
 actell_bridge_init();if(!started)return false;
 job*j=calloc(1,sizeof(*j));if(!j)return false;j->local_action=action;j->local_index=index;
 xSemaphoreTake(mutex,portMAX_DELAY);
 bool queued=xQueueSend(queue,&j,0)==pdTRUE;
 if(queued){pending_jobs++;idle_hint_generation=0;}
 xSemaphoreGive(mutex);
 if(!queued)free(j);
 return queued;
}
void actell_bridge_pair_wake(void){
 /* A full queue already wakes the worker. Clock/network/session state is retained. */
 job *wake=NULL;
 if(queue)xQueueSend(queue,&wake,0);
}
esp_err_t actell_pair_hello(const actell_config*c,bool*rejected){
 *rejected=false;char id[37];actell_random_uuid(id);
 cJSON*envelope=cJSON_CreateObject();cJSON_AddNumberToObject(envelope,"v",1);cJSON_AddStringToObject(envelope,"client_id",c->client);cJSON_AddStringToObject(envelope,"request_id",id);cJSON_AddStringToObject(envelope,"op","companion.hello");cJSON*p=cJSON_AddObjectToObject(envelope,"params");cJSON_AddStringToObject(p,"device_name",ACTELL_DEVICE_NAME);cJSON_AddNumberToObject(p,"protocol",1);
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
 if(!actell_flat_command(command))return NULL;
#if !CONFIG_ACTELL_BRIDGE_IMAGES
 if(!strcmp(command,"actell.screenshot"))return error("model_visibility_gate","Image output is disabled");
#endif
 const char *op=command;
 cJSON *p=params?cJSON_Duplicate(params,true):cJSON_CreateObject();
 if(!p)return error("memory","Request unavailable");
 if(!actell_id_valid(id)||!actell_flat_validate(command,p)||!unique(p,0)){cJSON_Delete(p);return error("bad_request","Invalid flat-tool arguments");}
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
 unsigned char payload_digest[32];
 if(!actell_flat_arguments_digest(command,p,payload_digest)){cJSON_Delete(p);free(j);return error("unavailable","Argument identity protection unavailable; no action queued");}
 xSemaphoreTake(mutex,portMAX_DELAY);
 for(unsigned i=0;i<guard->used;i++){
  actell_receipt *r=&guard->receipts[i];
  if(!strcmp(r->id,id)&&r->has_payload_digest&&memcmp(r->payload_digest,payload_digest,32)){
   xSemaphoreGive(mutex);cJSON_Delete(p);free(j);return error("idempotency_conflict","This invoke ID has different arguments; no new action queued");
  }
 }
 actell_admission admitted=actell_guard_admit(guard,generation,id,op,digest);
 if(admitted==ACTELL_ADMIT_NEW)for(unsigned i=0;i<guard->used;i++)if(!strcmp(guard->receipts[i].id,id)){
  memcpy(guard->receipts[i].payload_digest,payload_digest,32);guard->receipts[i].has_payload_digest=true;break;
 }
 bool queued=false;
 if(admitted==ACTELL_ADMIT_NEW) {
     queued=xQueueSend(queue,&j,0)==pdTRUE;
     if(queued){pending_jobs++;idle_hint_generation=0;}
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
         char operation[65];for(unsigned i=0;i<32;i++)snprintf(operation+i*2,3,"%02x",digest[i]);
         char message[256];snprintf(message,sizeof(message),"This invoke was already handled. Query actell.call_status with operation_id=%s; never replay the action.",operation);
         cJSON *r=error("invoke_result_unavailable",message);cJSON *payload=cJSON_AddObjectToObject(r,"payload");cJSON_AddStringToObject(payload,"operation_id",operation);return r;
     }
     if(admitted==ACTELL_ADMIT_WRITE_LIMIT)
         return error("write_receipt_limit",
             "This control session reached its protected write-invoke limit. This new request was not queued. Read-only status queries remain available. Do not replay actions or reconnect to bypass protection.");
     return error(admitted==ACTELL_ADMIT_INVALID?"session_changed":"busy",
                  "Request was not queued in this control session; preserve original run/call IDs and do not replay actions");
 }
 ESP_LOGI("actell.rpc","submitted op=%s",op);
 cJSON*r=cJSON_CreateObject();
cJSON_AddBoolToObject(r,"_async",true);
return r;

}
