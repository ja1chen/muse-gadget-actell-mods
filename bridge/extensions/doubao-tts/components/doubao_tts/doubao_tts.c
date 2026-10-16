/* SPDX-License-Identifier: Apache-2.0 */
#include "sdkconfig.h"
#include "doubao_tts.h"

#if CONFIG_DOUBAO_TTS
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#if !CONFIG_DOUBAO_TTS_BIDIRECTIONAL
#include "esp_http_client.h"
#else
#include "doubao_ws.h"
#endif
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "tts_stream.h"

#define PCM_CAP (128 * 1024)
#define JSON_CAP (128 * 1024)
#define TEXT_CAP 1024
#if CONFIG_DOUBAO_TTS_BIDIRECTIONAL
#define WORKER_STACK 12288
#else
#define WORKER_STACK 8192
#endif
#define REQUEST_US (120LL * 1000000)
#define IDLE_US (15LL * 1000000)
#define URL "https://openspeech.bytedance.com/api/v3/tts/unidirectional"
#define RESOURCE_ID "seed-tts-2.0"
#define SPEAKER "zh_female_vv_uranus_bigtts"

typedef struct { uint32_t epoch; char text[TEXT_CAP]; } job_t;
typedef struct {
    uint32_t epoch;
    int64_t deadline;
    size_t received;
    bool terminal;
} request_t;

static const char *TAG = "doubao_tts";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t s_jobs;
static uint8_t *s_pcm;
static char *s_json;
static size_t s_head, s_size, s_peak;
static uint32_t s_epoch;
static doubao_tts_status s_status;

static bool active(const request_t *r)
{
    portENTER_CRITICAL(&s_lock);
    bool ok = r->epoch == s_epoch && s_status == DOUBAO_TTS_RUNNING;
    portEXIT_CRITICAL(&s_lock);
    return ok && esp_timer_get_time() < r->deadline;
}

/* A full ring stops network reads and applies TCP backpressure. Never drop PCM. */
static bool audio(void *context, const uint8_t *bytes, size_t size)
{
    request_t *r = context;
    while (size && active(r)) {
        portENTER_CRITICAL(&s_lock);
        size_t n = r->epoch == s_epoch ? PCM_CAP - s_size : 0;
        if (n > size) n = size;
        if (n > 1024) n = 1024;
        size_t tail = (s_head + s_size) % PCM_CAP;
        if (n > PCM_CAP - tail) n = PCM_CAP - tail;
        if (n) {
            memcpy(s_pcm + tail, bytes, n);
            s_size += n;
            if (s_size > s_peak) s_peak = s_size;
        }
        portEXIT_CRITICAL(&s_lock);
        bytes += n;
        size -= n;
        r->received += n;
        if (!n) vTaskDelay(pdMS_TO_TICKS(10));
    }
    return !size && active(r);
}

#if !CONFIG_DOUBAO_TTS_BIDIRECTIONAL
static bool record(void *context, const char *json, size_t size)
{
    request_t *r = context;
    if (!active(r) || r->terminal) return false;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, size + 1, &end, true);
    if (!root || !cJSON_IsObject(root)) { cJSON_Delete(root); return false; }
    /* Reject duplicate envelope fields rather than interpreting ambiguous data. */
    unsigned codes = 0, datas = 0;
    for (cJSON *p = root->child; p; p = p->next) {
        if (p->string && !strcmp(p->string, "code")) ++codes;
        if (p->string && !strcmp(p->string, "data")) ++datas;
    }
    cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    bool ok = codes == 1 && datas <= 1 && cJSON_IsNumber(code) &&
              (code->valuedouble == 0 || code->valuedouble == 20000000);
    if (ok && data && !cJSON_IsNull(data)) {
        const char *encoded = cJSON_GetStringValue(data);
        ok = encoded && tts_base64_decode(encoded, strlen(encoded), audio, r);
    }
    if (ok && code->valuedouble == 20000000) r->terminal = true;
    /* Never log cloud messages, response bodies, credentials or reply text. */
    cJSON_Delete(root);
    return ok;
}

