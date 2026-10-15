/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { DOUBAO_TTS_IDLE, DOUBAO_TTS_RUNNING, DOUBAO_TTS_DONE, DOUBAO_TTS_ERROR } doubao_tts_status;
/* Session-task API. start copies text; no credentials/text are logged. */
bool doubao_tts_start(const char *text);
void doubao_tts_cancel(void);
size_t doubao_tts_read(int16_t *pcm, size_t frames);
doubao_tts_status doubao_tts_state(void);
#ifdef __cplusplus
}
#endif
