/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_buffer.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

actell_buffer_result actell_buffer_init(actell_buffer *b, size_t limit, size_t expected) {
    memset(b, 0, sizeof(*b));
    if (limit == SIZE_MAX || expected > limit) return ACTELL_BUFFER_LIMIT;
    b->limit = limit;
    b->capacity = expected ? expected : (limit < 1024 ? limit : 1024);
    b->data = malloc(b->capacity + 1);
    if (!b->data) return ACTELL_BUFFER_MEMORY;
    b->data[0] = 0;
    return ACTELL_BUFFER_OK;
}
actell_buffer_result actell_buffer_append(actell_buffer *b, const void *data, size_t length) {
    if (length > b->limit - b->length) return ACTELL_BUFFER_LIMIT;
    size_t needed = b->length + length;
    if (needed > b->capacity) {
        size_t capacity = b->capacity ? b->capacity : 1;
        while (capacity < needed) {
            if (capacity > b->limit / 2) { capacity = b->limit; break; }
            capacity *= 2;
        }
        unsigned char *grown = realloc(b->data, capacity + 1);
        if (!grown) return ACTELL_BUFFER_MEMORY;
        b->data = grown;
        b->capacity = capacity;
    }
    if (length) memcpy(b->data + b->length, data, length);
    b->length = needed;
    b->data[b->length] = 0;
    return ACTELL_BUFFER_OK;
}
void actell_buffer_free(actell_buffer *b) {
    free(b->data);
    memset(b, 0, sizeof(*b));
}
