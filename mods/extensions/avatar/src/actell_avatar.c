/* SPDX-License-Identifier: Apache-2.0 */
#if !defined(ESP_PLATFORM)
#define _POSIX_C_SOURCE 200809L
#endif
#include "actell_avatar.h"
#include <stdatomic.h>
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#elif !defined(ACTELL_AVATAR_TEST)
#include <time.h>
#endif
#if defined(ESP_PLATFORM)
/* Xtensa GCC reports ATOMIC_INT_LOCK_FREE=1. Use short SDK critical sections
 * for the state snapshot rather than assuming unavailable lock-free atomics.
 * No allocation, clock call, rendering, or I/O occurs inside the lock. */
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool state_enabled;
static unsigned state_activity;
static uint64_t state_expiry;
#else
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "avatar needs lock-free 32-bit int");
_Static_assert(sizeof(unsigned int) == 4, "avatar needs 32-bit int");
/* Single dispatcher writer; any number of readers. A seqlock snapshot WITHOUT
 * retry: a frame racing an update simply renders the normal base pose. 64-bit
 * time is split into lock-free words, avoiding ESP32 64-bit atomic locks. */
static atomic_uint revision, enabled, activity, expires_lo, expires_hi;
#endif
static uint64_t now_ms(void) {
#ifdef ACTELL_AVATAR_TEST
    extern uint64_t actell_avatar_test_clock_ms(void);
    return actell_avatar_test_clock_ms();
#elif defined(ESP_PLATFORM)
    return (uint64_t)(esp_timer_get_time() / 1000);
#else
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return UINT64_MAX;
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
#endif
}
#if defined(ESP_PLATFORM)
void actell_avatar_set_enabled(bool on) {
    portENTER_CRITICAL(&state_lock);
    state_enabled = on;
    if (!on) state_activity = ACTELL_AVATAR_NONE;
    portEXIT_CRITICAL(&state_lock);
}
bool actell_avatar_enabled(void) {
    portENTER_CRITICAL(&state_lock);
    bool on = state_enabled;
    portEXIT_CRITICAL(&state_lock);
    return on;
}
void actell_avatar_set_activity(actell_avatar_activity_t a, uint32_t ttl) {
    if (ttl > 60000) ttl = 60000;
    uint64_t now = now_ms();
    if (a < ACTELL_AVATAR_READ || a > ACTELL_AVATAR_WRITE || !ttl || now > UINT64_MAX - ttl)
        a = ACTELL_AVATAR_NONE;
    portENTER_CRITICAL(&state_lock);
    if (!state_enabled) a = ACTELL_AVATAR_NONE;
    state_activity = (unsigned)a;
    state_expiry = a == ACTELL_AVATAR_NONE ? 0 : now + ttl;
    portEXIT_CRITICAL(&state_lock);
}
actell_avatar_activity_t actell_avatar_activity(void) {
    portENTER_CRITICAL(&state_lock);
    bool on = state_enabled;
    unsigned a = state_activity;
    uint64_t expiry = state_expiry;
    portEXIT_CRITICAL(&state_lock);
    return !on || now_ms() >= expiry ? ACTELL_AVATAR_NONE : (actell_avatar_activity_t)a;
}
#else
void actell_avatar_set_enabled(bool on) {
    atomic_fetch_add(&revision,1);
    atomic_store(&enabled,on);
    if(!on) atomic_store(&activity,ACTELL_AVATAR_NONE);
    atomic_fetch_add(&revision,1);
}
bool actell_avatar_enabled(void) { return atomic_load(&enabled)!=0; }
void actell_avatar_set_activity(actell_avatar_activity_t a,uint32_t ttl) {
    if(ttl>60000) ttl=60000;
    uint64_t now=now_ms();
    if(!atomic_load(&enabled) || a<ACTELL_AVATAR_READ || a>ACTELL_AVATAR_WRITE || !ttl || now>UINT64_MAX-ttl) a=ACTELL_AVATAR_NONE;
    uint64_t expiry=a==ACTELL_AVATAR_NONE?0:now+ttl;
    atomic_fetch_add(&revision,1);
    atomic_store(&expires_lo,(uint32_t)expiry);
    atomic_store(&expires_hi,(uint32_t)(expiry>>32));
    atomic_store(&activity,(unsigned)a);
    atomic_fetch_add(&revision,1);
}
actell_avatar_activity_t actell_avatar_activity(void) {
    unsigned before=atomic_load(&revision);
    if(before&1) return ACTELL_AVATAR_NONE;
    unsigned on=atomic_load(&enabled), a=atomic_load(&activity);
    uint64_t expiry=(uint64_t)atomic_load(&expires_hi)<<32;
    expiry|=atomic_load(&expires_lo);
    if(before!=atomic_load(&revision) || !on || now_ms()>=expiry) return ACTELL_AVATAR_NONE;
    return (actell_avatar_activity_t)a;
}

#endif
