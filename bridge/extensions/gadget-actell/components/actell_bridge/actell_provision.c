/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_lan.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"
#include "psa/crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Preserve the exact peer_v1 ABI. New records never reinterpret old blobs. */
typedef struct {
    char origin[256], client[129], pem[4096], fingerprint[65];
    unsigned char secret[32];
} legacy_config;
#define RECORD_LEN (9 + 272 + 129 + 4096 + 65 + 32)
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
    return memchr(c->origin, 0, sizeof(c->origin)) && memchr(c->client, 0, sizeof(c->client)) &&
           memchr(c->pem, 0, sizeof(c->pem)) && memchr(c->fingerprint, 0, sizeof(c->fingerprint)) &&
           origin_valid(c->origin) && actell_id_valid(c->client) &&
           certificate_valid(c->pem, c->fingerprint);
}
esp_err_t actell_peer_save(const actell_config *c, actell_peer_phase phase) {
    if (!valid(c) || phase < ACTELL_PEER_PENDING || phase > ACTELL_PEER_ACTIVE)
        return ESP_ERR_INVALID_ARG;
    unsigned char *b = calloc(1, RECORD_LEN);
    if (!b)
        return ESP_ERR_NO_MEM;
    memcpy(b, "SLAN", 4);
    b[4] = 1;
    b[5] = phase;
    /* Reserved byte records the exact default name used in the request. */
    b[8] = c->named_muse_gadget ? 1 : 0;
    b[6] = RECORD_LEN >> 8;
    b[7] = RECORD_LEN & 255;
    size_t off = 9;
#define COPY(field)                                                                                \
    memcpy(b + off, c->field, sizeof(c->field));                                                   \
    off += sizeof(c->field)
    COPY(origin);
    COPY(client);
    COPY(pem);
    COPY(fingerprint);
    COPY(secret);
#undef COPY
    nvs_handle_t h;
    esp_err_t e = nvs_open("actell", NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_set_blob(h, "peer_v2", b, RECORD_LEN);
        if (e == ESP_OK)
            e = nvs_commit(h);
        nvs_close(h);
    }
    slan_zero(b, RECORD_LEN);
    free(b);
    return e;
}
esp_err_t actell_peer_load(actell_config *c, actell_peer_phase *phase) {
    memset(c, 0, sizeof(*c));
    nvs_handle_t h;
    esp_err_t e = nvs_open("actell", NVS_READONLY, &h);
    if (e != ESP_OK)
        return e;
    size_t n = 0;
    e = nvs_get_blob(h, "peer_v2", NULL, &n);
    if (e == ESP_ERR_NVS_NOT_FOUND) {
        legacy_config *old = calloc(1, sizeof(*old));
        if (!old) {
            nvs_close(h);
            return ESP_ERR_NO_MEM;
        }
        n = sizeof(*old);
        e = nvs_get_blob(h, "peer_v1", old, &n);
        if (e == ESP_OK && n == sizeof(*old) && memchr(old->origin, 0, sizeof(old->origin)) &&
            memchr(old->client, 0, sizeof(old->client)) && memchr(old->pem, 0, sizeof(old->pem)) &&
            memchr(old->fingerprint, 0, sizeof(old->fingerprint))) {
            memcpy(c->origin, old->origin, sizeof(old->origin));
            memcpy(c->client, old->client, sizeof(old->client));
            memcpy(c->pem, old->pem, sizeof(old->pem));
            memcpy(c->fingerprint, old->fingerprint, sizeof(old->fingerprint));
            memcpy(c->secret, old->secret, 32);
            *phase = ACTELL_PEER_LEGACY;
        } else
            e = ESP_ERR_INVALID_STATE;
        slan_zero(old, sizeof(*old));
        free(old);
    } else if (e == ESP_OK && n == RECORD_LEN) {
        unsigned char *b = calloc(1, n);
        if (!b) {
            nvs_close(h);
            return ESP_ERR_NO_MEM;
        }
        e = nvs_get_blob(h, "peer_v2", b, &n);
        if (e == ESP_OK && n == RECORD_LEN && !memcmp(b, "SLAN", 4) && b[4] == 1 &&
            (b[5] == 1 || b[5] == 2) && b[6] == (RECORD_LEN >> 8) && b[7] == (RECORD_LEN & 255) &&
            b[8] <= 1) {
            *phase = b[5];
            c->named_muse_gadget = b[8] == 1;
            size_t off = 9;
#define LOAD(field)                                                                                \
    memcpy(c->field, b + off, sizeof(c->field));                                                   \
    off += sizeof(c->field)
            LOAD(origin);
            LOAD(client);
            LOAD(pem);
            LOAD(fingerprint);
            LOAD(secret);
#undef LOAD
        } else
            e = ESP_ERR_INVALID_STATE;
        slan_zero(b, n);
        free(b);
    } else
        e = ESP_ERR_INVALID_STATE;
    nvs_close(h);
    if (e == ESP_OK && !valid(c))
        e = ESP_ERR_INVALID_STATE;
    if (e != ESP_OK)
        slan_zero(c, sizeof(*c));
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
    /* Durable deletion marker takes precedence over legacy data even if power fails
     * before cleanup. A new explicit pairing can later replace this marker. */
    nvs_handle_t h;
    esp_err_t e = nvs_open("actell", NVS_READWRITE, &h);
    if (e != ESP_OK)
        return e;
    const char marker[] = "REVOKED";
    e = nvs_set_blob(h, "peer_v2", marker, sizeof(marker));
    if (e == ESP_OK)
        e = nvs_commit(h);
    if (e == ESP_OK) {
        esp_err_t x = nvs_erase_key(h, "peer_v1");
        if (x != ESP_OK && x != ESP_ERR_NVS_NOT_FOUND)
            e = x;
        if (e == ESP_OK)
            e = nvs_commit(h);
    }
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
