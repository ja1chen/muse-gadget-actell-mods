/* SPDX-License-Identifier: Apache-2.0 */
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#include "actell_avatar.c"
#if defined(CONFIG_MUSE_AVATAR_V2) && CONFIG_MUSE_AVATAR_V2
#include "muse_pixel_160.c"
#else
#include "muse_pixel.c"
#endif

#ifdef ESP_PLATFORM
#include "muse_pixel_aa_init.c"
#endif
