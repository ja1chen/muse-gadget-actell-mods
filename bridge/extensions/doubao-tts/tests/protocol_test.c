/* SPDX-License-Identifier: Apache-2.0. Synthetic V1 protocol vectors. */
#include "doubao_protocol.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void)
{
    uint8_t out[256];
    const uint8_t start[] = {0x11,0x14,0x10,0,0,0,0,1,0,0,0,2,'{','}'};
    assert(doubao_pack(out, sizeof(out), DOUBAO_START_CONNECTION, NULL, "{}") == sizeof(start));
    assert(!memcmp(out, start, sizeof(start)));
    const uint8_t finish[] = {0x11,0x14,0x10,0,0,0,0,102,0,0,0,1,'s',0,0,0,2,'{','}'};
    assert(doubao_pack(out, sizeof(out), DOUBAO_FINISH_SESSION, "s", "{}") == sizeof(finish));
    assert(!memcmp(out, finish, sizeof(finish)));
    assert(!doubao_pack(out, sizeof(finish)-1, DOUBAO_FINISH_SESSION, "s", "{}"));
    assert(!doubao_pack(out, sizeof(out), DOUBAO_TASK, NULL, "{}"));
    char id[130]; memset(id, 'a', sizeof(id)); id[129] = 0;
    assert(!doubao_pack(out, sizeof(out), DOUBAO_TASK, id, "{}"));
    const uint8_t audio[] = {0x11,0xb4,0,0,0,0,1,96,0,0,0,1,'s',0,0,0,3,0,1,2};
    doubao_packet packet;
    assert(doubao_unpack(audio, sizeof(audio), &packet));
    assert(packet.event == 352 && packet.type == 11 && packet.id_size == 1 && packet.id[0] == 's');
    assert(packet.payload_size == 3 && !memcmp(packet.payload, "\0\1\2", 3));
    for (size_t n = 0; n < sizeof(audio); ++n) assert(!doubao_unpack(audio, n, &packet));
    memcpy(out, audio, sizeof(audio)); out[2] = 1;
    assert(!doubao_unpack(out, sizeof(audio), &packet)); /* gzip not requested */
    memcpy(out, audio, sizeof(audio)); out[1] = 0xf0;
    assert(!doubao_unpack(out, sizeof(audio), &packet)); /* error frame */
    memcpy(out, audio, sizeof(audio)); out[0] = 0x21;
    assert(!doubao_unpack(out, sizeof(audio), &packet));
    memcpy(out, audio, sizeof(audio)); out[1] = 0xb5;
    assert(!doubao_unpack(out, sizeof(audio), &packet));
    memcpy(out, audio, sizeof(audio)); out[8] = 0xff;
    assert(!doubao_unpack(out, sizeof(audio), &packet));
    memcpy(out, audio, sizeof(audio)); out[16] = 4;
    assert(!doubao_unpack(out, sizeof(audio), &packet));
    memcpy(out, audio, sizeof(audio)); out[sizeof(audio)] = 0;
    assert(!doubao_unpack(out, sizeof(audio)+1, &packet));
    puts("Doubao V1 request vectors, audio envelope and malformed lengths: passed");
}
