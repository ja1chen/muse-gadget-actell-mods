/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define ACTELL_HISTORY 64
#define ACTELL_ID_MAX 128
#define ACTELL_REQUEST_MAX (64 * 1024)
/* A completed app list is much larger than its queued receipt. Match the
 * bounded thumbnail budget while remaining below the official result budget. */
#define ACTELL_JSON_MAX (256 * 1024)
#define ACTELL_IMAGE_MAX (256 * 1024)
#define ACTELL_RESULT_MAX (360 * 1024)
/* Work slots are recycled only after completion. Mutating invoke IDs have an
 * independent exact SHA-256 fence retained for the whole control generation. */
#define ACTELL_WRITE_HISTORY 1024
#define ACTELL_STOP_RESERVE 32
#define ACTELL_ID_DIGEST 32
typedef struct { char id[ACTELL_ID_MAX+1]; bool finished,mutates,has_payload_digest; unsigned char payload_digest[32]; } actell_receipt;
typedef struct {
 uint64_t generation;
 unsigned used,next_slot,write_used;
 actell_receipt receipts[ACTELL_HISTORY];
 unsigned char write_ids[ACTELL_WRITE_HISTORY][ACTELL_ID_DIGEST];
} actell_guard;
typedef enum {
 ACTELL_ADMIT_NEW, ACTELL_ADMIT_PENDING, ACTELL_ADMIT_FINISHED,
 ACTELL_ADMIT_BUSY, ACTELL_ADMIT_WRITE_LIMIT, ACTELL_ADMIT_INVALID
} actell_admission;
bool actell_id_valid(const char *id);
bool actell_op_valid(const char *op);
bool actell_op_mutates(const char *op);
void actell_guard_session(actell_guard *g, uint64_t generation);
/* Serialized by adapter mutex. digest is SHA-256 of the validated invoke ID.
 * Collisions fail closed; completed write IDs are never evicted in a generation. */
actell_admission actell_guard_admit(actell_guard *,uint64_t,const char *,const char *op,
                                  const unsigned char digest[ACTELL_ID_DIGEST]);
/* Roll back only a NEW receipt whose job never entered the worker queue. */
void actell_guard_unsent(actell_guard *,uint64_t,const char *,const unsigned char digest[ACTELL_ID_DIGEST]);
bool actell_guard_finish(actell_guard *g, uint64_t generation, const char *id);
#include <stddef.h>
bool actell_jpeg_dimensions(const unsigned char *data,size_t length,unsigned *width,unsigned *height);
