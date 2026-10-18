/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "tts_stream.h"
typedef bool (*doubao_active_fn)(void *context);
/* One connection/session per submitted segment. No reconnect, retry or fallback. */
bool doubao_ws_synthesize(const char *text, void *context, doubao_active_fn active,
                          tts_bytes_fn audio, uint8_t *scratch, size_t capacity);
