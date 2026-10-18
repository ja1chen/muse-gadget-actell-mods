/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Owned JPEG; move through the result queue, never duplicate or log its bytes. */
typedef struct {
    unsigned char *data;
    size_t length;
    char operation_id[65];
    int64_t queued_at_us, invoked_at_us;
} actell_image_blob;
typedef struct { size_t prefix, metadata_length, encoded_length, wire_length; } actell_image_layout;
/* Metadata must contain exactly one empty data_base64 property. No ownership transfer. */
bool actell_image_layout_init(actell_image_layout *, const char *metadata,
                             size_t jpeg_length, size_t wire_limit);
/* Random-access, bounded working storage; preserves base64 across arbitrary chunk boundaries. */
size_t actell_image_stream_read(const actell_image_layout *, const char *metadata,
                               const unsigned char *jpeg, size_t jpeg_length,
                               size_t offset, unsigned char *out, size_t capacity);
#ifdef __cplusplus
}
#endif
