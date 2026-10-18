# Gadget command adapter and flat phone tools

The RPC envelope and exact-byte ACTELL1 authentication remain v1. Official Muse
account/BLE pairing and `/link-control` remain upstream-owned. No `/link-tunnel`,
VM LAN proxy or new gadget service is used. SLAN pairing is unchanged.

The [Flat Tools contract](FLAT-TOOLS-1.md) defines Actell's
normalized-display-v1 input and thumbnail-v2 historical images. The gadget now
registers screenshot, tap, drag, home, list_apps, launch_app, type_text, wait and
call_status under `actell.`. It does not expose rpc/image/view aliases. Arguments
contain no run or image identity; unsupported process-argument arrays are omitted.

## Invocation and results

`actell_flat.c` validates unique fields, integer coordinates 0..1000, duration,
purpose, bundle-ID and UTF-8 text bounds. The SDK raw JSON guard additionally
rejects embedded NUL before cJSON can discard string length. Request JSON is
bounded to 64 KiB to accommodate escaping of up to 8192 UTF-8 text bytes; this is
separate from the unchanged 8192-byte registration serialization ceiling.

`actell_flat_execute.c` requires a worker-local operation identity record before phone I/O.
It negotiates independent_input and normalized-display-v1, creates a run with a
fixed explicit deadline, submits once and polls original IDs. Confirmed terminal
calls close only their own run. Pending/unknown calls are not cancelled by an
idle heuristic. Local wait creates no phone run. The fixed SDK exposes invoke
identity and connection generation, but no task identity, task-end event or
cancellation hook. One run per invoke is the explicit fallback; task-wide grouping
and cancellation are not implemented. No 60-second run or five-second idle policy
is added.

Screenshot performs capture, polling and images.get in one invocation. The existing
JPEG checks retain request/image identity, generation, geometry revision, SHA-256,
MIME, SOF dimensions, 1280-pixel long edge and 256-KiB bound. The outer payload keeps
`format: jpeg-base64` and `data_base64` at the official camera-result location,
adding operation_id, execution state, effect, image_status, capture/expiry time and
normalized-display-v1 coordinates. Capture succeeded with image unavailable is
not successful image delivery. Status retrieves only the original cached image,
never a replacement capture. Image retention is independent of input admission.

The gadget retains the validated JPEG and streams base64 into the existing
length-prefixed `link.result` body chunks. This changes local buffering, not the
wire schema or the number of model calls. Queue/session cleanup owns the JPEG;
the 360-KiB result limit covers the metadata and generated base64 together.
A network locator may be reused only within one invocation after a successful
exchange; TLS identity validation and HMAC are repeated for every request.
Failures discard the locator without replaying an action.

## In-memory identity protection

Before phone I/O, a worker-owned RAM cache binds the invoke fingerprint,
canonical arguments, pairing ownership and fixed deadline. Full SHA-256-derived
IDs address original calls; 128-bit cached fingerprints fail closed on conflict.
The cache retains the latest 64 operations (3.5 KiB of records), recycling the
oldest slot only when the serialized worker starts a subsequent operation.
Status queries do not allocate records. An evicted or reboot-lost operation is
reported unavailable; it never triggers a replacement action through call_status.
The separate admission guard retains completed write IDs for the entire control
generation, even after a cache slot is recycled. Reboot clears both RAM structures;
there is no device-side durable replay protection or historical lookup guarantee
across reboot, or across generations after cache eviction. The client must never
resend an uncertain action as a new request.

Operation history has no NVS read, write, recovery or startup cleanup path.
Legacy operation data on the existing device was removed during one-time
maintenance. Pairing persists separately as the compact `peer_v3` record;
older pairing records are neither loaded nor migrated. Auto-connect, official
account and Wi-Fi settings remain independent.

Duplicate delivery in the same live connection receives an error with the original
operation ID. Missing records never authorize a replacement action. First-receipt
loss remains a platform recovery limitation.

For registration budgets, tests, deployment boundaries and remaining platform
limitations see [integration status](../actell-flat-tools-integration-status.md).

正常终态关闭 run 后，仅以原 runs.get 的 execution_released=true 确认执行权已释放；在剩余预算内只读轮询，缺字段、超时或失败则返回 run_released=false，不抹去已确认动作结果，也不重新提交动作。跨 App 后台续接由 Actell 在现有用户选择和真实执行收据下处理，Gadget 不跨任务共享 run 或虚构任务身份。
