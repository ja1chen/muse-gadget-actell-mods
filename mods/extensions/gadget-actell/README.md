# Independent gadget → Actell component

This is the real ESP-IDF implementation, separate from the historical Python
transport experiment, optional avatar, and headless LAN text console. The official
Muse account/pairing and `/link-control` Noise channel remain upstream-owned.
Actell requests go directly from gadget Wi-Fi to the iPhone's authenticated HTTPS
listener. Disable `CONFIG_HOMEHUB_TUNNEL`; no VM Home Link socket API is needed.

## Installation and target validation

Use an untouched copy of pinned Muse SDK 812c46f9fe42b69d916613a9dc702cbac5ff7b54:

```
python mods/extensions/gadget-actell/tools/stage.py /path/to/sdk-copy --check
python mods/extensions/gadget-actell/tools/stage.py /path/to/sdk-copy
```

The patch connects official registration, invoke dispatch, Noise session lifecycle,
optional development USB setup, component dependencies, and the Actell settings page.
It starts the existing Actell worker after storage/network initialization. Official
BLE, accounts, screen drivers, headless APIs and avatar implementation stay intact.
The component is compiled only when enabled; dependencies remain declared during
ESP-IDF pre-configuration resolution, including disabled builds.

## Simple LAN pairing

Normal pairing uses [SLAN/1](../../../docs/slan-pairing.md): enable the existing iPhone
listener and Add Muse window, open Actell in device settings and tap Pair with Actell,
then allow its request on iPhone. Credentials are saved as pending_ack before an
authenticated hello; only `pairing_state=active` completes pairing. Pending permissions stay empty; first activation applies the default scopes,
and later reconnects preserve user changes. No manual certificate transfer or USB bootstrap is required.

First pairing is TOFU on trusted Wi-Fi and does not resist an active LAN attacker.
Request/claim stay bound to one temporary DER pin; subsequent RPC uses the saved
exact leaf and existing HMAC. No BLE/QR/ESP server or extra permanent task is added.
The public build overlay contains empty account/Wi-Fi settings and disables signing;
full build success does not establish device/iPhone acceptance.

Set `CONFIG_ACTELL_BRIDGE=y`, `CONFIG_HOMEHUB_TUNNEL=n` with ESP-IDF **v6.0.1**.
Use the upstream board overlay. Both display and headless builds use this exact
component. Apply the separate headless patch only for its text I/O. No code assumes
that an arbitrary board has a screen, camera, microphone, touch input or button.
No firmware was flashed by these development tools.

## Trusted one-time bootstrap (development compatibility)

Actell generates its independent TLS identity and a short-lived invitation. The
invitation contains a secret: never paste it into chat or put it in source control.
It includes `certificate_pem`, DER SHA256 `certificate_sha256`, endpoint with trusted
TLS hostname and port, client ID and a 32-byte secret, `pairing_required:true`,
`tls_verification_required:true`, and `_actell-muse._tcp` discovery service.

1. Explicitly enable development `CONFIG_ACTELL_BRIDGE_USB_SETUP=y`, and set
   `CONFIG_ACTELL_SETUP_BUTTON_GPIO` to a physically verified active-low
   button. Default `-1` disables setup. Do not reuse a display/flash/peripheral pin.
2. After the user authorizes deployment and bootstrap, connect trusted native USB
   serial/JTAG, release the button during reset, then hold it for two seconds within the
   first eight seconds of normal app startup. No LAN service is
   started during the 120-second USB setup window.
3. Save the invitation in a local mode-0600 file and explicitly run
   `python3 mods/extensions/gadget-actell/tools/provision_usb.py --port /dev/... --invitation local/credentials/invitation.json`.
   It does not echo the invitation. The gadget validates its certificate fingerprint,
   rejects a CA certificate/bundle or a non-self-issued leaf, stores the private configuration in its own NVS
   namespace, clears temporary buffers, closes the USB driver and starts Muse normally.
   Timeout also closes the window and starts normally. No NVS erase or second flash.
4. The worker sends authenticated `companion.hello` after Wi-Fi/control startup.
   The iPhone asks for per-device screenshot/list-apps/control authorization. Until
   authorized, operations fail closed. Invitation expiry is enforced by Actell.

The dev gate is a physical trusted-USB bootstrap, not protection against someone
with physical firmware/NVS access. NVS is not claimed encrypted. Disable the setup
option in production builds. No secret import/export is exposed on the headless
LAN page; that development text page retains its existing isolated-LAN warning.