static char *body(const char *text)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON *params = cJSON_AddObjectToObject(root, "req_params");
    cJSON *format = params ? cJSON_AddObjectToObject(params, "audio_params") : NULL;
    bool ok = format && cJSON_AddStringToObject(params, "text", text) &&
        cJSON_AddStringToObject(params, "speaker", SPEAKER) &&
        cJSON_AddStringToObject(format, "format", "pcm") &&
        cJSON_AddNumberToObject(format, "sample_rate", 16000) &&
        cJSON_AddStringToObject(params, "additions",
            "{\"disable_markdown_filter\":true,\"disable_emoji_filter\":true}");
    char *result = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    return result;
}

static bool synthesize(job_t *job, request_t *r)
{
    if (!active(r)) return false;
    char *payload = body(job->text);
    if (!payload) return false;
    /* Suppress HTTP header debug dumps, including X-Api-Key. */
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_WARN);
    esp_http_client_config_t cfg = {
        .url = URL, .method = HTTP_METHOD_POST, .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach, .disable_auto_redirect = true,
        .buffer_size = 2048, .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    bool ok = false;
    if (!client) goto cleanup;
    uint8_t uuid[16];
    esp_fill_random(uuid, sizeof(uuid));
    uuid[6] = (uuid[6] & 15) | 0x40;
    uuid[8] = (uuid[8] & 63) | 0x80;
    char request_id[37];
    snprintf(request_id, sizeof(request_id),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid[0],uuid[1],uuid[2],uuid[3],uuid[4],uuid[5],uuid[6],uuid[7],
        uuid[8],uuid[9],uuid[10],uuid[11],uuid[12],uuid[13],uuid[14],uuid[15]);
    if (esp_http_client_set_header(client, "Content-Type", "application/json") != ESP_OK ||
        esp_http_client_set_header(client, "X-Api-Key", CONFIG_DOUBAO_TTS_API_KEY) != ESP_OK ||
        esp_http_client_set_header(client, "X-Api-Resource-Id", RESOURCE_ID) != ESP_OK ||
        esp_http_client_set_header(client, "X-Api-Request-Id", request_id) != ESP_OK ||
        !active(r) || esp_http_client_open(client, (int)strlen(payload)) != ESP_OK) goto cleanup;
    size_t sent = 0, total = strlen(payload);
    while (sent < total && active(r)) {
        int n = esp_http_client_write(client, payload + sent, (int)(total - sent));
        if (n <= 0) goto cleanup;
        sent += (size_t)n;
    }
    if (sent != total || !active(r) || esp_http_client_fetch_headers(client) < 0 ||
        esp_http_client_get_status_code(client) != 200) goto cleanup;
    tts_json_stream stream;
    tts_json_init(&stream, s_json, JSON_CAP);
    int64_t last_data = esp_timer_get_time();
    char chunk[1024];
    while (active(r)) {
        int n = esp_http_client_read(client, chunk, sizeof(chunk));
        if (n == -ESP_ERR_HTTP_EAGAIN) {
            if (esp_timer_get_time() - last_data >= IDLE_US) break;
            continue;
        }
        if (n < 0) break;
        if (!n) {
            ok = esp_http_client_is_complete_data_received(client) &&
                tts_json_complete(&stream) && r->received && !(r->received & 1);
            break;
        }
        if (!tts_json_feed(&stream, chunk, (size_t)n, record, r)) break;
        /* Local backpressure time does not count as a network stall. */
        last_data = esp_timer_get_time();
    }
cleanup:
    if (client) esp_http_client_cleanup(client);
    cJSON_free(payload);
    return ok && active(r);
}
#else
static bool ws_active(void *context) { return active(context); }
static bool synthesize(job_t *job, request_t *r)
{
    return doubao_ws_synthesize(job->text, r, ws_active, audio, (uint8_t *)s_json, JSON_CAP);
}
#endif

