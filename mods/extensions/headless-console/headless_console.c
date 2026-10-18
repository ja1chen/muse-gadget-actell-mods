/* Independent development-only headless HTTP bridge; Apache-2.0.
 * No Actell credentials or tools are exposed by this firmware. */
#include "headless_console.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "cJSON.h"
#include "muse_chat.h"
#include "voice_muse_chat.h"
#include "wifi_mgr.h"

#define EVENTS 64
#define EVENT_SIZE 400
#define TEXT_LIMIT 4096
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static char (*ring)[EVENT_SIZE]; /* 25.6 KiB PSRAM, allocated before chat task. */
static uint32_t next_seq;
static bool active;
static bool oversized_event;
#if CONFIG_MUSE_HEADLESS_HTTP_DEBUG
static int64_t last_submit, last_events, last_cancel;
static char access_token[33];
#endif
extern const uint8_t page_start[] asm("_binary_headless_html_start");
extern const uint8_t page_end[] asm("_binary_headless_html_end");

/* Runs in the existing Muse producer task. Bounded copy; never blocks on I/O. */
void headless_console_observe(const char *line, size_t len) {
    if (!ring || len < 7 || memcmp(line, "@chat ", 6)) return;
    const char *json = line + 6;
    size_t n = len - 6;
    while (n && (json[n-1] == '\n' || json[n-1] == '\r')) --n;
    if (n >= EVENT_SIZE) { portENTER_CRITICAL(&lock); oversized_event=true; portEXIT_CRITICAL(&lock); return; }
    portENTER_CRITICAL(&lock);
    ++next_seq;
    memcpy(ring[next_seq % EVENTS], json, n);
    ring[next_seq % EVENTS][n] = '\0';
    if (strstr(ring[next_seq % EVENTS], "\"type\":\"done\"") || strstr(ring[next_seq % EVENTS], "\"type\":\"error\"")) active = false;
    portEXIT_CRITICAL(&lock);
}
#if CONFIG_MUSE_HEADLESS_HTTP_DEBUG
static esp_err_t reply(httpd_req_t *r, const char *status, const char *text) {
    httpd_resp_set_status(r, status);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
    return httpd_resp_sendstr(r, text);
}
static bool same_origin(httpd_req_t *r, bool require_origin) {
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (!netif || esp_netif_get_ip_info(netif, &ip) != ESP_OK || !ip.ip.addr) return false;
    char expected[48], host[64], origin[80], full[80];
    snprintf(expected, sizeof(expected), IPSTR, IP2STR(&ip.ip));
    if (httpd_req_get_hdr_value_str(r,"Host",host,sizeof(host)) != ESP_OK) return false;
    snprintf(full,sizeof(full),"%s:80",expected);
    if (strcmp(host,expected) && strcmp(host,full)) return false;
    if (!require_origin) return true;
    if (httpd_req_get_hdr_value_str(r,"Origin",origin,sizeof(origin)) != ESP_OK) return false;
    snprintf(full,sizeof(full),"http://%s",host);
    return !strcmp(origin,full);
}
static bool authorized(httpd_req_t *r) {
    char got[40];
    if (!same_origin(r, true) || httpd_req_get_hdr_value_len(r,"X-Console-Token") != 32 ||
        httpd_req_get_hdr_value_str(r,"X-Console-Token",got,sizeof(got)) != ESP_OK) return false;
    unsigned diff = 0;
    for (size_t i=0;i<32;i++) diff |= (unsigned char)got[i] ^ (unsigned char)access_token[i];
    return diff == 0;
}
static cJSON *body(httpd_req_t *r) {
    if (r->content_len <= 0 || r->content_len > TEXT_LIMIT + 100) return NULL;
    char *text = malloc(r->content_len + 1);
    if (!text) return NULL;
    int pos=0;
    while (pos < r->content_len) {
        int n=httpd_req_recv(r,text+pos,r->content_len-pos);
        if(n<=0){free(text);return NULL;}
        pos+=n;
    }
    text[pos]='\0';
    cJSON *obj=cJSON_ParseWithLength(text,pos+1);
    free(text);
    return obj;
}
static esp_err_t index_get(httpd_req_t *r) {
    if (!same_origin(r, false)) return reply(r,"403 Forbidden","{\"error\":\"host rejected\"}");
    httpd_resp_set_type(r,"text/html; charset=utf-8");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    httpd_resp_set_hdr(r,"X-Frame-Options","DENY");
    httpd_resp_set_hdr(r,"Content-Security-Policy","default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'none'");
    return httpd_resp_send(r,(const char *)page_start,page_end-page_start-1);
}
static esp_err_t api_post(httpd_req_t *r) {
    if(!authorized(r)) return reply(r,"403 Forbidden","{\"error\":\"token or origin rejected\"}");
    int64_t now_request=esp_timer_get_time();
    int64_t *rate = !strcmp(r->uri,"/api/events") ? &last_events :
                    !strcmp(r->uri,"/api/cancel") ? &last_cancel : NULL;
    if(rate && now_request-*rate<100000) return reply(r,"429 Too Many Requests","{\"error\":\"rate limited\"}");
    if(rate) *rate=now_request;
    cJSON *obj=body(r);
    if(!cJSON_IsObject(obj)){cJSON_Delete(obj);return reply(r,"400 Bad Request","{\"error\":\"invalid bounded JSON body\"}");}
    if(!strcmp(r->uri,"/api/text")) {
        const cJSON *text=cJSON_GetObjectItemCaseSensitive(obj,"text");
        if(!cJSON_IsString(text) || !text->valuestring[0] || strlen(text->valuestring)>TEXT_LIMIT){cJSON_Delete(obj);return reply(r,"400 Bad Request","{\"error\":\"text must be 1..4096 UTF-8 bytes\"}");}
        int64_t now=esp_timer_get_time();
        portENTER_CRITICAL(&lock); bool busy=active; portEXIT_CRITICAL(&lock);
        if(busy || now-last_submit<1000000){cJSON_Delete(obj);return reply(r,"409 Conflict","{\"error\":\"turn active or submit rate limited\"}");}
        voice_hatch_refresh();
        if(!muse_hatch_ready()){cJSON_Delete(obj);return reply(r,"503 Service Unavailable","{\"error\":\"Muse not paired or Wi-Fi unavailable\"}");}
        char *owned=strdup(text->valuestring);
        cJSON_Delete(obj);
        if(!owned)return reply(r,"503 Service Unavailable","{\"error\":\"out of memory\"}");
        portENTER_CRITICAL(&lock); active=true; portEXIT_CRITICAL(&lock);
        last_submit=now;
        muse_hatch_text_turn(owned); /* Actual SDK entry point owns/frees buffer. */
        return reply(r,"202 Accepted","{\"queued\":true,\"note\":\"SDK queued; wait for sent/done events\"}");
    }
    if(!strcmp(r->uri,"/api/cancel")) {
        cJSON_Delete(obj); muse_hatch_text_cancel();
        return reply(r,"202 Accepted","{\"cancel_requested\":true,\"note\":\"wait for terminal event; remote actions may already have run\"}");
    }
    const cJSON *cursor=cJSON_GetObjectItemCaseSensitive(obj,"after");
    double requested=cJSON_IsNumber(cursor)?cursor->valuedouble:0;
    if(requested<0 || requested>UINT32_MAX || requested!=(uint32_t)requested){cJSON_Delete(obj);return reply(r,"400 Bad Request","{\"error\":\"invalid cursor\"}");}
    uint32_t after=(uint32_t)requested;
    cJSON_Delete(obj);
    char copy[EVENT_SIZE];
    uint32_t latest;bool running,oversized;
    portENTER_CRITICAL(&lock); latest=next_seq;running=active;oversized=oversized_event;portEXIT_CRITICAL(&lock);
    uint32_t first=latest>=EVENTS?latest-EVENTS+1:1;
    bool lost=oversized || after+1<first || after>latest;
    if(after<first-1 || after>latest)after=first-1;
    cJSON *out=cJSON_CreateObject(), *events=cJSON_CreateArray();
    if(!out || !events){cJSON_Delete(out);cJSON_Delete(events);return reply(r,"503 Service Unavailable","{\"error\":\"out of memory\"}");}
    cJSON_AddNumberToObject(out,"cursor",latest);cJSON_AddBoolToObject(out,"active",running);
    muse_hatch_status_t status;muse_hatch_status(&status);
    cJSON_AddStringToObject(out,"status",muse_hatch_state_name(status.state));
    cJSON_AddStringToObject(out,"detail",status.detail);
    cJSON_AddItemToObject(out,"events",events);
    for(uint32_t i=after+1;i<=latest;i++){
        portENTER_CRITICAL(&lock);
        bool overwritten=next_seq-i>=EVENTS;
        if(!overwritten)memcpy(copy,ring[i%EVENTS],EVENT_SIZE);
        portEXIT_CRITICAL(&lock);
        if(overwritten){lost=true;continue;}
        cJSON *event=cJSON_Parse(copy);
        if(event)cJSON_AddItemToArray(events,event);else lost=true;
    }
    cJSON_AddBoolToObject(out,"lost",lost);
    char *encoded=cJSON_PrintUnformatted(out);cJSON_Delete(out);
    if(!encoded)return reply(r,"503 Service Unavailable","{\"error\":\"out of memory\"}");
    esp_err_t result=reply(r,"200 OK",encoded);free(encoded);return result;
}
#endif
static void refresh_task(void *unused) {
    (void)unused;
    for(;;){voice_hatch_refresh();vTaskDelay(pdMS_TO_TICKS(1000));}
}
void headless_console_start(void) {
    ring=heap_caps_calloc(EVENTS, EVENT_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(!ring){ESP_LOGE("headless","PSRAM event buffer unavailable; headless session disabled");return;}
    voice_hatch_refresh();
    muse_hatch_start();
    if(xTaskCreate(refresh_task,"headless-config",3072,NULL,2,NULL)!=pdPASS) {
        ESP_LOGE("headless","config refresh unavailable; HTTP disabled");return;
    }
#if CONFIG_MUSE_HEADLESS_HTTP_DEBUG
    uint8_t bytes[16];esp_fill_random(bytes,sizeof(bytes));
    for(size_t i=0;i<sizeof(bytes);i++)snprintf(access_token+i*2,3,"%02x",bytes[i]);
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets=3;config.lru_purge_enable=true;config.stack_size=8192;
    config.recv_wait_timeout=3;config.send_wait_timeout=3;
    httpd_handle_t server=NULL;
    if(httpd_start(&server,&config)!=ESP_OK){ESP_LOGE("headless","HTTP start failed");return;}
    httpd_uri_t root={.uri="/",.method=HTTP_GET,.handler=index_get};
    httpd_register_uri_handler(server,&root);
    const char *routes[]={"/api/events","/api/text","/api/cancel"};
    for(size_t i=0;i<3;i++){httpd_uri_t route={.uri=routes[i],.method=HTTP_POST,.handler=api_post};httpd_register_uri_handler(server,&route);}
    /* Deliberately serial only, never ESP_LOG (support logging can capture it). */
    printf("\nHEADLESS DEBUG: isolated trusted LAN only; HTTP/chat is plaintext.\nConsole boot token: %s\n",access_token);fflush(stdout);
#endif
}
