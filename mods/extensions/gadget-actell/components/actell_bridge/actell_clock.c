/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_lan.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include <stdatomic.h>
#include <time.h>
/* A plausible RTC/build date is not proof of synchronization this boot. */
static atomic_bool synchronized;
static void clock_synchronized(struct timeval *value) {
    (void)value;
    atomic_store(&synchronized, true);
    ESP_LOGI("actell.clock", "phase=synchronized");
    actell_pair_clock_synchronized();
}
bool actell_clock_ready(void) {
    /* Called by the serialized Actell worker; SNTP callback runs separately. */
    static bool started;
    if (!started) {
        esp_sntp_set_time_sync_notification_cb(clock_synchronized);
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
        started = true;
    }
    return atomic_load(&synchronized) && time(NULL) >= 1700000000;
}
