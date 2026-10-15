/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_private.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"
#include "psa/crypto.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
/* Kept at the official ABI boundary without depending back on main component. */
extern void noise_ctrl_send_command_result(uint64_t generation,const char *id,cJSON *result);

typedef struct {uint64_t generation;
int64_t expires;
char id[129],op[32];
cJSON *params;
} job;

static actell_guard guard;

static QueueHandle_t queue;

static SemaphoreHandle_t mutex;

static bool started;

bool actell_bridge_current(uint64_t generation){
 if(!mutex)return false;

 xSemaphoreTake(mutex,portMAX_DELAY);
bool current=generation&&generation==guard.generation;
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
return error("pairing_required","Trusted USB provisioning required");
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

 if(e!=ESP_OK)return error(actell_op_mutates(j->op)?"unknown":"transport_error","No automatic replay. Reconcile original run/call IDs; not_found is not proof of nonexecution.");

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
 if(guard.generation==j->generation)actell_activity_observe(j->generation,j->op,j->params,result);
 xSemaphoreGive(mutex);
}
out=cJSON_CreateObject();
cJSON_AddBoolToObject(out,"ok",cJSON_IsTrue(ok));
cJSON_AddItemToObject(out,cJSON_IsTrue(ok)?"payload":"error",cJSON_Duplicate(cJSON_IsTrue(ok)?result:cJSON_GetObjectItemCaseSensitive(reply,"error"),true));
}
  else out=error(actell_op_mutates(j->op)?"unknown":"protocol_error","Invalid response; do not replay writes");

  cJSON_Delete(reply);

 }
 free(r.body);
return out;

}
static void worker(void*unused){(void)unused;
job*j;

 uint64_t hello_generation=0;
bool authorized=false;

 for(;
;
){
  xSemaphoreTake(mutex,portMAX_DELAY);
uint64_t live=guard.generation;
xSemaphoreGive(mutex);

  if(live&&live!=hello_generation){hello_generation=live;
authorized=false;
}
  if(live&&!authorized){job hello={.generation=live,.expires=esp_timer_get_time()+20000000};
snprintf(hello.id,sizeof(hello.id),"hello-%llu-%lld",(unsigned long long)live,(long long)esp_timer_get_time());
strcpy(hello.op,"companion.hello");
hello.params=cJSON_CreateObject();
cJSON_AddStringToObject(hello.params,"device_name","Muse gadget");
cJSON_AddNumberToObject(hello.params,"protocol",1);
cJSON*response=execute(&hello);
const char*state=str(cJSON_GetObjectItemCaseSensitive(response,"payload"),"state");
authorized=state&&!strcmp(state,"authorized");
cJSON_Delete(response);
cJSON_Delete(hello.params);
}
  if(xQueueReceive(queue,&j,pdMS_TO_TICKS(30000))!=pdTRUE)continue;

  xSemaphoreTake(mutex,portMAX_DELAY);
bool current=j->generation==guard.generation;
xSemaphoreGive(mutex);

  cJSON*r=!current?error("unknown","Session changed; queued request was not sent"):esp_timer_get_time()>j->expires?error("deadline_exceeded","Request expired in local queue"):execute(j);

  /* Exactly one completion attempt. Upstream owns result; disconnected/old-generation
     responses can be dropped by its documented bounded result queue. */
  xSemaphoreTake(mutex,portMAX_DELAY);
bool finish=actell_guard_finish(&guard,j->generation,j->id);
xSemaphoreGive(mutex);

  if(finish)noise_ctrl_send_command_result(j->generation,j->id,r);
else cJSON_Delete(r);

  cJSON_Delete(j->params);
free(j);

 }
}
void actell_bridge_session(uint64_t generation){
 if(!mutex)mutex=xSemaphoreCreateMutex();
if(!mutex)return;

 xSemaphoreTake(mutex,portMAX_DELAY);
actell_guard_session(&guard,generation);

 if(!started){queue=xQueueCreate(3,sizeof(job*));
if(queue&&xTaskCreate(worker,"actell_rpc",8192,NULL,3,NULL)==pdPASS)started=true;
else if(queue){vQueueDelete(queue);
queue=NULL;
}}
 xSemaphoreGive(mutex);

}
void actell_bridge_register(cJSON*commands){
 const char*names[]={"actell.rpc","actell.image"};
for(unsigned i=0;
i<2;
i++){
#if !CONFIG_ACTELL_BRIDGE_IMAGES
 if(i==1)continue;

#endif
 cJSON*c=cJSON_AddObjectToObject(commands,names[i]);
cJSON_AddStringToObject(c,"description",i?"Fetch thumbnail-v2 image as experimental JPEG. Coordinates use delivered pixels and geometry_revision. Model visibility requires runtime validation.":"Independent Wi-Fi Actell RPC. op: companion.hello, capabilities, runs.open/get/cancel, tools.call, calls.get. Pass params_json object. Never repeat unknown actions; poll original IDs. Coordinate calls require image_id, generation, geometry_revision.");

 cJSON_AddNumberToObject(c,"timeout_ms",60000);
cJSON_AddObjectToObject(c,"optional");
cJSON*required=cJSON_AddObjectToObject(c,"required");
const char*keys[]={"params_json","op"};
for(unsigned k=0;
k<(i?1:2);
k++){cJSON*p=cJSON_AddObjectToObject(required,keys[k]);
cJSON_AddStringToObject(p,"type","string");
cJSON_AddStringToObject(p,"description",k?"Allowed RPC operation":"JSON object; runs.open negotiates thumbnail-v2. tools.call uses stable run_id/call_id and deadline_ms.");
}
 }
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
j->expires=esp_timer_get_time()+40000000;
strcpy(j->id,id);
strcpy(j->op,op);

 if(!started||!mutex){cJSON_Delete(p);
free(j);
return error("unavailable","Bridge worker unavailable");
}
 xSemaphoreTake(mutex,portMAX_DELAY);

 bool duplicate=false;
for(unsigned i=0;
i<guard.used;
i++)if(guard.generation==generation&&!strcmp(guard.receipts[i].id,id))duplicate=true;

 if(duplicate){xSemaphoreGive(mutex);
cJSON_Delete(p);
free(j);
cJSON*r=cJSON_CreateObject();
cJSON_AddBoolToObject(r,"_async",true);
return r;
}
 bool accepted=uxQueueSpacesAvailable(queue)>0&&actell_guard_accept(&guard,generation,id);
if(accepted)accepted=xQueueSend(queue,&j,0)==pdTRUE;
xSemaphoreGive(mutex);

 if(!accepted){cJSON_Delete(p);
free(j);
return error("busy_or_duplicate","Queue/session receipt budget exhausted or duplicate invoke; reconcile original IDs");
}
 cJSON*r=cJSON_CreateObject();
cJSON_AddBoolToObject(r,"_async",true);
return r;

}
