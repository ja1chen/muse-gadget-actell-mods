/* SPDX-License-Identifier: Apache-2.0 */
#include "tts_stream.h"
#include <string.h>

void tts_json_init(tts_json_stream *s, char *buffer, size_t capacity)
{
    *s = (tts_json_stream){.buffer = buffer, .capacity = capacity};
}

bool tts_json_feed(tts_json_stream *s, const char *data, size_t size,
                   tts_record_fn record, void *context)
{
    if (s->failed) return false;
    for (size_t i = 0; i < size; ++i) {
        unsigned char c = (unsigned char)data[i];
        if (!s->length && (c == ' ' || c == '\r' || c == '\n' || c == '\t')) continue;
        if ((!s->length && c != '{') || !c || s->length + 1 >= s->capacity) goto fail;
        s->buffer[s->length++] = (char)c;
        if (s->quoted) {
            if (s->escaped) s->escaped = false;
            else if (c == '\\') s->escaped = true;
            else if (c == '"') s->quoted = false;
        } else {
            if (c == '"') s->quoted = true;
            else if (c == '{') { if (++s->depth > 64) goto fail; }
            else if (c == '}') {
                if (!s->depth) goto fail;
                if (!--s->depth) {
                    s->buffer[s->length] = '\0';
                    if (!record(context, s->buffer, s->length)) goto fail;
                    s->length = 0;
                }
            }
        }
    }
    return true;
fail:
    s->failed = true;
    return false;
}

bool tts_json_complete(const tts_json_stream *s)
{
    return !s->failed && !s->length && !s->depth && !s->quoted;
}

static int digit(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool tts_base64_decode(const char *encoded, size_t size, tts_bytes_fn output, void *context)
{
    uint8_t bytes[768];
    size_t used = 0;
    if (size % 4) return false;
    for (size_t i = 0; i < size; i += 4) {
        int a = digit(encoded[i]), b = digit(encoded[i+1]);
        int c = encoded[i+2] == '=' ? 0 : digit(encoded[i+2]);
        int d = encoded[i+3] == '=' ? 0 : digit(encoded[i+3]);
        bool pad2 = encoded[i+2] == '=', pad1 = encoded[i+3] == '=';
        if (a < 0 || b < 0 || c < 0 || d < 0 || (pad2 && !pad1) ||
            ((pad1 || pad2) && i + 4 != size) || (pad2 && (b & 15)) ||
            (pad1 && !pad2 && (c & 3))) return false;
        bytes[used++] = (uint8_t)((a << 2) | (b >> 4));
        if (!pad2) bytes[used++] = (uint8_t)((b << 4) | (c >> 2));
        if (!pad1) bytes[used++] = (uint8_t)((c << 6) | d);
        if (used >= sizeof(bytes) - 3) {
            if (!output(context, bytes, used)) return false;
            used = 0;
        }
    }
    return !used || output(context, bytes, used);
}
