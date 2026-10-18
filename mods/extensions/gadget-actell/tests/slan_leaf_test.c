/* SPDX-License-Identifier: Apache-2.0 */
/* Native entry point for the actual firmware's X.509 verifier. No fake crypto. */
#include "actell_lan.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include <stdlib.h>
#include <string.h>
bool actell_clock_ready(void) { return true; }
int slan_test_leaf(const char *pem, const char *host, const char *expected_pin, char out[65]) {
    actell_config *anchor = calloc(1, sizeof(*anchor));
    if (!anchor)
        return -1;
    if (expected_pin && strlen(expected_pin) == 64)
        strcpy(anchor->fingerprint, expected_pin);
    mbedtls_x509_crt cert;
    mbedtls_x509_crt_init(&cert);
    bool ok = psa_crypto_init() == PSA_SUCCESS &&
              !mbedtls_x509_crt_parse(&cert, (const unsigned char *)pem, strlen(pem) + 1) &&
              actell_verify_self_leaf(host, anchor, &cert);
    if (ok)
        strcpy(out, anchor->fingerprint);
    mbedtls_x509_crt_free(&cert);
    slan_zero(anchor, sizeof(*anchor));
    free(anchor);
    return ok;
}

#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
typedef struct {
    actell_config *anchor;
    const char *host;
    bool verified;
} native_verify;
static int native_verify_cb(void *arg, mbedtls_x509_crt *cert, int depth, uint32_t *flags) {
    native_verify *v = arg;
    if (depth != 0 || !actell_verify_self_leaf(v->host, v->anchor, cert)) {
        *flags |= MBEDTLS_X509_BADCERT_NOT_TRUSTED;
        return MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
    }
    v->verified = true;
    *flags = 0;
    return 0;
}
/* Native TLS reproduces the same VERIFY_REQUIRED + empty anchor + isolated
 * verifier pattern as the ESP attach hook, including server proof of key. */
int slan_test_handshake(const char *port, const char *expected_pin) {
    actell_config *anchor = calloc(1, sizeof(*anchor));
    if (!anchor)
        return -1;
    if (expected_pin && strlen(expected_pin) == 64)
        strcpy(anchor->fingerprint, expected_pin);
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config cfg;
    mbedtls_net_context net;
    mbedtls_x509_crt empty;
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&cfg);
    mbedtls_net_init(&net);
    mbedtls_x509_crt_init(&empty);
    native_verify v = {anchor, "actell-fixture.local", false};
    int result = -1;
    if (psa_crypto_init() != PSA_SUCCESS ||
        mbedtls_ssl_config_defaults(&cfg, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT))
        goto done;
    mbedtls_ssl_conf_authmode(&cfg, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&cfg, &empty, NULL);
    mbedtls_ssl_conf_verify(&cfg, native_verify_cb, &v);
    if (mbedtls_ssl_setup(&ssl, &cfg) || mbedtls_ssl_set_hostname(&ssl, v.host) ||
        mbedtls_net_connect(&net, "127.0.0.1", port, MBEDTLS_NET_PROTO_TCP))
        goto done;
    mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, mbedtls_net_recv, NULL);
    result = mbedtls_ssl_handshake(&ssl) == 0 && v.verified ? 1 : 0;
done:
    mbedtls_net_free(&net);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&cfg);
    mbedtls_x509_crt_free(&empty);
    slan_zero(anchor, sizeof(*anchor));
    free(anchor);
    return result;
}
