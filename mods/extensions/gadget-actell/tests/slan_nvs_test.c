/* SPDX-License-Identifier: Apache-2.0 */
/* Execute real record/provision code with atomic memory-backed NVS. X.509/PSA
 * adapters here are synthetic stubs; this harness makes no TLS/crypto claim. */
#include "actell_lan.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char live[5000], staged[5000];
static size_t live_len, staged_len;
static unsigned char auto_live, auto_staged;
static bool auto_present, auto_pending;
static int fail_set, fail_commit, fail_erase, fail_open, fail_get;
static bool erase_pending;
static unsigned peer_reads, peer_writes;
esp_err_t nvs_open(const char *n, int mode, nvs_handle_t *h) {
    assert(!strcmp(n, "actell"));
    (void)mode;
    if (fail_open) return ESP_FAIL;
    *h = 1;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *p, size_t *n) {
    (void)h;
    if (!strcmp(k,"auto_v1")) {
        if(!auto_present) return ESP_ERR_NVS_NOT_FOUND;
        assert(*n >= 1 && p); *(unsigned char*)p=auto_live; *n=1; return ESP_OK;
    }
    assert(!strcmp(k, "peer_v3"));
    peer_reads++;
    if (fail_get) return ESP_FAIL;
    unsigned char *b = live;
    size_t len = live_len;
    if (!len)
        return ESP_ERR_NVS_NOT_FOUND;
    if (!p) {
        *n = len;
        return ESP_OK;
    }
    if (*n < len)
        return ESP_FAIL;
    memcpy(p, b, len);
    *n = len;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *p, size_t n) {
    (void)h;
    if (!strcmp(k,"auto_v1")) {
        assert(n==1); if(fail_set) return ESP_FAIL;
        auto_staged=*(const unsigned char*)p; auto_pending=true; return ESP_OK;
    }
    assert(!strcmp(k, "peer_v3"));
    peer_writes++;
    assert(n <= sizeof(staged));
    if (fail_set)
        return ESP_FAIL;
    memcpy(staged, p, n);
    staged_len = n;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    (void)h;
    if (fail_commit)
        return ESP_FAIL;
    if(auto_pending) { auto_live=auto_staged; auto_present=true; auto_pending=false; }
    if (erase_pending) { live_len = 0; erase_pending = false; }
    if (staged_len) {
        memcpy(live, staged, staged_len);
        live_len = staged_len;
        staged_len = 0;
    }
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h, const char *k) {
    (void)h;
    assert(!strcmp(k, "peer_v3"));
    if (fail_erase) return ESP_FAIL;
    if (!live_len) return ESP_ERR_NVS_NOT_FOUND;
    erase_pending = true;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h) {
    (void)h;
    staged_len = 0; auto_pending=false; erase_pending=false;
}
void mbedtls_x509_crt_init(mbedtls_x509_crt *c) { memset(c, 0, sizeof(*c)); }
void mbedtls_x509_crt_free(mbedtls_x509_crt *c) { (void)c; }
int mbedtls_x509_crt_parse(mbedtls_x509_crt *c, const unsigned char *p, size_t n) {
    if (n < 21 || n > 4096 || memcmp(p, "PUBLIC SYNTHETIC CERT", 20))
        return -1;
    c->raw.p = (unsigned char *)p;
    c->raw.len = n;
    c->issuer_raw = c->subject_raw = c->raw;
    return 0;
}
int mbedtls_x509_crt_get_ca_istrue(const mbedtls_x509_crt *c) {
    (void)c;
    return 0;
}
int psa_crypto_init(void) { return 0; }
int psa_hash_compute(int alg, const unsigned char *p, size_t n, unsigned char *out, size_t cap,
                     size_t *len) {
    (void)alg;
    (void)p;
    (void)n;
    assert(cap == 32);
    memset(out, 0xaa, 32);
    *len = 32;
    return 0;
}
static void erased(const actell_config *c) {
    for (size_t i = 0; i < sizeof(*c); i++) assert(!((const unsigned char *)c)[i]);
}
static void expect_invalid(void) {
    actell_config read; memset(&read, 0x77, sizeof(read));
    actell_peer_phase phase = ACTELL_PEER_ACTIVE;
    assert(actell_peer_load(&read, &phase) != ESP_OK); erased(&read); assert(phase == 0);
}
int main(void) {
    actell_config c = {0}, read = {0};
    actell_peer_phase phase;
    strcpy(c.origin, "https://actell-fixture.local:8765");
    strcpy(c.client, "22222222-2222-4222-8222-222222222222");
    strcpy(c.pem, "PUBLIC SYNTHETIC CERT");
    memset(c.fingerprint, 'a', 64);
    memset(c.secret, 0x11, 32);
    expect_invalid(); assert(peer_reads == 1); /* No old-key fallback. */
    assert(actell_peer_save(&c, ACTELL_PEER_PENDING) == ESP_OK);
    size_t compact = 78 + strlen(c.origin) + strlen(c.client) + strlen(c.pem);
    assert(live_len == compact && compact < 512 && !memcmp(live, "SLP3", 4));
    assert(actell_peer_load(&read, &phase) == ESP_OK && phase == ACTELL_PEER_PENDING);
    assert(!memcmp(&c, &read, sizeof(c)));
    assert(actell_config_load(&read) == ESP_ERR_INVALID_STATE); erased(&read);
    fail_set = 1; assert(actell_peer_save(&c, ACTELL_PEER_ACTIVE) != ESP_OK); fail_set = 0;
    assert(actell_peer_load(&read, &phase) == ESP_OK && phase == ACTELL_PEER_PENDING);
    fail_commit = 1; assert(actell_peer_save(&c, ACTELL_PEER_ACTIVE) != ESP_OK); fail_commit = 0;
    assert(actell_peer_load(&read, &phase) == ESP_OK && phase == ACTELL_PEER_PENDING);
    assert(actell_peer_save(&c, ACTELL_PEER_ACTIVE) == ESP_OK);
    assert(actell_config_load(&read) == ESP_OK && !memcmp(&c, &read, sizeof(c)));
    assert(live_len == compact && peer_writes == 4);
    unsigned char good[5000]; size_t good_len = live_len; memcpy(good, live, good_len);
    /* Header lengths, version, state, trailing data and embedded NUL fail closed. */
    for (unsigned i = 0; i < 14; i++) {
        memcpy(live, good, good_len); live[i] ^= 0x80; expect_invalid();
    }
    const size_t corrupt[] = {0, 4, 5, 7, 9, 11, 13, 46, 78, 78+31, 78+31+36};
    for (unsigned i = 0; i < sizeof(corrupt)/sizeof(corrupt[0]); i++) {
        memcpy(live, good, good_len); live[corrupt[i]] = 0; expect_invalid();
    }
    memcpy(live, good, good_len); live[live_len++] = 1; expect_invalid();
    for (size_t n = 1; n < good_len; n++) { memcpy(live, good, good_len); live_len = n; expect_invalid(); }
    memset(live, 0, sizeof(live)); memcpy(live, "SLAN", 4); live_len = 4603; expect_invalid();
    memcpy(live, good, good_len); live_len = good_len;
    fail_get = 1; expect_invalid(); fail_get = 0;
    fail_open = 1; expect_invalid(); fail_open = 0;
    assert(actell_peer_save(&c, 0) == ESP_ERR_INVALID_ARG);
    assert(actell_peer_save(NULL, ACTELL_PEER_ACTIVE) == ESP_ERR_INVALID_ARG);
    c.fingerprint[0] = 'b'; assert(actell_peer_save(&c, ACTELL_PEER_ACTIVE) != ESP_OK); c.fingerprint[0] = 'a';
    assert(live_len == good_len && !memcmp(live, good, good_len));
    bool automatic = false;
    assert(actell_auto_connect_load(&automatic) == ESP_OK && automatic);
    assert(actell_auto_connect_save(false) == ESP_OK);
    assert(actell_auto_connect_load(&automatic) == ESP_OK && !automatic);
    fail_commit = 1; assert(actell_auto_connect_save(true) != ESP_OK); fail_commit = 0;
    assert(actell_auto_connect_load(&automatic) == ESP_OK && !automatic);
    assert(actell_auto_connect_save(true) == ESP_OK);
    assert(actell_auto_connect_load(&automatic) == ESP_OK && automatic);
    assert(live_len == good_len && !memcmp(live, good, good_len));
    fail_erase = 1; assert(actell_peer_forget() != ESP_OK); fail_erase = 0;
    fail_commit = 1; assert(actell_peer_forget() != ESP_OK); fail_commit = 0;
    assert(actell_config_load(&read) == ESP_OK);
    assert(actell_peer_forget() == ESP_OK && !live_len && auto_present && auto_live == 1);
    assert(actell_peer_forget() == ESP_OK); expect_invalid();
    /* Maximum supported strings round-trip without padding or truncation. */
    memset(c.origin, 'a', sizeof(c.origin)-1); memcpy(c.origin, "https://", 8);
    memset(c.client, 'b', sizeof(c.client)-1);
    memset(c.pem, 'x', sizeof(c.pem)-1); memcpy(c.pem, "PUBLIC SYNTHETIC CERT", 20);
    assert(actell_peer_save(&c, ACTELL_PEER_ACTIVE) == ESP_OK && live_len == 4572);
    assert(actell_config_load(&read) == ESP_OK && !memcmp(&c, &read, sizeof(c)));
    c.pem[sizeof(c.pem)-1] = 'x'; assert(actell_peer_save(&c, ACTELL_PEER_ACTIVE) != ESP_OK);
    puts("PASS compact NVS: exact lengths, pending/active, cold load, faults, strict bounds, pin, delete, settings isolation; no legacy access");
    return 0;
}
