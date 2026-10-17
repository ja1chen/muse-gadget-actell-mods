#pragma once
#include <stddef.h>
#define PSA_ALG_SHA_256 1
#define PSA_SUCCESS 0
int psa_crypto_init(void);
int psa_hash_compute(int,const unsigned char *,size_t,unsigned char *,size_t,size_t *);
