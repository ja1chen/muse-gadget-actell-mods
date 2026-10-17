/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_lan.h"
#include "actell_buffer.h"
#include "esp_http_client.h"
#include "esp_random.h"
#include "lwip/inet.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "psa/crypto.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define LOCATOR_TTL_US (30LL * 1000000)
#define SESSION_IDLE_US (10LL * 60 * 1000000)
#define SESSION_REQUESTS 16
#define CACHED_CONNECT_MS 2000

typedef struct {
    actell_response *response;
    int64_t end;
    uint64_t generation;
    uint32_t network_epoch;
    bool failed;
    const char *failure;
} sink;
typedef struct {
    esp_http_client_handle_t http;
    sink receiver;
    /* ESP transport retains these pointers for the life of the client. */
    char pem[4096], hostname[256];
    struct ifreq iface;
    int64_t last_used;
    uint64_t owner_generation; /* Business owner; hello has its own request fence. */
    unsigned requests;
} http_session;

/* Worker-only. Address caching and live TLS ownership have separate lifetimes.
 * The idle slot never grants authorization or retains a response/body pointer. */
static actell_http_locator cached;
static actell_http_locator idle;

void actell_http_close_idle(void) {
    actell_http_locator_close(&idle);
    memset(&idle, 0, sizeof(idle));
}

