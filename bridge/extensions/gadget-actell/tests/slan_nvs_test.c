/* SPDX-License-Identifier: Apache-2.0 */
/* Execute real record/provision code with atomic memory-backed NVS. X.509/PSA
 * adapters here are synthetic stubs; this harness makes no TLS/crypto claim. */
#include "actell_lan.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char live[5000], staged[5000], old[5000];
static size_t live_len, staged_len, old_len;
static unsigned char auto_live, auto_staged;
static bool auto_present, auto_pending;
static int fail_set, fail_commit;
esp_err_t nvs_open(const char *n, int mode, nvs_handle_t *h) {
    assert(!strcmp(n, "actell"));
    (void)mode;
    *h = 1;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *p, size_t *n) {
    (void)h;
    if (!strcmp(k,"auto_v1")) {
        if(!auto_present) return ESP_ERR_NVS_NOT_FOUND;
        assert(*n >= 1 && p); *(unsigned char*)p=auto_live; *n=1; return ESP_OK;
    }
    unsigned char *b = !strcmp(k, "peer_v2") ? live : old;
    size_t len = !strcmp(k, "peer_v2") ? live_len : old_len;
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
    assert(!strcmp(k, "peer_v2"));
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
    if (staged_len) {
        memcpy(live, staged, staged_len);
        live_len = staged_len;
        staged_len = 0;
    }
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h, const char *k) {
    (void)h;
    assert(!strcmp(k, "peer_v1"));
    old_len = 0;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h) {
    (void)h;
    staged_len = 0; auto_pending=false;
}
void mbedtls_x509_crt_init(mbedtls_x509_crt *c) { memset(c, 0, sizeof(*c)); }
void mbedtls_x509_crt_free(mbedtls_x509_crt *c) { (void)c; }
int mbedtls_x509_crt_parse(mbedtls_x509_crt *c, const unsigned char *p, size_t n) {
    if (strcmp((char *)p, "PUBLIC SYNTHETIC CERT"))
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
int main(void) {
    actell_config c = {0}, read = {0};
    actell_peer_phase phase;
    strcpy(c.origin, "https://actell-fixture.local:8765");
    strcpy(c.client, "22222222-2222-4222-8222-222222222222");
    strcpy(c.pem, "PUBLIC SYNTHETIC CERT");
    memset(c.fingerprint, 'a', 64);
    memset(c.secret, 0x11, 32);
    assert(actell_peer_save(&c, ACTELL_PEER_PENDING) == ESP_OK);
    assert(actell_peer_load(&read, &phase) == ESP_OK && phase == ACTELL_PEER_PENDING);
    assert(!memcmp(&c, &read, sizeof(c)));
    assert(actell_config_load(&read) == ESP_ERR_INVALID_STATE);
    for (size_t i = 0; i < sizeof(read); i++)
        assert(!((unsigned char *)&read)[i]);
    fail_set = 1;
    assert(actell_peer_save(&c, ACTELL_PEER_ACTIVE) != ESP_OK);
    fail_set = 0;
    assert(actell_peer_load(&read, &phase) == ESP_OK && phase == ACTELL_PEER_PENDING);
    fail_commit = 1;
    assert(actell_peer_save(&c, ACTELL_PEER_ACTIVE) != ESP_OK);
    fail_commit = 0;
    assert(actell_peer_load(&read, &phase) == ESP_OK && phase == ACTELL_PEER_PENDING);
    assert(actell_peer_save(&c, ACTELL_PEER_ACTIVE) == ESP_OK);
    assert(actell_config_load(&read) == ESP_OK);
    /* New default survives pending recovery; old records keep their old name. */
    assert(!read.named_muse_gadget);
    c.named_muse_gadget = true;
    assert(actell_peer_save(&c, ACTELL_PEER_PENDING) == ESP_OK);
    assert(actell_peer_load(&read, &phase) == ESP_OK && read.named_muse_gadget);
    assert(!strcmp(actell_device_name(&read), "Muse Gadget"));
    assert(!memcmp(&c, &read, sizeof(c)));
    assert(actell_peer_save(&c, ACTELL_PEER_ACTIVE) == ESP_OK);
    bool automatic=false;
    assert(actell_auto_connect_load(&automatic)==ESP_OK && automatic);
    unsigned char peer_copy[5000]; memcpy(peer_copy,live,live_len);
    size_t peer_len=live_len;
    assert(actell_auto_connect_save(false)==ESP_OK);
    assert(actell_auto_connect_load(&automatic)==ESP_OK && !automatic);
    fail_commit=1; assert(actell_auto_connect_save(true)!=ESP_OK); fail_commit=0;
    assert(actell_auto_connect_load(&automatic)==ESP_OK && !automatic);
    assert(actell_auto_connect_save(true)==ESP_OK);
    assert(actell_auto_connect_load(&automatic)==ESP_OK && automatic);
    assert(live_len==peer_len && !memcmp(peer_copy,live,live_len));
    auto_live=2; assert(actell_auto_connect_load(&automatic)!=ESP_OK && !automatic);
    /* Exact old ABI loads without modifying old data; v2 takes precedence. */
    typedef struct {
        char origin[256], client[129], pem[4096], fingerprint[65];
        unsigned char secret[32];
    } legacy;
    legacy v1 = {0};
    strcpy(v1.origin, c.origin);
    strcpy(v1.client, c.client);
    strcpy(v1.pem, c.pem);
    strcpy(v1.fingerprint, c.fingerprint);
    memcpy(v1.secret, c.secret, 32);
    memcpy(old, &v1, sizeof(v1));
    old_len = sizeof(v1);
    live_len = 0;
    assert(actell_peer_load(&read, &phase) == ESP_OK && phase == ACTELL_PEER_LEGACY);
    assert(old_len == sizeof(v1));
    assert(actell_peer_save(&c, ACTELL_PEER_PENDING) == ESP_OK);
    assert(actell_config_load(&read) == ESP_ERR_INVALID_STATE);
    assert(actell_peer_forget() == ESP_OK);
    assert(actell_peer_load(&read, &phase) != ESP_OK);
    assert(!old_len);
    memcpy(old, &v1, sizeof(v1));
    old_len = sizeof(v1);
    assert(actell_peer_load(&read, &phase) != ESP_OK); /* marker prevents v1 revival */
    assert(actell_peer_save(&c, ACTELL_PEER_ACTIVE) == ESP_OK);
    live[8] = 2;
    assert(actell_peer_load(&read, &phase) != ESP_OK); /* unknown name flag */
    live[8] = 1;
    live[4] = 2;
    assert(actell_peer_load(&read, &phase) !=
           ESP_OK); /* unknown version fails closed, no fallback */
    puts("PASS real NVS adapter model: pending/active/write-failure/legacy/version/deletion-fence");
    return 0;
}
