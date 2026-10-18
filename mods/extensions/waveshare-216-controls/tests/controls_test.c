#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include "sdk_events.h"
#include "waveshare_216_controls.h"

enum { MUSE_MODE_IDLE, MUSE_MODE_OFF, MUSE_MODE_TALK };
static bool asleep, speaker = true, s_talk_down;
static int mode, sleeps, wakes, shutdowns, toggles, releases;
static struct { const char *power_button, *speaker_button; } board = { "PWR", "boot" }, *muse_board = &board;
static bool muse_state_asleep(void) { return asleep; }
static void set_asleep(bool value, const char *why) {
    (void)why;
    if (value != asleep) { if (value) sleeps++; else wakes++; }
    asleep = value;
}
static void muse_state_poke(void) {}
static int muse_state_mode(void *unused) { (void)unused; return mode; }
static bool muse_settings_speaker_on(void) { return speaker; }
static void muse_settings_set_speaker_on(bool on) { speaker = on; toggles++; }
static void muse_state_set_caption(const char *s) { (void)s; }
static void talk_button(unsigned ev) {
    if (ev & MUSE_BTN_TALK_PRESS) { set_asleep(false, "Talk"); s_talk_down = true; }
    if (ev & MUSE_BTN_TALK_RELEASE) { s_talk_down = false; releases++; }
}
static void power_off(void) { shutdowns++; /* model failed shutdown too */ }
/* Actual pinned+patched input handlers, extracted by the Python test runner. */
#include "sdk_input.inc"
static void run_dedicated(unsigned ev) { dedicated_buttons(ev, muse_state_asleep()); }
static void combined_keys(unsigned ev) {
    bool before = muse_state_asleep();
    talk_button(ev);
    dedicated_buttons(ev, before);
}

static waveshare_216_controls_t state;
static unsigned longs_in_press;
static unsigned poll_key(bool ok, unsigned pmu) {
    unsigned ev = waveshare_216_power(&state, ok, pmu);
    int before_sleep = sleeps;
    if (ev & (MUSE_BTN_POWER_PRESS | MUSE_BTN_POWER_CANCEL)) longs_in_press = 0;
    if (ev & MUSE_BTN_POWER_LONG) assert(longs_in_press++ == 0);
    run_dedicated(ev);
    if (ev & MUSE_BTN_POWER_LONG) assert(sleeps == before_sleep);
    return ev;
}
static void reset(void) {
    state = (waveshare_216_controls_t){0};
    asleep = s_talk_down = s_power_down = s_power_swallow = false;
    speaker = true;
    mode = MUSE_MODE_IDLE;
    sleeps = wakes = shutdowns = toggles = releases = 0;
    longs_in_press = 0;
}
#define P MUSE_PMU_KEY_PRESS
#define R MUSE_PMU_KEY_RELEASE
#define L MUSE_PMU_KEY_LONG
#define C MUSE_PMU_KEY_CLICK