void actell_http_locator_close(actell_http_locator *locator) {
    http_session *session = locator ? locator->session : NULL;
    if (!session) return;
    session->receiver.response = NULL;
    esp_http_client_close(session->http);
    esp_http_client_cleanup(session->http);
    free(session);
    locator->session = NULL;
}
void actell_http_invalidate(actell_http_locator *locator) {
    memset(&cached, 0, sizeof(cached));
    actell_http_close_idle();
    actell_http_locator_close(locator);
    if (locator) { locator->address[0] = 0; locator->located_at = 0; }
}
static bool network_equal(actell_network_stamp a, actell_network_stamp b) {
    return a.epoch == b.epoch && a.ip == b.ip && a.netmask == b.netmask && a.gateway == b.gateway;
}
static bool locator_matches(const actell_http_locator *l, const actell_config *c,
                            actell_network_stamp network) {
    return l->address[0] &&
        !strcmp(l->origin, c->origin) && !strcmp(l->client, c->client) &&
        !strcmp(l->fingerprint, c->fingerprint) && network_equal(l->network, network) &&
        actell_lan_address(inet_addr(l->address));
}
static bool locator_fresh(const actell_http_locator *l, const actell_config *c,
                          actell_network_stamp network, int64_t now) {
    return locator_matches(l, c, network) && now >= l->located_at &&
        now - l->located_at < LOCATOR_TTL_US;
}
static bool session_live(const actell_http_locator *l, actell_network_stamp network,
                         uint64_t generation, int64_t now) {
    const http_session *s = l->session;
    return s && !s->receiver.response && s->owner_generation == generation &&
        actell_bridge_current(generation) && network_equal(l->network, network) &&
        actell_lan_address(inet_addr(l->address)) && now >= s->last_used &&
        now - s->last_used < SESSION_IDLE_US && s->requests < SESSION_REQUESTS;
}
unsigned actell_http_idle_wait_ms(void) {
    http_session *s = idle.session;
    if (!s) return 30000;
    actell_network_stamp network;
    int64_t now = esp_timer_get_time();
    if (!actell_lan_network(&network) || !session_live(&idle, network, s->owner_generation, now)) {
        actell_http_close_idle();
        ESP_LOGI("actell.rpc", "phase=idle_close reason=expired_or_invalid");
        return 30000;
    }
    unsigned remaining = (unsigned)((SESSION_IDLE_US - (now - s->last_used) + 999) / 1000);
    return remaining < 30000 ? remaining : 30000; /* Pairing maintenance keeps its own cadence. */
}
void actell_http_locator_release(actell_http_locator *locator) {
    http_session *s = locator ? locator->session : NULL;
    if (!s) return;
    actell_http_locator_close(&idle);
    memset(&idle, 0, sizeof(idle));
    actell_network_stamp network;
    if (!actell_lan_network(&network) ||
        !session_live(locator, network, s->owner_generation, esp_timer_get_time())) {
        actell_http_locator_close(locator);
        return;
    }
    idle = *locator;
    idle.operation_id[0] = 0;
    locator->session = NULL; /* Ownership moves; never copy an owning pointer. */
    s->receiver.end = 0;
}
static bool current(const sink *s) {
    return esp_timer_get_time() < s->end && actell_bridge_current(s->generation) &&
        s->network_epoch == actell_lan_epoch();
}
static esp_err_t receive(esp_http_client_event_t *event) {
    sink *s = event->user_data;
    if (!s->response) return ESP_OK;
    if (!current(s)) {
        s->failed = true; s->failure = "session_or_deadline"; return ESP_FAIL;
    }
    if (event->event_id == HTTP_EVENT_ON_HEADER) {
#define HEADER(name, field) if (!strcasecmp(event->header_key, name)) { \
    if (strlen(event->header_value) >= sizeof(s->response->field)) { \
        s->failed = true; s->failure = "response_header"; return ESP_FAIL; } \
    strcpy(s->response->field, event->header_value); }
        HEADER("Content-Type", mime) HEADER("X-Actell-Request-Id", request_id)
        HEADER("X-Actell-Image-Id", image_id) HEADER("X-Actell-Generation", generation)
        HEADER("X-Actell-Width", width) HEADER("X-Actell-Height", height)
        HEADER("X-Actell-SHA256", digest) HEADER("X-Actell-Geometry-Revision", revision)
        HEADER("X-Actell-Image-Contract", contract)
#undef HEADER
    }
    return ESP_OK;
}
static bool sign_request(const actell_config *c, const char *id, const char *body,
                         char stamp[24], char nonce[37], char signature[65]) {
    unsigned char random[16], mac[32]; size_t length = 0;
    char prefix[360];
    esp_fill_random(random, sizeof(random));
    snprintf(nonce, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             random[0],random[1],random[2],random[3],random[4],random[5],random[6],random[7],
             random[8],random[9],random[10],random[11],random[12],random[13],random[14],random[15]);
    snprintf(stamp, 24, "%lld", (long long)time(NULL));
    int n = snprintf(prefix, sizeof(prefix), "ACTELL1\n%s\n%s\n%s\n%s\n", c->client,id,stamp,nonce);
    if (n < 0 || (size_t)n >= sizeof(prefix)) return false;
    psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key = 0;
    psa_mac_operation_t op = PSA_MAC_OPERATION_INIT;
    psa_set_key_type(&attrs, PSA_KEY_TYPE_HMAC);
    psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attrs, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    bool ok = psa_crypto_init() == PSA_SUCCESS &&
        psa_import_key(&attrs, c->secret, 32, &key) == PSA_SUCCESS &&
        psa_mac_sign_setup(&op, key, PSA_ALG_HMAC(PSA_ALG_SHA_256)) == PSA_SUCCESS &&
        psa_mac_update(&op, (const unsigned char *)prefix, n) == PSA_SUCCESS &&
        psa_mac_update(&op, (const unsigned char *)body, strlen(body)) == PSA_SUCCESS &&
        psa_mac_sign_finish(&op, mac, sizeof(mac), &length) == PSA_SUCCESS && length == 32;
    psa_mac_abort(&op);
    if (key) psa_destroy_key(key);
    psa_reset_key_attributes(&attrs);
    if (ok) for (unsigned i = 0; i < 32; i++) snprintf(signature + 2*i, 3, "%02x", mac[i]);
    return ok;
}
static int timeout_left(int64_t end) {
    int64_t left = (end - esp_timer_get_time()) / 1000;
    return left > 15000 ? 15000 : left > 0 ? (int)left : 1;
}
static bool operation_id_valid(const char *id) {
    if (strlen(id) != 64) return false;
    for (unsigned i = 0; i < 64; i++)
        if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return false;
    return true;
}

