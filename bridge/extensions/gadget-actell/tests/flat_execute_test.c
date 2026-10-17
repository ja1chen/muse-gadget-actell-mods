/* SPDX-License-Identifier: Apache-2.0 */
/* Synthetic durable-store seam and phone RPC. No claim of real NVS durability. */
#include "actell_flat.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {unsigned opens,submits,gets,downloads,closes,prepares,pauses;long long left;int mode;bool existing,checkpoint,current;actell_flat_record record;} fixture;
static const char *word(const cJSON *j,const char *k){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,k);return cJSON_IsString(v)?v->valuestring:NULL;}
static int prepare(void *ctx,const char *invoke,const char *command,const cJSON *a,actell_flat_record *r){
 fixture *f=ctx;(void)invoke;(void)command;(void)a;f->prepares++;if(f->mode==1)return -1;
 if(!f->existing){strcpy(f->record.operation_id,"operation-fixture");strcpy(f->record.run_id,"run-fixture");strcpy(f->record.call_id,"call-fixture");f->record.deadline_ms=1234567890000.0;}
 *r=f->record;f->checkpoint=true;return f->existing?0:1;
}
static bool lookup(void *ctx,const char *op,actell_flat_record *r){fixture *f=ctx;if(strcmp(op,"operation-fixture"))return false;*r=f->record;return f->existing;}
static bool current(void *ctx){return ((fixture*)ctx)->current;}
static long long remaining(void *ctx){return ((fixture*)ctx)->left;}
static void pause_ms(void *ctx,unsigned ms){fixture *f=ctx;f->left-=ms;f->pauses++;}
static cJSON *step(void *ctx,const char *op,const cJSON *p){
 fixture *f=ctx;assert(f->checkpoint||f->existing);
 cJSON *r=cJSON_CreateObject();cJSON_AddBoolToObject(r,"ok",true);cJSON *out=cJSON_AddObjectToObject(r,"payload");
 if(!strcmp(op,"capabilities")){cJSON_AddBoolToObject(out,"independent_input",f->mode!=2);cJSON_AddItemToArray(cJSON_AddArrayToObject(out,"input_contracts"),cJSON_CreateString(ACTELL_NORMALIZED_INPUT));}
 else if(!strcmp(op,"runs.open")){
  if(f->mode==18){cJSON_ReplaceItemInObject(r,"ok",cJSON_CreateBool(false));cJSON *error=cJSON_AddObjectToObject(r,"error");cJSON_AddStringToObject(error,"code","unknown");cJSON_AddStringToObject(error,"message","Synthetic lost open reply wrapped by real transport");}
  if(f->mode==9){cJSON_ReplaceItemInObject(r,"ok",cJSON_CreateBool(false));cJSON *error=cJSON_AddObjectToObject(r,"error");cJSON_AddStringToObject(error,"code","device_busy");cJSON_AddStringToObject(error,"message","Synthetic admission rejected");}
  f->opens++;if(f->mode==17){cJSON_Delete(r);return NULL;}assert(!strcmp(word(p,"input_contract"),ACTELL_NORMALIZED_INPUT));assert(!strcmp(word(p,"image_contract"),"thumbnail-v2"));
  cJSON_AddStringToObject(out,"run_id",word(p,"run_id"));cJSON_AddStringToObject(out,"state","open");cJSON_AddNumberToObject(out,"deadline_ms",f->record.deadline_ms+(f->mode==15?1:0));
  if(f->mode==16)cJSON_ReplaceItemInObject(out,"run_id",cJSON_CreateString("other-run"));
 }else if(!strcmp(op,"runs.cancel")||!strcmp(op,"runs.get")){
  if(!strcmp(op,"runs.cancel"))f->closes++;assert(!strcmp(word(p,"run_id"),"run-fixture"));cJSON_AddStringToObject(out,"run_id","run-fixture");cJSON_AddStringToObject(out,"state","cancelled");cJSON_AddBoolToObject(out,"execution_released",f->mode!=8&&f->mode!=23&&!strcmp(op,"runs.get"));
 }else if(!strcmp(op,"images.get")){
  /* Real retained-image contract permits download after cancellation. */
  if(!f->existing)assert(f->closes==1);
  f->downloads++;if(f->mode==22)f->left=0;assert(!strcmp(word(p,"image_id"),"image-fixture"));
  if(f->mode==6){cJSON_ReplaceItemInObject(r,"ok",cJSON_CreateBool(false));cJSON *error=cJSON_AddObjectToObject(r,"error");cJSON_AddStringToObject(error,"code","image_unavailable");cJSON_AddStringToObject(error,"message","Synthetic original image unavailable");}
  cJSON_AddStringToObject(out,"format","jpeg-base64");cJSON_AddStringToObject(out,"data_base64","synthetic-pixels");cJSON_AddStringToObject(out,"image_id","image-fixture");cJSON_AddNumberToObject(out,"generation",7);cJSON_AddStringToObject(out,"geometry_revision","geometry-fixture");cJSON_AddNumberToObject(out,"width",410);cJSON_AddNumberToObject(out,"height",502);
 }else{
  bool submission=!strcmp(op,"tools.call");assert(submission||!strcmp(op,"calls.get"));
  if(submission){f->submits++;assert(!cJSON_GetObjectItemCaseSensitive(p,"observation"));if(f->mode==3){cJSON_Delete(r);return NULL;}}
  else f->gets++;
  if((f->mode==10&&submission)||(f->mode==11&&!submission)){cJSON_ReplaceItemInObject(r,"ok",cJSON_CreateBool(false));return r;}
  cJSON_AddStringToObject(out,"run_id",word(p,"run_id"));cJSON_AddStringToObject(out,"call_id",word(p,"call_id"));
  bool pending=f->mode==4||(submission&&f->mode!=7);
  cJSON_AddStringToObject(out,"state",f->mode==13?"unknown":f->mode==14?"invalid":f->mode==19?"failed":f->mode==7?"cancelled":pending?"queued":"succeeded");
  if(f->mode==19){cJSON *error=cJSON_AddObjectToObject(out,"error");cJSON_AddStringToObject(error,"code","capture_failed");cJSON_AddStringToObject(error,"message","Synthetic capture failed");}
  if(f->mode==12)cJSON_ReplaceItemInObject(out,"call_id",cJSON_CreateString("wrong-call"));
  cJSON_AddStringToObject(out,"effect",f->mode==7?"may_have_occurred":"acknowledged");
  if(f->mode==5||f->mode==6||f->mode==22||f->mode==23){
   cJSON *image=cJSON_AddObjectToObject(out,"result");cJSON_AddStringToObject(image,"image_contract","thumbnail-v2");cJSON_AddStringToObject(image,"coordinate_space",ACTELL_NORMALIZED_INPUT);cJSON_AddStringToObject(image,"image_id","image-fixture");cJSON_AddNumberToObject(image,"generation",7);cJSON_AddNumberToObject(image,"width",410);cJSON_AddNumberToObject(image,"height",502);cJSON_AddNumberToObject(image,"captured_at_ms",1234567800000.0);cJSON_AddNumberToObject(image,"expires_at_ms",1234569600000.0);cJSON_AddStringToObject(cJSON_AddObjectToObject(image,"geometry"),"revision","geometry-fixture");
  }
 }
 return r;
}
static cJSON *execute(fixture *f,const char *command,const char *json){
 cJSON *a=cJSON_Parse(json);assert(a);actell_flat_executor e={prepare,lookup,step,current,remaining,pause_ms,f};
 cJSON *r=actell_flat_execute(command,a,"invoke-fixture",&e);cJSON_Delete(a);assert(r);return r;
}
static void state(cJSON *r,const char *expected){assert(!strcmp(word(cJSON_GetObjectItemCaseSensitive(r,"payload"),"state"),expected));}
static fixture fresh(int mode){fixture f={0};f.left=5000;f.current=true;f.mode=mode;return f;}
int main(void){
 fixture f=fresh(0);cJSON *r=execute(&f,"actell.tap","{\"x\":250,\"y\":750,\"purpose\":\"Open item\"}");state(r,"succeeded");assert(f.opens==1&&f.submits==1&&f.gets==1&&f.closes==1);cJSON_Delete(r);
 /* Same durable record after simulated executor restart reconciles only. */
 f.existing=true;f.checkpoint=false;r=execute(&f,"actell.tap","{\"x\":250,\"y\":750,\"purpose\":\"Open item\"}");state(r,"succeeded");assert(f.opens==1&&f.submits==1&&f.closes==1);cJSON_Delete(r);
 f=fresh(1);r=execute(&f,"actell.list_apps","{}");state(r,"failed");assert(!f.opens&&!f.submits);cJSON_Delete(r);
 f=fresh(2);r=execute(&f,"actell.list_apps","{}");state(r,"failed");assert(!f.opens&&!f.submits);cJSON_Delete(r);
 f=fresh(3);r=execute(&f,"actell.list_apps","{}");state(r,"unknown");assert(f.submits==1&&f.closes==1&&!f.gets);cJSON_Delete(r);
 f=fresh(4);r=execute(&f,"actell.list_apps","{}");state(r,"pending");assert(f.submits==1&&f.closes==1);cJSON_Delete(r);
 f=fresh(5);r=execute(&f,"actell.screenshot","{}");state(r,"succeeded");cJSON *p=cJSON_GetObjectItemCaseSensitive(r,"payload");assert(!strcmp(word(p,"format"),"jpeg-base64")&&!strcmp(word(p,"coordinate_space"),ACTELL_NORMALIZED_INPUT));assert(f.submits==1&&f.downloads==1&&f.closes==1);cJSON_Delete(r);
 f.existing=true;unsigned submissions=f.submits,closes=f.closes;r=execute(&f,"actell.call_status","{\"operation_id\":\"operation-fixture\"}");state(r,"succeeded");assert(f.submits==submissions&&f.closes==closes&&f.downloads==2);cJSON_Delete(r);
 f=fresh(6);r=execute(&f,"actell.screenshot","{}");state(r,"succeeded");assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(r,"ok")));assert(!strcmp(word(cJSON_GetObjectItemCaseSensitive(r,"payload"),"image_status"),"unavailable"));assert(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r,"payload"),"result"));assert(f.submits==1&&f.downloads==1);assert(strstr(word(cJSON_GetObjectItemCaseSensitive(r,"error"),"message"),"Synthetic original image unavailable"));assert(strstr(word(cJSON_GetObjectItemCaseSensitive(r,"error"),"message"),"operation-fixture"));cJSON_Delete(r);
 f=fresh(7);r=execute(&f,"actell.tap","{\"x\":0,\"y\":0,\"purpose\":\"Open\"}");state(r,"cancelled");assert(!strcmp(word(cJSON_GetObjectItemCaseSensitive(r,"payload"),"effect"),"may_have_occurred"));cJSON_Delete(r);
 f=fresh(9);r=execute(&f,"actell.list_apps","{}");state(r,"failed");assert(!f.submits&&!f.closes&&!strcmp(word(cJSON_GetObjectItemCaseSensitive(r,"payload"),"effect"),"not_sent"));assert(strstr(word(cJSON_GetObjectItemCaseSensitive(r,"error"),"message"),"Synthetic admission rejected"));cJSON_Delete(r);
 f=fresh(8);r=execute(&f,"actell.list_apps","{}");state(r,"succeeded");assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r,"payload"),"run_released")));assert(f.submits==1&&f.closes==1&&f.left>=2500);cJSON_Delete(r);
 for(int mode=10;mode<=18;mode++){
  f=fresh(mode);r=execute(&f,"actell.list_apps","{}");
  state(r,mode>=17?"failed":"unknown");assert(f.opens==1&&f.closes==(mode==16?0:1));
  assert(f.submits==(mode>=15?0:1));cJSON_Delete(r);
 }
 /* A failed historical lookup must not cancel its owner, even for duplicate delivery. */
 f=fresh(10);f.existing=true;strcpy(f.record.operation_id,"operation-fixture");strcpy(f.record.run_id,"run-fixture");strcpy(f.record.call_id,"call-fixture");f.record.deadline_ms=1234567890000.0;f.mode=11;
 r=execute(&f,"actell.call_status","{\"operation_id\":\"operation-fixture\"}");state(r,"unknown");assert(!f.opens&&!f.submits&&!f.closes);cJSON_Delete(r);
 /* An image transfer that exhausts the budget cannot retain phone ownership. */
 f=fresh(22);r=execute(&f,"actell.screenshot","{}");state(r,"succeeded");assert(f.downloads==1&&f.closes==1&&f.left==0);assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r,"payload"),"run_released")));cJSON_Delete(r);
 f=fresh(23);r=execute(&f,"actell.screenshot","{}");state(r,"succeeded");assert(f.downloads==1&&f.closes==1);assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r,"payload"),"run_released")));cJSON_Delete(r);
 f=fresh(19);r=execute(&f,"actell.screenshot","{}");state(r,"failed");assert(strstr(word(cJSON_GetObjectItemCaseSensitive(r,"error"),"message"),"Synthetic capture failed"));assert(strstr(word(cJSON_GetObjectItemCaseSensitive(r,"error"),"message"),"operation-fixture"));assert(f.submits==1&&f.closes==1);cJSON_Delete(r);
 f=fresh(0);r=execute(&f,"actell.wait","{\"duration_ms\":3000}");state(r,"succeeded");assert(!f.opens&&!f.submits&&!f.prepares&&f.pauses);cJSON_Delete(r);
 f=fresh(0);r=execute(&f,"actell.call_status","{\"operation_id\":\"missing\"}");state(r,"unknown");assert(!f.opens&&!f.submits&&!f.prepares);cJSON_Delete(r);
 puts("flat executor candidate: durable seam, single submit, status-only recovery and JPEG payload passed");return 0;
}
