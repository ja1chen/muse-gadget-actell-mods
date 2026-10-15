/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_private.h"
#include "esp_http_client.h"
#include "esp_random.h"
#include "mdns.h"
#include "lwip/inet.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "psa/crypto.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
typedef struct {actell_response*r;
size_t cap;
int64_t end;
uint64_t generation;
bool failed;
} sink;

static esp_err_t receive(esp_http_client_event_t *event){
 sink*s=event->user_data;
if(esp_timer_get_time()>s->end||!actell_bridge_current(s->generation)){s->failed=true;
return ESP_FAIL;
}
 if(event->event_id==HTTP_EVENT_ON_HEADER){
 #define HEADER(name,field) if(!strcasecmp(event->header_key,name)){if(strlen(event->header_value)>=sizeof(s->r->field)){s->failed=true;return ESP_FAIL;}strcpy(s->r->field,event->header_value);}
  HEADER("Content-Type",mime) HEADER("X-Actell-Request-Id",request_id) HEADER("X-Actell-Image-Id",image_id)
  HEADER("X-Actell-Generation",generation) HEADER("X-Actell-Width",width) HEADER("X-Actell-Height",height)
  HEADER("X-Actell-SHA256",digest) HEADER("X-Actell-Geometry-Revision",revision) HEADER("X-Actell-Image-Contract",contract)
 #undef HEADER
 }
 return ESP_OK;

}
esp_err_t actell_http(const actell_config*c,const char*id,const char*body,bool image,uint64_t generation,int64_t expires,actell_response*r){
 if(!actell_bridge_current(generation)||esp_timer_get_time()>expires)return ESP_ERR_INVALID_STATE;

 time_t now=time(NULL);

 if(now<1700000000){
  static bool time_started;

  if(!time_started){esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
esp_sntp_setservername(0,"pool.ntp.org");
esp_sntp_init();
time_started=true;
}
  return ESP_ERR_INVALID_STATE;

 } /* TLS and replay window need synchronized time. */
 size_t blen=strlen(body);
if(blen>ACTELL_REQUEST_MAX)return ESP_ERR_INVALID_SIZE;

 char nonce[37],stamp[24],signature[65],url[280],prefix[360];
unsigned char random[16],mac[32];
size_t maclen=0;

 esp_fill_random(random,sizeof(random));
snprintf(nonce,sizeof(nonce),"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",random[0],random[1],random[2],random[3],random[4],random[5],random[6],random[7],random[8],random[9],random[10],random[11],random[12],random[13],random[14],random[15]);

 snprintf(stamp,sizeof(stamp),"%lld",(long long)now);

 int n=snprintf(prefix,sizeof(prefix),"ACTELL1\n%s\n%s\n%s\n%s\n",c->client,id,stamp,nonce);

 if(n<0||(size_t)n>=sizeof(prefix))return ESP_ERR_INVALID_SIZE;

 psa_key_attributes_t attrs=PSA_KEY_ATTRIBUTES_INIT;
psa_key_id_t key=0;
psa_mac_operation_t op=PSA_MAC_OPERATION_INIT;

 psa_set_key_type(&attrs,PSA_KEY_TYPE_HMAC);
psa_set_key_usage_flags(&attrs,PSA_KEY_USAGE_SIGN_MESSAGE);
psa_set_key_algorithm(&attrs,PSA_ALG_HMAC(PSA_ALG_SHA_256));

 bool ok=psa_crypto_init()==PSA_SUCCESS&&psa_import_key(&attrs,c->secret,32,&key)==PSA_SUCCESS&&
  psa_mac_sign_setup(&op,key,PSA_ALG_HMAC(PSA_ALG_SHA_256))==PSA_SUCCESS&&psa_mac_update(&op,(const unsigned char*)prefix,n)==PSA_SUCCESS&&
  psa_mac_update(&op,(const unsigned char*)body,blen)==PSA_SUCCESS&&psa_mac_sign_finish(&op,mac,sizeof(mac),&maclen)==PSA_SUCCESS&&maclen==32;

 psa_mac_abort(&op);
if(key)psa_destroy_key(key);
psa_reset_key_attributes(&attrs);
if(!ok)return ESP_FAIL;

 for(unsigned i=0;
i<32;
i++)snprintf(signature+2*i,3,"%02x",mac[i]);

 char hostname[256];
strcpy(hostname,c->origin+8);
char *colon=strrchr(hostname,':');
int port=443;

 if(colon){*colon++=0;
char*end;
long n=strtol(colon,&end,10);
if(*end||n<1||n>65535)return ESP_ERR_INVALID_ARG;
port=(int)n;
}
 /* Bonjour is an untrusted locator. Only OOB identity/port selects a candidate;
    TLS still verifies the independently trusted hostname and leaf anchor. */
 static bool mdns_ready;

 if(!mdns_ready){if(mdns_init()!=ESP_OK)return ESP_FAIL;
mdns_ready=true;
}
 mdns_result_t *found=NULL;
char address[16]={0};

 esp_err_t discovery=mdns_query_ptr("_actell-muse","_tcp",2000,8,&found);

 if(discovery==ESP_OK)for(mdns_result_t *candidate=found;
candidate&&!address[0];
candidate=candidate->next){
  bool match=false;
for(size_t i=0;
i<candidate->txt_count;
i++)if(!strcmp(candidate->txt[i].key,"identity")&&candidate->txt[i].value&&!strcmp(candidate->txt[i].value,c->fingerprint))match=true;

  if(!match||candidate->port!=port)continue;

  for(mdns_ip_addr_t *a=candidate->addr;
a;
a=a->next)if(a->addr.type==ESP_IPADDR_TYPE_V4){snprintf(address,sizeof(address),IPSTR,IP2STR(&a->addr.u_addr.ip4));
break;
}
 }
 mdns_query_results_free(found);

 if(!address[0])return ESP_ERR_NOT_FOUND;

 snprintf(url,sizeof(url),"https://%s:%d/v1/rpc",address,port);
memset(r,0,sizeof(*r));

 size_t cap=image?ACTELL_IMAGE_MAX:ACTELL_JSON_MAX;
r->body=malloc(cap+1);
if(!r->body)return ESP_ERR_NO_MEM;

 int64_t end=esp_timer_get_time()+15000000;
if(expires<end)end=expires;

 sink s={r,cap,end,generation,false};

 esp_http_client_config_t config={.url=url,.cert_pem=c->pem,.common_name=hostname,.timeout_ms=15000,.disable_auto_redirect=true,
   .event_handler=receive,.user_data=&s,.keep_alive_enable=false,.skip_cert_common_name_check=false};

 esp_http_client_handle_t http=esp_http_client_init(&config);
if(!http){free(r->body);
r->body=NULL;
return ESP_ERR_NO_MEM;
}
 esp_http_client_set_header(http,"Host",c->origin+8);
esp_http_client_set_method(http,HTTP_METHOD_POST);
esp_http_client_set_header(http,"Content-Type","application/json");

 esp_http_client_set_header(http,"X-Actell-Time",stamp);
esp_http_client_set_header(http,"X-Actell-Nonce",nonce);
esp_http_client_set_header(http,"X-Actell-Signature",signature);

 /* Connect/send headers without body; check again after TLS before any action bytes.
    Each partial write/read is fenced. Already sent bytes cannot be rolled back. */
 esp_err_t e=ESP_FAIL;

 if(actell_bridge_current(generation)&&esp_timer_get_time()<end)e=esp_http_client_open(http,blen);

 size_t sent=0;

 while(e==ESP_OK&&sent<blen){
  if(!actell_bridge_current(generation)||esp_timer_get_time()>end){e=ESP_FAIL;
break;
}
  size_t chunk=blen-sent;
if(chunk>512)chunk=512;

  int n=esp_http_client_write(http,body+sent,chunk);
if(n<=0){e=ESP_FAIL;
break;
}sent+=(size_t)n;

 }
 int status=0;

 if(e==ESP_OK){int64_t content=esp_http_client_fetch_headers(http);
status=esp_http_client_get_status_code(http);
if(content<0||content>(int64_t)cap||status!=200)e=ESP_FAIL;
}
 while(e==ESP_OK){
  if(!actell_bridge_current(generation)||esp_timer_get_time()>end){e=ESP_FAIL;
break;
}
  unsigned char block[1024];
int n=esp_http_client_read(http,(char*)block,sizeof(block));

  if(n<0||(size_t)n>cap-r->length){e=ESP_FAIL;
break;
}
  if(!n){if(!esp_http_client_is_complete_data_received(http))e=ESP_FAIL;
break;
}
  memcpy(r->body+r->length,block,n);
r->length+=(size_t)n;

 }
 esp_http_client_close(http);
esp_http_client_cleanup(http);

 if(e!=ESP_OK||s.failed||status!=200){free(r->body);
r->body=NULL;
return ESP_FAIL;
}
 r->body[r->length]=0;
return ESP_OK;

}
