#pragma once
#include <stddef.h>
#include <stdint.h>
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 1
int psa_crypto_init(void);
int psa_hash_compute(int algorithm, const void *input, size_t size, uint8_t *output, size_t capacity, size_t *written);
