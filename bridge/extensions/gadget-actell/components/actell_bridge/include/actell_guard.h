/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define ACTELL_HISTORY 64
#define ACTELL_ID_MAX 128
#define ACTELL_REQUEST_MAX 8192
#define ACTELL_JSON_MAX 16384
#define ACTELL_IMAGE_MAX (256 * 1024)
#define ACTELL_RESULT_MAX (360 * 1024)
typedef struct { char id[ACTELL_ID_MAX+1]; bool finished; } actell_receipt;
typedef struct { uint64_t generation; unsigned used; actell_receipt receipts[ACTELL_HISTORY]; } actell_guard;
bool actell_id_valid(const char *id);
bool actell_op_valid(const char *op);
bool actell_op_mutates(const char *op);
void actell_guard_session(actell_guard *g, uint64_t generation);
/* Serialized by adapter mutex. Receipt exhaustion fails closed until reconnect. */
bool actell_guard_accept(actell_guard *g, uint64_t generation, const char *id);
bool actell_guard_finish(actell_guard *g, uint64_t generation, const char *id);
#include <stddef.h>
bool actell_jpeg_dimensions(const unsigned char *data,size_t length,unsigned *width,unsigned *height);
