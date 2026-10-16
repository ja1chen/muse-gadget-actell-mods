/* Independent Waveshare 2.16 power latch reducer. See root LICENSE. */
#pragma once
#include <stdbool.h>

/* Include muse_board.h and muse_pmu.h first. No GPIO, clock or I2C dependency. */
typedef struct {
    bool down;
    bool long_sent;
    bool release_pending;
    bool quarantine;
} waveshare_216_controls_t;

static inline unsigned waveshare_216_power(waveshare_216_controls_t *s,
                                           bool ok, unsigned pmu)
{
    if (!ok) {
        unsigned ev = s->down ? MUSE_BTN_POWER_CANCEL : 0;
        *s = (waveshare_216_controls_t){ .quarantine = true };
        return ev;
    }
    /* After an uncertain read/ack, discard even successfully cleared replays.
     * Only a clean empty latch can restore eligibility for a NEW press. */
    if (s->quarantine) {
        if (!pmu) s->quarantine = false;
        return 0;
    }
    if (s->release_pending) {
        /* Caller drains this before the next I2C read, so no snapshot is lost. */
        *s = (waveshare_216_controls_t){0};
        return MUSE_BTN_POWER_RELEASE;
    }
    bool press = pmu & MUSE_PMU_KEY_PRESS;
    bool release = pmu & MUSE_PMU_KEY_RELEASE;
    bool long_key = pmu & MUSE_PMU_KEY_LONG;
    unsigned ev = 0;
    if (s->down && press) {
        /* An old release mixed with another press has no recoverable order.
         * Cancel the current transaction; do not invent a second action. */
        *s = (waveshare_216_controls_t){ .quarantine = true };
        return MUSE_BTN_POWER_CANCEL;
    }
    if (!s->down) {
        /* Boot hold LONG/RELEASE and orphan CLICK never arm a transaction. */
        if (!press) return 0;
        s->down = true;
        ev = MUSE_BTN_POWER_PRESS;
    }
    if (long_key && !s->long_sent) {
        s->long_sent = true;
        ev |= MUSE_BTN_POWER_LONG;
    }
    if (release) {
        if (ev & MUSE_BTN_POWER_LONG) {
            /* dedicated_buttons consumes release before LONG when already
             * down. Defer release one input loop so shutdown wins. */
            s->release_pending = true;
        } else {
            ev |= MUSE_BTN_POWER_RELEASE;
            *s = (waveshare_216_controls_t){0};
        }
    }
    /* CLICK is redundant with edges; never generate a second short press. */
    return ev;
}