After bootstrap, mDNS only locates a candidate with the matching public fingerprint
TXT and port. Advertised hostnames/IPs never become a trust anchor. Requests connect
to its IPv4 address, with the saved hostname for TLS verification and HTTP Host. The
only trust anchor is the saved non-CA leaf certificate; TLS handshake proof-of-key, hostname and
validity verification stay enabled. No arbitrary-certificate acceptance or skip-CN
option exists. The currently implemented discovery route is IPv4; IPv6-only LANs
fail discovery rather than silently use a weaker connection.

## Official command interface

The firmware advertises self-contained phone-tool descriptions and direct parameters
in official `commands_v2`; no separate model-context injection is required. Read the
[usage skill](../skills/gadget-actell-remote/SKILL.md). The gadget, not the model,
negotiates capabilities. Native regression executes the actual SDK serializer with
synthetic identity and optional tools to enforce the 8 KiB budget. Runtime audit
lines contain fixed operation/state words, capacity counts and opaque timing correlation
IDs, never phone content, parameters, credentials or account/device identity.

One worker retains at most one authenticated TLS connection for up to ten minutes
of business inactivity. Hello can borrow it without extending that deadline. The
thirty-second network locator cache remains separate. Optional completion hints
reclaim only idle resources; queued/active work and a new control generation fence
cleanup. No chat subscription is started, extended or owned by this policy. Actell
independently checks media ownership before honoring `companion.release_idle`.
See [transport lifecycle](../../../docs/protocol/ACTELL-PERF-1.md).

The advertised interface is nine flat commands: `actell.screenshot`, `actell.tap`,
`actell.drag`, `actell.home`, `actell.list_apps`, `actell.launch_app`,
`actell.type_text`, `actell.wait` and `actell.call_status`. There are no public
`actell.rpc/image/view` aliases. Screenshot returns the official JPEG-base64 payload
and metadata in one invocation; inputs use normalized-display-v1 coordinates,
0..1000, without image/run identifiers or mandatory capture. Process arguments
are omitted until the platform array transport is verified.

Screenshot delivery retains one validated JPEG and small metadata, then generates
base64 directly into the SDK's existing control body chunks. The queued image
has explicit ownership and is freed on delivery, queue rejection, session change
or disconnect. No full base64 string or image-sized serialized JSON is required
on this path; the receiver still gets one ordinary `link.result` JSON with
`format: jpeg-base64` and `data_base64`. The wire limit includes the virtual
base64 bytes. This reduces resident memory, not wire size; JPEG remains fully
buffered for identity, dimensions and hash checks before transmission.

Complete HTTP responses can reuse the same verified TLS connection within an
invocation and across consecutive successful invocations. One serialized worker
owns a single idle slot, bound to saved identity, Wi-Fi network and Muse control
generation. Failure, identity/network/generation change, ten minutes business-idle or
sixteen total requests close it; idle expiry is enforced without another request.
Pairing actions discard the slot. Hello borrows it without extending business idle;
a cold hello closes its own connection. Local tool waits also reap it at expiry.
A thirty-second locator cache also avoids repeat discovery across invocations.
Its TTL applies before a new connection, never to an otherwise live connection.
Cached connection attempts are capped at two seconds; only failure before any body
write attempt may fall back once to discovery. Every RPC is freshly signed; failed
phone operations are never replayed. Peers advertising bounded completion/release
waits can avoid unnecessary status queries while preserving actual release checks.

[The performance contract](../../../docs/protocol/ACTELL-PERF-1.md) describes limits,
old-peer fallback and correlated timing through image validation and SDK upload.
Opaque operation/request IDs and numeric measurements are logged without parameters,
images, account/device identity or credentials. Platform receipt and LLM timing need
separate instrumentation; host tests and compilation do not establish a speedup.

The gadget negotiates capabilities and internally manages one run per invocation.
It submits once, polls original IDs, and closes only its own confirmed terminal run.
There is no fixed 60-second run or five-second idle release. The pinned SDK lacks
platform task/cancel events; no task-wide cancellation or lossless initial-result
recovery is claimed.

Capability preflight failures retain the original error code and message in the
outer result, including available transport stage, HTTP status and receive progress.
They remain `failed` / `not_sent`: no run or phone tool is submitted, and the
capability request is not automatically retried. Missing error details retain the
generic `capability_unavailable` fallback; a successful response with unsupported
features remains `incompatible_capability`. This does not persist the failure
details in the operation cache or change historical status lookup.

