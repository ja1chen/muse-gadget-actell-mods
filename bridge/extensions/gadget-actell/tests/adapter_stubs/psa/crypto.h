#pragma once
#include "../../http_stubs/psa/crypto.h"
int psa_hash_compute(int,const unsigned char *,size_t,unsigned char *,size_t,size_t *);
