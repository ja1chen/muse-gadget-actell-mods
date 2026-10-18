/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { ACTELL_AVATAR_NONE = 0, ACTELL_AVATAR_READ = 1,
               ACTELL_AVATAR_WRITE = 2 } actell_avatar_activity_t;
/* Only the two phone actions are enabled/disabled. Default: disabled.
 * These calls never mutate Muse state, allocate or perform I/O.
 * ESP targets use short critical sections for a coherent state snapshot;
 * host builds use lock-free words. Neither waits for animation or networking.
 * TTL is clamped to 60 seconds; zero clears. Refresh for longer operations.
 * SETTERS MUST run on one serialized event-dispatch task (single writer).
 * Readers/rendering may run concurrently. Do not call setters from separate
 * UI/network tasks; forward all events to that one dispatcher.
 * A transport must validate/order events before invoking this local sink. */
void actell_avatar_set_enabled(bool enabled);
bool actell_avatar_enabled(void);
void actell_avatar_set_activity(actell_avatar_activity_t activity, uint32_t ttl_ms);
actell_avatar_activity_t actell_avatar_activity(void);
#ifdef __cplusplus
}
#endif
