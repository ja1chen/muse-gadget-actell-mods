/* SPDX-License-Identifier: Apache-2.0 */
/* Actual adapter + guard + upstream cJSON. Only scheduler/network/PSA are
 * synthetic. Fake digest below tests admission plumbing, not cryptography. */
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <time.h>
#include "../components/actell_bridge/actell_bridge.c"
static jmp_buf stop_worker;
static void *items[ACTELL_QUEUE_CAPACITY];
static unsigned q_count,http_count,action_count,callbacks,pair_polls;
static int locked;
static bool reject_send,drop_result,fail_http,fail_hash;
static unsigned hello_mode,hello_names_checked;
static bool view_mode,bad_view_image;
static unsigned invalidations;
static unsigned connection_releases,idle_polls,idle_closes,idle_notifications;
static bool hint_during_http;
static void (*on_idle_http)(void);
static void completed_hint(void){
 cJSON *e=cJSON_Parse("{\"type\":\"event\",\"event\":\"task.status\",\"payload\":{\"status\":\"completed\"}}");
 assert(e);actell_bridge_observe_event(e);cJSON_Delete(e);
}
static unsigned view_captures,view_downloads,view_closes,view_capture_closes;
static const char *expected_hello_name="Muse Gadget";
static cJSON *last_result;
static void (*created_worker)(void *);
int64_t esp_timer_get_time(void){return 1000000;}
void vTaskDelay(unsigned ms){(void)ms;}
bool actell_clock_ready(void){return true;}
void *heap_caps_calloc(size_t n,size_t size,unsigned caps){(void)caps;return calloc(n,size);}
SemaphoreHandle_t xSemaphoreCreateMutex(void){return &locked;}
int xSemaphoreTake(SemaphoreHandle_t m,unsigned wait){(void)m;(void)wait;assert(!locked);locked=1;return 1;}
int xSemaphoreGive(SemaphoreHandle_t m){(void)m;assert(locked);locked=0;return 1;}
QueueHandle_t xQueueCreate(unsigned n,size_t size){assert(n==20 && size==sizeof(void*));return items;}
void vQueueDelete(QueueHandle_t q){(void)q;}
int xQueueSend(QueueHandle_t q,const void *p,unsigned wait){(void)q;(void)wait;
 if(reject_send){reject_send=false;return 0;}if(q_count==ACTELL_QUEUE_CAPACITY)return 0;
 items[q_count++]=*(void*const*)p;return pdTRUE;
}
int xQueueReceive(QueueHandle_t q,void *p,unsigned wait){(void)q;assert(wait==18); /* transport's next reap deadline */
 if(!q_count)longjmp(stop_worker,1);
 *(void**)p=items[0];for(unsigned i=1;i<q_count;i++)items[i-1]=items[i];q_count--;return pdTRUE;
}
int xTaskCreate(void (*fn)(void*),const char *name,unsigned stack,void *arg,unsigned priority,void *handle){
 (void)name;(void)stack;(void)arg;(void)priority;(void)handle;created_worker=fn;return pdPASS;
}
const char *esp_err_to_name(int e){return e==ESP_ERR_NO_MEM?"ESP_ERR_NO_MEM":"ESP_FAIL";}
void test_http_log(const char *tag,const char *fmt,...){(void)tag;(void)fmt;}
int psa_crypto_init(void){return PSA_SUCCESS;}
int psa_hash_compute(int alg,const unsigned char *p,size_t n,unsigned char *out,size_t cap,size_t *len){
 (void)alg;assert(cap==32);if(fail_hash)return -1;
 uint64_t h=1469598103934665603ULL;for(size_t i=0;i<n;i++)h=(h^p[i])*1099511628211ULL;
 for(unsigned i=0;i<4;i++)memcpy(out+i*8,&h,8);*len=32;return PSA_SUCCESS;
}
bool actell_pair_current(void){return true;}
bool actell_pair_rpc_allowed(void){return true;}
void actell_pair_boot(void){}
void actell_pair_poll(void){pair_polls++;}
void actell_pair_process(unsigned a,unsigned i){(void)a;(void)i;}
static int avatar_activity;
static unsigned avatar_ttl;
void actell_avatar_set_enabled(bool enabled){assert(enabled);}
void actell_avatar_set_activity(int activity,uint32_t ttl){avatar_activity=activity;avatar_ttl=ttl;}
void actell_random_uuid(char out[37]){strcpy(out,"11111111-1111-4111-8111-111111111111");}
esp_err_t actell_config_load(actell_config *c){memset(c,0,sizeof(*c));strcpy(c->client,"synthetic-client");memset(c->fingerprint,'a',64);return ESP_OK;}
esp_err_t actell_http(const actell_config *c,const char *id,const char *body,bool image,uint64_t g,int64_t end,actell_response *r){
 (void)c;(void)image;(void)end;assert(actell_bridge_current(g));http_count++;
 cJSON *envelope=cJSON_Parse(body);const char *op=str(envelope,"op");if(actell_op_mutates(op))action_count++;
 if(hint_during_http){assert(pending_jobs);completed_hint();assert(!idle_hint_generation);}
 if(!strcmp(op,"companion.release_idle")){
  idle_notifications++;if(on_idle_http){void(*hook)(void)=on_idle_http;on_idle_http=NULL;hook();}
  assert(end-esp_timer_get_time()<=2000000);
  if(fail_http){cJSON_Delete(envelope);return ESP_FAIL;}
  cJSON *response=cJSON_CreateObject();cJSON_AddNumberToObject(response,"v",1);cJSON_AddStringToObject(response,"request_id",id);cJSON_AddBoolToObject(response,"ok",true);
  cJSON_AddBoolToObject(cJSON_AddObjectToObject(response,"result"),"released",true);
  r->body=(unsigned char*)cJSON_PrintUnformatted(response);r->length=strlen((char*)r->body);strcpy(r->mime,"application/json");
  cJSON_Delete(response);cJSON_Delete(envelope);return ESP_OK;
 }
 if(view_mode){
  const cJSON *p=cJSON_GetObjectItemCaseSensitive(envelope,"params");
  if(!strcmp(op,"images.get")){
   assert(avatar_activity==1&&avatar_ttl>350); /* outer screenshot still displays during retained image fetch */
   assert(view_closes>view_capture_closes); /* screenshot owner released before image transfer */
   static const unsigned char jpeg[]={255,216,255,192,0,8,8,0,2,0,2,3,255,217};view_downloads++;
   r->body=malloc(sizeof(jpeg));memcpy(r->body,jpeg,sizeof(jpeg));r->length=sizeof(jpeg);
   strcpy(r->mime,"image/jpeg");strcpy(r->contract,"thumbnail-v2");strcpy(r->request_id,id);
   strcpy(r->image_id,"image-fixture");strcpy(r->generation,"7");strcpy(r->revision,"geometry-fixture");strcpy(r->width,"2");strcpy(r->height,"2");
   unsigned char hash[32];size_t n;assert(psa_hash_compute(PSA_ALG_SHA_256,jpeg,sizeof(jpeg),hash,32,&n)==PSA_SUCCESS);
   for(unsigned i=0;i<32;i++)snprintf(r->digest+i*2,3,"%02x",hash[i]);
   if(bad_view_image)r->digest[0]='z';
   cJSON_Delete(envelope);return ESP_OK;
  }
  cJSON *response=cJSON_CreateObject();cJSON_AddNumberToObject(response,"v",1);cJSON_AddStringToObject(response,"request_id",id);cJSON_AddBoolToObject(response,"ok",true);
  cJSON *out=cJSON_AddObjectToObject(response,"result");cJSON_AddStringToObject(out,"run_id",str(p,"run_id"));
  if(!strcmp(op,"capabilities")){cJSON_AddBoolToObject(cJSON_AddObjectToObject(out,"performance"),"idle_release",true);cJSON_AddBoolToObject(out,"independent_input",true);cJSON_AddItemToArray(cJSON_AddArrayToObject(out,"input_contracts"),cJSON_CreateString(ACTELL_NORMALIZED_INPUT));}
  else if(!strcmp(op,"runs.open")){cJSON_AddStringToObject(out,"state","open");cJSON_AddNumberToObject(out,"deadline_ms",cJSON_GetObjectItemCaseSensitive(p,"deadline_ms")->valuedouble);}
  else if(!strcmp(op,"runs.cancel")){view_closes++;cJSON_AddStringToObject(out,"state","cancelled");cJSON_AddBoolToObject(out,"execution_released",true);}
  else{
   cJSON_AddStringToObject(out,"call_id",str(p,"call_id"));
   bool submit=!strcmp(op,"tools.call");if(submit){assert(avatar_activity==(!strcmp(str(p,"tool"),"screenshot")?1:2));if(!strcmp(str(p,"tool"),"screenshot")){view_captures++;view_capture_closes=view_closes;}assert(!cJSON_GetObjectItemCaseSensitive(p,"observation"));}
   cJSON_AddStringToObject(out,"state",submit?"queued":"succeeded");
   if(!submit&&view_captures){cJSON *im=cJSON_AddObjectToObject(out,"result");cJSON_AddStringToObject(im,"image_id","image-fixture");cJSON_AddNumberToObject(im,"generation",7);cJSON_AddNumberToObject(im,"width",2);cJSON_AddNumberToObject(im,"height",2);cJSON_AddStringToObject(im,"image_contract","thumbnail-v2");cJSON_AddStringToObject(im,"coordinate_space",ACTELL_NORMALIZED_INPUT);cJSON_AddNumberToObject(im,"captured_at_ms",1234567800000.0);cJSON_AddNumberToObject(im,"expires_at_ms",1234569600000.0);cJSON_AddStringToObject(cJSON_AddObjectToObject(im,"geometry"),"revision","geometry-fixture");}
  }
  char *raw=cJSON_PrintUnformatted(response);cJSON_Delete(response);r->body=(unsigned char*)raw;r->length=strlen(raw);strcpy(r->mime,"application/json");cJSON_Delete(envelope);return ESP_OK;
 }
 if(hello_mode) {
  assert(!strcmp(op,"companion.hello"));
  cJSON *params=cJSON_GetObjectItemCaseSensitive(envelope,"params");
  assert(!strcmp(str(params,"device_name"),expected_hello_name));
  assert(cJSON_GetObjectItemCaseSensitive(params,"protocol")->valueint==1);
  hello_names_checked++;
  if(hello_mode==6 || hello_mode==7) {
   r->http_status=hello_mode==6?401:400;cJSON_Delete(envelope);return ESP_FAIL;
  }
 }
 if(fail_http){cJSON_Delete(envelope);return ESP_FAIL;}
 cJSON *reply=cJSON_CreateObject();cJSON_AddNumberToObject(reply,"v",1);cJSON_AddStringToObject(reply,"request_id",id);cJSON_AddBoolToObject(reply,"ok",true);
 cJSON *result=cJSON_AddObjectToObject(reply,"result"),*params=cJSON_GetObjectItemCaseSensitive(envelope,"params");
 const char *run=str(params,"run_id"),*call=str(params,"call_id");
 if(run)cJSON_AddStringToObject(result,"run_id",run);if(call)cJSON_AddStringToObject(result,"call_id",call);
 cJSON_AddStringToObject(result,"state",hello_mode==2?"pending_authorization":"succeeded");
 if(hello_mode && hello_mode!=3)cJSON_AddStringToObject(result,"pairing_state","active");
 if(hello_mode==4) {cJSON_ReplaceItemInObject(reply,"request_id",cJSON_CreateString("wrong-synthetic-id"));}
 if(hello_mode==5) {
  cJSON_ReplaceItemInObject(reply,"ok",cJSON_CreateFalse());
  cJSON *error=cJSON_AddObjectToObject(reply,"error");cJSON_AddStringToObject(error,"code","unauthorized");
 }
 r->body=(unsigned char*)cJSON_PrintUnformatted(reply);r->length=strlen((char*)r->body);strcpy(r->mime,"application/json");r->http_status=200;
 cJSON_Delete(envelope);cJSON_Delete(reply);return ESP_OK;
}
void actell_http_close_idle(void){assert(!locked);idle_closes++;}
void actell_http_invalidate(actell_http_locator *l){(void)l;invalidations++;}
void actell_http_locator_close(actell_http_locator *l){assert(!l->session);}
void actell_http_locator_release(actell_http_locator *l){assert(!locked&&!l->session);connection_releases++;}
unsigned actell_http_idle_wait_ms(void){assert(!locked);idle_polls++;return 17;}
esp_err_t actell_http_with_locator(const actell_config *c,const char *id,const char *body,bool image,uint64_t g,int64_t end,actell_response *r,actell_http_locator *locator){
 (void)locator;return actell_http(c,id,body,image,g,end,r);
}
void noise_ctrl_send_command_result(uint64_t g,const char *id,cJSON *r){
 (void)id;assert(g==guard->generation);callbacks++;cJSON_Delete(last_result);last_result=NULL;
 if(drop_result)cJSON_Delete(r);else last_result=r;
}
void noise_ctrl_send_image_result(uint64_t g,const char *id,cJSON *r,actell_image_blob image){
 assert(image.data&&image.length);
 cJSON *p=cJSON_GetObjectItemCaseSensitive(r,"payload");
 assert(!strcmp(str(p,"data_base64"),""));
 assert(!cJSON_GetObjectItemCaseSensitive(p,"_actell_stream_image"));
 char *metadata=cJSON_PrintUnformatted(r);actell_image_layout layout;
 assert(actell_image_layout_init(&layout,metadata,image.length,ACTELL_RESULT_MAX));
 char *wire=malloc(layout.wire_length+1);assert(wire);
 size_t at=0;
 while(at<layout.wire_length){size_t n=actell_image_stream_read(&layout,metadata,image.data,image.length,at,(unsigned char*)wire+at,7);assert(n);at+=n;}
 wire[at]=0;cJSON *decoded=cJSON_Parse(wire);assert(decoded);
 p=cJSON_GetObjectItemCaseSensitive(decoded,"payload");
 assert(strlen(str(p,"data_base64"))==4*((image.length+2)/3));
 free(wire);free(metadata);free(image.data);cJSON_Delete(r);
 noise_ctrl_send_command_result(g,id,decoded);
}
static cJSON *invoke(const char *id,const char *command,const char *json){
 cJSON *p=cJSON_Parse(json);assert(p);cJSON *r=actell_bridge_invoke(command,p,id,guard->generation);cJSON_Delete(p);return r;
}
static void queued(cJSON *r){assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r,"_async")));cJSON_Delete(r);}
static void rejected(cJSON *r,const char *code){
 assert(!cJSON_GetObjectItemCaseSensitive(r,"_async"));assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(r,"ok")));
 assert(!strcmp(str(cJSON_GetObjectItemCaseSensitive(r,"error"),"code"),code));cJSON_Delete(r);
}
static void run_jobs(void){if(!setjmp(stop_worker))created_worker(NULL);assert(!locked);}
#if CONFIG_ACTELL_BRIDGE_IMAGES
/* Synthetic encoder isolates bridge framing; target uses real Mbed TLS. */
int mbedtls_base64_encode(unsigned char *dst,size_t cap,size_t *n,const unsigned char *src,size_t len){
 (void)src;if(len>20){*n=4*((len+2)/3);assert(cap>*n);memset(dst,'A',*n);return 0;}assert(cap>8);memcpy(dst,"fixture=",8);*n=8;return 0;
}
static void image_contract_test(void){
 unsigned char jpeg[]={255,216,255,192,0,8,8,0,2,0,2,3,255,217};
 actell_response r={0};r.body=jpeg;r.length=sizeof(jpeg);
 strcpy(r.mime,"image/jpeg");strcpy(r.contract,"thumbnail-v2");
 strcpy(r.request_id,"fixture-rpc");strcpy(r.image_id,"fixture-image");
 strcpy(r.generation,"7");strcpy(r.revision,"fixture-geometry");strcpy(r.width,"2");strcpy(r.height,"2");
 unsigned char hash[32];size_t n=0;
 assert(psa_hash_compute(PSA_ALG_SHA_256,jpeg,sizeof(jpeg),hash,32,&n)==PSA_SUCCESS);
 for(unsigned i=0;i<32;i++)snprintf(r.digest+i*2,3,"%02x",hash[i]);
 cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"image_id","fixture-image");cJSON_AddNumberToObject(p,"generation",7);
 cJSON *out=image_result(&r,p,"fixture-rpc",NULL);assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(out,"ok")));
 cJSON *payload=cJSON_GetObjectItemCaseSensitive(out,"payload");
 assert(!strcmp(str(payload,"format"),"jpeg-base64"));assert(!strcmp(str(payload,"data_base64"),"fixture="));
 assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(payload,"model_visibility_verified")));cJSON_Delete(out);
 r.digest[0]=r.digest[0]=='0'?'1':'0';out=image_result(&r,p,"fixture-rpc",NULL);
 rejected(out,"image_invalid");
 strcpy(r.generation,"8");out=image_result(&r,p,"fixture-rpc",NULL);rejected(out,"image_invalid");
 unsigned char *maximum=calloc(1,ACTELL_IMAGE_MAX+1);assert(maximum);memcpy(maximum,jpeg,sizeof(jpeg)-2);maximum[ACTELL_IMAGE_MAX-2]=255;maximum[ACTELL_IMAGE_MAX-1]=217;
 r.body=maximum;r.length=ACTELL_IMAGE_MAX;strcpy(r.generation,"7");assert(psa_hash_compute(PSA_ALG_SHA_256,maximum,r.length,hash,32,&n)==PSA_SUCCESS);for(unsigned i=0;i<32;i++)snprintf(r.digest+i*2,3,"%02x",hash[i]);
 out=image_result(&r,p,"fixture-rpc",NULL);assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(out,"ok")));payload=cJSON_GetObjectItemCaseSensitive(out,"payload");assert(strlen(str(payload,"data_base64"))==349528);cJSON_Delete(out);
 r.length=ACTELL_IMAGE_MAX+1;out=image_result(&r,p,"fixture-rpc",NULL);rejected(out,"image_invalid");free(maximum);cJSON_Delete(p);
}
#endif
static void enqueue_during_idle_rpc(void){
 queued(invoke("during-release","actell.tap","{\"x\":250,\"y\":750,\"purpose\":\"fixture\"}"));
 assert(pending_jobs==1);
}
static void idle_hint_tests(void){
 const char *tap="{\"x\":250,\"y\":750,\"purpose\":\"fixture\"}";
 actell_bridge_session(30);run_jobs();view_mode=true;
 cJSON *e=cJSON_Parse("{\"type\":\"event\",\"event\":\"task.status\",\"payload\":{\"status\":\"running\"}}");
 actell_bridge_observe_event(e);cJSON_Delete(e);actell_bridge_observe_event(NULL);assert(!idle_hint_generation);
 queued(invoke("idle-first","actell.tap",tap));assert(pending_jobs==1);
 completed_hint();assert(!idle_hint_generation);hint_during_http=true;run_jobs();hint_during_http=false;
 assert(!pending_jobs&&peer_idle_release_generation==30);
 unsigned n=idle_notifications,a=action_count;
 completed_hint();completed_hint();assert(idle_hint_generation==30&&q_count==1);
 run_jobs();assert(idle_notifications==n+1&&action_count==a&&!pending_jobs);
 completed_hint();run_jobs();assert(idle_notifications==n+1); /* no business since prior hint */
 queued(invoke("idle-next","actell.tap",tap));run_jobs();
 completed_hint();queued(invoke("queued-after-hint","actell.tap",tap));
 assert(!idle_hint_generation);run_jobs();assert(idle_notifications==n+1);
 completed_hint();actell_bridge_session(31);assert(!idle_hint_generation);run_jobs();assert(idle_notifications==n+1);
 queued(invoke("new-generation","actell.tap",tap));run_jobs();
 completed_hint();on_idle_http=enqueue_during_idle_rpc;run_jobs();
 assert(idle_notifications==n+2&&!pending_jobs&&callbacks>0); /* admission during cleanup runs afterward */
 completed_hint();fail_http=true;run_jobs();fail_http=false;
 assert(idle_notifications==n+3);completed_hint();run_jobs();assert(idle_notifications==n+3); /* no replay/retry */
 queued(invoke("old-peer","actell.tap",tap));run_jobs();peer_idle_release_generation=0;
 unsigned closes=idle_closes;completed_hint();run_jobs();assert(idle_notifications==n+3&&idle_closes==closes+1);
 queued(invoke("obsolete-queued","actell.tap",tap));actell_bridge_session(32);
 assert(pending_jobs==1);completed_hint();assert(!idle_hint_generation);run_jobs();assert(!pending_jobs);
}
int main(void){
 actell_flat_store_reset();
#if CONFIG_ACTELL_BRIDGE_IMAGES
 image_contract_test();
#endif
 actell_bridge_session(1);assert(guard&&started);view_mode=true;
 assert(!invoke("removed","actell.rpc","{}"));assert(!invoke("removed","actell.image","{}"));assert(!invoke("removed","actell.view","{}"));
 rejected(invoke("invalid","actell.tap","{\"x\":true,\"y\":0,\"purpose\":\"Open\"}"),"bad_request");
 actell_bridge_pair_wake();assert(q_count==1&&!items[0]);run_jobs();assert(pair_polls==2&&!http_count&&!q_count);
 for(unsigned i=0;i<ACTELL_QUEUE_CAPACITY+1;i++)actell_bridge_pair_wake();assert(q_count==ACTELL_QUEUE_CAPACITY);run_jobs();assert(pair_polls==ACTELL_QUEUE_CAPACITY+3&&!http_count&&!q_count);
 const char *tap="{\"x\":250,\"y\":750,\"purpose\":\"Open item\"}";
 queued(invoke("write-1","actell.tap",tap));queued(invoke("write-1","actell.tap",tap));assert(q_count==1&&!http_count);
 rejected(invoke("write-1","actell.tap","{\"x\":251,\"y\":750,\"purpose\":\"Open item\"}"),"idempotency_conflict");
 run_jobs();assert(action_count==3&&callbacks==1&&last_result&&connection_releases==1&&idle_polls>0);unsigned actions=action_count;
 cJSON *payload=cJSON_GetObjectItemCaseSensitive(last_result,"payload");char operation[129];snprintf(operation,sizeof(operation),"%s",str(payload,"operation_id"));
 rejected(invoke("write-1","actell.tap",tap),"invoke_result_unavailable");assert(action_count==actions);
 drop_result=true;queued(invoke("lost-delivery","actell.tap",tap));run_jobs();assert(!last_result);rejected(invoke("lost-delivery","actell.tap",tap),"invoke_result_unavailable");drop_result=false;
 /* Across a new connection generation, the retained RAM receipt still blocks submit. */
 actell_bridge_session(2);actions=action_count;queued(invoke("write-1","actell.tap",tap));run_jobs();assert(action_count==actions); /* reconciliation never cancels the historical owner */
 actions=action_count;queued(invoke("write-1-conflict","actell.tap",tap));run_jobs();actell_bridge_session(3);
 queued(invoke("write-1-conflict","actell.tap","{\"x\":251,\"y\":750,\"purpose\":\"Open item\"}"));unsigned before=http_count;run_jobs();assert(http_count==before);
 char query[200];snprintf(query,sizeof(query),"{\"operation_id\":\"%s\"}",operation);actions=action_count;
 for(unsigned i=0;i<2000;i++){char id[32];snprintf(id,sizeof(id),"query-%u",i);queued(invoke(id,"actell.call_status",query));run_jobs();}assert(action_count==actions);
 reject_send=true;unsigned writes=guard->write_used;rejected(invoke("queue-rejected","actell.tap",tap),"busy");assert(guard->write_used==writes);queued(invoke("queue-rejected","actell.tap",tap));run_jobs();
 queued(invoke("old-queued","actell.tap",tap));before=http_count;unsigned done=callbacks;actell_bridge_session(4);run_jobs();assert(http_count==before&&callbacks==done);
#if CONFIG_ACTELL_BRIDGE_IMAGES
 queued(invoke("single-screen","actell.screenshot","{}"));run_jobs();assert(view_captures==1&&view_downloads==1&&last_result&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(last_result,"ok")));
 payload=cJSON_GetObjectItemCaseSensitive(last_result,"payload");assert(!strcmp(str(payload,"coordinate_space"),ACTELL_NORMALIZED_INPUT));
 rejected(invoke("single-screen","actell.screenshot","{}"),"invoke_result_unavailable");assert(view_captures==1);
 unsigned invalid_before=invalidations;bad_view_image=true;
 queued(invoke("invalid-image","actell.screenshot","{}"));run_jobs();
 assert(last_result&&!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(last_result,"ok")));
 assert(invalidations==invalid_before+1&&view_captures==2&&view_downloads==2);bad_view_image=false;
