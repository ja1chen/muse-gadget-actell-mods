#pragma once
#include <stddef.h>
typedef struct {size_t len;unsigned char *p;} fake_buf;
typedef struct mbedtls_x509_crt {struct mbedtls_x509_crt *next;fake_buf raw,issuer_raw,subject_raw;} mbedtls_x509_crt;
void mbedtls_x509_crt_init(mbedtls_x509_crt *);
void mbedtls_x509_crt_free(mbedtls_x509_crt *);
int mbedtls_x509_crt_parse(mbedtls_x509_crt *,const unsigned char *,size_t);
int mbedtls_x509_crt_get_ca_istrue(const mbedtls_x509_crt *);
