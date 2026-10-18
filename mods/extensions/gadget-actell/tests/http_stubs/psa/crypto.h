#pragma once
#include <stddef.h>
#define PSA_SUCCESS 0
#define PSA_KEY_ATTRIBUTES_INIT 0
#define PSA_MAC_OPERATION_INIT 0
#define PSA_KEY_TYPE_HMAC 1
#define PSA_KEY_USAGE_SIGN_MESSAGE 1
#define PSA_ALG_SHA_256 1
#define PSA_ALG_HMAC(x) (x)
typedef int psa_key_attributes_t;
typedef int psa_key_id_t;
typedef int psa_mac_operation_t;
int psa_crypto_init(void);
void psa_set_key_type(psa_key_attributes_t *,int);
void psa_set_key_usage_flags(psa_key_attributes_t *,int);
void psa_set_key_algorithm(psa_key_attributes_t *,int);
int psa_import_key(const psa_key_attributes_t *,const unsigned char *,size_t,psa_key_id_t *);
int psa_mac_sign_setup(psa_mac_operation_t *,psa_key_id_t,int);
int psa_mac_update(psa_mac_operation_t *,const unsigned char *,size_t);
int psa_mac_sign_finish(psa_mac_operation_t *,unsigned char *,size_t,size_t *);
void psa_mac_abort(psa_mac_operation_t *);
void psa_destroy_key(psa_key_id_t);
void psa_reset_key_attributes(psa_key_attributes_t *);
