/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_private.h"
#include "nvs.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
static bool origin_valid(const char *s) {
 if (!s || strncmp(s,"https://",8) || !s[8] || strlen(s)>=256) return false;

 for(const char*p=s+8;
*p;
p++) if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='.'||*p=='-'||*p==':'||*p=='['||*p==']'))return false;

 return true;

}
static bool certificate_valid(const char *pem,const char *fp) {
 if(!pem||!fp||strlen(pem)>=4096||strlen(fp)!=64)return false;

 mbedtls_x509_crt cert;
mbedtls_x509_crt_init(&cert);

 bool ok=false;
unsigned char hash[32];
size_t n=0;
char hex[65];

 if(mbedtls_x509_crt_parse(&cert,(const unsigned char*)pem,strlen(pem)+1)==0 && !cert.next && !mbedtls_x509_crt_get_ca_istrue(&cert) && cert.issuer_raw.len==cert.subject_raw.len && !memcmp(cert.issuer_raw.p,cert.subject_raw.p,cert.issuer_raw.len) &&
    psa_crypto_init()==PSA_SUCCESS && psa_hash_compute(PSA_ALG_SHA_256,cert.raw.p,cert.raw.len,hash,sizeof(hash),&n)==PSA_SUCCESS && n==32){
  for(unsigned i=0;
i<32;
i++)snprintf(hex+i*2,3,"%02x",hash[i]);
ok=!strcmp(hex,fp);

 }
 mbedtls_x509_crt_free(&cert);
return ok;

}
esp_err_t actell_bridge_provision(const char*origin,const char*client,const unsigned char secret[32],const char*pem,const char*fp){
 if(!origin_valid(origin)||!actell_id_valid(client)||!secret||!certificate_valid(pem,fp))return ESP_ERR_INVALID_ARG;

 actell_config *c=calloc(1,sizeof(*c));
 if(!c)return ESP_ERR_NO_MEM;
 strcpy(c->origin,origin);strcpy(c->client,client);strcpy(c->pem,pem);
 strcpy(c->fingerprint,fp);memcpy(c->secret,secret,32);
 nvs_handle_t h;esp_err_t e=nvs_open("actell",NVS_READWRITE,&h);
 if(e==ESP_OK){
     e=nvs_set_blob(h,"peer_v1",c,sizeof(*c));
     if(e==ESP_OK)e=nvs_commit(h);
     nvs_close(h);
 }
 volatile unsigned char *p=(volatile unsigned char*)c;
 for(size_t i=0;i<sizeof(*c);i++)p[i]=0;
 free(c);return e;

}
esp_err_t actell_config_load(actell_config*c){
 nvs_handle_t h;
size_t n=sizeof(*c);
esp_err_t e=nvs_open("actell",NVS_READONLY,&h);

 if(e!=ESP_OK)return e;
e=nvs_get_blob(h,"peer_v1",c,&n);
nvs_close(h);

 if(e!=ESP_OK||n!=sizeof(*c))return ESP_ERR_INVALID_STATE;

 c->origin[255]=0;
c->client[128]=0;
c->pem[4095]=0;
c->fingerprint[64]=0;

 return origin_valid(c->origin)&&actell_id_valid(c->client)&&certificate_valid(c->pem,c->fingerprint)?ESP_OK:ESP_ERR_INVALID_STATE;

}
