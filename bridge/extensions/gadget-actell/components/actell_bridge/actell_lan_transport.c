/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_lan.h"
#include "esp_http_client.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "lwip/inet.h"
#include "mbedtls/oid.h"
#include "mbedtls/pem.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mdns.h"
#include "psa/crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

void actell_random_uuid(char out[37]) {
    unsigned char r[16];
    esp_fill_random(r, 16);
    r[6] = (r[6] & 15) | 64;
    r[8] = (r[8] & 63) | 128;
    snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", r[0],
             r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12], r[13],
             r[14], r[15]);
}

typedef struct {
    const actell_candidate *c;
    actell_config *anchor;
    bool verified, failed;
    int64_t end;
    unsigned headers;
    char *response;
    mbedtls_x509_crt empty_anchor;
} pairing_tls;
/* Only the existing serialized Actell worker calls this attach hook. Each SSL
 * config captures its own context; neither global CA store nor other clients
 * are modified. No callback is registered on official Muse transports. */
static pairing_tls *attaching;
static int verify_leaf(void *ctx, mbedtls_x509_crt *cert, int depth, uint32_t *flags) {
    pairing_tls *s = ctx;
    if (depth != 0 || !actell_verify_self_leaf(s->c->host, s->anchor, cert)) {
        s->failed = true;
        *flags |= MBEDTLS_X509_BADCERT_NOT_TRUSTED;
        return MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
    }
    s->verified = true;
    *flags = 0;
    return 0;
}
static esp_err_t attach_verifier(void *conf) {
    if (!attaching)
        return ESP_ERR_INVALID_STATE; /* Match the supported bundle-hook pattern: a non-trusting
                                       * empty anchor satisfies Mbed TLS's non-NULL chain
                                       * requirement; our callback independently verifies the exact
                                       * self-issued leaf. No public CA bundle is attached. */
    mbedtls_ssl_conf_ca_chain(conf, &attaching->empty_anchor, NULL);
    mbedtls_ssl_conf_verify(conf, verify_leaf, attaching);
    return ESP_OK;
}
static esp_err_t pair_headers(esp_http_client_event_t *event) {
    pairing_tls *s = event->user_data;
    if (!actell_pair_current() || esp_timer_get_time() >= s->end) {
        s->failed = true;
        return ESP_FAIL;
    }
    if (event->event_id != HTTP_EVENT_ON_HEADER)
        return ESP_OK;
    const char *k = event->header_key, *v = event->header_value;
    unsigned bit = 0;
    if (!strcasecmp(k, "Content-Type")) {
        bit = 1;
        if (strcasecmp(v, "application/json") && strcasecmp(v, "application/json; charset=utf-8"))
            s->failed = true;
    } else if (!strcasecmp(k, "Cache-Control")) {
        bit = 2;
        if (strcasecmp(v, "no-store"))
            s->failed = true;
    } else if (!strcasecmp(k, "Content-Length")) {
        bit = 4;
        if (!*v)
            s->failed = true;
        for (const char *p = v; *p; p++)
            if (*p < '0' || *p > '9')
                s->failed = true;
    } else if (!strcasecmp(k, "Transfer-Encoding") || !strcasecmp(k, "Content-Encoding") ||
               !strcasecmp(k, "Set-Cookie") || !strcasecmp(k, "Location"))
        s->failed = true;
    if (bit && (s->headers & bit))
        s->failed = true;
    s->headers |= bit;
    return s->failed ? ESP_FAIL : ESP_OK;
}
esp_err_t actell_pair_post(const actell_candidate *c, actell_config *anchor, const char *path,
                           const char *token, const char *body, int *status, char response[4097]) {
    *status = 0;
    response[0] = 0;
    struct ifreq iface;
    ip4_addr_t ip;
    if (!actell_pair_current() || !actell_clock_ready() || !actell_lan_interface(&iface) ||
        !ip4addr_aton(c->address, &ip) || !actell_lan_address(ip.addr) || !slan_host(c->host) ||
        !c->port || strlen(body) > 512 ||
        (strcmp(path, "/v1/pair/request") && strcmp(path, "/v1/pair/claim")))
        return ESP_ERR_INVALID_ARG;
    char url[96], auth[52], host[272];
    snprintf(url, sizeof(url), "https://%s:%u%s", c->address, c->port, path);
    snprintf(auth, sizeof(auth), "Bearer %s", token);
    snprintf(host, sizeof(host), "%s:%u", c->host, c->port);
    pairing_tls state = {
        .c = c, .anchor = anchor, .end = esp_timer_get_time() + 10000000, .response = response};
    esp_http_client_config_t cfg = {.url = url,
                                    .common_name = c->host,
                                    .timeout_ms = 10000,
                                    .disable_auto_redirect = true,
                                    .skip_cert_common_name_check = false,
                                    .keep_alive_enable = false,
                                    .if_name = &iface,
                                    .crt_bundle_attach = attach_verifier,
                                    .event_handler = pair_headers,
                                    .user_data = &state};
    attaching = &state;
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    esp_err_t e = ESP_FAIL;
    if (h) {
        esp_http_client_set_method(h, HTTP_METHOD_POST);
        esp_http_client_set_header(h, "Host", host);
        esp_http_client_set_header(h, "Content-Type", "application/json");
        esp_http_client_set_header(h, "Authorization", auth);
        esp_http_client_set_header(h, "Cache-Control", "no-store");
        e = esp_http_client_open(h, strlen(body));
        if (e == ESP_OK && (!state.verified || state.failed || !actell_pair_current()))
            e = ESP_FAIL;
        size_t sent = 0;
        while (e == ESP_OK && sent < strlen(body)) {
            if (!actell_pair_current() || esp_timer_get_time() >= state.end) {
                e = ESP_FAIL;
                break;
            }
            int n = esp_http_client_write(h, body + sent, strlen(body) - sent);
            if (n <= 0) {
                e = ESP_FAIL;
                break;
            }
            sent += n;
        }
        int64_t length = e == ESP_OK ? esp_http_client_fetch_headers(h) : -1;
        *status = esp_http_client_get_status_code(h);
        if (length < 0 || length > 4096 || esp_http_client_is_chunked_response(h) || state.failed ||
            state.headers != 7 || *status < 200 || (*status >= 300 && *status < 400))
            e = ESP_FAIL;
        size_t used = 0;
        while (e == ESP_OK && used < (size_t)length) {
            if (!actell_pair_current() || esp_timer_get_time() >= state.end) {
                e = ESP_FAIL;
                break;
            }
            int n = esp_http_client_read(h, response + used, length - used);
            if (n <= 0) {
                e = ESP_FAIL;
                break;
            }
            used += n;
        }
        if (e == ESP_OK && !esp_http_client_is_complete_data_received(h))
            e = ESP_FAIL;
        response[used] = 0;
        esp_http_client_close(h);
        esp_http_client_cleanup(h);
    }
    attaching = NULL;
    slan_zero(auth, sizeof(auth));
    if (e != ESP_OK || state.failed) {
        slan_zero(response, 4097);
        return ESP_FAIL;
    }
    return ESP_OK;
}
