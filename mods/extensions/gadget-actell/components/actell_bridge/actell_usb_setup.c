/* SPDX-License-Identifier: Apache-2.0 */
#include "sdkconfig.h"
#if CONFIG_ACTELL_BRIDGE_USB_SETUP
#include "actell_bridge.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <stdlib.h>
static const char *field(cJSON*j,const char*k){cJSON*v=cJSON_GetObjectItemCaseSensitive(j,k);
return cJSON_IsString(v)?v->valuestring:NULL;
}
static void say(const char*s){usb_serial_jtag_write_bytes(s,strlen(s),pdMS_TO_TICKS(1000));
}
/* Explicit development mode, verified active-low GPIO held after boot in an eight-second arming window.
 * Native USB only, no Wi-Fi/listener during the 120-second setup window. */
void actell_bridge_usb_setup(void){
 int pin=CONFIG_ACTELL_SETUP_BUTTON_GPIO;
 if(pin<0||!GPIO_IS_VALID_GPIO(pin))return;

 gpio_config_t button={.pin_bit_mask=1ULL<<(unsigned)pin,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE};

 if(gpio_config(&button)!=ESP_OK)return;
 /* Arm after the application has booted, never require a strap pin low at reset.
  * A verified BOOT GPIO0 can be released through reset, then held here. */
 int64_t arm_until=esp_timer_get_time()+8000000,low_since=0;
 bool confirmed=false;
 while(esp_timer_get_time()<arm_until){
     int64_t now=esp_timer_get_time();
     if(gpio_get_level(pin)==0){
         if(!low_since)low_since=now;
         if(now-low_since>=2000000){confirmed=true;break;}
     }else low_since=0;
     vTaskDelay(pdMS_TO_TICKS(20));
 }
 if(!confirmed)return;

 usb_serial_jtag_driver_config_t cfg=USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
cfg.rx_buffer_size=8192;

 if(nvs_flash_init()!=ESP_OK||usb_serial_jtag_driver_install(&cfg)!=ESP_OK)return;

 char*line=calloc(8192,1);
if(!line){usb_serial_jtag_driver_uninstall();return;}
size_t used=0;
bool overflow=false;
say("ACTELL USB SETUP READY\n");

 int64_t deadline=esp_timer_get_time()+120000000;

 for(;
esp_timer_get_time()<deadline;
){unsigned char ch;
if(usb_serial_jtag_read_bytes(&ch,1,pdMS_TO_TICKS(100))!=1)continue;

  if(ch=='\r')continue;
if(ch!='\n'){if(used<8191)line[used++]=(char)ch;
else overflow=true;
continue;
}
  line[used]=0;
const char*end=NULL;
cJSON*j=overflow?NULL:cJSON_ParseWithOpts(line,&end,true);
bool duplicate=false;

  for(cJSON*a=j?j->child:NULL;
a;
a=a->next)for(cJSON*b=a->next;
b;
b=b->next)if(a->string&&b->string&&!strcmp(a->string,b->string))duplicate=true;

  unsigned char secret[32];
size_t n=0;
const char*encoded=field(j,"secret_base64"),*kind=field(j,"kind"),*discovery=field(j,"discovery_service");

  cJSON*v=cJSON_GetObjectItemCaseSensitive(j,"v");
if(!v)v=cJSON_GetObjectItemCaseSensitive(j,"version");

  bool ok=cJSON_IsObject(j)&&!duplicate&&cJSON_IsNumber(v)&&v->valuedouble==1&&kind&&!strcmp(kind,"actell-muse-connection")&&discovery&&!strcmp(discovery,"_actell-muse._tcp")&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,"tls_verification_required"))&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,"pairing_required"))&&encoded&&mbedtls_base64_decode(secret,32,&n,(const unsigned char*)encoded,strlen(encoded))==0&&n==32;

  if(ok)ok=actell_bridge_provision(field(j,"endpoint"),field(j,"client_id"),secret,field(j,"certificate_pem"),field(j,"certificate_sha256"))==ESP_OK;

  volatile unsigned char*z=secret;
for(size_t i=0;
i<32;
i++)z[i]=0;
if(encoded){volatile char*e=(volatile char*)encoded;for(size_t i=0,n=strlen(encoded);i<n;i++)e[i]=0;}
cJSON_Delete(j);
z=(volatile unsigned char*)line;
for(size_t i=0;
i<8192;
i++)z[i]=0;
used=0;
overflow=false;

  say(ok?"ACTELL SETUP STORED\n":"ACTELL SETUP REJECTED\n");
if(ok)break;

 }
 volatile unsigned char*z=(volatile unsigned char*)line;
for(size_t i=0;
i<8192;
i++)z[i]=0;
free(line);
usb_serial_jtag_driver_uninstall();

}
#endif
