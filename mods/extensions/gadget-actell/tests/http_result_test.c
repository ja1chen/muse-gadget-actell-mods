/* Actual production HTTP/cache lifecycle with synthetic transport/crypto only. */
#include "actell_lan.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "psa/crypto.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct fake_http { esp_http_client_config_t config; bool prepared; unsigned header_updates; char operation[65]; };
static unsigned char *reply;
static size_t reply_size,offset;
static unsigned reads,discoveries,opens,initializations,cleanups,reused_headers,writes,signs;
static unsigned open_failures,header_failures;
static bool local_address=true,network_up=true,persistent,chunked,truncated,cancelled,cancel_during_read;
static bool write_failure,partial_write,read_header_failure,parser_completed_early;
static int status=200,connect_timeout;
static int64_t monotonic=1000000;
static uint32_t epoch=1;
static uint64_t generation=7;
static uint32_t network_ip=1,network_mask=2,network_gateway=3;
static char expected_operation[65];
static actell_config config;
static size_t dma_free=64*1024,dma_largest=16*1024;
static bool pressure_during_read;
size_t heap_caps_get_free_size(unsigned caps){assert(caps==MALLOC_CAP_DMA);return dma_free;}
size_t heap_caps_get_largest_free_block(unsigned caps){assert(caps==MALLOC_CAP_DMA);return dma_largest;}
static time_t wall=1800000000;
void test_http_log(const char *tag,const char *format,...){(void)tag;(void)format;}
const char *esp_err_to_name(int e){(void)e;return "synthetic";}
bool actell_lan_address(uint32_t address){(void)address;return local_address;}
bool actell_bridge_current(uint64_t g){return (g==generation||g==SLAN_LOCAL_GENERATION)&&!cancelled;}
int64_t esp_timer_get_time(void){return monotonic;}
uint32_t actell_lan_epoch(void){return epoch;}
void actell_lan_network_changed(void){epoch++;}
bool actell_lan_network(actell_network_stamp *out){*out=(actell_network_stamp){epoch,network_ip,network_mask,network_gateway};return network_up;}
time_t time(time_t *out){if(out)*out=wall;return wall;}
bool actell_clock_ready(void){wall+=120;return true;}
void esp_fill_random(void *p,size_t n){memset(p,(unsigned char)++signs,n);}
bool actell_lan_interface(struct ifreq *p){memset(p,0,sizeof(*p));return true;}
esp_err_t actell_lan_discover(actell_candidate out[SLAN_CANDIDATES],unsigned *n,const actell_config *p){
    assert(p==&config);discoveries++;wall+=120;
    strcpy(out[0].address,discoveries%2?"192.168.1.2":"192.168.1.3");*n=1;return ESP_OK;
}
int psa_crypto_init(void){return 0;}
void psa_set_key_type(psa_key_attributes_t *p,int v){(void)p;(void)v;}
void psa_set_key_usage_flags(psa_key_attributes_t *p,int v){(void)p;(void)v;}
void psa_set_key_algorithm(psa_key_attributes_t *p,int v){(void)p;(void)v;}
int psa_import_key(const psa_key_attributes_t *a,const unsigned char *p,size_t n,psa_key_id_t *k){(void)a;(void)p;(void)n;*k=1;return 0;}
int psa_mac_sign_setup(psa_mac_operation_t *p,psa_key_id_t k,int a){(void)p;(void)k;(void)a;return 0;}
int psa_mac_update(psa_mac_operation_t *p,const unsigned char *d,size_t n){(void)p;(void)d;(void)n;return 0;}
int psa_mac_sign_finish(psa_mac_operation_t *p,unsigned char *d,size_t n,size_t *used){(void)p;assert(n>=32);memset(d,0,32);*used=32;return 0;}
void psa_mac_abort(psa_mac_operation_t *p){(void)p;}
void psa_destroy_key(psa_key_id_t k){(void)k;}
void psa_reset_key_attributes(psa_key_attributes_t *p){(void)p;}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c){
    assert(initializations==cleanups); /* No idle TLS allocation survives a second client. */
    assert(!c->skip_cert_common_name_check && c->disable_auto_redirect);
    assert(!strcmp(c->common_name,"stable-cert.local")&&!strcmp(c->cert_pem,"synthetic-cert"));
    assert(!strcmp(c->url,"https://192.168.1.2:8765/v1/rpc")||!strcmp(c->url,"https://192.168.1.3:8765/v1/rpc")||!strcmp(c->url,"https://192.168.1.3:8766/v1/rpc"));
    struct fake_http *h=calloc(1,sizeof(*h));assert(h);h->config=*c;initializations++;return h;
}
int esp_http_client_set_header(esp_http_client_handle_t h,const char *k,const char *v){
    if(!strcmp(k,"X-Actell-Time"))assert(strtoll(v,NULL,10)==wall);
    if(!strcmp(k,"X-Actell-Nonce"))h->header_updates++;
    if(!strcmp(k,"Host"))assert(!strcmp(v,config.origin+8));
    if(!strcmp(k,"X-Actell-Operation-Id")){assert(strlen(v)==64);strcpy(h->operation,v);}
    return 0;
}
esp_err_t esp_http_client_delete_header(esp_http_client_handle_t h,const char *k){
    assert(!strcmp(k,"X-Actell-Operation-Id"));
    if(!h->operation[0])return ESP_ERR_NOT_FOUND;
    h->operation[0]=0;return ESP_OK;
}
int esp_http_client_set_method(esp_http_client_handle_t h,int m){(void)h;(void)m;return 0;}
int esp_http_client_set_timeout_ms(esp_http_client_handle_t h,int t){assert(t>0&&t<=15000);h->config.timeout_ms=t;return 0;}
esp_err_t esp_http_client_open(esp_http_client_handle_t h,int n){
    assert(n>0&&h->header_updates);opens++;connect_timeout=h->config.timeout_ms;
    assert(!strcmp(h->operation,expected_operation));
    assert(!strcmp(h->config.cert_pem,"synthetic-cert"));
    if(open_failures){open_failures--;return ESP_FAIL;}offset=0;return ESP_OK;
}
esp_err_t esp_http_client_prepare(esp_http_client_handle_t h){h->prepared=true;return ESP_OK;}
esp_err_t esp_http_client_request_send(esp_http_client_handle_t h,int n){
    assert(h->prepared&&n>0);h->prepared=false;reused_headers++;offset=0;
    assert(!strcmp(h->operation,expected_operation));
    if(header_failures){header_failures--;return ESP_FAIL;}return ESP_OK;
}
int esp_http_client_write(esp_http_client_handle_t h,const char *p,int n){
    (void)h;(void)p;writes++;
    if(write_failure)return -1;
    if(partial_write){partial_write=false;write_failure=true;return n/2;}
    return n;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h){
    if(read_header_failure)return -1;
    esp_http_client_event_t e={HTTP_EVENT_ON_HEADER,h->config.user_data,"Content-Type","application/json"};
    assert(h->config.event_handler(&e)==ESP_OK);
    return chunked?0:(int64_t)reply_size;
}
int esp_http_client_get_status_code(esp_http_client_handle_t h){(void)h;return status;}
int esp_http_client_read(esp_http_client_handle_t h,char *p,int n){
    (void)h;reads++;if(cancel_during_read)cancelled=true;
    if(pressure_during_read){
        dma_free=15*1024;
        unsigned prior=cleanups;
        assert(actell_http_idle_wait_ms()==30000&&cleanups==prior);
    }
    size_t end=truncated?reply_size/2:reply_size,take=end-offset;
    if(take>(size_t)n)take=(size_t)n;if(take>713)take=713;
    memcpy(p,reply+offset,take);offset+=take;return (int)take;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h){(void)h;return parser_completed_early||offset==reply_size;}
