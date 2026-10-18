#pragma once
#include <stddef.h>
#include <stdbool.h>
/* read-only mapped raw file, exact byte length, 4-byte aligned, caller-owned.
 * Serialize with render AND draw; bind/unbind invalidates caches. After changing
 * binding, render a fresh frame before drawing. Unmap only after unbind+render.
 * bind failure preserves previously bound resource; no implicit allocation. */
bool muse_avatar_aa_bind(const void *data,size_t bytes);
void muse_avatar_aa_unbind(void);
bool muse_avatar_aa_ready(void);

/* ESP: both framebuffer and head cache must be external PSRAM. Host: true. */
bool muse_avatar_aa_memory_ready(void);
