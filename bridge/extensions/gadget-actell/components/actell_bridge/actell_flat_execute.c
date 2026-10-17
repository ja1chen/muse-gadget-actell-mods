/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_flat.h"
#include "actell_guard.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static const char *word(const cJSON *j,const char *k){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,k);return cJSON_IsString(v)?v->valuestring:NULL;}
static bool ok(const cJSON *r){return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r,"ok"));}
static bool available(const actell_flat_executor *e){return e->current(e->context)&&e->remaining_ms(e->context)>0;}
static bool record_valid(const actell_flat_record *r){
 return memchr(r->operation_id,0,sizeof(r->operation_id))&&memchr(r->run_id,0,sizeof(r->run_id))&&memchr(r->call_id,0,sizeof(r->call_id))&&actell_id_valid(r->operation_id)&&actell_id_valid(r->run_id)&&actell_id_valid(r->call_id)&&isfinite(r->deadline_ms)&&r->deadline_ms>0&&r->deadline_ms<=9007199254740991.0&&floor(r->deadline_ms)==r->deadline_ms;
}
static cJSON *result(bool success,const char *state,const char *effect,const char *image,const actell_flat_record *r,const char *code){
 cJSON *out=cJSON_CreateObject();cJSON_AddBoolToObject(out,"ok",success);cJSON *p=cJSON_AddObjectToObject(out,"payload");
 cJSON_AddStringToObject(p,"state",state);cJSON_AddStringToObject(p,"effect",effect);cJSON_AddStringToObject(p,"image_status",image);
 if(r&&*r->operation_id)cJSON_AddStringToObject(p,"operation_id",r->operation_id);
 if(code){cJSON *error=cJSON_AddObjectToObject(out,"error");cJSON_AddStringToObject(error,"code",code);char message[384];snprintf(message,sizeof(message),"Actell %s (operation_id=%s). Query actell.call_status for this operation; do not replay. Missing records do not prove nonexecution.",code,r&&*r->operation_id?r->operation_id:"unavailable");cJSON_AddStringToObject(error,"message",message);}
 return out;
}
static cJSON *params(const actell_flat_record *r,bool call){cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"run_id",r->run_id);if(call)cJSON_AddStringToObject(p,"call_id",r->call_id);return p;}
static cJSON *request(const actell_flat_executor *e,const char *op,cJSON *p){cJSON *r=e->step(e->context,op,p);cJSON_Delete(p);return r;}
static cJSON *unknown(const actell_flat_record *r,const char *code){return result(false,"unknown","may_have_occurred","unavailable",r,code);}
static void surface_error(cJSON *out,const cJSON *remote){
 const char *message=word(remote,"message");
 cJSON *error=cJSON_GetObjectItemCaseSensitive(out,"error");
 if(message&&cJSON_IsObject(error)){
  const char *operation=word(cJSON_GetObjectItemCaseSensitive(out,"payload"),"operation_id");
  const char *code=word(remote,"code");char visible[768];
  size_t length=strlen(message),prefix=length>400?400:length;
  while(prefix<length&&prefix&&((unsigned char)message[prefix]&0xc0)==0x80)prefix--;
  snprintf(visible,sizeof(visible),"%.*s [code=%.80s; operation_id=%s]. Query original status; do not replay.",(int)prefix,message,code?code:"remote_error",operation?operation:"unavailable");
  cJSON_DeleteItemFromObjectCaseSensitive(error,"message");cJSON_AddStringToObject(error,"message",visible);
 }
}
static cJSON *remote_failure(const actell_flat_record *r,cJSON *reply,bool call_may_exist,const char *stage){
 cJSON *out=call_may_exist?unknown(r,stage):result(false,"failed","not_sent","not_applicable",r,stage);
 const cJSON *error=cJSON_GetObjectItemCaseSensitive(reply,"error");
 if(cJSON_IsObject(error)){
  cJSON_AddItemToObject(cJSON_GetObjectItemCaseSensitive(out,"payload"),"remote_error",cJSON_Duplicate(error,true));
  surface_error(out,error);
 }
 cJSON_Delete(reply);return out;
}

