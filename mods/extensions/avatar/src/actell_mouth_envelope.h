/* UI-only mouth shaping. PCM playback and the shared legacy level are unchanged. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include "muse_state.h"
#ifndef ACTELL_MOUTH_ATTACK_MS
#define ACTELL_MOUTH_ATTACK_MS 60.0f
#endif
#ifndef ACTELL_MOUTH_RELEASE_MS
#define ACTELL_MOUTH_RELEASE_MS 140.0f
#endif
#ifndef ACTELL_MOUTH_QUIET_HOLD_MS
#define ACTELL_MOUTH_QUIET_HOLD_MS 60.0f
#endif
#ifndef ACTELL_MOUTH_CLOSE_FADE_MS
#define ACTELL_MOUTH_CLOSE_FADE_MS 80.0f
#endif
typedef struct {
    int64_t last_us;
    uint32_t generation;
    float value;
    bool initialized;
} actell_mouth_envelope_t;
/* One UI consumer. sample.peak summarizes ALL producer updates since last take;
 * sample.last_voice_us is independent of UI phase and does not reset on a short
 * queue underrun. Silence confirmation and amplitude smoothing are separate. */
static inline float actell_mouth_step(actell_mouth_envelope_t *s,
                                     const muse_mouth_sample_t *sample, int64_t now_us) {
    if (!s || !sample) return 0;
    if (!sample->speaking || now_us < 0) {
        *s = (actell_mouth_envelope_t){0};
        return 0;
    }
    bool reset = !s->initialized || s->generation != sample->generation || now_us < s->last_us;
    if (reset) {
        *s = (actell_mouth_envelope_t){.last_us=now_us,.generation=sample->generation,.initialized=true};
    }
    float dt_ms = (float)(now_us - s->last_us) / 1000.0f;
    s->last_us = now_us;
    float peak = isfinite(sample->peak) ? fminf(fmaxf(sample->peak,0),1) : 0;
    float tau = peak > s->value ? ACTELL_MOUTH_ATTACK_MS : ACTELL_MOUTH_RELEASE_MS;
    if (!isfinite(tau) || tau <= 0) tau = 1;
    s->value += (peak-s->value)*(-expm1f(-dt_ms/tau));
    s->value = fminf(fmaxf(s->value,0),1);
    if (!sample->heard_voice || reset) return 0;
    float quiet_ms = now_us >= sample->last_voice_us ? (float)(now_us-sample->last_voice_us)/1000.0f : 0;
    float hold = fmaxf(ACTELL_MOUTH_QUIET_HOLD_MS,0);
    float fade = fmaxf(ACTELL_MOUTH_CLOSE_FADE_MS,1);
    float activity = fminf(fmaxf(1-(quiet_ms-hold)/fade,0),1);
    /* Preserve the old open-mouth amplitude range, but allow its baseline to
     * fade continuously to a closed line after confirmed silence. */
    return activity*((.65f+2.5f*s->value)/3.15f);
}
