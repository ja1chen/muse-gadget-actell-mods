#pragma once
#include <sys/time.h>
#define SNTP_OPMODE_POLL 0
void esp_sntp_set_time_sync_notification_cb(void (*callback)(struct timeval *));
void esp_sntp_setoperatingmode(int mode);
void esp_sntp_setservername(int index,const char *server);
void esp_sntp_init(void);
