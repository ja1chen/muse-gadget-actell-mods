/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* HTTP chunks need not coincide with JSON records or PCM sample boundaries. */
typedef bool (*tts_record_fn)(void *context, const char *json, size_t size);
typedef bool (*tts_bytes_fn)(void *context, const uint8_t *bytes, size_t size);
typedef struct {
    char *buffer;
    size_t capacity, length;
    unsigned depth;
    bool quoted, escaped, failed;
} tts_json_stream;

void tts_json_init(tts_json_stream *s, char *buffer, size_t capacity);
bool tts_json_feed(tts_json_stream *s, const char *data, size_t size,
                   tts_record_fn record, void *context);
bool tts_json_complete(const tts_json_stream *s);
/* Strict, bounded base64 decoding, with a small fixed stack footprint. */
bool tts_base64_decode(const char *encoded, size_t size, tts_bytes_fn output, void *context);
