# Independent gadget → Actell component

This is the real ESP-IDF implementation, separate from the historical Python
transport experiment, optional avatar, and headless LAN text console. The official
Muse account/pairing and `/link-control` Noise channel remain upstream-owned.
Actell requests go directly from gadget Wi-Fi to the iPhone's authenticated HTTPS
listener. Disable `CONFIG_HOMEHUB_TUNNEL`; no VM Home Link socket API is needed.

## Installation and target validation

Use an untouched copy of pinned Muse SDK 812c46f9fe42b69d916613a9dc702cbac5ff7b54:

```
python bridge/extensions/gadget-actell/tools/stage.py /path/to/sdk-copy --check
python bridge/extensions/gadget-actell/tools/stage.py /path/to/sdk-copy
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
   `python3 bridge/extensions/gadget-actell/tools/provision_usb.py --port /dev/... --invitation local/credentials/invitation.json`.
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

The firmware advertises self-contained iPhone-tool instructions, exact operation
fields, discovery/polling examples and approval/reconciliation rules in the official
`commands_v2` descriptions. No separate model-context injection is required. Start
with `{"op":"capabilities","params_json":"{}"}`, then use the returned tool schemas.
See the [call sequence](../skills/gadget-actell-remote/SKILL.md#discover-and-perform-one-read-only-call).
Native regression executes the real registration serializer with synthetic identity
and the optional tools enabled, ensuring it stays within the SDK's 8 KiB limit.
Runtime audit lines contain only operation/outcome/fixed state words, never IDs,
phone contents, parameters, credentials or tool results.

`actell.rpc` requires string `op` and string `params_json` containing a bounded,
unique-key JSON object. Allowed operations: `companion.hello`, `capabilities`,
`runs.open`, `runs.get`, `runs.cancel`, `tools.call`, `calls.get`. `runs.open` always
negotiates `image_contract:"thumbnail-v2"`. Responses become the official
`{ok,payload}` / `{ok:false,error}` result; every actual HTTP request uses exact-body
ACTELL1 HMAC with unique nonce over verified TLS. IDs/deadlines/scopes and execution
approval are authoritatively validated by Actell.

`actell.image` accepts `params_json` with `run_id,image_id,generation`; it is not
advertised unless `CONFIG_ACTELL_BRIDGE_IMAGES=y` and PSRAM is enabled. It verifies
request/image/generation identity, geometry revision, SHA256, JPEG marker dimensions,
1280-pixel edge and 256-KiB budget. No 8-MiB original or base64 JSON RPC is allowed.
The small JPEG is converted to the official `camera.capture` result shape
`payload:{format:"jpeg-base64",data_base64:...}` plus image/geometry metadata.

**Model-visibility gate:** upstream `main/app.c` camera capture establishes this wire
shape; `noise_control.cpp` forwards payload and fragments outgoing body. This is not
evidence that a real Muse model can see this image. Until a real runtime experiment
verifies visual input, keep images disabled and do not claim screenshot-based control.
Coordinates are delivered-image pixels, with the original image ID/generation and
geometry revision; never scale a second time. JPEG header/hash checks are not a full
pixel decode. The iOS thumbnail producer must decode successfully before delivery.

## Bounds and outcome semantics

- One worker; three queued requests; parameters ≤4 KiB; HTTP requests ≤8 KiB;
  JSON responses ≤256 KiB; JPEG ≤256 KiB; result payload under 360 KiB.
- Registration is small (two commands maximum), preserving the SDK's 8-KiB budget.
  Full board registration must still be measured in target/runtime validation.
- Sixty-second advertised timeout, 40-second local queue deadline, 15-second network
  observation budget plus bounded discovery. Socket I/O can consume its configured
  timeout before the next absolute-deadline check. No unbounded retries or queue.
- Up to 64 live/recent invocation work slots are retained; only finished slots
  are recycled, so sequential read-only queries do not exhaust a session budget.
  A retained pending duplicate shares the original completion attempt. A completed
  duplicate returns `invoke_result_unavailable` synchronously; it does not imply
  delivery or nonexecution, and never resubmits a phone action. Reconcile original
  run/call IDs using a new read-only invocation. Once an old read-only slot has been
  recycled, that read-only ID may enter a fresh read-only query rather than a cached result;
  the peer may still reject that ID under its anti-replay policy. Prefer a new
  transport invocation ID when reconciling the same original run/call.
- Accepted mutating invocation IDs also have independent SHA-256 tombstones for the
  whole control generation, including uncertain outcomes and lost result delivery.
  These fences survive work-slot recycling and apply even if a duplicate changes
  its operation. There are 1,024 fences: ordinary writes stop at 992, leaving 32
  additional entries for `runs.cancel`. At full write capacity only new writes are
  rejected with `write_receipt_limit`; read-only reconciliation still works.
  Do not reconnect or create replacement action IDs to bypass this protection.
  This is a bounded local policy, not an unbounded result cache or durable deduplication
  across control generations/reboots. Existing phone run/call reconciliation remains required.
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
  Optional `CONFIG_ACTELL_BRIDGE_AVATAR_ACTIVITY` is default OFF and uses the
  unchanged approved avatar setters only for authenticated running/succeeded
  read/write results, with 1500/350-ms leases, 16 tracked IDs, terminal fencing
  and no waits/I/O. Missing renderer has no effect on RPC completion.

## Host tests

```
PYTHONPATH=bridge/host python3 -B -m unittest discover -s bridge/host/tests -p 'test_gadget_component.py' -v
```

Host tests cover the real guards, SLAN codec, worker and NVS adapter with synthetic I/O. They do not establish target
compilation, TLS interoperability, Bonjour operation, iPhone authorization, device
execution, or model image visibility. Consult the separately recorded build results.

## Combining existing overlays

Verified staging order, starting from a separate clean SDK copy:

```
git -C /path/to/sdk-copy apply --check /path/to/muse-actell-bridge/bridge/patches/muse-gadget-sdk/headless.patch
git -C /path/to/sdk-copy apply /path/to/muse-actell-bridge/bridge/patches/muse-gadget-sdk/headless.patch
python /path/to/muse-actell-bridge/bridge/extensions/gadget-actell/tools/stage.py /path/to/sdk-copy --check
python /path/to/muse-actell-bridge/bridge/extensions/gadget-actell/tools/stage.py /path/to/sdk-copy
python /path/to/muse-actell-bridge/bridge/extensions/avatar/tools/stage.py --sdk-root /path/to/sdk-copy
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

The approved `bridge/extensions/avatar/src/muse_pixel.c` renderer and all artwork are
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