#endif
 fail_hash=true;rejected(invoke("no-identity","actell.tap",tap),"unavailable");fail_hash=false;view_mode=false;
 /* Execute the real authenticated hello parser: empty scopes are not unpaired;
  * failed or malformed ACKs must not be reported as completed pairing. */
 actell_config config={0};strcpy(config.client,"synthetic-client");
 bool refused=false;
 hello_mode=1;assert(actell_pair_hello(&config,&refused)==ESP_OK && !refused);
 hello_mode=2;assert(actell_pair_hello(&config,&refused)==ESP_OK && !refused);
 hello_mode=3;assert(actell_pair_hello(&config,&refused)!=ESP_OK && !refused);
 hello_mode=4;assert(actell_pair_hello(&config,&refused)!=ESP_OK && !refused);
 hello_mode=5;assert(actell_pair_hello(&config,&refused)!=ESP_OK && refused);
 hello_mode=6;assert(actell_pair_hello(&config,&refused)!=ESP_OK && refused);
 hello_mode=7;assert(actell_pair_hello(&config,&refused)!=ESP_OK && !refused);
 assert(hello_names_checked==7);
 expected_hello_name="Muse Gadget";
 hello_mode=1;assert(actell_pair_hello(&config,&refused)==ESP_OK && !refused);
 assert(hello_names_checked==8);hello_mode=0;
 /* A burst can occupy all twenty waiting slots. Overflow does not consume
  * a receipt; duplicates do not consume another slot; draining restores room. */
 actell_bridge_session(10);run_jobs();view_mode=true; /* drain generation wake before burst */
 unsigned completed_before=callbacks;
 for(unsigned i=0;i<20;i++){char id[32];snprintf(id,sizeof(id),"burst-%u",i);queued(invoke(id,"actell.tap",tap));}
 assert(q_count==20&&guard->write_used==20);
 queued(invoke("burst-0","actell.tap",tap));assert(q_count==20);
 rejected(invoke("burst-overflow","actell.tap",tap),"busy");assert(q_count==20&&guard->write_used==20);
 run_jobs();assert(!q_count&&callbacks==completed_before+20);
 queued(invoke("after-burst","actell.tap",tap));run_jobs();assert(callbacks==completed_before+21);
 /* Recycling recent records must not reopen a completed write in this session. */
 actell_bridge_session(40);run_jobs();
 queued(invoke("recycled-first","actell.tap",tap));run_jobs();
 for(unsigned i=0;i<ACTELL_FLAT_CACHE_CAPACITY+5;i++){
  char id[40];snprintf(id,sizeof(id),"recycle-%u",i);queued(invoke(id,"actell.tap",tap));run_jobs();
 }
 actions=action_count;
 rejected(invoke("recycled-first","actell.tap",tap),"invoke_result_unavailable");
 assert(action_count==actions); /* Separate generation fence survives RAM eviction. */
 idle_hint_tests();
 assert(!q_count&&!pending_jobs);cJSON_Delete(last_result);free(guard);guard=NULL;actell_flat_store_reset();
 puts("PASS flat adapter: RAM receipts, duplicate protection, conflicts, 2000 status reads, 20-slot queue boundary/recovery, session fences, JPEG and hello");return 0;
}
