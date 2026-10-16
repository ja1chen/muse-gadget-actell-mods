/* SPDX-License-Identifier: Apache-2.0 */
#include "sdkconfig.h"
#if CONFIG_DOUBAO_TTS && CONFIG_DOUBAO_TTS_BIDIRECTIONAL
#if CONFIG_WS_BUFFER_SIZE < 4096
#error "Bidirectional TTS requires CONFIG_WS_BUFFER_SIZE >= 4096"
#endif
#include "doubao_ws.h"
#include "doubao_protocol.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_transport.h"
#include "esp_transport_ssl.h"
#include "esp_transport_ws.h"
#include <stdio.h>
#include <string.h>

#define WS_HOST "openspeech.bytedance.com"
#define WS_PATH "/api/v3/tts/bidirection"
#define WS_RESOURCE "seed-tts-2.0"
#define WS_SPEAKER "zh_female_vv_uranus_bigtts"
#define WS_IDLE_US (15LL * 1000000)

static void uuid(char out[37])
{
    uint8_t b[16];
    esp_fill_random(b, sizeof(b));
    b[6] = (b[6] & 15) | 0x40; b[8] = (b[8] & 63) | 0x80;
    snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15]);
}

static char *request_json(uint32_t event, const char *text)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON *params = cJSON_AddObjectToObject(root, "req_params");
    bool ok = params && cJSON_AddNumberToObject(root, "event", event) &&
        cJSON_AddStringToObject(root, "namespace", "BidirectionalTTS");
    if (ok && event == DOUBAO_START_SESSION) {
        cJSON *audio = cJSON_AddObjectToObject(params, "audio_params");
        ok = audio && cJSON_AddStringToObject(params, "speaker", WS_SPEAKER) &&
            cJSON_AddStringToObject(audio, "format", "pcm") &&
            cJSON_AddNumberToObject(audio, "sample_rate", 16000) &&
            cJSON_AddStringToObject(params, "additions",
                "{\"disable_markdown_filter\":true,\"disable_emoji_filter\":true}");
    }
    if (ok && text) ok = cJSON_AddStringToObject(params, "text", text) != NULL;
    char *json = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    return json;
}

static bool send_event(esp_transport_handle_t ws, uint32_t event,
                        const char *session, const char *json, int timeout)
{
    /* ESP-IDF masks this buffer in place; it must be writable. A short write
     * aborts this connection, never retries a partial synthesis request. */
    uint8_t packet[2048];
    size_t n = doubao_pack(packet, sizeof(packet), event, session, json);
    return n && esp_transport_ws_send_raw(ws,
        WS_TRANSPORT_OPCODES_BINARY | WS_TRANSPORT_OPCODES_FIN,
        (char *)packet, (int)n, timeout) == (int)n;
}

static bool send_json(esp_transport_handle_t ws, uint32_t event, const char *session, const char *text)
{
    char *json = request_json(event, text);
    if (!json) return false;
    bool ok = send_event(ws, event, session, json, 1000);
    cJSON_free(json);
    return ok;
}

/* Control payloads must be valid objects; use a local error pointer so cJSON
 * parsing can run alongside Muse's session task without its global error API. */
