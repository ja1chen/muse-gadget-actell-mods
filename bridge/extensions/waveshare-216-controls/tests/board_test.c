#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "sdk_events.h"
#include "waveshare_216_controls.h"
#define PMU_KEY_INTERVAL_US 20000
#define PMU_WAIT_MS 50
#define ESP_OK 0
#define ESP_LOGW(...) ((void)0)
typedef int esp_err_t;
typedef int muse_gpio_button_t;
static muse_gpio_button_t s_key = 1, s_boot = 2;
static waveshare_216_controls_t s_power;
static bool s_pmu_ready = true;
static unsigned key_event, boot_event, pmu_event;
static int pmu_error, pmu_reads, waited;
static int64_t now;
static unsigned muse_gpio_button_poll(muse_gpio_button_t *key) {
    unsigned result = key == &s_key ? key_event : boot_event;
    if(key == &s_key) key_event = 0; else boot_event = 0;
    return result;
}
static int64_t esp_timer_get_time(void) { return now; }
static esp_err_t muse_pmu_poll_key_checked(unsigned *event) {
    pmu_reads++; *event=pmu_error?0:pmu_event; pmu_event=0; return pmu_error;
}
static void muse_gpio_buttons_wait(muse_gpio_button_t *const *buttons, int count, int timeout) {
    assert(count==2 && buttons[0]==&s_key && buttons[1]==&s_boot); waited=timeout;
}
#include "sdk_board.inc"
int main(void) {
    key_event=MUSE_BTN_TALK_PRESS; boot_event=MUSE_BTN_TALK_PRESS;
    pmu_event=MUSE_PMU_KEY_PRESS|MUSE_PMU_KEY_RELEASE|MUSE_PMU_KEY_CLICK;
    assert(poll_buttons()==(MUSE_BTN_TALK_PRESS|MUSE_BTN_SPEAKER_PRESS|MUSE_BTN_POWER_PRESS|MUSE_BTN_POWER_RELEASE));
    assert(pmu_reads==1);
    now=10000; key_event=MUSE_BTN_TALK_RELEASE; boot_event=MUSE_BTN_TALK_RELEASE;
    pmu_event=MUSE_PMU_KEY_PRESS;
    assert(poll_buttons()==(MUSE_BTN_TALK_RELEASE|MUSE_BTN_SPEAKER_RELEASE));
    assert(pmu_reads==1); /* wall clock, not input loop count */
    now=5000000; assert(poll_buttons()==MUSE_BTN_POWER_PRESS); assert(pmu_reads==2);
    now+=20000; pmu_event=MUSE_PMU_KEY_LONG|MUSE_PMU_KEY_RELEASE;
    assert(poll_buttons()==MUSE_BTN_POWER_LONG);
    pmu_event=MUSE_PMU_KEY_PRESS; /* queued release must not lose next I2C batch */
    now+=50000; assert(poll_buttons()==MUSE_BTN_POWER_RELEASE); assert(pmu_reads==3);
    assert(poll_buttons()==MUSE_BTN_POWER_PRESS); assert(pmu_reads==4);
    now+=20000; pmu_error=-7; key_event=MUSE_BTN_TALK_PRESS; boot_event=MUSE_BTN_TALK_PRESS;
    assert(poll_buttons()==(MUSE_BTN_POWER_CANCEL|MUSE_BTN_TALK_PRESS|MUSE_BTN_SPEAKER_PRESS));
    now+=20000; pmu_error=0; pmu_event=MUSE_PMU_KEY_PRESS|MUSE_PMU_KEY_LONG;
    assert(poll_buttons()==0); /* successful ack of stale event remains quarantined */
    now+=20000; assert(poll_buttons()==0);
    now+=20000; pmu_event=MUSE_PMU_KEY_PRESS|MUSE_PMU_KEY_RELEASE;
    assert(poll_buttons()==(MUSE_BTN_POWER_PRESS|MUSE_BTN_POWER_RELEASE));
    wait_buttons(5000); assert(waited==50);
    wait_buttons(50); assert(waited==50);
    wait_buttons(17); assert(waited==17);
    wait_buttons(-1); assert(waited==50);
    s_pmu_ready=false; now+=500000; pmu_event=MUSE_PMU_KEY_PRESS;
    int before=pmu_reads; assert(poll_buttons()==0 && pmu_reads==before);
    puts("board: real GPIO/PMU routing, wall-clock polling, deferred release and 50ms wait cap passed");
}