With external BSS enabled, the 3.5 KiB operation record array and 5.2 KiB
phone-activity tracking tables reside in PSRAM instead of internal DRAM. They
remain volatile, worker-owned data; host tests and targets without external BSS
use ordinary RAM. No additional allocation, I/O or runtime fallback is introduced.

The worker retains an idle HTTP/TLS connection only with at least 24 KiB free
DMA-capable memory and a 2 KiB contiguous block. Otherwise it closes the completed
connection before result handoff; idle maintenance also checks pressure. This
keeps 8 KiB above the SDK's unchanged 16 KiB sender reserve. Active/borrowed
requests are never interrupted, and the short locator cache survives. The next
request authenticates a fresh connection; pressure cleanup sends no extra RPC
and never replays the phone action.

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
maintenance. Pairing persists separately as compact `peer_v3`: 78 fixed bytes
plus the actual origin, client ID and PEM lengths, without string padding or
terminators. The pin is 32 binary bytes. Reads validate every length, phase,
version and certificate/pin; old formats are neither read nor migrated. Forget
erases only the current key. Auto-connect, official account and Wi-Fi settings
remain independent. Existing old pairing requires explicitly authorized cleanup
and new approval; the production firmware has no startup cleanup path.

The phone retains bounded thumbnail-v2 results for 30 minutes independently of input
admission. Status reads can return the original image after run closure. A completed
capture with an unavailable JPEG remains capture success but outer delivery failure;
it never silently captures again. JPEG size, hash, identity and dimensions retain
existing checks. Image visibility and actual phone input require separate physical
acceptance; source and host tests are not deployment evidence.

See [the shared contract](../../../docs/protocol/FLAT-TOOLS-1.md),
[adapter protocol](../../../docs/protocol/gadget-v2.md) and
[integration status](../../../docs/actell-flat-tools-integration-status.md).

## Bounds and outcome semantics

- One worker; twenty queued requests, excluding the active request; flat arguments bounded by their schema; JSON ingress and HTTP requests ≤64 KiB;
  JSON responses ≤256 KiB; JPEG ≤256 KiB; result payload under 360 KiB.
- Registration contains nine phone commands when images are enabled, within the SDK's 8-KiB budget.
  Full board registration must still be measured in target/runtime validation.
- Sixty-second advertised timeout, 40-second local queue deadline, 15-second network
  observation budget plus bounded discovery. Socket I/O can consume its configured
  timeout before the next absolute-deadline check. No unbounded retries or queue.
- Up to 64 live/recent invocation work slots are retained; only finished slots
  are recycled, so sequential read-only queries do not exhaust a session budget.
  A retained pending duplicate shares the original completion attempt. A completed
  duplicate returns `invoke_result_unavailable` synchronously; it does not imply
  delivery or nonexecution, and never resubmits a phone action. Query the operation ID using a new read-only invocation. Once an old read-only slot has been
  recycled, that read-only ID may enter a fresh read-only query rather than a cached result;
  the peer may still reject that ID under its anti-replay policy. Prefer a new
  transport invocation ID when reconciling the same original run/call.
- Accepted mutating invocation IDs also have independent SHA-256 tombstones for the
  whole control generation, including uncertain outcomes and lost result delivery.
  These fences survive work-slot recycling and apply even if a duplicate changes
  its operation. There are 1,024 fences: ordinary writes stop at 992, retaining the internal stop reserve. At full write capacity only new writes are
  rejected with `write_receipt_limit`; read-only reconciliation still works.
  Do not reconnect or create replacement action IDs to bypass this protection.
  This admission guard is independent of the bounded recent-operation RAM cache. Existing phone run/call reconciliation remains required.
- Receipt state uses about 41 KiB, preferring PSRAM; initialization fails closed if
  allocation fails. Queue rejection rolls back only a receipt known never to have
  entered the worker. Session changes fence queued old work. Each body-write chunk
  is checked against generation/deadline, including after TLS connection.
- Exactly one result **submission attempt** per accepted live invocation. Upstream's
  bounded result queue can drop a result or old generation. This is not exactly-once
  network delivery. If a result is lost, query original run/call IDs.
- No write retry on error/timeout; the result is `unknown`. Restart never restores or
  replays outgoing actions. `not_found` is not proof of nonexecution. Already-sent
  bytes/actions cannot be rolled back by session loss or local cancellation.