bool esp_http_client_is_persistent_connection(esp_http_client_handle_t h){(void)h;return persistent;}
int esp_http_client_close(esp_http_client_handle_t h){(void)h;return 0;}
int esp_http_client_cleanup(esp_http_client_handle_t h){cleanups++;free(h);return 0;}
static void reset(void){
    actell_http_invalidate(NULL);
    assert(initializations==cleanups);
    reads=discoveries=opens=initializations=cleanups=reused_headers=writes=signs=0;
    open_failures=header_failures=0;local_address=network_up=true;
    persistent=chunked=truncated=cancelled=cancel_during_read=false;
    write_failure=partial_write=read_header_failure=parser_completed_early=false;
    status=200;monotonic=1000000;epoch=1;generation=7;
    dma_free=64*1024;dma_largest=16*1024;pressure_during_read=false;
    network_ip=1;network_mask=2;network_gateway=3;expected_operation[0]=0;
    memset(&config,0,sizeof(config));strcpy(config.origin,"https://stable-cert.local:8765");
    strcpy(config.client,"synthetic-client");strcpy(config.fingerprint,"synthetic-pin");strcpy(config.pem,"synthetic-cert");
}
static int run(actell_http_locator *locator,size_t size){
    reply_size=size;offset=reads=0;reply=malloc(size+1);assert(reply);
    memset(reply,'x',size);reply[size]=0;
    actell_response r={0};
    snprintf(expected_operation,sizeof(expected_operation),"%s",locator?locator->operation_id:"");
    int e=locator?actell_http_with_locator(&config,"synthetic-request","{}",false,generation,monotonic+90000000,&r,locator)
                 :actell_http(&config,"synthetic-request","{}",false,SLAN_LOCAL_GENERATION,monotonic+90000000,&r);
    if(e==ESP_OK){assert(r.length==size&&r.body[size]==0&&!memcmp(r.body,reply,size));free(r.body);}
    else assert(!r.body);
    free(reply);return e;
}
int main(void){
    size_t sizes[]={1024,16383,16384,16385,32768,65536,ACTELL_JSON_MAX,ACTELL_JSON_MAX+1};
    for(size_t i=0;i<sizeof(sizes)/sizeof(*sizes);i++)for(unsigned c=0;c<2;c++){
        reset();chunked=c;int e=run(NULL,sizes[i]);
        assert(e==(sizes[i]>ACTELL_JSON_MAX?ESP_ERR_INVALID_SIZE:ESP_OK));
        if(sizes[i]>ACTELL_JSON_MAX&&!c)assert(!reads);
    }
    reset();truncated=true;assert(run(NULL,65536)!=ESP_OK);
    reset();chunked=truncated=true;assert(run(NULL,65536)!=ESP_OK);
    reset();cancel_during_read=true;assert(run(NULL,65536)!=ESP_OK);
    reset();parser_completed_early=true;assert(run(NULL,1024)==ESP_OK); /* cached body still drained */

    reset();persistent=true;actell_http_locator l={0};
    memset(l.operation_id,'a',64);l.operation_id[64]=0;
    for(unsigned i=0;i<7;i++)assert(run(&l,i==6?65536:1024)==ESP_OK);
    assert(discoveries==1&&opens==1&&initializations==1&&reused_headers==6&&signs==7);
    actell_http_locator_close(&l);assert(cleanups==1);
    l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);
    assert(discoveries==1&&opens==2&&connect_timeout==2000); /* cross-call locator, new verified TLS */
    actell_http_locator_close(&l);
    assert(run(NULL,1024)==ESP_OK&&discoveries==1); /* hello closes its own connection */
    monotonic+=30000001;assert(run(NULL,1024)==ESP_OK&&discoveries==2);
    strcpy(config.client,"different-client");assert(run(NULL,1024)==ESP_OK&&discoveries==3);
    strcpy(config.fingerprint,"different-pin");assert(run(NULL,1024)==ESP_OK&&discoveries==4);
    epoch++;assert(run(NULL,1024)==ESP_OK&&discoveries==5);
    local_address=false;assert(run(NULL,1024)==ESP_OK&&discoveries==6);local_address=true;
    network_up=false;assert(run(NULL,1024)!=ESP_OK);network_up=true;
    assert(run(NULL,1024)==ESP_OK&&discoveries==7);

    reset();assert(run(NULL,1024)==ESP_OK);open_failures=1;
    assert(run(NULL,1024)==ESP_OK&&opens==3&&discoveries==2&&writes==2&&signs==3);
    reset();assert(run(NULL,1024)==ESP_OK);open_failures=2;
    assert(run(NULL,1024)!=ESP_OK&&opens==3&&discoveries==2&&writes==1); /* bounded fallback */
    reset();persistent=true;l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);
    header_failures=1;assert(run(&l,1024)==ESP_OK&&opens==2&&discoveries==2&&writes==2);
    actell_http_locator_close(&l);
    for(unsigned mode=0;mode<5;mode++){
        reset();assert(run(NULL,1024)==ESP_OK);
        write_failure=mode==0;partial_write=mode==1;read_header_failure=mode==2;truncated=mode==3;if(mode==4)status=401;
        assert(run(NULL,1024)!=ESP_OK&&opens==2&&discoveries==1); /* never replay after write attempt */
    }
    reset();persistent=true;l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);
    monotonic+=600000000;assert(run(&l,1024)==ESP_OK&&opens==2&&discoveries==2);
    for(unsigned i=0;i<16;i++)assert(run(&l,1024)==ESP_OK);
    assert(opens==3);epoch++;assert(run(&l,1024)==ESP_OK&&opens==4&&discoveries==3);
    actell_http_locator_close(&l);reset();

    /* Discovery TTL cannot retire a recently used authenticated connection. */
    persistent=true;assert(run(NULL,1024)==ESP_OK);monotonic+=29500000;
    l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);
    monotonic+=1000000;assert(run(&l,1024)==ESP_OK);
    assert(discoveries==1&&opens==2&&cleanups==1&&reused_headers==1);
    /* Once the socket actually expires, stale discovery must not be reused. */
    monotonic+=600000000;assert(run(&l,1024)==ESP_OK);
    assert(discoveries==2&&opens==3);actell_http_locator_close(&l);reset();

    /* New invocation, same connection: ownership moves, auth/trace are fresh. */
    persistent=true;l=(actell_http_locator){0};memset(l.operation_id,'a',64);
    assert(run(&l,1024)==ESP_OK);actell_http_locator_release(&l);
    assert(!l.session&&actell_http_idle_wait_ms()==30000&&cleanups==0);
    l=(actell_http_locator){0};memset(l.operation_id,'b',64);
    assert(run(&l,1024)==ESP_OK);actell_http_locator_release(&l);
    l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK); /* removes prior operation header */
    assert(opens==1&&discoveries==1&&reused_headers==2&&signs==3);
    actell_http_locator_release(&l);
    /* Authenticated hello borrows the same socket with a separate request
     * generation, removes the operation header and does NOT extend idle. */
    monotonic+=300000000;
    assert(run(NULL,1024)==ESP_OK&&opens==1&&cleanups==0&&signs==4);
    monotonic+=299999000;assert(actell_http_idle_wait_ms()==1&&cleanups==0);
    monotonic+=1000;assert(actell_http_idle_wait_ms()==30000&&cleanups==1);
    l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK&&opens==2&&discoveries==2);
    actell_http_locator_release(&l);reset();

    /* Explicit hint eviction keeps address caching and never touches an
     * in-use locator. Absent hints still expire exactly at ten minutes. */
    persistent=true;l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);
    actell_http_close_idle();assert(l.session&&cleanups==0);
    actell_http_locator_release(&l);actell_http_close_idle();
    assert(cleanups==1);l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);
    assert(discoveries==1&&opens==2);actell_http_locator_release(&l);reset();
    persistent=true;l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);actell_http_locator_release(&l);
    monotonic+=599999000;l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);
    assert(opens==1);actell_http_locator_release(&l);
    monotonic+=599999000;assert(actell_http_idle_wait_ms()==1&&cleanups==0);
    monotonic+=1000;assert(actell_http_idle_wait_ms()==30000&&cleanups==1);reset();

    /* The same TTL boundary must work when borrowing across invocations. */
    persistent=true;assert(run(NULL,1024)==ESP_OK);monotonic+=29500000;
    l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);actell_http_locator_release(&l);
    monotonic+=1000000;l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);
    assert(opens==2&&discoveries==1&&reused_headers==1);actell_http_locator_release(&l);reset();

    /* Request limit is per connection, never reset by an invocation boundary. */
    persistent=true;
    for(unsigned i=0;i<16;i++){
        l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);actell_http_locator_release(&l);
    }
    assert(opens==1&&reused_headers==15&&cleanups==1&&actell_http_idle_wait_ms()==30000);
    l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK&&opens==2&&discoveries==1);
    actell_http_locator_release(&l);reset();

    /* New identity, control generation or network must never inherit old TLS. */
    for(unsigned mode=0;mode<10;mode++){
        persistent=true;l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);actell_http_locator_release(&l);
        if(mode==0)strcpy(config.client,"new-client");
        if(mode==1)strcpy(config.fingerprint,"new-pin");
        if(mode==2)generation++;
        if(mode==3)epoch++;
        if(mode==4)network_ip++;
        if(mode==5)network_mask++;
        if(mode==6)network_gateway++;
        if(mode==7)monotonic--;
        if(mode==8)monotonic+=600000000;
        if(mode==9)strcpy(config.origin,"https://stable-cert.local:8766");
        l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK&&opens==2&&reused_headers==0&&cleanups==1);
        actell_http_locator_release(&l);reset();
    }
    /* Reap even without another request: event wakeups only schedule the owner. */
    for(unsigned mode=0;mode<3;mode++){
        persistent=true;l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);actell_http_locator_release(&l);
        if(mode==0)network_up=false;if(mode==1)epoch++;if(mode==2)generation++;
        assert(actell_http_idle_wait_ms()==30000&&cleanups==1&&writes==1);reset();
    }
    /* In-flight generation changes also invalidate a live locator. */
    persistent=true;l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);generation++;
    assert(run(&l,1024)==ESP_OK&&opens==2&&reused_headers==0);
    actell_http_locator_close(&l);reset();

    /* A stale borrowed socket may recover once ONLY before any body attempt. */
    persistent=true;l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);actell_http_locator_release(&l);
    header_failures=1;l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);
    assert(opens==2&&discoveries==2&&reused_headers==1&&writes==2&&signs==3);
    actell_http_locator_release(&l);reset();
    for(unsigned mode=0;mode<5;mode++){
        persistent=true;l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);actell_http_locator_release(&l);
        write_failure=mode==0;partial_write=mode==1;read_header_failure=mode==2;truncated=mode==3;if(mode==4)status=401;
        l=(actell_http_locator){0};assert(run(&l,1024)!=ESP_OK);
        assert(!l.session&&opens==1&&discoveries==1&&reused_headers==1&&cleanups==1);
        assert(actell_http_idle_wait_ms()==30000);reset();
    }
    /* Completed nonpersistent responses and expired releases cannot be pooled. */
    l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);actell_http_locator_release(&l);
    assert(cleanups==1&&actell_http_idle_wait_ms()==30000);reset();
    persistent=true;l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);monotonic+=600000000;
    actell_http_locator_release(&l);assert(!l.session&&cleanups==1&&actell_http_idle_wait_ms()==30000);reset();
    /* Low memory rejects optional idle retention only after the response is
     * complete. It sends no extra request and preserves the short locator cache. */
    persistent=true;l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);
    dma_free=24*1024-1;actell_http_locator_release(&l);
    assert(!l.session&&cleanups==1&&writes==1&&signs==1);
    dma_free=64*1024;l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);
    assert(opens==2&&discoveries==1&&writes==2&&signs==2);
    actell_http_locator_release(&l);reset();
    /* Both exact thresholds retain the socket; later pressure evicts it once. */
    persistent=true;l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);
    dma_free=24*1024;dma_largest=2048;actell_http_locator_release(&l);
    assert(cleanups==0&&actell_http_idle_wait_ms()==30000);
    dma_largest=2047;assert(actell_http_idle_wait_ms()==30000&&cleanups==1);
    assert(actell_http_idle_wait_ms()==30000&&cleanups==1&&writes==1);reset();
    persistent=true;l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);
    dma_largest=2047;actell_http_locator_release(&l);
    assert(!l.session&&cleanups==1&&writes==1);reset();
    /* Borrowed/in-use sessions remain intact while receiving under pressure. */
    persistent=true;l=(actell_http_locator){0};assert(run(&l,1024)==ESP_OK);
    actell_http_locator_release(&l);l=(actell_http_locator){0};pressure_during_read=true;
    assert(run(&l,1024)==ESP_OK&&l.session&&cleanups==0&&opens==1&&writes==2);
    actell_http_locator_release(&l);assert(!l.session&&cleanups==1&&writes==2);reset();
    puts("PASS HTTP: memory-pressure idle eviction, active ownership, live-session TTL, cross-invocation reuse, fresh auth, no replay, bounded responses");
}