esp_err_t actell_http(const actell_config *c, const char *id, const char *body, bool image,
                     uint64_t generation, int64_t expires, actell_response *r) {
    return actell_http_with_locator(c,id,body,image,generation,expires,r,NULL);
}
esp_err_t actell_http_with_locator(const actell_config *c, const char *id, const char *body,
                                  bool image, uint64_t generation, int64_t expires,
                                  actell_response *r, actell_http_locator *invocation) {
    /* One worker and one socket. Hello may borrow a business socket, but never
     * changes its owner or extends its business-idle deadline. A cold hello
     * closes its own connection instead of creating a permanent idle pool. */
    actell_http_locator temporary = {0}, *locator = invocation ? invocation : &temporary;
    int64_t began = esp_timer_get_time(), end = began + 15000000;
    if (expires < end) end = expires;
    memset(r, 0, sizeof(*r)); r->expected_bytes = -1; r->transport_stage = "session_or_deadline";
    if (!actell_bridge_current(generation) || began >= end) {
        actell_http_invalidate(locator); return ESP_ERR_INVALID_STATE;
    }
    r->transport_stage = "clock";
    if (!actell_clock_ready()) { actell_http_invalidate(locator); return ESP_ERR_INVALID_STATE; }
    r->transport_stage = "request_identity";
    size_t blen = strlen(body);
    if (blen > ACTELL_REQUEST_MAX) return ESP_ERR_INVALID_SIZE;
    char hostname[256];
    const char *begin = c->origin + 8, *colon = strrchr(begin, ':');
    size_t host_len = colon ? (size_t)(colon - begin) : strlen(begin);
    if (!host_len || host_len >= sizeof(hostname)) return ESP_ERR_INVALID_ARG;
    memcpy(hostname, begin, host_len); hostname[host_len] = 0;
    int port = 443;
    if (colon) {
        char *tail; long value = strtol(colon + 1, &tail, 10);
        if (!colon[1] || *tail || value < 1 || value > 65535) return ESP_ERR_INVALID_ARG;
        port = (int)value;
    }
    const char *trace = operation_id_valid(locator->operation_id) ? locator->operation_id : "-";
    /* Only the headers/connect phase can fall back. A write attempt, even one
     * reporting -1, may have delivered bytes and is never replayed here. */
    for (unsigned attempt = 0; attempt < 2; attempt++) {
        int64_t discovery_ms = 0, connect_ms = 0, read_ms = 0;
        actell_network_stamp network;
        memset(r, 0, sizeof(*r)); r->expected_bytes = -1; r->transport_stage = "interface";
        if (!actell_lan_network(&network)) {
            actell_http_invalidate(locator); return ESP_ERR_INVALID_STATE;
        }
        int64_t now = esp_timer_get_time();
        bool borrowed = false;
        if (!locator->session && idle.session) {
            const http_session *pooled = idle.session;
            uint64_t owner = invocation ? generation : pooled->owner_generation;
            if (locator_matches(&idle, c, network) && session_live(&idle, network, owner, now)) {
                char operation[65]; memcpy(operation, locator->operation_id, sizeof(operation));
                *locator = idle; memcpy(locator->operation_id, operation, sizeof(operation));
                borrowed = true;
            } else actell_http_locator_close(&idle);
            memset(&idle, 0, sizeof(idle));
        }
        http_session *existing = locator->session;
        uint64_t owner = invocation || !existing ? generation : existing->owner_generation;
        if (existing && (!locator_matches(locator, c, network) ||
                         !session_live(locator, network, owner, now)))
            actell_http_locator_close(locator);
        unsigned cache_hit = 0;
        /* An authenticated, live socket does not expire with its discovery
         * record. TTL only controls locating a peer before a new connection. */
        if (locator->session || locator_fresh(locator, c, network, now)) cache_hit = 1;
        else {
            actell_http_locator_close(locator); locator->address[0] = 0;
            if (locator_fresh(&cached, c, network, now)) {
                char operation[65]; memcpy(operation, locator->operation_id, sizeof(operation));
                *locator = cached; memcpy(locator->operation_id, operation, sizeof(operation));
                cache_hit = 2;
            } else memset(&cached, 0, sizeof(cached));
        }
        if (!cache_hit) {
            r->transport_stage = "discovery";
            actell_candidate *found = calloc(SLAN_CANDIDATES, sizeof(*found));
            if (!found) return ESP_ERR_NO_MEM;
            unsigned count = 0; int64_t start = esp_timer_get_time();
            esp_err_t discovery = actell_lan_discover(found, &count, c);
            discovery_ms = (esp_timer_get_time() - start) / 1000;
            if (discovery == ESP_OK && count) strcpy(locator->address, found[0].address);
            free(found);
            if (!locator->address[0]) {
                actell_http_invalidate(locator);
                ESP_LOGW("actell.rpc", "phase=http_discovery operation_id=%s request_id=%s elapsed_ms=%lld",trace,id,(long long)discovery_ms);
                return ESP_ERR_NOT_FOUND;
            }
            strcpy(locator->origin,c->origin); strcpy(locator->client,c->client);
            strcpy(locator->fingerprint,c->fingerprint);
            locator->network = network; locator->located_at = esp_timer_get_time();
        }
        sink initial = {.response=r,.end=end,.generation=generation,.network_epoch=network.epoch};
        if (!current(&initial)) {
            r->transport_stage = "session_or_deadline";
            actell_http_invalidate(locator); return ESP_ERR_INVALID_STATE;
        }
        http_session *session = locator->session;
        bool reused = session != NULL;
        int connect_timeout = timeout_left(end);
        if (cache_hit && connect_timeout > CACHED_CONNECT_MS) connect_timeout = CACHED_CONNECT_MS;
        if (!session) {
            r->transport_stage = "client_init";
            session = calloc(1, sizeof(*session));
            if (!session) return ESP_ERR_NO_MEM;
            session->owner_generation = generation;
            strcpy(session->pem,c->pem); strcpy(session->hostname,hostname);
            if (!actell_lan_interface(&session->iface)) { free(session); return ESP_ERR_INVALID_STATE; }
            char url[280]; snprintf(url,sizeof(url),"https://%s:%d/v1/rpc",locator->address,port);
            esp_http_client_config_t config = {.url=url,.cert_pem=session->pem,.if_name=&session->iface,
                .common_name=session->hostname,.timeout_ms=connect_timeout,.disable_auto_redirect=true,
                .event_handler=receive,.user_data=&session->receiver,.keep_alive_enable=false,
                .skip_cert_common_name_check=false};
            session->http = esp_http_client_init(&config);
            if (!session->http) { free(session); return ESP_ERR_NO_MEM; }
            locator->session = session;
        }
        session->receiver = initial;
        esp_http_client_handle_t http = session->http;
        esp_err_t e = ESP_FAIL;
        char nonce[37] = {0}, stamp[24] = {0}, signature[65] = {0};
        r->transport_stage = "request_auth";
        bool headers_ok = sign_request(c,id,body,stamp,nonce,signature);
#define SET_HEADER(k,v) do { if (esp_http_client_set_header(http,k,v) != ESP_OK) headers_ok=false; } while (0)
        SET_HEADER("Host",c->origin+8); SET_HEADER("Content-Type","application/json");
        SET_HEADER("Connection",invocation || borrowed ? "keep-alive" : "close");
        SET_HEADER("X-Actell-Time",stamp); SET_HEADER("X-Actell-Nonce",nonce);
        SET_HEADER("X-Actell-Signature",signature);
        if (operation_id_valid(locator->operation_id)) SET_HEADER("X-Actell-Operation-Id",locator->operation_id);
        else {
            esp_err_t removed = esp_http_client_delete_header(http,"X-Actell-Operation-Id");
            if (removed != ESP_OK && removed != ESP_ERR_NOT_FOUND) headers_ok=false;
        }
#undef SET_HEADER
        if (esp_http_client_set_method(http,HTTP_METHOD_POST) != ESP_OK ||
            esp_http_client_set_timeout_ms(http,connect_timeout) != ESP_OK) headers_ok=false;
        bool connect_attempted = false, write_attempted = false;
        size_t sent = 0;
        actell_buffer received = {0};
        if (headers_ok && current(&session->receiver)) {
            r->transport_stage = reused ? "send_headers" : "tls_connect";
            int64_t start = esp_timer_get_time(); connect_attempted = true;
            /* IDF 6 open() reconnects unconditionally. These public APIs reset
             * the parser and send only headers on the already verified socket. */
            if (reused) {
                e = esp_http_client_prepare(http);
                if (e == ESP_OK) e = esp_http_client_request_send(http,(int)blen);
            } else e = esp_http_client_open(http,(int)blen);
            connect_ms = (esp_timer_get_time() - start) / 1000;
        }
        bool header_failure = connect_attempted && e != ESP_OK;
        if (e == ESP_OK) esp_http_client_set_timeout_ms(http,timeout_left(end));
        while (e == ESP_OK && sent < blen) {
            r->transport_stage = "send_request";
            if (!current(&session->receiver)) { r->transport_stage="session_or_deadline"; e=ESP_FAIL; break; }
            size_t chunk = blen - sent; if (chunk > 512) chunk = 512;
            write_attempted = true;
            esp_http_client_set_timeout_ms(http,timeout_left(end));
            int n = esp_http_client_write(http,body+sent,(int)chunk);
            if (n <= 0) { e=ESP_FAIL; break; }
            sent += (size_t)n;
        }
        if (e == ESP_OK && !current(&session->receiver)) { r->transport_stage="session_or_deadline"; e=ESP_FAIL; }
        size_t cap = image ? ACTELL_IMAGE_MAX : ACTELL_JSON_MAX;
        if (e == ESP_OK) {
            r->transport_stage = "read_headers";
            esp_http_client_set_timeout_ms(http,timeout_left(end));
            r->expected_bytes = esp_http_client_fetch_headers(http);
            r->http_status = esp_http_client_get_status_code(http);
            if (r->expected_bytes < 0 || r->http_status != 200 || session->receiver.failed) e=ESP_FAIL;
            else if (r->expected_bytes > (int64_t)cap) e=ESP_ERR_INVALID_SIZE;
            else {
                r->transport_stage = "allocate_body";
                actell_buffer_result allocated = actell_buffer_init(&received,cap,(size_t)r->expected_bytes);
                if (allocated != ACTELL_BUFFER_OK) e=allocated==ACTELL_BUFFER_LIMIT?ESP_ERR_INVALID_SIZE:ESP_ERR_NO_MEM;
            }
        }
        int64_t read_start = esp_timer_get_time();
        while (e == ESP_OK && !session->receiver.failed) {
            r->transport_stage = "read_body";
            if (!current(&session->receiver)) { r->transport_stage="session_or_deadline"; e=ESP_FAIL; break; }
            unsigned char block[1024];
            esp_http_client_set_timeout_ms(http,timeout_left(end));
            int n = esp_http_client_read(http,(char *)block,sizeof(block)); r->read_result=n;
            if (n <= 0) { if (n < 0 || !esp_http_client_is_complete_data_received(http)) e=ESP_FAIL; break; }
            actell_buffer_result appended = actell_buffer_append(&received,block,(size_t)n);
            if (appended != ACTELL_BUFFER_OK) { r->transport_stage="append_body"; e=appended==ACTELL_BUFFER_LIMIT?ESP_ERR_INVALID_SIZE:ESP_ERR_NO_MEM; break; }
        }
        read_ms = (esp_timer_get_time() - read_start) / 1000;
        r->received_bytes = received.length;
        if (!current(&session->receiver)) { r->transport_stage="session_or_deadline"; e=ESP_FAIL; }
        if (session->receiver.failed) { r->transport_stage=session->receiver.failure; e=ESP_FAIL; }
        /* Parser completion can precede draining data buffered while fetching
         * headers. A positive content length must exactly match delivered bytes. */
        if (e == ESP_OK && r->expected_bytes > 0 && received.length != (size_t)r->expected_bytes) {
            r->transport_stage="read_body"; e=ESP_FAIL;
        }
        ESP_LOGI("actell.rpc", "phase=http_timing operation_id=%s request_id=%s image=%d attempt=%u cache_hit=%u connection_reused=%d connection_borrowed=%d discovery_ms=%lld connect_ms=%lld read_ms=%lld total_ms=%lld sent_bytes=%u bytes=%u ok=%d",
                 trace,id,image,attempt,cache_hit,reused,borrowed,(long long)discovery_ms,(long long)connect_ms,
                 (long long)read_ms,(long long)((esp_timer_get_time()-began)/1000),(unsigned)sent,(unsigned)received.length,e==ESP_OK);
        if (e != ESP_OK) {
            actell_buffer_free(&received);
            actell_http_invalidate(locator);
            if (!attempt && cache_hit && header_failure && !write_attempted &&
                actell_bridge_current(generation) && esp_timer_get_time() < end) continue;
            return e;
        }
        cached = *locator; cached.session=NULL; cached.operation_id[0]=0;
        session->requests++;
        if (invocation) session->last_used=esp_timer_get_time();
        session->receiver.response=NULL;
        if (!esp_http_client_is_persistent_connection(http) || (!invocation && !borrowed))
            actell_http_locator_close(locator);
        else if (!invocation) actell_http_locator_release(locator);
        r->transport_stage=NULL; r->body=received.data; r->length=received.length;
        return ESP_OK;
    }
    return ESP_FAIL;
}
