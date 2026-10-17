#pragma once
#include <stddef.h>
#include "esp_err.h"
#include "esp_partition.h"
#include "muse_pixel.h"
/* Optional singleton adapter. No partition lookup, writes, allocation or upload.
 * Caller supplies an already verified DATA partition and avatar entry range.
 * Call all operations under the SAME lock as render AND decoder/draw, between
 * complete frame cycles. While this adapter owns a mapping, never call the raw
 * bind/unbind API independently. The mapped flash range must remain immutable.
 * open refuses replacement: close first, then open the new verified entry.
 * After successful open, render a fresh frame before permitting any draw.
 * close unbinds, renders a fallback frame, then releases its owned mapping.
 * No app/OTA/NVS/font partition address or label is guessed here. */
esp_err_t muse_aa_partition_open(const esp_partition_t *partition,
                                 size_t avatar_offset, size_t avatar_length);
esp_err_t muse_aa_partition_close(const muse_pose_t *fallback_pose);
