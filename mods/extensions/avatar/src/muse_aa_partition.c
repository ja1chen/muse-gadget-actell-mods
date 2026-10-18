#include "muse_aa_partition.h"
#include "muse_pixel_aa_resource.h"
#include "muse_pixel_aa_assets.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#if defined(ESP_PLATFORM) && (!defined(CONFIG_MUSE_AVATAR_V2) || !CONFIG_MUSE_AVATAR_V2)
esp_err_t muse_aa_partition_open(const esp_partition_t *p, size_t o, size_t n) {
    (void)p; (void)o; (void)n;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t muse_aa_partition_close(const muse_pose_t *p) {
    (void)p;
    return ESP_OK;
}
#else
static bool mapping_owned;
static esp_partition_mmap_handle_t mapping_handle;

esp_err_t muse_aa_partition_open(const esp_partition_t *p,
                                 size_t offset, size_t length) {
    if (mapping_owned || muse_avatar_aa_ready()) return ESP_ERR_INVALID_STATE;
    if (!p || p->type != ESP_PARTITION_TYPE_DATA ||
        length != (size_t)64 + AA_RAW_BYTES || (offset & 3u) ||
        (p->address & 3u) || offset > p->size || length > p->size - offset)
        return ESP_ERR_INVALID_ARG;
    if (!muse_avatar_aa_memory_ready()) return ESP_ERR_INVALID_STATE;
    const void *mapped = NULL;
    esp_partition_mmap_handle_t handle;
    esp_err_t err = esp_partition_mmap(p, offset, length,
                                     ESP_PARTITION_MMAP_DATA, &mapped, &handle);
    if (err != ESP_OK) return err;
    if (!muse_avatar_aa_bind(mapped, length)) {
        esp_partition_munmap(handle);
        return ESP_ERR_INVALID_CRC;
    }
    mapping_handle = handle;
    mapping_owned = true;
    return ESP_OK;
}

esp_err_t muse_aa_partition_close(const muse_pose_t *fallback_pose) {
    if (!mapping_owned) return ESP_OK;
    if (!fallback_pose) return ESP_ERR_INVALID_ARG;
    muse_avatar_aa_unbind();
    muse_pixel_render(fallback_pose);
    esp_partition_munmap(mapping_handle);
    mapping_owned = false;
    mapping_handle = 0;
    return ESP_OK;
}

#endif
