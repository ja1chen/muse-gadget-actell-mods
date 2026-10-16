/* SPDX-License-Identifier: Apache-2.0 */
#include "doubao_protocol.h"
#include <string.h>

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

size_t doubao_pack(uint8_t *out, size_t capacity, uint32_t event,
                   const char *session, const char *json)
{
    if (!out || !json) return 0;
    bool connection = event == DOUBAO_START_CONNECTION || event == DOUBAO_FINISH_CONNECTION;
    size_t id_size = session ? strlen(session) : 0, payload = strlen(json);
    if ((!connection && (!id_size || id_size > 128)) || payload > UINT32_MAX) return 0;
    size_t head = 12 + (connection ? 0 : 4 + id_size);
    if (head > capacity || payload > capacity - head) return 0;
    out[0] = 0x11; out[1] = 0x14; out[2] = 0x10; out[3] = 0;
    put32(out + 4, event);
    size_t at = 8;
    if (!connection) {
        put32(out + at, (uint32_t)id_size); at += 4;
        memcpy(out + at, session, id_size); at += id_size;
    }
    put32(out + at, (uint32_t)payload); at += 4;
    memcpy(out + at, json, payload);
    return at + payload;
}

bool doubao_unpack(const uint8_t *data, size_t size, doubao_packet *out)
{
    if (!data || !out || size < 16 || data[0] != 0x11 || data[3] ||
        (data[1] & 15) != 4 || (data[2] & 15)) return false;
    uint8_t type = data[1] >> 4;
    if ((type != 9 && type != 11) || (data[2] >> 4) != (type == 9 ? 1 : 0)) return false;
    uint32_t event = get32(data + 4), id_size = get32(data + 8);
    if (id_size > 128 || id_size > size - 16) return false;
    size_t at = 12 + id_size;
    uint32_t payload_size = get32(data + at);
    if (payload_size != size - at - 4) return false;
    *out = (doubao_packet){.event = event, .type = type, .id = data + 12,
        .id_size = id_size, .payload = data + at + 4, .payload_size = payload_size};
    return true;
}
