/* SPDX-License-Identifier: Apache-2.0 */
/* Actual adapter + guard + upstream cJSON. Only scheduler/network/PSA are
 * synthetic. Fake digest below tests admission plumbing, not cryptography. */
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include "../components/actell_bridge/actell_bridge.c"
static jmp_buf stop_worker;
static void *items[3];
static unsigned q_count,http_count,action_count,callbacks,pair_polls;
static int locked;
static bool reject_send,drop_result,fail_http,fail_hash;
static unsigned hello_mode,hello_names_checked;
static const char *expected_hello_name="Muse gadget";
static cJSON *last_result;
static void (*created_worker)(void *);
int64_t esp_timer_get_time(void){return 1000000;}
void *heap_caps_calloc(size_t n,size_t size,unsigned caps){(void)caps;return calloc(n,size);}
SemaphoreHandle_t xSemaphoreCreateMutex(void){return &locked;}
int xSemaphoreTake(SemaphoreHandle_t m,unsigned wait){(void)m;(void)wait;assert(!locked);locked=1;return 1;}
int xSemaphoreGive(SemaphoreHandle_t m){(void)m;assert(locked);locked=0;return 1;}
QueueHandle_t xQueueCreate(unsigned n,size_t size){assert(n==3 && size==sizeof(void*));return items;}
void vQueueDelete(QueueHandle_t q){(void)q;}
int xQueueSend(QueueHandle_t q,const void *p,unsigned wait){(void)q;(void)wait;
 if(reject_send){reject_send=false;return 0;}if(q_count==3)return 0;
 items[q_count++]=*(void*const*)p;return pdTRUE;
}
int xQueueReceive(QueueHandle_t q,void *p,unsigned wait){(void)q;(void)wait;
 if(!q_count)longjmp(stop_worker,1);
 *(void**)p=items[0];for(unsigned i=1;i<q_count;i++)items[i-1]=items[i];q_count--;return pdTRUE;
}
int xTaskCreate(void (*fn)(void*),const char *name,unsigned stack,void *arg,unsigned priority,void *handle){
 (void)name;(void)stack;(void)arg;(void)priority;(void)handle;created_worker=fn;return pdPASS;
}
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
void actell_activity_observe(uint64_t g,const char *op,const cJSON *p,const cJSON *r){(void)g;(void)op;(void)p;(void)r;}
void actell_random_uuid(char out[37]){strcpy(out,"11111111-1111-4111-8111-111111111111");}
esp_err_t actell_config_load(actell_config *c){memset(c,0,sizeof(*c));strcpy(c->client,"synthetic-client");return ESP_OK;}
esp_err_t actell_http(const actell_config *c,const char *id,const char *body,bool image,uint64_t g,int64_t end,actell_response *r){
 (void)c;(void)image;(void)end;assert(actell_bridge_current(g));http_count++;
 cJSON *envelope=cJSON_Parse(body);const char *op=str(envelope,"op");if(actell_op_mutates(op))action_count++;
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
void noise_ctrl_send_command_result(uint64_t g,const char *id,cJSON *r){
 (void)id;assert(g==guard->generation);callbacks++;cJSON_Delete(last_result);last_result=NULL;
 if(drop_result)cJSON_Delete(r);else last_result=r;
}
static cJSON *invoke(const char *id,const char *op){
 cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"op",op);cJSON_AddStringToObject(p,"params_json","{\"run_id\":\"synthetic-run\",\"call_id\":\"synthetic-call\"}");
 cJSON *r=actell_bridge_invoke("actell.rpc",p,id,guard->generation);cJSON_Delete(p);assert(r);return r;
}
static void queued(cJSON *r){assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r,"_async")));cJSON_Delete(r);}
static void rejected(cJSON *r,const char *code){
 assert(!cJSON_GetObjectItemCaseSensitive(r,"_async"));assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(r,"ok")));
 assert(!strcmp(str(cJSON_GetObjectItemCaseSensitive(r,"error"),"code"),code));cJSON_Delete(r);
}
static void run_jobs(void){if(!setjmp(stop_worker))created_worker(NULL);assert(!locked);}
int main(void){
 actell_bridge_session(1);assert(guard && started);
 /* Run the actual worker with null wakeups, including its full-queue boundary. */
 actell_bridge_pair_wake();assert(q_count==1 && !items[0]);
 run_jobs();assert(pair_polls==2 && !http_count && !q_count);
 for(unsigned i=0;i<4;i++)actell_bridge_pair_wake();
 assert(q_count==3);run_jobs();assert(pair_polls==6 && !http_count && !q_count);
 queued(invoke("write-1","tools.call"));queued(invoke("write-1","tools.call"));assert(q_count==1 && !http_count);
 run_jobs();assert(action_count==1 && callbacks==1 && last_result);
 rejected(invoke("write-1","tools.call"),"invoke_result_unavailable");assert(action_count==1 && !q_count);
 /* Official SDK can discard an accepted completion; duplicate is synchronous,
  * truthful about unknown delivery, and never retries the phone action. */
 drop_result=true;queued(invoke("lost-delivery","tools.call"));run_jobs();assert(!last_result);
 rejected(invoke("lost-delivery","tools.call"),"invoke_result_unavailable");drop_result=false;
 fail_http=true;queued(invoke("uncertain-action","tools.call"));run_jobs();fail_http=false;
 rejected(invoke("uncertain-action","tools.call"),"invoke_result_unavailable");
 unsigned actions=action_count;
 for(unsigned i=0;i<2000;i++){char id[32];snprintf(id,sizeof(id),"query-%u",i);queued(invoke(id,"calls.get"));run_jobs();}
 assert(action_count==actions && guard->write_used==3);
 rejected(invoke("write-1","capabilities"),"invoke_result_unavailable");
 rejected(invoke("write-1","tools.call"),"invoke_result_unavailable");
 /* Recycled reads may safely fetch a fresh read-only observation. */
 unsigned previous=http_count;queued(invoke("query-0","calls.get"));run_jobs();assert(http_count==previous+1);
 /* Queue rejection creates neither a stranded pending receipt nor a write fence. */
 reject_send=true;unsigned writes=guard->write_used;
 rejected(invoke("queue-rejected","tools.call"),"busy");assert(guard->write_used==writes);
 queued(invoke("queue-rejected","tools.call"));run_jobs();
 /* Old queued work cannot execute or complete on the replacement generation. */
 queued(invoke("old-queued","tools.call"));unsigned before=http_count,done=callbacks;
 actell_bridge_session(2);run_jobs();assert(http_count==before && callbacks==done);
 for(unsigned i=0;i<ACTELL_WRITE_HISTORY;i++){
  char id[32];snprintf(id,sizeof(id),"capacity-%u",i);
  const char *op=i<ACTELL_WRITE_HISTORY-ACTELL_STOP_RESERVE?"tools.call":"runs.cancel";
  queued(invoke(id,op));run_jobs();
  if(i==ACTELL_WRITE_HISTORY-ACTELL_STOP_RESERVE-1){
   rejected(invoke("over-ordinary-limit","runs.open"),"write_receipt_limit");
   queued(invoke("can-still-reconcile","runs.get"));run_jobs();
  }
 }
 rejected(invoke("over-stop-limit","runs.cancel"),"write_receipt_limit");
 queued(invoke("read-at-full-write-budget","calls.get"));run_jobs();
 assert(guard->write_used==ACTELL_WRITE_HISTORY);
 rejected(invoke("capacity-0","capabilities"),"invoke_result_unavailable");
 fail_hash=true;rejected(invoke("no-identity-protection","tools.call"),"unavailable");fail_hash=false;
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
 config.named_muse_gadget=true;expected_hello_name="Muse Gadget";
 hello_mode=1;assert(actell_pair_hello(&config,&refused)==ESP_OK && !refused);
 assert(hello_names_checked==8);hello_mode=0;
 assert(!q_count);cJSON_Delete(last_result);free(guard);guard=NULL;
 puts("PASS actual adapter: duplicate liveness, dropped completion, 2000 reads, retained write fence, stop reserve, queue rollback, session fence, actual hello name/ACK parsing");return 0;
}
