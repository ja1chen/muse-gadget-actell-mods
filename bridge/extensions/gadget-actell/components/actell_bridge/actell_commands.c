/* SPDX-License-Identifier: Apache-2.0 */
#include "actell_bridge.h"
#include "sdkconfig.h"

/* These descriptions travel with commands_v2. No separately installed skill
 * or model context is needed to discover the protocol. Keep the whole official
 * registration message within its 8192-byte serialization budget. */
void actell_bridge_register(cJSON *commands) {
    cJSON *rpc = cJSON_AddObjectToObject(commands, "actell.rpc");
    cJSON_AddStringToObject(rpc, "description",
        "Use the user's paired iPhone through Actell over Wi-Fi: discover and run phone tools. "
        "Muse is the planner; Actell executes using iPhone permissions. "
        "FIRST call with op=capabilities and params_json=\"{}\". The result lists exact tool names, "
        "descriptions and argument schemas; never invent them. For a read-only test, choose the "
        "returned app-listing tool. Workflow: capabilities -> runs.open -> tools.call -> calls.get. "
        "Generate unique run_id/call_id; calculate a future deadline_ms from current UTC Unix "
        "milliseconds (e.g. now+120000, at most one hour); call deadline <= run deadline. "
        "Submit each action once, then poll its original IDs until terminal. pending_approval "
        "means wait for the user on iPhone; queued/running are not success. On timeout/unknown, "
        "reconcile with calls.get; never repeat under a new ID. not_found does not prove nonexecution. "
        "If pairing_required, ask the user to connect Actell in gadget settings; never ask for secrets. "
        "Capabilities/routes describe support, not live readiness. A combined 'unlocked, foreground, "
        "idle and connected' rejection does not identify which condition failed: report the exact "
        "error without guessing phone or route state. Do not reset pairing for run-admission errors. "
#if CONFIG_ACTELL_BRIDGE_IMAGES
        "Screenshot metadata is not a visible image. Only plan coordinate actions after actual "
        "model image visibility is verified; use actell.image for the delivered JPEG."
#else
        "This firmware does not expose images to the model: do not guess screen coordinates "
        "or attempt screenshot-based tap/swipe. Nonvisual tools from capabilities can still be used."
#endif
    );
    cJSON_AddNumberToObject(rpc, "timeout_ms", 60000);
    cJSON_AddObjectToObject(rpc, "optional");
    cJSON *required = cJSON_AddObjectToObject(rpc, "required");
    cJSON *op = cJSON_AddObjectToObject(required, "op");
    cJSON_AddStringToObject(op, "type", "string");
    cJSON_AddStringToObject(op, "description",
        "Exact operation: capabilities, companion.hello, runs.open, runs.get, runs.cancel, "
        "tools.call, calls.get. Start with capabilities; it needs no run.");
    cJSON *params = cJSON_AddObjectToObject(required, "params_json");
    cJSON_AddStringToObject(params, "type", "string");
    cJSON_AddStringToObject(params, "description",
        "JSON-encoded OBJECT passed as a STRING, not a nested object. Fields by op: "
        "capabilities: {}; companion.hello: {device_name:string,protocol:1}; "
        "runs.open: {run_id:string,deadline_ms:integer}; runs.get/runs.cancel: {run_id:string}; "
        "tools.call: {run_id:string,call_id:string,deadline_ms:integer,tool:string,arguments:object,"
        "observation?:{image_id:string,generation:integer,geometry_revision:string}}; "
        "calls.get: {run_id:string,call_id:string}. IDs: 1-128 ASCII letters/digits/_/-. "
        "Use the exact tool and arguments schema returned by capabilities. "
        "Example complete command arguments: {\"op\":\"capabilities\",\"params_json\":\"{}\"}. "
        "Example polling arguments: {\"op\":\"calls.get\","
        "\"params_json\":\"{\\\"run_id\\\":\\\"your-original-run\\\","
        "\\\"call_id\\\":\\\"your-original-call\\\"}\"}. "
        "Success is payload.state=succeeded; read payload.result. Failed/expired/cancelled/unknown "
        "must be reported honestly; do not bypass forbidden/approval_required.");

#if CONFIG_ACTELL_BRIDGE_IMAGES
    cJSON *image = cJSON_AddObjectToObject(commands, "actell.image");
    cJSON_AddStringToObject(image, "description",
        "Fetch a screenshot JPEG from this user's paired iPhone. Use only after a screenshot tool "
        "succeeds; use its original run_id/image_id/generation. Output contains delivered pixels "
        "and geometry_revision. A returned image does not prove the model can see it: verify "
        "visibility before coordinate planning. Never guess coordinates from metadata.");
    cJSON_AddNumberToObject(image, "timeout_ms", 60000);
    cJSON_AddObjectToObject(image, "optional");
    required = cJSON_AddObjectToObject(image, "required");
    params = cJSON_AddObjectToObject(required, "params_json");
    cJSON_AddStringToObject(params, "type", "string");
    cJSON_AddStringToObject(params, "description",
        "JSON-encoded object STRING with run_id:string, image_id:string, generation:integer "
        "from the screenshot result. Coordinate calls also use that image's geometry_revision "
        "and integer delivered-image pixels; never rescale.");
#endif
}