- `runs.cancel` uses the independent server protocol and the existing Actell execution
  queue. It cannot undo an already-dispatched phone action. Do not automatically
  create a replacement call ID for an unknown action.
- No avatar events are inferred from request submission or a pending approval.
  The normal bridge profile enables `CONFIG_ACTELL_BRIDGE_AVATAR_ACTIVITY`, using the
  unchanged approved avatar setters only for authenticated running/succeeded
  read/write results, with 1500/350-ms leases, 16 tracked IDs, terminal fencing
  and no waits/I/O. Missing renderer has no effect on RPC completion.

## Host tests

```
PYTHONPATH=mods/host python3 -B -m unittest discover -s mods/host/tests -p 'test_gadget_component.py' -v
```

Host tests cover the real guards, SLAN codec, worker and NVS adapter with synthetic I/O. They do not establish target
compilation, TLS interoperability, Bonjour operation, iPhone authorization, device
execution, or model image visibility. Consult the separately recorded build results.

## Combining existing overlays

Verified staging order, starting from a separate clean SDK copy:

```
git -C /path/to/sdk-copy apply --check /path/to/muse-gadget-actell-mods/mods/patches/muse-gadget-sdk/headless.patch
git -C /path/to/sdk-copy apply /path/to/muse-gadget-actell-mods/mods/patches/muse-gadget-sdk/headless.patch
python /path/to/muse-gadget-actell-mods/mods/extensions/gadget-actell/tools/stage.py /path/to/sdk-copy --check
python /path/to/muse-gadget-actell-mods/mods/extensions/gadget-actell/tools/stage.py /path/to/sdk-copy
python /path/to/muse-gadget-actell-mods/mods/extensions/avatar/tools/stage.py --sdk-root /path/to/sdk-copy
```

The approved renderer and artwork are copied unchanged; the state sink includes the ESP portability fix described below. On a headless profile they are
not rendered; no screen is invented. For a display build use its official board
profile. Headless build defaults can use `overlays/sdkconfig.headless-s3` after the
upstream defaults and `overlays/sdkconfig.bridge` last. This is a compile profile,
not a claim about any attached module. Development LAN text I/O must be separately
enabled with `CONFIG_MUSE_HEADLESS_HTTP_DEBUG=y` on an isolated trusted LAN.

The SDK's plain ESP-IDF defaults do not synchronize wall time. This component starts
SNTP with `pool.ntp.org` only when its clock is unset; pending hello retries remain
read-only. Until time is plausible, TLS/HMAC requests fail closed. SNTP is not an
identity trust anchor; a wrong/unavailable clock cannot bypass certificate checks
or the server's HMAC timestamp window. Restricted LANs need a reachable time source
before use. Real cold-boot clock/TLS interoperation remains a hardware test gate.

### Board-specific USB setup example

For Chinese/Japanese reply captions on display builds, append
`overlays/sdkconfig.cjk` to the defaults and ensure the generated configuration
has `CONFIG_MUSE_CJK_FONT=y`. The upstream caption fallback font is optional;
without it Chinese text appears as boxes. Existing build configurations must
also enable the option explicitly, because new defaults do not override cached
values.

Without the optional [Doubao TTS extension](../../../docs/doubao-tts.md), the
pinned upstream SDK displays text replies at reading pace using silent PCM.
Its `SPEAKING` state alone does not prove synthesized speech. The extension
connects completed voice-turn replies to verified HTTPS streaming and the
existing PCM playback path; real cloud/device playback requires separate
validation. The USB `m` MP3 self-test checks decoding and speaker output only.

The default has USB setup OFF. Normal initial pairing uses the Actell settings page;
only the development USB invitation path requires this option.
Opt in deliberately when building the provisioning-capable development profile.
For the exact Waveshare S3-Touch-AMOLED-2.16, append
`overlays/sdkconfig.usb-waveshare-s3-216` last. The pinned official
`components/muse/boards/board_waveshare_s3_216.c` documents KEY3 GPIO18 active-low;
release KEY3 during reset, then hold it for two seconds in the first eight
seconds after normal startup. Do not hold BOOT GPIO0 through reset, because that
selects the ROM download boot mode instead of running this code.

For the old generic Actell N16R8 board, module memory size is insufficient evidence
of a button pin. Keep GPIO=-1/setup OFF until the physical board and pin wiring are
verified. Do not copy the Waveshare KEY3 profile to that board. If its BOOT button
is verified to be GPIO0, release it during reset, then long-press after normal
startup within the eight-second arming window; the code supports this strap-safe
timing and does not require adding another button.

