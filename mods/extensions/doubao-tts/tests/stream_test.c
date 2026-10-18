/* SPDX-License-Identifier: Apache-2.0. Synthetic protocol vectors only. */
#include "tts_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { unsigned records; char text[4096]; size_t size; } capture;
static bool record(void *ctx, const char *json, size_t n)
{
    capture *c = ctx;
    assert(n && json[0] == '{' && json[n-1] == '}' && json[n] == 0);
    c->records++;
    return true;
}
static bool bytes(void *ctx, const uint8_t *p, size_t n)
{
    capture *c = ctx;
    assert(c->size + n <= sizeof(c->text));
    memcpy(c->text + c->size, p, n); c->size += n;
    return true;
}
static bool reject(void *ctx, const uint8_t *p, size_t n)
{ (void)ctx; (void)p; (void)n; return false; }

int main(void)
{
    const char *wire = " \r\n{\"code\":0,\"data\":\"AAEC\",\"nested\":[{\"q\":\"} \\\" \\\\ {\"}]}\n{\"code\":20000000}";
    size_t length = strlen(wire);
    for (size_t split = 0; split <= length; split++) {
        char storage[1024]; tts_json_stream s; capture c = {0};
        tts_json_init(&s, storage, sizeof(storage));
        assert(tts_json_feed(&s, wire, split, record, &c));
        assert(tts_json_feed(&s, wire + split, length - split, record, &c));
        assert(tts_json_complete(&s) && c.records == 2);
    }
    char storage[1024]; tts_json_stream s; capture c = {0};
    tts_json_init(&s, storage, sizeof(storage));
    for (size_t i = 0; i < length; i++) assert(tts_json_feed(&s, wire+i, 1, record, &c));
    assert(tts_json_complete(&s) && c.records == 2);
    tts_json_init(&s, storage, 4);
    assert(!tts_json_feed(&s, "{\"a\":0}", 7, record, &c));
    assert(!tts_json_complete(&s));
    assert(!tts_json_feed(&s, "{}", 2, record, &c));
    tts_json_init(&s, storage, sizeof(storage));
    assert(tts_json_feed(&s, "{\"a\":", 5, record, &c));
    assert(!tts_json_complete(&s));
    tts_json_init(&s, storage, sizeof(storage));
    assert(!tts_json_feed(&s, "data: {}", 8, record, &c));
    tts_json_init(&s, storage, sizeof(storage));
    assert(!tts_json_feed(&s, "{\0}", 3, record, &c));
    char deep[65]; memset(deep, '{', sizeof(deep));
    tts_json_init(&s, storage, sizeof(storage));
    assert(!tts_json_feed(&s, deep, sizeof(deep), record, &c));

    c.size = 0;
    assert(tts_base64_decode("AAECA/7/", 8, bytes, &c));
    const uint8_t expected[] = {0,1,2,3,254,255};
    assert(c.size == sizeof(expected) && !memcmp(c.text, expected, sizeof(expected)));
    const char *invalid[] = {"A", "AA", "AAA", "!!!!", "=AAA", "A===", "AA=A", "AA==AAAA", "AB==", "AAB=", "AA A"};
    for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++)
        assert(!tts_base64_decode(invalid[i], strlen(invalid[i]), bytes, &c));
    c.size = 0;
    assert(tts_base64_decode("AA==", 4, bytes, &c));
    assert(tts_base64_decode("AQI=", 4, bytes, &c));
    assert(c.size == 3 && !memcmp(c.text, "\0\1\2", 3));
    assert(!tts_base64_decode("AAAA", 4, reject, NULL));
    char large[4096]; memset(large, 'A', sizeof(large)); c.size = 0;
    assert(tts_base64_decode(large, sizeof(large), bytes, &c));
    assert(c.size == 3072);
    puts("TTS fragmented JSON, bounds and strict base64: passed");
}