static unsigned negotiated_wait(const cJSON *capability,const char *key,unsigned limit){
 const cJSON *v=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(capability,"performance"),key);
 if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<1||v->valuedouble>60000||floor(v->valuedouble)!=v->valuedouble)return 0;
 return v->valuedouble<limit?(unsigned)v->valuedouble:limit;
}
static unsigned remaining_wait(const actell_flat_executor *e,unsigned preferred){
 long long remaining=e->remaining_ms(e->context)-250;
 return remaining<=0?0:remaining<preferred?(unsigned)remaining:preferred;
}
static void close_run(const actell_flat_executor *e,const actell_flat_record *r,cJSON *out,unsigned release_wait_ms){
 /* Cancellation acknowledgement is not proof of completed owner cleanup. */
 bool closed=false;
 unsigned polls=0;
 if(available(e)){
  cJSON *p=params(r,false);unsigned wait=remaining_wait(e,release_wait_ms);
  if(wait)cJSON_AddNumberToObject(p,"wait_ms",wait);
  cJSON *c=request(e,"runs.cancel",p);
  for(;;){
   const cJSON *payload=cJSON_GetObjectItemCaseSensitive(c,"payload"),*released=cJSON_GetObjectItemCaseSensitive(payload,"execution_released");
   const char *run=word(payload,"run_id");
   bool valid=ok(c)&&run&&!strcmp(run,r->run_id)&&cJSON_IsBool(released);
   closed=valid&&cJSON_IsTrue(released);cJSON_Delete(c);
   if(closed||!valid||!available(e)||e->remaining_ms(e->context)<=100||polls++>=20)break;
   e->pause(e->context,100);if(!available(e))break;
   c=request(e,"runs.get",params(r,false)); /* read-only; never resubmit input */
  }
 }
 cJSON_AddBoolToObject(cJSON_GetObjectItemCaseSensitive(out,"payload"),"run_released",closed);
}
/* Only this invocation's confirmed run may be released. Historical status
 * reads and rejected/mismatched run admission never acquire ownership. */
