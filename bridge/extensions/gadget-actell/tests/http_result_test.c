/* Actual actell_http.c with synthetic SDK transport/crypto only. No network. */
#include "actell_lan.h"
#include "esp_http_client.h"
#include "psa/crypto.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static struct fake_http { esp_http_client_config_t config; } http;
static unsigned char *reply;
static size_t reply_size,offset;
static unsigned reads,discoveries;
static bool local_address=true;
static actell_http_locator *test_locator;
static unsigned config_variant;
bool actell_lan_address(uint32_t address){(void)address;return local_address;}
static bool chunked,truncated,cancelled,cancel_during_read;
void test_http_log(const char *tag,const char *format,...){(void)tag;(void)format;}
const char *esp_err_to_name(int e){(void)e;return "synthetic";}
bool actell_bridge_current(uint64_t g){return g==7&&!cancelled;}
int64_t esp_timer_get_time(void){return 1000000;}
static time_t wall=1800000000;
time_t time(time_t *out){if(out)*out=wall;return wall;}
bool actell_clock_ready(void){wall+=120;return true;}
void esp_fill_random(void *p,size_t n){memset(p,1,n);}
bool actell_lan_interface(struct ifreq *p){memset(p,0,sizeof(*p));return true;}
esp_err_t actell_lan_discover(actell_candidate out[SLAN_CANDIDATES],unsigned *n,const actell_config *p){
 discoveries++;
    wall+=120; /* Discovery also delays the signing timestamp. */
    (void)p;strcpy(out[0].address,"192.168.1.2");*n=1;return ESP_OK;
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
    assert(!strcmp(c->common_name,"stable-cert.local"));
    assert(!strcmp(c->url,"https://192.168.1.2:8765/v1/rpc"));
    http.config=*c;return &http;
}
int esp_http_client_set_header(esp_http_client_handle_t h,const char *k,const char *v){(void)h;if(!strcmp(k,"X-Actell-Time"))assert(strtoll(v,NULL,10)==wall);return 0;}
int esp_http_client_set_method(esp_http_client_handle_t h,int m){(void)h;(void)m;return 0;}
esp_err_t esp_http_client_open(esp_http_client_handle_t h,int n){(void)h;(void)n;return ESP_OK;}
int esp_http_client_write(esp_http_client_handle_t h,const char *p,int n){(void)h;(void)p;return n;}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h){
    esp_http_client_event_t e={HTTP_EVENT_ON_HEADER,h->config.user_data,"Content-Type","application/json"};
    assert(h->config.event_handler(&e)==ESP_OK);
    return chunked?0:(int64_t)reply_size;
}
int esp_http_client_get_status_code(esp_http_client_handle_t h){(void)h;return 200;}
int esp_http_client_read(esp_http_client_handle_t h,char *p,int n){
    (void)h;reads++;
    if(cancel_during_read)cancelled=true;
    size_t end=truncated?reply_size/2:reply_size;
    size_t take=end-offset;
    if(take>(size_t)n)take=(size_t)n;
    if(take>713)take=713; /* deliberately fragmented, not aligned to 1 KiB */
    memcpy(p,reply+offset,take);offset+=take;return (int)take;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h){(void)h;return offset==reply_size;}
int esp_http_client_close(esp_http_client_handle_t h){(void)h;return 0;}
int esp_http_client_cleanup(esp_http_client_handle_t h){(void)h;return 0;}

static void run(size_t size,bool unknown,bool cut,bool cancel) {
    reply_size=size;offset=reads=0;chunked=unknown;truncated=cut;
    cancelled=false;cancel_during_read=cancel;
    const char *prefix="{\"v\":1,\"request_id\":\"test-request\",\"ok\":true,\"result\":{\"run_id\":\"test-run\",\"call_id\":\"test-call\",\"state\":\"succeeded\",\"result\":{\"text\":\"";
    const char *suffix="\"}}}";
    assert(size>strlen(prefix)+strlen(suffix));
    reply=malloc(size+1);assert(reply);memset(reply,'x',size);reply[size]=0;
    memcpy(reply,prefix,strlen(prefix));memcpy(reply+size-strlen(suffix),suffix,strlen(suffix));
    actell_config config={0};strcpy(config.origin,"https://stable-cert.local:8765");
    strcpy(config.client,config_variant?"synthetic-other":"synthetic-client");
    actell_response response={0};
    int e=test_locator?actell_http_with_locator(&config,"test-request","{}",false,7,90000000,&response,test_locator):actell_http(&config,"test-request","{}",false,7,90000000,&response);
    if(size>ACTELL_JSON_MAX){
        assert(e==ESP_ERR_INVALID_SIZE && !response.body);
        if(!unknown)assert(!reads); /* reject Content-Length before allocating/reading */
    }else if(cut||cancel){
        assert(e!=ESP_OK && !response.body);
        assert(response.transport_stage&&!strcmp(response.transport_stage,cancel?"session_or_deadline":"read_body"));
        assert(response.expected_bytes==(unknown?0:(long long)size));
        assert(response.received_bytes==offset&&response.received_bytes<size);
    }
    else {
        assert(e==ESP_OK && response.length==size && response.body[size]==0);
        assert(!response.transport_stage&&response.received_bytes==size);
        assert(!memcmp(response.body,reply,size));
        free(response.body);
    }
    free(reply);
}
int main(void){
    size_t sizes[]={1024,16383,16384,16385,32768,65536,ACTELL_JSON_MAX,ACTELL_JSON_MAX+1};
    for(size_t i=0;i<sizeof(sizes)/sizeof(*sizes);i++)for(unsigned c=0;c<2;c++)run(sizes[i],c,false,false);
    run(65536,false,true,false);run(65536,true,true,false);
    run(65536,false,false,true);run(65536,true,false,true);
    actell_http_locator locator={0};test_locator=&locator;
    unsigned before=discoveries;
    run(1024,false,false,false);assert(discoveries==before+1&&locator.address[0]);
    run(1024,false,false,false);assert(discoveries==before+1); /* same invocation, still authenticates TLS */
    run(1024,false,true,false);assert(discoveries==before+1&&!locator.address[0]);
    run(1024,false,false,false);assert(discoveries==before+2); /* failed read invalidates, never replays */
    config_variant=1;run(1024,false,false,false);assert(discoveries==before+3);
    local_address=false;run(1024,false,false,false);assert(discoveries==before+4);local_address=true;
    test_locator=NULL;
    puts("PASS actual HTTP receive: completed JSON above 16 KiB, declared/chunked bounds, truncation, session fence; synthetic only");
}
