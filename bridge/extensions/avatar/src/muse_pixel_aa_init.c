/* SPDX-License-Identifier: Apache-2.0 */
/* Startup-only catalog lookup. No writes, background task or runtime rebinding. */
#include "sdkconfig.h"
#if defined(CONFIG_MUSE_AVATAR_V2) && CONFIG_MUSE_AVATAR_V2
#include "muse_aa_partition.c"
#include "esp_log.h"
#include "cJSON.h"
#include "psa/crypto.h"
#include <stdlib.h>
static uint32_t aa_catalog_u32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static bool aa_catalog_integer(const cJSON *v, uint32_t *out) {
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble<0 ||
        v->valuedouble>0x7d0000 || floor(v->valuedouble)!=v->valuedouble) return false;
    *out=(uint32_t)v->valuedouble; return true;
}
void muse_avatar_aa_init(void) {
    static bool tried;
    if (tried) return;
    tried=true;
    if (!muse_avatar_aa_memory_ready()) {
        ESP_LOGE("avatar_aa", "PSRAM placement invalid; avatar rendering disabled");
        return;
    }
    ESP_LOGI("avatar_aa", "Image buffers verified in external PSRAM");
    const esp_partition_t *p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x40,"actell_assets");
    uint8_t h[64], digest[32]; size_t written=0;
    char *directory=NULL; cJSON *root=NULL;
    if (!p || p->size>0x7d0000 || p->size<65536 || esp_partition_read(p,0,h,sizeof h)!=ESP_OK) goto fail;
    uint32_t n=aa_catalog_u32(h+12), count=aa_catalog_u32(h+16), total=aa_catalog_u32(h+20);
    if (memcmp(h,"ACTRES01",8) || aa_catalog_u32(h+8)!=1 || !n || n>8192 ||
        !count || count>32 || total<65536 || total>p->size || memcmp(h+56,(uint8_t[8]){0},8)) goto fail;
    directory=malloc(n+1);
    if (!directory || esp_partition_read(p,64,directory,n)!=ESP_OK) goto fail;
    directory[n]=0;
    if (psa_crypto_init()!=PSA_SUCCESS || psa_hash_compute(PSA_ALG_SHA_256,(const uint8_t*)directory,n,digest,sizeof digest,&written)!=PSA_SUCCESS ||
        written!=32 || memcmp(digest,h+24,32)) goto fail;
    const char *end;
    root=cJSON_ParseWithLengthOpts(directory,n,&end,false);
    if (!root || end!=directory+n || !cJSON_IsObject(root)) goto fail;
    uint32_t abi;
    cJSON *entries=cJSON_GetObjectItemCaseSensitive(root,"entries");
    if (!aa_catalog_integer(cJSON_GetObjectItemCaseSensitive(root,"reader_abi"),&abi) || abi!=1 ||
        !cJSON_IsArray(entries) || cJSON_GetArraySize(entries)!=(int)count) goto fail;
    const char *ids[32]; uint32_t previous=65536, avatar_offset=0, avatar_length=0;
    for (uint32_t i=0;i<count;i++) {
        cJSON *e=cJSON_GetArrayItem(entries,i), *id=cJSON_GetObjectItemCaseSensitive(e,"id"), *fmt=cJSON_GetObjectItemCaseSensitive(e,"format");
        uint32_t offset,length;
        if (!cJSON_IsString(id) || !cJSON_IsString(fmt) || !id->valuestring[0] || strlen(id->valuestring)>96 ||
            !aa_catalog_integer(cJSON_GetObjectItemCaseSensitive(e,"offset"),&offset) ||
            !aa_catalog_integer(cJSON_GetObjectItemCaseSensitive(e,"length"),&length) || !length ||
            offset%65536 || offset<previous || offset>total || length>total-offset) goto fail;
        ids[i]=id->valuestring;
        for (uint32_t j=0;j<i;j++) if (!strcmp(ids[j],ids[i])) goto fail;
        previous=offset+length;
        if (!strcmp(id->valuestring,"avatar.aa")) {
            if (strcmp(fmt->valuestring,"actell-avatar-aa-raw-v1")) goto fail;
            avatar_offset=offset; avatar_length=length;
        }
    }
    if (previous!=total || !avatar_offset) goto fail;
    cJSON_Delete(root);root=NULL;free(directory);directory=NULL;
    esp_err_t err=muse_aa_partition_open(p,avatar_offset,avatar_length);
    if (err!=ESP_OK) {
        ESP_LOGW("avatar_aa", "Raw mapping/bind unavailable (%d); procedural fallback",(int)err);
        return;
    }
    ESP_LOGI("avatar_aa", "Raw AA ready: %u bytes, 102400 external buffer bytes",(unsigned)avatar_length);
    return;
fail:
    cJSON_Delete(root);free(directory);
    ESP_LOGW("avatar_aa", "Raw resource absent or incompatible; procedural fallback");
}
#else
void muse_avatar_aa_init(void) {}
#endif