static cJSON *finish(const actell_flat_executor *e,const actell_flat_record *r,bool owned,cJSON *out,unsigned release_wait_ms){
 if(owned)close_run(e,r,out,release_wait_ms);
 return out;
}
static bool same(const cJSON *j,const char *k,const char *value){const char *s=word(j,k);return s&&!strcmp(s,value);}
static cJSON *image_unavailable(const actell_flat_record *r,const cJSON *metadata,const char *code){
 cJSON *out=result(false,"succeeded","not_applicable","unavailable",r,code);
 if(metadata)cJSON_AddItemToObject(cJSON_GetObjectItemCaseSensitive(out,"payload"),"result",cJSON_Duplicate(metadata,true));
 return out;
}
static cJSON *screenshot(const actell_flat_executor *e,const actell_flat_record *r,const cJSON *metadata){
 const char *image=word(metadata,"image_id"),*contract=word(metadata,"image_contract"),*coordinate=word(metadata,"coordinate_space");
 const cJSON *g=cJSON_GetObjectItemCaseSensitive(metadata,"generation"),*w=cJSON_GetObjectItemCaseSensitive(metadata,"width"),*h=cJSON_GetObjectItemCaseSensitive(metadata,"height");
 const cJSON *geometry=cJSON_GetObjectItemCaseSensitive(metadata,"geometry");const char *revision=word(geometry,"revision");
 const cJSON *capture=cJSON_GetObjectItemCaseSensitive(metadata,"captured_at_ms"),*expiry=cJSON_GetObjectItemCaseSensitive(metadata,"expires_at_ms");
 bool valid=actell_id_valid(image)&&contract&&!strcmp(contract,"thumbnail-v2")&&coordinate&&!strcmp(coordinate,ACTELL_NORMALIZED_INPUT)&&revision&&*revision&&strlen(revision)<=128&&cJSON_IsNumber(g)&&isfinite(g->valuedouble)&&g->valuedouble>=0&&g->valuedouble<=9007199254740991.0&&floor(g->valuedouble)==g->valuedouble&&cJSON_IsNumber(w)&&cJSON_IsNumber(h)&&w->valuedouble>=2&&w->valuedouble<=1280&&h->valuedouble>=2&&h->valuedouble<=1280&&floor(w->valuedouble)==w->valuedouble&&floor(h->valuedouble)==h->valuedouble&&cJSON_IsNumber(capture)&&cJSON_IsNumber(expiry)&&isfinite(capture->valuedouble)&&isfinite(expiry->valuedouble)&&capture->valuedouble>=0&&expiry->valuedouble<=9007199254740991.0&&floor(capture->valuedouble)==capture->valuedouble&&floor(expiry->valuedouble)==expiry->valuedouble&&expiry->valuedouble>capture->valuedouble;
 if(!valid)return image_unavailable(r,metadata,"image_metadata_invalid");
 if(!available(e))return image_unavailable(r,metadata,"image_deadline_exceeded");
 cJSON *p=params(r,false);cJSON_AddStringToObject(p,"image_id",image);cJSON_AddNumberToObject(p,"generation",g->valuedouble);
 cJSON *out=request(e,"images.get",p);cJSON *pixels=cJSON_GetObjectItemCaseSensitive(out,"payload");
 const char *data=word(pixels,"data_base64");
 const cJSON *pg=cJSON_GetObjectItemCaseSensitive(pixels,"generation"),*pw=cJSON_GetObjectItemCaseSensitive(pixels,"width"),*ph=cJSON_GetObjectItemCaseSensitive(pixels,"height");
 /* step(images.get) must already verify JPEG bytes/hash/SOF/header identity. */
 bool image_ok=ok(out)&&same(pixels,"format","jpeg-base64")&&data&&(*data||cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(pixels,"_actell_stream_image")))&&same(pixels,"image_id",image)&&same(pixels,"geometry_revision",revision)&&cJSON_IsNumber(pg)&&pg->valuedouble==g->valuedouble&&cJSON_IsNumber(pw)&&pw->valuedouble==w->valuedouble&&cJSON_IsNumber(ph)&&ph->valuedouble==h->valuedouble;
 if(!image_ok){
  cJSON *failure=image_unavailable(r,metadata,ok(out)?"image_response_invalid":"image_unavailable");
  const cJSON *error=cJSON_GetObjectItemCaseSensitive(out,"error");
  if(cJSON_IsObject(error)){
   cJSON_AddItemToObject(cJSON_GetObjectItemCaseSensitive(failure,"payload"),"remote_error",cJSON_Duplicate(error,true));
   surface_error(failure,error);
  }
  cJSON_Delete(out);return failure;
 }
 cJSON_DeleteItemFromObjectCaseSensitive(pixels,"coordinate_space");cJSON_AddStringToObject(pixels,"coordinate_space",ACTELL_NORMALIZED_INPUT);
 cJSON_AddStringToObject(pixels,"operation_id",r->operation_id);cJSON_AddStringToObject(pixels,"state","succeeded");cJSON_AddStringToObject(pixels,"effect","not_applicable");cJSON_AddStringToObject(pixels,"image_status","available");
 cJSON_AddNumberToObject(pixels,"captured_at_ms",capture->valuedouble);cJSON_AddNumberToObject(pixels,"expires_at_ms",expiry->valuedouble);
 return out;
}
cJSON *actell_flat_execute(const char *command,const cJSON *a,const char *invoke,const actell_flat_executor *e){
 if(!e||!e->prepare||!e->lookup||!e->step||!e->current||!e->remaining_ms||!e->pause||!actell_id_valid(invoke)||!actell_flat_validate(command,a))return result(false,"failed","not_sent","not_applicable",NULL,"invalid_executor_or_arguments");
 bool query=!strcmp(command,"actell.call_status");
 if(!strcmp(command,"actell.wait")){
  unsigned duration=(unsigned)cJSON_GetObjectItemCaseSensitive(a,"duration_ms")->valueint;
  /* Local wait deliberately does not create a phone run or enter the journal. */
  while(duration){if(!available(e))return result(false,"failed","not_sent","not_applicable",NULL,"deadline_exceeded");unsigned n=duration>50?50:duration;if(e->remaining_ms(e->context)<n)return result(false,"failed","not_sent","not_applicable",NULL,"deadline_exceeded");e->pause(e->context,n);duration-=n;}
  return result(true,"succeeded","not_applicable","not_applicable",NULL,NULL);
 }
 actell_flat_record record={0};int admission=0;
 if(query){
  if(!e->lookup(e->context,word(a,"operation_id"),&record)||!record_valid(&record))return result(false,"unknown","may_have_occurred","unavailable",NULL,"operation_unavailable");
 }else{
  admission=e->prepare(e->context,invoke,command,a,&record);
  if(admission<0||admission>1||!record_valid(&record))return result(false,"failed","not_sent","not_applicable",NULL,"journal_unavailable");
 }
 if(!available(e))return unknown(&record,"deadline_exceeded");
 cJSON *reply=NULL;bool owned=false;unsigned call_wait_ms=0,release_wait_ms=0;
 if(admission==1){
  reply=request(e,"capabilities",cJSON_CreateObject());
  if(!ok(reply))return remote_failure(&record,reply,false,"capability_unavailable");
  const cJSON *capability=cJSON_GetObjectItemCaseSensitive(reply,"payload");
  bool compatible=actell_flat_capability(capability);
  call_wait_ms=negotiated_wait(capability,"tools_call_wait_ms_max",1500);
  release_wait_ms=negotiated_wait(capability,"runs_cancel_wait_ms_max",500);
  cJSON_Delete(reply);
  if(!compatible)return result(false,"failed","not_sent","not_applicable",&record,"incompatible_capability");
  if(!available(e))return result(false,"failed","not_sent","not_applicable",&record,"deadline_exceeded");
  cJSON *p=params(&record,false);cJSON_AddNumberToObject(p,"deadline_ms",record.deadline_ms);cJSON_AddStringToObject(p,"input_contract",ACTELL_NORMALIZED_INPUT);cJSON_AddStringToObject(p,"image_contract","thumbnail-v2");
  reply=request(e,"runs.open",p);
  if(!ok(reply)){
   /* A lost open reply may hide a successfully created run. Read only its
    * deterministic ID to establish ownership; never cancel a busy owner. */
   const cJSON *open_error=cJSON_GetObjectItemCaseSensitive(reply,"error");
   bool uncertain=!reply||same(open_error,"code","unknown")||same(open_error,"code","transport_error")||same(open_error,"code","protocol_error")||same(open_error,"code","response_too_large");
   if(uncertain&&available(e)){
    cJSON *check=request(e,"runs.get",params(&record,false));
    owned=ok(check)&&same(cJSON_GetObjectItemCaseSensitive(check,"payload"),"run_id",record.run_id);
    cJSON_Delete(check);
   }
   return finish(e,&record,owned,remote_failure(&record,reply,false,"run_open_unconfirmed"),release_wait_ms);
  }
  const cJSON *run=cJSON_GetObjectItemCaseSensitive(reply,"payload"),*deadline=cJSON_GetObjectItemCaseSensitive(run,"deadline_ms");
  owned=same(run,"run_id",record.run_id);
  bool valid=owned&&same(run,"state","open")&&cJSON_IsNumber(deadline)&&deadline->valuedouble==record.deadline_ms;
  cJSON_Delete(reply);
  if(!valid||!available(e))return finish(e,&record,owned,unknown(&record,"run_open_unconfirmed"),release_wait_ms);
  p=actell_flat_call(command,a,record.run_id,record.call_id,record.deadline_ms);
  if(!p)return finish(e,&record,owned,unknown(&record,"call_build_failed"),release_wait_ms);
  unsigned wait=remaining_wait(e,call_wait_ms);
  if(wait)cJSON_AddNumberToObject(p,"wait_ms",wait);
  reply=request(e,"tools.call",p); /* Exactly one submission, never retried. */
 }else reply=request(e,"calls.get",params(&record,true));
 for(;;){
  if(!ok(reply))return finish(e,&record,owned,remote_failure(&record,reply,true,"result_unconfirmed"),release_wait_ms);
  const cJSON *receipt=cJSON_GetObjectItemCaseSensitive(reply,"payload");const char *state=word(receipt,"state");
  if(!same(receipt,"run_id",record.run_id)||!same(receipt,"call_id",record.call_id)||!state){cJSON_Delete(reply);return finish(e,&record,owned,unknown(&record,"invalid_receipt"),release_wait_ms);}
  if(!strcmp(state,"queued")||!strcmp(state,"running")||!strcmp(state,"pending_approval")){
   cJSON_Delete(reply);
   if(query||!available(e)||e->remaining_ms(e->context)<=500)return finish(e,&record,owned,result(true,"pending","may_have_occurred","unavailable",&record,NULL),release_wait_ms);
   e->pause(e->context,500);if(!available(e))return finish(e,&record,owned,result(true,"pending","may_have_occurred","unavailable",&record,NULL),release_wait_ms);
   reply=request(e,"calls.get",params(&record,true));continue;
  }
  if(!strcmp(state,"unknown")){cJSON_Delete(reply);return finish(e,&record,owned,unknown(&record,"result_unconfirmed"),release_wait_ms);}
  if(strcmp(state,"succeeded")&&strcmp(state,"failed")&&strcmp(state,"cancelled")&&strcmp(state,"expired")){cJSON_Delete(reply);return finish(e,&record,owned,unknown(&record,"invalid_receipt"),release_wait_ms);}
  const cJSON *remote_result=cJSON_GetObjectItemCaseSensitive(receipt,"result");
  bool capture=!strcmp(command,"actell.screenshot")||(query&&cJSON_IsObject(remote_result)&&word(remote_result,"image_contract"));
  cJSON *out;
  if(!strcmp(state,"succeeded")&&capture){
   /* Terminal capture is retained independently of execution ownership.
    * Release before network/image work can consume the remaining budget. */
   cJSON *release=NULL;
   if(owned){
    release=result(true,"succeeded","not_applicable","unavailable",&record,NULL);
    close_run(e,&record,release,release_wait_ms);owned=false;
   }
   out=screenshot(e,&record,remote_result);
   const cJSON *released=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(release,"payload"),"run_released");
   if(cJSON_IsBool(released))cJSON_AddBoolToObject(cJSON_GetObjectItemCaseSensitive(out,"payload"),"run_released",cJSON_IsTrue(released));
   cJSON_Delete(release);
  }
  else{
   const char *effect=word(receipt,"effect");
   if(!effect|| (strcmp(effect,"not_sent")&&strcmp(effect,"acknowledged")&&strcmp(effect,"may_have_occurred")&&strcmp(effect,"not_applicable")))effect=!strcmp(state,"succeeded")?"acknowledged":"may_have_occurred";
   out=result(!strcmp(state,"succeeded"),state,effect,"not_applicable",&record,strcmp(state,"succeeded")?"remote_terminal_error":NULL);
   cJSON *payload=cJSON_GetObjectItemCaseSensitive(out,"payload");
   if(remote_result)cJSON_AddItemToObject(payload,"result",cJSON_Duplicate(remote_result,true));
   const cJSON *error=cJSON_GetObjectItemCaseSensitive(receipt,"error");if(error){cJSON_AddItemToObject(payload,"remote_error",cJSON_Duplicate(error,true));surface_error(out,error);}
  }
  cJSON_Delete(reply);
  /* Status reads never acquire/release execution ownership. */
  return finish(e,&record,owned,out,release_wait_ms);
 }
}