int main(void) {
    reset(); /* startup hold, orphan click/release; next real press counts */
    assert(!poll_key(true, L)); assert(!poll_key(true, R|C));
    poll_key(true, P|R|C); assert(sleeps == 1 && !shutdowns);
    poll_key(true, C); assert(sleeps == 1);
    poll_key(true, P|R|C); assert(wakes == 1 && !asleep);

    reset(); poll_key(true, P); poll_key(true, L|R);
    assert(shutdowns == 1 && !sleeps);
    poll_key(true, 0); assert(!s_power_down && !sleeps);
    poll_key(true, R|C); assert(shutdowns == 1 && !sleeps);
    poll_key(true, P|R|C); assert(sleeps == 1); /* failed shutdown re-arms */

    reset(); poll_key(true, P|L|R); poll_key(true, 0);
    assert(shutdowns == 1 && !sleeps && !s_power_down);
    reset(); poll_key(true, P); poll_key(true, L); poll_key(true, L);
    poll_key(true, R); assert(shutdowns == 1 && !sleeps);

    reset(); asleep = true; poll_key(true, P); poll_key(true, L|R);
    poll_key(true, 0); assert(wakes == 1 && !sleeps && !shutdowns);

    reset(); poll_key(true, P); poll_key(false, 0);
    assert(!s_power_down && !sleeps && !shutdowns);
    poll_key(true, P|L|R); /* replay after failed clear must be discarded */
    poll_key(true, L); poll_key(true, R); poll_key(true, 0);
    poll_key(true, P|R); assert(sleeps == 1 && !shutdowns);
    reset(); poll_key(false, 0); poll_key(false, 0);
    poll_key(true, P|R); assert(!sleeps); poll_key(true, 0);
    poll_key(true, P|R); assert(sleeps == 1);

    reset(); poll_key(true, P); poll_key(true, P|R|L);
    assert(!sleeps && !shutdowns && !s_power_down);
    poll_key(true, 0); poll_key(true, P|R); assert(sleeps == 1);

    reset(); s_talk_down = true; mode = MUSE_MODE_TALK;
    run_dedicated(MUSE_BTN_SPEAKER_PRESS);
    run_dedicated(MUSE_BTN_SPEAKER_RELEASE);
    assert(toggles == 1 && !speaker && s_talk_down && !shutdowns);
    run_dedicated(MUSE_BTN_SPEAKER_PRESS); assert(toggles == 2 && speaker);
    poll_key(true, P); poll_key(true, L);
    assert(shutdowns == 1 && releases == 1 && !s_talk_down);
    reset(); asleep = true; run_dedicated(MUSE_BTN_SPEAKER_PRESS);
    assert(wakes == 1 && toggles == 0 && speaker);
    run_dedicated(MUSE_BTN_SPEAKER_RELEASE);
    run_dedicated(MUSE_BTN_SPEAKER_PRESS); assert(toggles == 1);

    reset(); asleep = true;
    run_dedicated(MUSE_BTN_POWER_PRESS | MUSE_BTN_POWER_RELEASE | MUSE_BTN_SPEAKER_PRESS);
    assert(wakes == 1 && sleeps == 0 && toggles == 0 && !asleep);
    reset();
    run_dedicated(MUSE_BTN_POWER_PRESS | MUSE_BTN_POWER_RELEASE | MUSE_BTN_SPEAKER_PRESS);
    assert(sleeps == 1 && wakes == 0 && toggles == 1 && asleep);
    reset(); s_talk_down = true;
    run_dedicated(MUSE_BTN_POWER_PRESS | MUSE_BTN_POWER_LONG | MUSE_BTN_SPEAKER_PRESS);
    assert(shutdowns == 1 && toggles == 0 && releases == 1 && !s_talk_down);
    run_dedicated(MUSE_BTN_POWER_RELEASE); assert(!sleeps);

    reset(); asleep = true;
    combined_keys(MUSE_BTN_TALK_PRESS | MUSE_BTN_SPEAKER_PRESS | MUSE_BTN_POWER_PRESS | MUSE_BTN_POWER_RELEASE);
    assert(wakes == 1 && sleeps == 0 && toggles == 0 && s_talk_down);
    reset();
    combined_keys(MUSE_BTN_TALK_PRESS | MUSE_BTN_SPEAKER_PRESS | MUSE_BTN_POWER_PRESS | MUSE_BTN_POWER_LONG);
    assert(shutdowns == 1 && toggles == 0 && releases == 1 && !s_talk_down);

    /* Exhaust all short PMU/error sequences: no software timer, at most one
     * LONG until release/cancel, and no held state after explicit cancellation. */
    for (unsigned sequence = 0; sequence < 83521; sequence++) {
        reset();
        unsigned bits = sequence;
        for (int step = 0; step < 4; step++, bits /= 17) {
            unsigned event = bits % 17;
            if (state.release_pending) poll_key(true, 0);
            poll_key(event != 16, event);
        }
        poll_key(false, 0);
        assert(!s_power_down && !state.down && !state.release_pending);
    }
    puts("controls: real dedicated handlers + 83521 event/error sequences passed");
}
