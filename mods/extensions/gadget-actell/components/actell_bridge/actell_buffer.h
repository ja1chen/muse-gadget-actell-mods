/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
typedef struct {
    unsigned char *data;
    size_t length, capacity, limit;
} actell_buffer;
typedef enum { ACTELL_BUFFER_OK, ACTELL_BUFFER_LIMIT, ACTELL_BUFFER_MEMORY } actell_buffer_result;
actell_buffer_result actell_buffer_init(actell_buffer *, size_t limit, size_t expected);
actell_buffer_result actell_buffer_append(actell_buffer *, const void *, size_t);
void actell_buffer_free(actell_buffer *);
