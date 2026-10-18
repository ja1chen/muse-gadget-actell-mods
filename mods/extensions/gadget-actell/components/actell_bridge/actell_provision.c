/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_lan.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"
#include "psa/crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Compact, length-delimited record. Only this format is read or written.
 * Strings have no trailing NUL/padding; the pin is stored as 32 binary bytes. */
#define PEER_KEY "peer_v3"
#define HEADER_LEN 14u
#define FIXED_LEN (HEADER_LEN + 32u + 32u)
#define RECORD_MAX (FIXED_LEN + 271u + 128u + 4095u)
static unsigned get16(const unsigned char *p) { return ((unsigned)p[0] << 8) | p[1]; }
static void put16(unsigned char *p, size_t n) { p[0] = n >> 8; p[1] = n & 255; }
static bool origin_valid(const char *s) {
    if (!s || strncmp(s, "https://", 8) || strlen(s) >= 272)
        return false;
    for (const char *p = s + 8; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              strchr(".-:[]", *p)))
            return false;
    return s[8] != 0;
}
static bool certificate_valid(const char *pem, const char *fp) {
    if (!pem || !fp || strlen(pem) >= 4096 || strlen(fp) != 64)
        return false;
    mbedtls_x509_crt cert;
    mbedtls_x509_crt_init(&cert);
    unsigned char hash[32];
    size_t n = 0;
    char hex[65];
    bool ok = false;
    if (!mbedtls_x509_crt_parse(&cert, (const unsigned char *)pem, strlen(pem) + 1) && !cert.next &&
        !mbedtls_x509_crt_get_ca_istrue(&cert) && cert.issuer_raw.len == cert.subject_raw.len &&
        !memcmp(cert.issuer_raw.p, cert.subject_raw.p, cert.issuer_raw.len) &&
        psa_crypto_init() == PSA_SUCCESS &&
        psa_hash_compute(PSA_ALG_SHA_256, cert.raw.p, cert.raw.len, hash, 32, &n) == PSA_SUCCESS &&
        n == 32) {
        for (unsigned i = 0; i < 32; i++)
            snprintf(hex + 2 * i, 3, "%02x", hash[i]);
        ok = !strcmp(hex, fp);
    }
    mbedtls_x509_crt_free(&cert);
    return ok;
}
static bool valid(const actell_config *c) {
    return c && memchr(c->origin, 0, sizeof(c->origin)) && memchr(c->client, 0, sizeof(c->client)) &&
           memchr(c->pem, 0, sizeof(c->pem)) && memchr(c->fingerprint, 0, sizeof(c->fingerprint)) &&
           origin_valid(c->origin) && actell_id_valid(c->client) &&
           certificate_valid(c->pem, c->fingerprint);
}
esp_err_t actell_peer_save(const actell_config *c, actell_peer_phase phase) {
    if (!valid(c) || (phase != ACTELL_PEER_PENDING && phase != ACTELL_PEER_ACTIVE))
        return ESP_ERR_INVALID_ARG;
    size_t origin = strlen(c->origin), client = strlen(c->client), pem = strlen(c->pem);
    size_t n = FIXED_LEN + origin + client + pem;
    unsigned char *b = calloc(1, n);
    if (!b) return ESP_ERR_NO_MEM;
    memcpy(b, "SLP3", 4);
    b[4] = 1;
    b[5] = phase;
    put16(b + 6, n);
    put16(b + 8, origin);
    put16(b + 10, client);
    put16(b + 12, pem);
    memcpy(b + HEADER_LEN, c->secret, 32);
    /* valid() already checked the lowercase SHA-256 hex against the leaf. */
    const char *hex = "0123456789abcdef";
    for (unsigned i = 0; i < 32; i++)
        b[HEADER_LEN + 32 + i] = (unsigned)(strchr(hex, c->fingerprint[2*i]) - hex) * 16 +
                               (unsigned)(strchr(hex, c->fingerprint[2*i+1]) - hex);
    size_t off = FIXED_LEN;
    memcpy(b + off, c->origin, origin); off += origin;
    memcpy(b + off, c->client, client); off += client;
    memcpy(b + off, c->pem, pem);
    nvs_handle_t h;
    esp_err_t e = nvs_open("actell", NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_set_blob(h, PEER_KEY, b, n);
        if (e == ESP_OK) e = nvs_commit(h);
        nvs_close(h);
    }
    slan_zero(b, n);
    free(b);
    return e;
}
static bool decode(const unsigned char *b, size_t n, actell_config *c, actell_peer_phase *phase) {
    if (n < FIXED_LEN || memcmp(b, "SLP3", 4) || b[4] != 1 ||
        (b[5] != ACTELL_PEER_PENDING && b[5] != ACTELL_PEER_ACTIVE) || get16(b + 6) != n)
        return false;
    size_t origin = get16(b + 8), client = get16(b + 10), pem = get16(b + 12);
    if (!origin || origin >= sizeof(c->origin) || !client || client >= sizeof(c->client) ||
        !pem || pem >= sizeof(c->pem) || FIXED_LEN + origin + client + pem != n)
        return false;
    const unsigned char *p = b + FIXED_LEN;
    if (memchr(p, 0, origin + client + pem)) return false;
    memcpy(c->origin, p, origin); p += origin;
    memcpy(c->client, p, client); p += client;
    memcpy(c->pem, p, pem);
    memcpy(c->secret, b + HEADER_LEN, 32);
    for (unsigned i = 0; i < 32; i++)
        snprintf(c->fingerprint + 2*i, 3, "%02x", b[HEADER_LEN + 32 + i]);
    if (!valid(c)) return false;
    *phase = b[5];
    return true;
}
esp_err_t actell_peer_load(actell_config *c, actell_peer_phase *phase) {
    if (!c || !phase) return ESP_ERR_INVALID_ARG;
    memset(c, 0, sizeof(*c));
    *phase = 0;
    nvs_handle_t h;
    esp_err_t e = nvs_open("actell", NVS_READONLY, &h);
    if (e != ESP_OK) return e;
    size_t n = 0;
    e = nvs_get_blob(h, PEER_KEY, NULL, &n);
    if (e == ESP_OK) {
        if (n < FIXED_LEN + 3 || n > RECORD_MAX) e = ESP_ERR_INVALID_STATE;
        else {
            unsigned char *b = malloc(n);
            if (!b) e = ESP_ERR_NO_MEM;
            else {
                size_t actual = n;
                e = nvs_get_blob(h, PEER_KEY, b, &actual);
                if (e == ESP_OK && (actual != n || !decode(b, n, c, phase)))
                    e = ESP_ERR_INVALID_STATE;
                slan_zero(b, n);
                free(b);
            }
        }
    }
    nvs_close(h);
    if (e != ESP_OK) { slan_zero(c, sizeof(*c)); *phase = 0; }
    return e;
}
esp_err_t actell_config_load(actell_config *c) {
    actell_peer_phase phase;
    esp_err_t e = actell_peer_load(c, &phase);
    if (e == ESP_OK && phase == ACTELL_PEER_PENDING) {
        slan_zero(c, sizeof(*c));
        return ESP_ERR_INVALID_STATE;
    }
    return e;
}
esp_err_t actell_peer_forget(void) {
    /* No legacy fallback or tombstone: remove the sole supported record. */
    nvs_handle_t h;
    esp_err_t e = nvs_open("actell", NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_erase_key(h, PEER_KEY);
    if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}
esp_err_t actell_bridge_provision(const char *origin, const char *client,
                                  const unsigned char secret[32], const char *pem, const char *fp) {
    if (!origin || !client || !secret || !pem || !fp || strlen(origin) >= 272 ||
        strlen(client) >= 129 || strlen(pem) >= 4096 || strlen(fp) != 64)
        return ESP_ERR_INVALID_ARG;
    actell_config *c = calloc(1, sizeof(*c));
    if (!c)
        return ESP_ERR_NO_MEM;
    strcpy(c->origin, origin);
    strcpy(c->client, client);
    strcpy(c->pem, pem);
    strcpy(c->fingerprint, fp);
    memcpy(c->secret, secret, 32);
    esp_err_t e = actell_peer_save(c, ACTELL_PEER_ACTIVE);
    slan_zero(c, sizeof(*c));
    free(c);
    return e;
}

/* Independent preference: never rewrite pairing or trust material. Missing key
 * preserves the existing automatic reconnect behavior. Corruption fails closed. */
esp_err_t actell_auto_connect_load(bool *enabled) {
    *enabled = false;
    nvs_handle_t h;
    esp_err_t e = nvs_open("actell", NVS_READONLY, &h);
    if (e == ESP_ERR_NVS_NOT_FOUND) { *enabled = true; return ESP_OK; }
    if (e != ESP_OK) return e;
    uint8_t value = 0;
    size_t n = sizeof(value);
    e = nvs_get_blob(h, "auto_v1", &value, &n);
    nvs_close(h);
    if (e == ESP_ERR_NVS_NOT_FOUND) { *enabled = true; return ESP_OK; }
    if (e != ESP_OK) return e;
    if (n != 1 || value > 1) return ESP_ERR_INVALID_ARG;
    *enabled = value != 0;
    return ESP_OK;
}
esp_err_t actell_auto_connect_save(bool enabled) {
    nvs_handle_t h;
    esp_err_t e = nvs_open("actell", NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    uint8_t value = enabled ? 1 : 0;
    e = nvs_set_blob(h, "auto_v1", &value, sizeof(value));
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}
