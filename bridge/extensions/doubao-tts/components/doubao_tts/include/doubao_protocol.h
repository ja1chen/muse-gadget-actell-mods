/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    DOUBAO_START_CONNECTION = 1, DOUBAO_FINISH_CONNECTION = 2,
    DOUBAO_CONNECTION_STARTED = 50, DOUBAO_CONNECTION_FINISHED = 52,
    DOUBAO_START_SESSION = 100, DOUBAO_CANCEL_SESSION = 101, DOUBAO_FINISH_SESSION = 102,
    DOUBAO_SESSION_STARTED = 150, DOUBAO_SESSION_FINISHED = 152,
    DOUBAO_USAGE = 154, DOUBAO_TASK = 200,
    DOUBAO_SENTENCE_START = 350, DOUBAO_SENTENCE_END = 351,
    DOUBAO_AUDIO = 352, DOUBAO_SUBTITLE = 364,
};
typedef struct {
    uint32_t event;
    uint8_t type;
    const uint8_t *id, *payload;
    size_t id_size, payload_size;
} doubao_packet;

/* V1 event-bearing protocol, no compression. Network byte order, bounded IDs. */
size_t doubao_pack(uint8_t *out, size_t capacity, uint32_t event,
                   const char *session, const char *json);
bool doubao_unpack(const uint8_t *data, size_t size, doubao_packet *out);
