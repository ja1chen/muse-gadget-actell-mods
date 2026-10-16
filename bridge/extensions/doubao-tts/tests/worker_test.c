/* SPDX-License-Identifier: Apache-2.0. Synthetic HTTPS responses, no sockets. */
#include <assert.h>
#include "../components/doubao_tts/doubao_tts.c"

static const char *wire;
static size_t position, fragment, drain_bytes;
static unsigned opens, reads, waits;
static int http_status = 200;
static bool complete = true, cancel_on_read, replace_on_read, cancel_on_wait;
static int64_t fake_time;
void *esp_crt_bundle_attach = (void *)1;

void fake_log(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
void esp_log_level_set(const char *tag, int level) { (void)tag; (void)level; }
int64_t esp_timer_get_time(void) { return fake_time; }
void esp_fill_random(void *out, size_t n) { memset(out, 0, n); }
void *heap_caps_malloc(size_t n, unsigned caps) { (void)caps; return malloc(n); }
size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return 123456; }
QueueHandle_t xQueueCreate(unsigned n, unsigned size) { (void)n; (void)size; return (void *)1; }
void vQueueDelete(QueueHandle_t q) { (void)q; }
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, int pri, void *handle)
{ (void)fn; (void)name; (void)stack; (void)arg; (void)pri; (void)handle; return pdPASS; }
int xTaskCreateWithCaps(void (*fn)(void *), const char *name, unsigned stack, void *arg, int pri, void *handle, unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return xTaskCreate(fn,name,stack,arg,pri,handle);
}
int xQueueOverwrite(QueueHandle_t q, const void *job) { (void)q; (void)job; return pdPASS; }
int xQueueReceive(QueueHandle_t q, void *job, unsigned wait) { (void)q; (void)job; (void)wait; return 0; }
void vTaskDelay(unsigned ms)
{
    fake_time += (int64_t)ms * 1000;
    ++waits;
    if (cancel_on_wait) doubao_tts_cancel();
    else {
        int16_t pcm[512];
        size_t n = doubao_tts_read(pcm, 512);
        for (size_t i = 0; i < n; ++i) assert(pcm[i] == 0);
        drain_bytes += n * 2;
    }
}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg)
{
    assert(!strcmp(cfg->url, URL) && cfg->crt_bundle_attach && cfg->disable_auto_redirect);
    return (void *)1;
}
int esp_http_client_cleanup(esp_http_client_handle_t c) { (void)c; return 0; }
int esp_http_client_set_header(esp_http_client_handle_t c, const char *k, const char *v)
{ (void)c; assert(k && v); if (!strcmp(k, "X-Api-Resource-Id")) assert(!strcmp(v, "seed-tts-2.0")); return 0; }
int esp_http_client_open(esp_http_client_handle_t c, int n) { (void)c; assert(n > 0); ++opens; return ESP_OK; }
int esp_http_client_write(esp_http_client_handle_t c, const char *p, int n)
{
    (void)c;
    cJSON *j = cJSON_Parse(p);
    assert(j);
    cJSON *params = cJSON_GetObjectItem(j, "req_params");
    assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(params, "speaker")), "zh_female_vv_uranus_bigtts"));
    cJSON *audio = cJSON_GetObjectItem(params, "audio_params");
    assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(audio, "format")), "pcm"));
    assert(cJSON_GetObjectItem(audio, "sample_rate")->valueint == 16000);
    cJSON_Delete(j);
    return n;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c) { (void)c; return 0; }
int esp_http_client_get_status_code(esp_http_client_handle_t c) { (void)c; return http_status; }
int esp_http_client_read(esp_http_client_handle_t c, char *p, int capacity)
{
    (void)c; ++reads;
    if (cancel_on_read) { cancel_on_read = false; doubao_tts_cancel(); }
    if (replace_on_read) { replace_on_read = false; assert(doubao_tts_start("new synthetic turn")); }
    size_t n = strlen(wire) - position;
    if (n > fragment) n = fragment;
    if (n > (size_t)capacity) n = (size_t)capacity;
    memcpy(p, wire + position, n); position += n;
    return (int)n;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c) { (void)c; return complete; }

static bool run(const char *response, size_t split)
{
    wire = response; position = 0; fragment = split;
    s_jobs = (void *)1; s_head = s_size = s_peak = 0;
    s_status = DOUBAO_TTS_RUNNING; ++s_epoch;
    job_t job = {.epoch = s_epoch, .text = "synthetic test"};
    request_t r = {.epoch = job.epoch, .deadline = fake_time + REQUEST_US};
    return synthesize(&job, &r);
}

int main(void)
{
    s_pcm = malloc(PCM_CAP); s_json = malloc(JSON_CAP);
    assert(s_pcm && s_json);
    const char *valid = "{\"code\":0,\"data\":\"AA==\"}\n{\"code\":0,\"data\":\"AQI=\"}"
                        "{\"code\":0,\"data\":\"Aw==\"}\n{\"code\":20000000,\"data\":null}";
    for (size_t split = 1; split < strlen(valid); ++split) {
        unsigned before = opens;
        assert(run(valid, split) && opens == before + 1);
        int16_t pcm[4] = {0};
        assert(doubao_tts_read(pcm, 4) == 2);
        assert((uint16_t)pcm[0] == 0x0100 && (uint16_t)pcm[1] == 0x0302);
    }
    assert(run("{\"code\":0,\"data\":\"AAAAAA==\"}", 1));
    const char *bad[] = {
        "{\"code\":401,\"data\":\"AAAA\"}", "{\"code\":0,\"code\":0,\"data\":\"AAAA\"}",
        "{\"code\":0,\"data\":\"AAA!\"}", "{\"code\":0,\"data\":\"AA==\"}",
        "{\"code\":0,\"data\":\"AAAAAA==\"", "{}", "{\"code\":0,\"data\":{}}",
        "{\"code\":20000000}{\"code\":0,\"data\":\"AAAAAA==\"}",
    };
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        unsigned before = opens;
        assert(!run(bad[i], 7) && opens == before + 1); /* no retries */
    }
    http_status = 302; unsigned before = reads;
    assert(!run(valid, 7) && reads == before); http_status = 200;
    complete = false; assert(!run(valid, 7)); complete = true;
    cancel_on_read = true;
    assert(!run(valid, 1024) && s_size == 0 && doubao_tts_state() == DOUBAO_TTS_IDLE);
    replace_on_read = true;
    assert(!run(valid, 1024) && s_size == 0 && doubao_tts_state() == DOUBAO_TTS_RUNNING);

    /* Many independent records exceed the ring; fake consumer checks no dropped bytes. */
    char *many = malloc(500000); assert(many); size_t used = 0;
    const char *part = "{\"code\":0,\"data\":\"AAAAAAAAAAAAAAAA\"}\n";
    for (int i = 0; i < 20000; ++i) {
        if (used + strlen(part) + 1 > 500000) break;
        memcpy(many + used, part, strlen(part)); used += strlen(part);
    }
    many[used] = 0;
    size_t expected = used / strlen(part) * 12;
    drain_bytes = waits = 0;
    assert(run(many, 1024) && waits > 0 && s_peak == PCM_CAP);
    assert(drain_bytes + s_size == expected);
    cancel_on_wait = true;
    assert(!run(many, 1024) && s_size == 0);
    free(many); free(s_pcm); free(s_json);
    puts("TTS worker: fragmented HTTPS, PCM continuity, errors, cancellation, replacement and backpressure passed");
}