Pinned ESP-IDF6 Mbed TLS source evidence: `x509_crt_check_ee_locally_trusted`
in `components/mbedtls/mbedtls/library/x509_crt.c` requires a self-issued leaf
and an exact raw DER match to the local trust list. Validity is checked before
that branch; TLS proves possession of the matching private key. Self-signature
verification is not claimed separately. Bootstrap rejects CA and non-self-issued
certificates rather than assuming a private CA-issued leaf works as an anchor.

### Avatar portability validation

The approved `mods/extensions/avatar/src/muse_pixel.c` renderer and all artwork are
unchanged. A real Xtensa build exposed an old host-only assumption in the optional
state sink: `ATOMIC_INT_LOCK_FREE` is 1, not 2. Only `actell_avatar.c` platform
synchronization was adapted. ESP builds take short FreeRTOS critical snapshots of
plain state fields; time queries, rendering, allocation and network work remain
outside the critical section. Host builds keep their original atomic seqlock.
Both host branches run the same TTL/wraparound/toggle/frame tests; target builds
validate the actual FreeRTOS headers. This adds no completion/celebration animation.

### Completed result size and connection errors

Queued receipts are small; `calls.get` may contain the full app-list result.
The JSON response budget is 256 KiB, separate from the 8 KiB request budget.
Known Content-Length responses allocate their bounded actual size; unknown-length
responses grow incrementally within the same cap. Both still read in 1 KiB pieces.
Oversized read results return `response_too_large`, not a fabricated network outage;
mutating operations retain an unknown outcome and must never be replayed.
Diagnostics report only response/limit byte counts, transport stage, and discovery
filter counts. A zero identity match is not alone proof of changed credentials:
SRV/interface eligibility and presence of the identity TXT field are separate checks.
Keep the original run/call IDs for reconciliation and retain the saved host/pin.

Actell 设置提供持久保存的 Auto-connect 开关，默认开启；启动与每 30 秒只读 hello 恢复已保存连接，校时成功唤醒未连接的保存配对立即尝试（不绕过关闭、取消或拒绝暂停），关闭后仍可手动 Reconnect。偏好与配对分别保存；自动重连不会删除配对或重放手机动作。

串口 `phase=` 诊断覆盖 worker 等待/完成、结果交接、控制结果入队/取出、序列化及 DMA 等待；只记录固定阶段、操作名、耗时和字节/内存计数，不记录参数、内容或真实 ID。发送完成仅证明设备传输完成，不能替代平台收到结果的确认。

The native receipt integration harness compiles the actual adapter/guard with the
prepared SDK's cJSON dependency and synthetic scheduler, transport and PSA adapters.
Prepare the SDK dependency under `local/build/slan-complete-sdk` before running it;
without that cJSON source the integration test explicitly skips. Portable guard
policy tests run independently. The harness covers lost completion delivery,
completed duplicates, thousands of reads, retained write fences, cancellation
reserve, queue rejection and generation changes; it makes no physical-device claim.

Pairing records are deleted only by an explicit **Forget pairing** action. Manual
Reconnect and automatic hello rejection, timeout, clock/storage-read failure and
Cancel preserve saved trust and the reconnect entry. Rejection pauses background
attempts until manual retry or an explicit Auto-connect enable. An active peer's
successful reconnect does not rewrite credentials; only pending acknowledgement
transitions to active. This fix does not recover a record erased by older firmware
and does not import or restore old credential snapshots automatically.

New pairing uses one ESP32 tap: open Add Muse on the target iPhone, tap Pair with
Actell on the gadget, then approve that Muse in Actell. A single eligible pairing
window is requested automatically; no service-name/phone-selection list is shown.
Multiple eligible windows cause no pairing traffic and ask the user to keep only
the target window open. First-time TLS trust, phone approval, and saved identity
rules are unchanged. No parallel or sequential fan-out to other phones occurs.

Phone activity feedback follows the outer worker request through connection, phone result polling and image delivery; it indicates processing, not confirmed execution. Internal run cleanup cannot erase that pose, and completed display records are reused instead of disabling feedback after a fixed number of calls. The existing command budget bounds the pose without adding RPCs or waits. Listening and speech retain avatar priority. See [presentation events](../../../docs/optional-avatar-events.md).