static bool control_json(const doubao_packet *packet, unsigned *service_code)
{
    if (!packet->payload_size || memchr(packet->payload, 0, packet->payload_size)) return false;
    const char *end = NULL;
    cJSON *json = cJSON_ParseWithLengthOpts((const char *)packet->payload,
        packet->payload_size, &end, false);
    bool ok = cJSON_IsObject(json);
    const char *limit = (const char *)packet->payload + packet->payload_size;
    while (ok && end < limit && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t')) ++end;
    ok = ok && end == limit;
    if (ok && (packet->event == 51 || packet->event == 153)) {
        cJSON *code = cJSON_GetObjectItemCaseSensitive(json, "status_code");
        if (!code) code = cJSON_GetObjectItemCaseSensitive(json, "code");
        if (cJSON_IsNumber(code) && code->valuedouble >= 0 && code->valuedouble <= 99999999 &&
            code->valuedouble == (unsigned)code->valuedouble) *service_code = (unsigned)code->valuedouble;
    }
    cJSON_Delete(json);
    return ok;
}

bool doubao_ws_synthesize(const char *text, void *context, doubao_active_fn active,
                          tts_bytes_fn audio, uint8_t *scratch, size_t capacity)
{
    if (!text || !*text || !scratch || !capacity || !active(context)) return false;
    /* Even an ERROR-level upgrade-write failure can dump the full request
     * (including X-Api-Key). Emit only our bounded numeric diagnostics. */
    esp_log_level_set("transport_ws", ESP_LOG_NONE);
    esp_transport_handle_t tls = esp_transport_ssl_init(), ws = NULL;
    bool ok = false, connected = false, session_started = false, finish_sent = false;
    const char *phase = "allocate";
    unsigned event = 0, wire_type = 0, serialization = 0, service_code = 0;
    size_t received = 0;
    char session[37], connection[37], headers[1024];
    uuid(session); uuid(connection);
    int size = snprintf(headers, sizeof(headers),
        "X-Api-Key: %s\r\nX-Api-Resource-Id: %s\r\nX-Api-Connect-Id: %s\r\n",
        CONFIG_DOUBAO_TTS_API_KEY, WS_RESOURCE, connection);
    if (!tls || size < 0 || (size_t)size >= sizeof(headers)) goto cleanup;
    phase = "configure";
    esp_transport_ssl_crt_bundle_attach(tls, esp_crt_bundle_attach);
    ws = esp_transport_ws_init(tls);
    if (!ws) goto cleanup;
    esp_transport_ws_config_t cfg = {.ws_path = WS_PATH, .headers = headers,
        .propagate_control_frames = false};
    if (esp_transport_ws_set_config(ws, &cfg) != ESP_OK || !active(context)) goto cleanup;
    phase = "connect";
    if (esp_transport_connect(ws, WS_HOST, 443, 5000) < 0 ||
        esp_transport_ws_get_upgrade_request_status(ws) != 101) goto cleanup;
    connected = true;
    phase = "start_connection";
    if (!active(context) || !send_event(ws, DOUBAO_START_CONNECTION, NULL, "{}", 1000)) goto cleanup;

    enum { WAIT_CONNECTION, WAIT_SESSION, STREAM } stage = WAIT_CONNECTION;
    size_t used = 0, frame_left = 0, text_offset = 0;
    bool message_open = false, frame_fin = false;
    int64_t last_data = esp_timer_get_time();
    while (active(context)) {
        if (stage == STREAM && !finish_sent) {
            size_t n = strlen(text + text_offset);
            if (n) {
                /* Feed UTF-8 pieces, without sentence splitting, inside this
                 * session. Poll inbound audio between sends for full duplex. */
                if (n > 256) {
                    n = 256;
                    while (n && ((unsigned char)text[text_offset+n] & 0xc0) == 0x80) --n;
                }
                if (!n) goto cleanup;
                char chunk[257]; memcpy(chunk, text + text_offset, n); chunk[n] = 0;
                phase = "send_text";
                if (!active(context) || !send_json(ws, DOUBAO_TASK, session, chunk)) goto cleanup;
                text_offset += n;
            } else {
                phase = "finish_session";
                if (!send_event(ws, DOUBAO_FINISH_SESSION, session, "{}", 1000)) goto cleanup;
                finish_sent = true;
            }
            int ready = esp_transport_poll_read(ws, 0);
            if (ready < 0) goto cleanup;
            if (!ready && !finish_sent) continue;
        }
        char bytes[1024];
        phase = stage == WAIT_CONNECTION ? "receive_connection" :
                stage == WAIT_SESSION ? "receive_session" : "receive_audio";
        int n = esp_transport_read(ws, bytes, sizeof(bytes), 1000);
        if (n < 0) goto cleanup;
        if (!n) {
            if (esp_timer_get_time() - last_data >= WS_IDLE_US) { phase = "idle_timeout"; goto cleanup; }
            continue; /* includes automatically handled WebSocket ping/pong */
        }
        if (!active(context)) goto cleanup;
        phase = "frame";
        if (!frame_left) {
            int opcode = esp_transport_ws_get_read_opcode(ws);
            int total = esp_transport_ws_get_read_payload_len(ws);
            if (total < n || (size_t)total > capacity - used) goto cleanup;
            if (opcode == WS_TRANSPORT_OPCODES_BINARY && !message_open) message_open = true;
            else if (opcode != WS_TRANSPORT_OPCODES_CONT || !message_open) goto cleanup;
            frame_left = (size_t)total;
            frame_fin = esp_transport_ws_get_fin_flag(ws);
        }
        if ((size_t)n > frame_left || (size_t)n > capacity - used) goto cleanup;
        memcpy(scratch + used, bytes, (size_t)n); used += (size_t)n; frame_left -= (size_t)n;
        last_data = esp_timer_get_time();
        if (frame_left || !frame_fin) continue;
        phase = "protocol";
        if (used >= 4 && scratch[0] == 0x11) {
            wire_type = scratch[1] >> 4;
            serialization = scratch[2] >> 4;
            if (wire_type == 15 && used >= 12) {
                unsigned code = (unsigned)scratch[4] << 24 | (unsigned)scratch[5] << 16 |
                                (unsigned)scratch[6] << 8 | scratch[7];
                if (code <= 99999999) service_code = code;
            }
        }
        doubao_packet packet;
        if (!doubao_unpack(scratch, used, &packet)) goto cleanup;
        used = 0; message_open = false; event = packet.event;
        phase = "control_json";
        if (packet.type == 9 && !control_json(&packet, &service_code)) goto cleanup;
        phase = "event";
        if (stage == WAIT_CONNECTION) {
            if (packet.type != 9 || packet.event != DOUBAO_CONNECTION_STARTED ||
                !send_json(ws, DOUBAO_START_SESSION, session, NULL)) goto cleanup;
            stage = WAIT_SESSION;
            continue;
        }
        phase = "session_id";
        if (packet.id_size != strlen(session) || memcmp(packet.id, session, packet.id_size)) goto cleanup;
        phase = "event";
        if (stage == WAIT_SESSION) {
            if (packet.type != 9 || packet.event != DOUBAO_SESSION_STARTED) goto cleanup;
            session_started = true; stage = STREAM;
            continue;
        }
        if (packet.type == 11 && packet.event == DOUBAO_AUDIO && packet.payload_size) {
            phase = "audio";
            if (!audio(context, packet.payload, packet.payload_size)) goto cleanup;
            received += packet.payload_size;
            last_data = esp_timer_get_time(); /* exclude local backpressure */
        } else if (packet.type == 9 && packet.event == DOUBAO_SESSION_FINISHED && finish_sent) {
            phase = "finished";
            ok = received && !(received & 1) && active(context);
            break;
        } else if (packet.type != 9 || (packet.event != DOUBAO_SENTENCE_START &&
            packet.event != DOUBAO_SENTENCE_END && packet.event != DOUBAO_SUBTITLE &&
            packet.event != DOUBAO_USAGE)) goto cleanup;
    }
cleanup:
    ESP_LOGI("doubao_ws", "phase=%s http_status=%d event=%u wire_type=%u serialization=%u service_code=%u pcm_bytes=%u active=%d",
        phase, ws ? esp_transport_ws_get_upgrade_request_status(ws) : 0,
        event, wire_type, serialization, service_code, (unsigned)received, active(context));
    if (ws && connected) {
        if (!ok && session_started) (void)send_event(ws, DOUBAO_CANCEL_SESSION, session, "{}", 500);
        (void)send_event(ws, DOUBAO_FINISH_CONNECTION, NULL, "{}", 500);
    }
    if (ws) { esp_transport_close(ws); esp_transport_destroy(ws); }
    if (tls) esp_transport_destroy(tls);
    memset(headers, 0, sizeof(headers));
    return ok;
}
#endif
