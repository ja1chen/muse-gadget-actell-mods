/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_lan.h"
#include "mbedtls/asn1.h"
#include "mbedtls/oid.h"
#include "mbedtls/pem.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include <stdio.h>
#include <string.h>
/* Exact self-signature check as well as the TLS proof of private-key possession.
 * Parsing/verification stays in Mbed TLS; no custom signature algorithm. */
static bool self_signed(mbedtls_x509_crt *cert) {
    psa_algorithm_t alg;
    mbedtls_md_type_t md;
    if (MBEDTLS_OID_CMP(MBEDTLS_OID_ECDSA_SHA256, &cert->sig_oid) == 0) {
        alg = PSA_ALG_SHA_256;
        md = MBEDTLS_MD_SHA256;
    } else if (MBEDTLS_OID_CMP(MBEDTLS_OID_ECDSA_SHA384, &cert->sig_oid) == 0) {
        alg = PSA_ALG_SHA_384;
        md = MBEDTLS_MD_SHA384;
    } else if (MBEDTLS_OID_CMP(MBEDTLS_OID_ECDSA_SHA512, &cert->sig_oid) == 0) {
        alg = PSA_ALG_SHA_512;
        md = MBEDTLS_MD_SHA512;
    } else
        return false;
    unsigned char hash[64];
    size_t hash_len = 0, alg_len = 0, sig_len = 0;
    unsigned char *p = cert->tbs.p + cert->tbs.len, *end = cert->raw.p + cert->raw.len;
    if (mbedtls_asn1_get_tag(&p, end, &alg_len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE) ||
        alg_len > (size_t)(end - p))
        return false;
    p += alg_len;
    if (mbedtls_asn1_get_bitstring_null(&p, end, &sig_len) || sig_len != (size_t)(end - p))
        return false;
    if (psa_hash_compute(alg, cert->tbs.p, cert->tbs.len, hash, sizeof(hash), &hash_len) !=
        PSA_SUCCESS)
        return false;
    return mbedtls_pk_verify_ext(MBEDTLS_PK_SIGALG_ECDSA, &cert->pk, md, hash, hash_len, p,
                                 sig_len) == 0;
}
bool actell_verify_self_leaf(const char *host, actell_config *anchor, mbedtls_x509_crt *cert) {
    if (!slan_host(host) || cert->next || mbedtls_x509_crt_get_ca_istrue(cert) ||
        cert->issuer_raw.len != cert->subject_raw.len ||
        memcmp(cert->issuer_raw.p, cert->subject_raw.p, cert->issuer_raw.len))
        return false;
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    int key_ok = mbedtls_pk_get_psa_attributes(&cert->pk, PSA_KEY_USAGE_VERIFY_HASH, &attr);
    bool curve = !key_ok && PSA_KEY_TYPE_IS_ECC(psa_get_key_type(&attr)) &&
                 PSA_KEY_TYPE_ECC_GET_FAMILY(psa_get_key_type(&attr)) == PSA_ECC_FAMILY_SECP_R1 &&
                 psa_get_key_bits(&attr) == 256;
    psa_reset_key_attributes(&attr);
    if (!curve || !self_signed(cert))
        goto reject;
    bool san = false;
    for (const mbedtls_x509_sequence *p = &cert->subject_alt_names; p && p->buf.p; p = p->next) {
        mbedtls_x509_subject_alternative_name name = {0};
        if (!mbedtls_x509_parse_subject_alt_name(&p->buf, &name)) {
            if (name.type == MBEDTLS_X509_SAN_DNS_NAME &&
                name.san.unstructured_name.len == strlen(host) &&
                !memcmp(name.san.unstructured_name.p, host, strlen(host)))
                san = true;
            mbedtls_x509_free_subject_alt_name(&name);
        }
    }
    uint32_t checked = 0;
    if (!san || !actell_clock_ready() ||
        mbedtls_x509_crt_check_key_usage(cert, MBEDTLS_X509_KU_DIGITAL_SIGNATURE) ||
        mbedtls_x509_crt_check_extended_key_usage(cert, MBEDTLS_OID_SERVER_AUTH,
                                                  MBEDTLS_OID_SIZE(MBEDTLS_OID_SERVER_AUTH)) ||
        mbedtls_x509_crt_verify(cert, cert, NULL, host, &checked, NULL, NULL) || checked)
        goto reject;
    unsigned char digest[32];
    char hex[65];
    size_t len = 0;
    if (psa_hash_compute(PSA_ALG_SHA_256, cert->raw.p, cert->raw.len, digest, 32, &len) !=
            PSA_SUCCESS ||
        len != 32)
        goto reject;
    for (unsigned i = 0; i < 32; i++)
        snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    if (anchor->fingerprint[0]) {
        if (strcmp(hex, anchor->fingerprint))
            goto reject;
    } else {
        size_t written = 0;
        if (mbedtls_pem_write_buffer("-----BEGIN CERTIFICATE-----\n", "-----END CERTIFICATE-----\n",
                                     cert->raw.p, cert->raw.len, (unsigned char *)anchor->pem,
                                     sizeof(anchor->pem), &written))
            goto reject;
        strcpy(anchor->fingerprint, hex);
    }
    /* Sole TOFU exception: independently validated self-issued leaf becomes this
     * transaction's exact anchor. All date/SAN/key/pin errors remain fatal. */
    return true;
reject:
    return false;
}