static void worker(void *unused)
{
    (void)unused;
    job_t job;
    for (;;) {
        if (xQueueReceive(s_jobs, &job, portMAX_DELAY) != pdTRUE) continue;
        int64_t started = esp_timer_get_time();
        request_t r = {.epoch = job.epoch, .deadline = started + REQUEST_US};
        bool ok = synthesize(&job, &r);
        memset(job.text, 0, sizeof(job.text));
        portENTER_CRITICAL(&s_lock);
        bool current = job.epoch == s_epoch;
        size_t peak = s_peak;
        if (current) {
            s_status = ok ? DOUBAO_TTS_DONE : DOUBAO_TTS_ERROR;
            if (!ok) s_head = s_size = 0;
        }
        portEXIT_CRITICAL(&s_lock);
        if (current) ESP_LOGI(TAG, "result=%s elapsed_ms=%ld pcm_bytes=%u ring_peak=%u internal_free=%u",
            ok ? "ok" : "failed", (long)((esp_timer_get_time() - started) / 1000),
            (unsigned)r.received, (unsigned)peak,
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
}

/* Only the session task initializes and submits work. The worker owns the network transport. */
bool doubao_tts_start(const char *text)
{
    if (!text || !text[0] || strnlen(text, TEXT_CAP) >= TEXT_CAP ||
        !CONFIG_DOUBAO_TTS_API_KEY[0] ||
        strpbrk(CONFIG_DOUBAO_TTS_API_KEY, "\r\n")) {
        ESP_LOGI(TAG, "start_failed reason=invalid_request");
        return false;
    }
    if (!s_jobs) {
        s_pcm = heap_caps_malloc(PCM_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_json = heap_caps_malloc(JSON_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_jobs = xQueueCreate(1, sizeof(job_t));
        bool task_started = false;
        if (s_pcm && s_json && s_jobs) {
#if CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
            /* Like Muse's existing TLS task, this worker never writes flash.
             * Keep its contiguous stack out of fragmented internal RAM; the
             * FreeRTOS control block remains internal. */
            task_started = xTaskCreateWithCaps(worker, "doubao_tts", WORKER_STACK, NULL, 4, NULL,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS;
#else
            task_started = xTaskCreate(worker, "doubao_tts", WORKER_STACK, NULL, 4, NULL) == pdPASS;
#endif
        }
        if (!task_started) {
            ESP_LOGI(TAG, "start_failed reason=allocate pcm=%d scratch=%d queue=%d internal_free=%u psram_free=%u",
                s_pcm != NULL, s_json != NULL, s_jobs != NULL,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (s_jobs) vQueueDelete(s_jobs);
            free(s_pcm); free(s_json);
            s_pcm = NULL; s_json = NULL; s_jobs = NULL;
            return false;
        }
    }
    job_t job = {0};
    memcpy(job.text, text, strlen(text) + 1);
    portENTER_CRITICAL(&s_lock);
    job.epoch = ++s_epoch;
    s_head = s_size = s_peak = 0;
    s_status = DOUBAO_TTS_RUNNING;
    portEXIT_CRITICAL(&s_lock);
    return xQueueOverwrite(s_jobs, &job) == pdPASS;
}

void doubao_tts_cancel(void)
{
    portENTER_CRITICAL(&s_lock);
    ++s_epoch;
    s_status = DOUBAO_TTS_IDLE;
    s_head = s_size = 0;
    portEXIT_CRITICAL(&s_lock);
}

size_t doubao_tts_read(int16_t *pcm, size_t frames)
{
    /* Keep critical sections short; preserve a split PCM sample across records. */
    if (frames > 512) frames = 512;
    portENTER_CRITICAL(&s_lock);
    size_t n = s_size < frames * 2 ? s_size & ~(size_t)1 : frames * 2;
    size_t first = n < PCM_CAP - s_head ? n : PCM_CAP - s_head;
    if (n) {
        memcpy(pcm, s_pcm + s_head, first);
        memcpy((uint8_t *)pcm + first, s_pcm, n - first);
        s_head = (s_head + n) % PCM_CAP;
        s_size -= n;
    }
    portEXIT_CRITICAL(&s_lock);
    return n / 2;
}

doubao_tts_status doubao_tts_state(void)
{
    portENTER_CRITICAL(&s_lock);
    doubao_tts_status status = s_status;
    portEXIT_CRITICAL(&s_lock);
    return status;
}
#else
bool doubao_tts_start(const char *text) { (void)text; return false; }
void doubao_tts_cancel(void) {}
size_t doubao_tts_read(int16_t *pcm, size_t frames) { (void)pcm; (void)frames; return 0; }
doubao_tts_status doubao_tts_state(void) { return DOUBAO_TTS_IDLE; }
#endif
