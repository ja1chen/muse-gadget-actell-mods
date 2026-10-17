/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_bridge.h"
#include "actell_flat.h"
#include "sdkconfig.h"
void actell_bridge_register(cJSON *commands){
#if CONFIG_ACTELL_BRIDGE_IMAGES
 actell_flat_register(commands,true);
#else
 actell_flat_register(commands,false);
#endif
}
bool actell_bridge_handles(const char *command){return actell_flat_command(command);}
