/* Actual production HTTP/cache lifecycle with synthetic transport/crypto only. */
#include "actell_lan.h"
#include "esp_http_client.h"
#include "psa/crypto.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct fake_http { esp_http_client_config_t config; bool prepared; unsigned header_updates; };
static unsigned char *reply;
static size_t reply_size,offset;
static unsigned reads,discoveries,opens,initializations,cleanups,reused_headers,writes,signs;
static unsigned open_failures,header_failures;
static bool local_address=true,network_up=true,persistent,chunked,truncated,cancelled,cancel_during_read;
static bool write_failure,partial_write,read_header_failure,parser_completed_early;
static int status=200,connect_timeout;
static int64_t monotonic=1000000;
static uint32_t epoch=1;
static actell_config config;
static time_t wall=1800000000;
void test_http_log(const char *tag,const char *format,...){(void)tag;(void)format;}
const char *esp_err_to_name(int e){(void)e;return "synthetic";}
bool actell_lan_address(uint32_t address){(void)address;return local_address;}
bool actell_bridge_current(uint64_t g){return g==7&&!cancelled;}
int64_t esp_timer_get_time(void){return monotonic;}
uint32_t actell_lan_epoch(void){return epoch;}
void actell_lan_network_changed(void){epoch++;}
bool actell_lan_network(actell_network_stamp *out){*out=(actell_network_stamp){epoch,1,2,3};return network_up;}
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
    assert(!c->skip_cert_common_name_check && c->disable_auto_redirect);
    assert(!strcmp(c->common_name,"stable-cert.local")&&!strcmp(c->cert_pem,"synthetic-cert"));
    assert(!strcmp(c->url,"https://192.168.1.2:8765/v1/rpc")||!strcmp(c->url,"https://192.168.1.3:8765/v1/rpc"));
    struct fake_http *h=calloc(1,sizeof(*h));assert(h);h->config=*c;initializations++;return h;
}
int esp_http_client_set_header(esp_http_client_handle_t h,const char *k,const char *v){
    if(!strcmp(k,"X-Actell-Time"))assert(strtoll(v,NULL,10)==wall);
    if(!strcmp(k,"X-Actell-Nonce"))h->header_updates++;
    if(!strcmp(k,"Host"))assert(!strcmp(v,"stable-cert.local:8765"));
    if(!strcmp(k,"X-Actell-Operation-Id"))assert(strlen(v)==64);
    return 0;
}
int esp_http_client_set_method(esp_http_client_handle_t h,int m){(void)h;(void)m;return 0;}
int esp_http_client_set_timeout_ms(esp_http_client_handle_t h,int t){assert(t>0&&t<=15000);h->config.timeout_ms=t;return 0;}
esp_err_t esp_http_client_open(esp_http_client_handle_t h,int n){
    assert(n>0&&h->header_updates);opens++;connect_timeout=h->config.timeout_ms;
    assert(!strcmp(h->config.cert_pem,"synthetic-cert"));
    if(open_failures){open_failures--;return ESP_FAIL;}offset=0;return ESP_OK;
}
esp_err_t esp_http_client_prepare(esp_http_client_handle_t h){h->prepared=true;return ESP_OK;}
esp_err_t esp_http_client_request_send(esp_http_client_handle_t h,int n){
    assert(h->prepared&&n>0);h->prepared=false;reused_headers++;offset=0;
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
    status=200;monotonic=1000000;epoch=1;
    memset(&config,0,sizeof(config));strcpy(config.origin,"https://stable-cert.local:8765");
    strcpy(config.client,"synthetic-client");strcpy(config.fingerprint,"synthetic-pin");strcpy(config.pem,"synthetic-cert");
}
static int run(actell_http_locator *locator,size_t size){
    reply_size=size;offset=reads=0;reply=malloc(size+1);assert(reply);
    memset(reply,'x',size);reply[size]=0;
    actell_response r={0};
    int e=actell_http_with_locator(&config,"synthetic-request","{}",false,7,monotonic+90000000,&r,locator);
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
    monotonic+=4000000;assert(run(&l,1024)==ESP_OK&&opens==2&&discoveries==1);
    for(unsigned i=0;i<16;i++)assert(run(&l,1024)==ESP_OK);
    assert(opens==3);epoch++;assert(run(&l,1024)==ESP_OK&&opens==4&&discoveries==2);
    actell_http_locator_close(&l);reset();
    puts("PASS HTTP: sequential persistence, fresh signing, cache TTL/identity/network, bounded pre-body fallback, no replay, large/chunked/truncated responses");
}
