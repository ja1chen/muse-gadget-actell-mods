/* SPDX-License-Identifier: Apache-2.0. Synthetic WebSocket peer, no sockets. */
#include <assert.h>
#include "../components/doubao_tts/doubao_tts.c"
#include "doubao_protocol.h"

static int64_t now;
static size_t split = 1024, queued, offset, consumed, drained;
static uint8_t wire[512 * 1024];
static char sid[129], sent_text[1024];
static unsigned opens, tasks, closes, cancels, short_event;
static int status = 101, fault;
static bool cancel_read, replace_read, cancel_wait, fail_allocation, fail_task, internal_stack_unavailable;
static size_t audio_size = 4;
void *esp_crt_bundle_attach = (void *)1;
static char last_diagnostic[256];
void fake_log(const char *tag, const char *fmt, ...)
{
    if (strcmp(tag, "doubao_ws") && strcmp(tag, "doubao_tts")) return;
    va_list args; va_start(args,fmt);
    vsnprintf(last_diagnostic,sizeof(last_diagnostic),fmt,args); va_end(args);
    assert(!strstr(last_diagnostic,"SYNTHETIC_API_KEY"));
    assert(!strstr(last_diagnostic,"synthetic"));
}
void esp_log_level_set(const char *tag, int level) { assert(!strcmp(tag,"transport_ws")); assert(level == ESP_LOG_NONE); }
int64_t esp_timer_get_time(void) { return now; }
void esp_fill_random(void *p, size_t n) { memset(p, 0x42, n); }
void *heap_caps_malloc(size_t n, unsigned caps) { (void)caps; return fail_allocation ? NULL : malloc(n); }
size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return 123456; }
QueueHandle_t xQueueCreate(unsigned n, unsigned size) { (void)n; (void)size; return (void *)1; }
void vQueueDelete(QueueHandle_t q) { (void)q; }
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, int pri, void *handle)
{ (void)fn; (void)name; (void)stack; (void)arg; (void)pri; (void)handle; return fail_task || internal_stack_unavailable ? 0 : pdPASS; }
int xTaskCreateWithCaps(void (*fn)(void *), const char *name, unsigned stack, void *arg, int pri, void *handle, unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    (void)fn; (void)name; (void)stack; (void)arg; (void)pri; (void)handle;
    return fail_task ? 0 : pdPASS;
}
int xQueueOverwrite(QueueHandle_t q, const void *job) { (void)q; (void)job; return pdPASS; }
int xQueueReceive(QueueHandle_t q, void *job, unsigned wait) { (void)q; (void)job; (void)wait; return 0; }
void vTaskDelay(unsigned ms)
{
    now += ms * 1000;
    if (cancel_wait) doubao_tts_cancel();
    else { int16_t pcm[512]; size_t n = doubao_tts_read(pcm,512); drained += n*2;
        for (size_t i=0;i<n;++i) assert(pcm[i] == 0); }
}
static void be(uint8_t *p, uint32_t n) { p[0]=n>>24; p[1]=n>>16; p[2]=n>>8; p[3]=n; }
static uint32_t rd(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static void response(unsigned event, bool raw, size_t size)
{
    const char *id = event == 50 ? "synthetic-connection" : fault == 1 ? "stale-session" : sid;
    size_t ids = strlen(id), length=16+ids+size;
    assert(queued + 4 + length < sizeof(wire));
    be(wire+queued,length); queued+=4;
    uint8_t *p=wire+queued; memset(p,0,length);
    p[0]=0x11; p[1]=raw?0xb4:0x94; p[2]=raw?0:0x10;
    if (fault == 2 && event == 352) p[2] |= 1; /* unsupported compression */
    be(p+4,event); be(p+8,ids); memcpy(p+12,id,ids); be(p+12+ids,size);
    if (!raw) { assert(size==2); memcpy(p+16+ids,"{}",2); }
    if (fault == 4 && !raw && event == 152) p[16+ids] = '[';
    queued+=length;
}
esp_transport_handle_t esp_transport_ssl_init(void) { return (void *)1; }
void esp_transport_ssl_crt_bundle_attach(esp_transport_handle_t t, void *fn) { assert(t && fn); }
esp_transport_handle_t esp_transport_ws_init(esp_transport_handle_t t) { assert(t); return (void *)2; }
int esp_transport_ws_set_config(esp_transport_handle_t t, const esp_transport_ws_config_t *cfg)
{
    assert(t && !strcmp(cfg->ws_path,"/api/v3/tts/bidirection") && !cfg->propagate_control_frames);
    assert(strstr(cfg->headers,"X-Api-Key: SYNTHETIC_API_KEY\r\n"));
    assert(strstr(cfg->headers,"X-Api-Resource-Id: seed-tts-2.0\r\n")); return 0;
}
int esp_transport_connect(esp_transport_handle_t t, const char *host, int port, int timeout)
{ assert(t && !strcmp(host,"openspeech.bytedance.com") && port==443 && timeout==5000); ++opens; return 0; }
int esp_transport_ws_get_upgrade_request_status(esp_transport_handle_t t) { (void)t; return status; }
int esp_transport_ws_send_raw(esp_transport_handle_t t, ws_transport_opcodes_t opcode, const char *bytes, int n, int timeout)
{
    (void)t; assert(opcode==(WS_TRANSPORT_OPCODES_BINARY|WS_TRANSPORT_OPCODES_FIN) && timeout>0);
    const uint8_t *p=(const uint8_t *)bytes; assert(n>=14 && p[0]==0x11 && p[1]==0x14 && p[2]==0x10);
    unsigned event=rd(p+4); if (event==short_event) return n-1;
    size_t at=8;
    if (event!=1 && event!=2) {
        size_t ids=rd(p+at); at+=4; assert(ids && ids<=128 && at+ids+4<(size_t)n);
        if (event==100) { memcpy(sid,p+at,ids); sid[ids]=0; }
        else assert(strlen(sid)==ids && !memcmp(sid,p+at,ids));
        at+=ids;
    }
    size_t size=rd(p+at); at+=4; assert(at+size==(size_t)n);
    cJSON *j=cJSON_ParseWithLength((const char *)p+at,size); assert(j);
    cJSON *params=cJSON_GetObjectItem(j,"req_params");
    if (event==100 || event==200) {
        assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(j,"namespace")),"BidirectionalTTS"));
        assert(cJSON_GetObjectItem(j,"event")->valueint==(int)event);
    }
    if (event==1) response(fault == 7 ? 51 : 50,false,2);
    else if (event==100) {
        assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(params,"speaker")),"zh_female_vv_uranus_bigtts"));
        cJSON *a=cJSON_GetObjectItem(params,"audio_params");
        assert(cJSON_GetObjectItem(a,"sample_rate")->valueint==16000);
        assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(a,"format")),"pcm"));
        response(fault == 8 ? 153 : 150,false,2);
    } else if (event==200) {
        const char *s=cJSON_GetStringValue(cJSON_GetObjectItem(params,"text"));
        assert(s && strlen(s)<=256 && ((unsigned char)*s & 0xc0)!=0x80);
        assert(strlen(sent_text)+strlen(s)<sizeof(sent_text)); strcat(sent_text,s); ++tasks;
        if (audio_size) response(352,true,audio_size);
    } else if (event==102) { if (fault!=3) response(152,false,2); }
    else if (event==101) ++cancels;
    else assert(event==2);
    cJSON_Delete(j); return n;
}
int esp_transport_poll_read(esp_transport_handle_t t, int ms) { (void)t; (void)ms; return offset<queued; }
static size_t frame_size, frame_pos;
static bool frame_fin;
static int frame_opcode;
int esp_transport_read(esp_transport_handle_t t, char *out, int capacity, int timeout)
{
    (void)t; (void)timeout;
    if (cancel_read) { cancel_read=false; doubao_tts_cancel(); }
    if (replace_read) { replace_read=false; assert(doubao_tts_start("replacement")); }
    if (!consumed) {
        if (offset==queued) { now+=1000000; return 0; }
        consumed=rd(wire+offset); offset+=4; frame_pos=0;
    }
    /* Each protocol message is also fragmented across two WS frames. */
    if (!frame_pos || frame_pos==frame_size) {
        frame_opcode=frame_pos?0:(fault == 5 ? 1 : 2); frame_size=frame_pos?consumed:1;
        frame_fin=frame_pos!=0; frame_pos=0;
    }
    size_t n=frame_size-frame_pos; if(n>split)n=split; if(n>(size_t)capacity)n=capacity;
    memcpy(out,wire+offset,n); offset+=n; frame_pos+=n; consumed-=n;
    return n;
}
int esp_transport_ws_get_read_opcode(esp_transport_handle_t t) { (void)t; return frame_opcode; }
int esp_transport_ws_get_read_payload_len(esp_transport_handle_t t) { (void)t; return fault == 6 ? JSON_CAP+1 : (int)frame_size; }
bool esp_transport_ws_get_fin_flag(esp_transport_handle_t t) { (void)t; return frame_fin; }
int esp_transport_close(esp_transport_handle_t t) { (void)t; ++closes; return 0; }
int esp_transport_destroy(esp_transport_handle_t t) { (void)t; return 0; }
static bool run(const char *text)
{
    queued=offset=consumed=frame_size=frame_pos=drained=0; tasks=0; sent_text[0]=0;
    s_jobs=(void *)1; s_head=s_size=s_peak=0; s_status=DOUBAO_TTS_RUNNING; ++s_epoch;
    job_t job={.epoch=s_epoch}; assert(strlen(text)<sizeof(job.text)); strcpy(job.text,text);
    request_t r={.epoch=job.epoch,.deadline=now+REQUEST_US};
    unsigned before=opens;
    bool ok=synthesize(&job,&r); assert(opens==before+1); return ok;
}
int main(void)
{
    s_pcm=malloc(PCM_CAP); s_json=malloc(JSON_CAP); assert(s_pcm && s_json);
    for (split=1;split<80;++split) { assert(run("synthetic")); assert(s_size==4 && tasks==1); }
    split=1024;
    char utf8[901]; for(size_t i=0;i<900;i+=3)memcpy(utf8+i,"语",3); utf8[900]=0;
    assert(run(utf8) && tasks==4 && !strcmp(utf8,sent_text));
    audio_size=100000; assert(run("stream")); assert(s_size==100000);
    assert(run(utf8) && drained+s_size==400000 && s_peak==PCM_CAP && drained>0);
    cancel_wait=true; assert(!run(utf8)); cancel_wait=false;
    audio_size=4;
    for(fault=1;fault<=8;++fault) assert(!run("synthetic")); fault=0;
    audio_size=0; assert(!run("synthetic")); audio_size=3; assert(!run("synthetic")); assert(run(utf8) && s_size==12); audio_size=4;
    status=302; assert(!run("synthetic")); assert(strstr(last_diagnostic,"phase=connect http_status=302")); status=101;
    short_event=200; assert(!run("synthetic")); short_event=0;
    cancel_read=true; assert(!run("synthetic") && !s_size);
    replace_read=true; assert(!run("synthetic") && !s_size);
    assert(closes==opens && cancels>0);
    free(s_pcm); free(s_json); s_pcm=NULL; s_json=NULL; s_jobs=NULL;
    fail_allocation=true; assert(!doubao_tts_start("allocation test"));
    assert(strstr(last_diagnostic,"start_failed reason=allocate pcm=0 scratch=0"));
    assert(!s_jobs && !s_pcm && !s_json); fail_allocation=false;
    fail_task=true; assert(!doubao_tts_start("task test"));
    assert(strstr(last_diagnostic,"start_failed reason=allocate pcm=1 scratch=1"));
    assert(!s_jobs && !s_pcm && !s_json); fail_task=false;
    assert(!doubao_tts_start("")); assert(strstr(last_diagnostic,"reason=invalid_request"));
#if CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
    /* Reproduce the board: internal stack allocation fails, PSRAM still has
     * space. The real worker initialization must choose external stack caps. */
    internal_stack_unavailable=true;
    assert(doubao_tts_start("fragmented internal RAM test"));
    assert(s_jobs && s_pcm && s_json);
    free(s_pcm); free(s_json); s_pcm=NULL; s_json=NULL; s_jobs=NULL;
#endif
    puts("WebSocket worker synthetic tests passed");
}
