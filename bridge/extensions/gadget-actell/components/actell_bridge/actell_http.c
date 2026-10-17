/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_lan.h"
#include "actell_buffer.h"
#include "esp_http_client.h"
#include "esp_random.h"
#include "mdns.h"
#include "lwip/inet.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "esp_log.h"
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
 if(!actell_bridge_current(generation)||esp_timer_get_time()>expires){
 ESP_LOGW("actell.rpc","transport stage=session_or_deadline");return ESP_ERR_INVALID_STATE;}

 if(!actell_clock_ready()){
 ESP_LOGW("actell.rpc","transport stage=clock");return ESP_ERR_INVALID_STATE;}
 size_t blen=strlen(body);
if(blen>ACTELL_REQUEST_MAX)return ESP_ERR_INVALID_SIZE;

 char nonce[37],stamp[24],signature[65],url[280],prefix[360];
unsigned char random[16],mac[32];
size_t maclen=0;

 char hostname[256];
 const char *begin=c->origin+8,*colon=strrchr(begin,':');
 size_t host_len=colon?(size_t)(colon-begin):strlen(begin);
 if(!host_len||host_len>=sizeof(hostname))return ESP_ERR_INVALID_ARG;
 memcpy(hostname,begin,host_len);hostname[host_len]=0;
 int port=443;
 if(colon){char*end;long parsed=strtol(colon+1,&end,10);
 if(!colon[1]||*end||parsed<1||parsed>65535)return ESP_ERR_INVALID_ARG;
 port=(int)parsed;}
 /* Discovery is only a locator; the stored SAN hostname and leaf remain trust. */
 actell_candidate *found=calloc(SLAN_CANDIDATES,sizeof(*found));
 if(!found)return ESP_ERR_NO_MEM;
 unsigned count=0;esp_err_t discovery=actell_lan_discover(found,&count,c);
 char address[16]={0};
 if(discovery==ESP_OK&&count)strcpy(address,found[0].address);
 free(found);
 if(!address[0]){
 ESP_LOGW("actell.rpc","transport stage=discovery error=%s",esp_err_to_name(discovery));
 return ESP_ERR_NOT_FOUND;}
 struct ifreq iface;if(!actell_lan_interface(&iface))return ESP_ERR_INVALID_STATE;
 snprintf(url,sizeof(url),"https://%s:%d/v1/rpc",address,port);
memset(r,0,sizeof(*r));

 size_t cap=image?ACTELL_IMAGE_MAX:ACTELL_JSON_MAX;
 actell_buffer received={0};

 int64_t end=esp_timer_get_time()+15000000;
if(expires<end)end=expires;

 if(esp_timer_get_time()>=end)return ESP_ERR_INVALID_STATE;
 int timeout_ms=(int)((end-esp_timer_get_time())/1000);
 if(timeout_ms<1)timeout_ms=1;
 sink s={r,cap,end,generation,false};

 /* Discovery or SNTP may advance wall time. Sample only after readiness and
  * location lookup, immediately before signing, never before clock sync. */
 time_t now=time(NULL);
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

 esp_http_client_config_t config={.url=url,.cert_pem=c->pem,.if_name=&iface,.common_name=hostname,.timeout_ms=timeout_ms,.disable_auto_redirect=true,
   .event_handler=receive,.user_data=&s,.keep_alive_enable=false,.skip_cert_common_name_check=false};

 esp_http_client_handle_t http=esp_http_client_init(&config);
if(!http)return ESP_ERR_NO_MEM;
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
 int64_t content=-1;

 if(e==ESP_OK){content=esp_http_client_fetch_headers(http);
status=esp_http_client_get_status_code(http);r->http_status=status;
if(content<0||status!=200)e=ESP_FAIL;
else if(content>(int64_t)cap)e=ESP_ERR_INVALID_SIZE;
else {
    actell_buffer_result allocated=actell_buffer_init(&received,cap,(size_t)content);
    if(allocated!=ACTELL_BUFFER_OK)e=allocated==ACTELL_BUFFER_LIMIT?ESP_ERR_INVALID_SIZE:ESP_ERR_NO_MEM;
}
}
 while(e==ESP_OK){
  if(!actell_bridge_current(generation)||esp_timer_get_time()>end){e=ESP_FAIL;
break;
}
  unsigned char block[1024];
int n=esp_http_client_read(http,(char*)block,sizeof(block));

if(n<0){e=ESP_FAIL;
break;
}
  if(!n){if(!esp_http_client_is_complete_data_received(http))e=ESP_FAIL;
break;
}
  actell_buffer_result appended=actell_buffer_append(&received,block,(size_t)n);
  if(appended!=ACTELL_BUFFER_OK){e=appended==ACTELL_BUFFER_LIMIT?ESP_ERR_INVALID_SIZE:ESP_ERR_NO_MEM;break;}

 }
 esp_http_client_close(http);
esp_http_client_cleanup(http);

 if(e!=ESP_OK||s.failed||status!=200){
 ESP_LOGW("actell.rpc","transport stage=https error=%s status=%d content_bytes=%lld received_bytes=%u limit_bytes=%u",
          esp_err_to_name(e),status,(long long)content,(unsigned)received.length,(unsigned)cap);
 actell_buffer_free(&received);
 return e!=ESP_OK?e:ESP_FAIL;
}
 r->body=received.data;
 r->length=received.length;
 ESP_LOGI("actell.rpc","transport response_bytes=%u limit_bytes=%u",(unsigned)r->length,(unsigned)cap);
return ESP_OK;

}
