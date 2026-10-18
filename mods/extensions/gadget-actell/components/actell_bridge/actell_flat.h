/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "cJSON.h"
#include <stdbool.h>
/* Flat interface: executor owns in-memory identity and per-invoke lifecycle. */
#define ACTELL_NORMALIZED_INPUT "normalized-display-v1"
void actell_flat_register(cJSON *commands, bool images);
bool actell_flat_command(const char *command);
bool actell_flat_validate(const char *command, const cJSON *arguments);
bool actell_flat_capability(const cJSON *capabilities);
/* Borrowing arguments; caller owns returned params. Identity/deadline supplied
 * by an executor, never inferred from the paired client/control generation. */
cJSON *actell_flat_call(const char *command, const cJSON *arguments,
                       const char *run_id, const char *call_id, double deadline_ms);

/* Worker-local executor seam. prepare must bind pairing identity + invoke
 * identity + canonical command/arguments + fixed run/call/deadline BEFORE I/O.
 * It returns 1 only for a new operation retained in memory, 0 for reconciliation,
 * and -1 on identity/conflict errors. lookup is pairing-scoped and must
 * not treat absence as permission to resubmit. */
typedef struct {
 char operation_id[129], run_id[129], call_id[129];
 double deadline_ms;
} actell_flat_record;
typedef struct {
 int (*prepare)(void *,const char *,const char *,const cJSON *,actell_flat_record *);
 bool (*lookup)(void *,const char *,actell_flat_record *);
 cJSON *(*step)(void *,const char *,const cJSON *);
 bool (*current)(void *);
 long long (*remaining_ms)(void *);
 void (*pause)(void *,unsigned);
 void *context;
} actell_flat_executor;
cJSON *actell_flat_execute(const char *command,const cJSON *arguments,
                           const char *invoke_id,const actell_flat_executor *executor);
