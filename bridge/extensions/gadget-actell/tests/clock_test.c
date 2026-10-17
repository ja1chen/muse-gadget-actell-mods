/* Synthetic clock only: no SNTP network, RTC or real device access. */
#include "actell_lan.h"
#include "esp_sntp.h"
#include <assert.h>
#include <string.h>
#include <time.h>
void test_http_log(const char *tag,const char *format,...) { (void)tag;(void)format; }
static time_t wall = 1800000000;
static unsigned starts,notifications;
void actell_pair_clock_synchronized(void) { notifications++; }
static void (*notify)(struct timeval *);
time_t time(time_t *out) { if(out)*out=wall; return wall; }
void esp_sntp_set_time_sync_notification_cb(void (*callback)(struct timeval *)) { notify=callback; }
void esp_sntp_setoperatingmode(int mode) { assert(mode==SNTP_OPMODE_POLL); }
void esp_sntp_setservername(int index,const char *name) { assert(!index&&!strcmp(name,"pool.ntp.org")); }
void esp_sntp_init(void) { starts++; }
int main(void) {
    /* Valid-looking RTC must still start SNTP and block unsigned-clock RPC. */
    assert(!actell_clock_ready() && starts==1 && notify);
    assert(!actell_clock_ready() && starts==1);
    wall=0; assert(!actell_clock_ready());
    wall=1800000120; struct timeval value={0}; notify(&value);
    assert(actell_clock_ready() && starts==1 && notifications==1);
    wall=0; assert(!actell_clock_ready());
    wall=1800000240; notify(&value); assert(actell_clock_ready());
    return 0;
}
