# Optional Actell → avatar presentation events

This is an optional local presentation layer. The original replacement character's seven normal states remain independent. The switch only adds **looking at the phone** and **operating the phone**; disabling it does not disable the normal character. No completion celebration is added.

## Files and boundaries

- `bridge/host/muse_client/presentation.py`: explicit tool classification, default-off client facade, bounded local activity tracker, asynchronous latest-only callback worker, and native C event sink.
- `bridge/extensions/avatar/`: separate original C character renderer/activity API, its previews and installation instructions.
- `bridge/host/web_console/presentation.js` and the independent `/api/presentation` endpoints: optional, session-local mock controls. No real device route is activated.
- Tests: `bridge/host/tests/test_presentation.py`, `bridge/host/tests/test_presentation_native.py`, `bridge/host/tests/test_presentation_ui.py` (dependency-free Node DOM/request-order regression), and the optional-presentation cases in `bridge/host/tests/test_web_console.py`.

The existing `Client`, HMAC signing, v1 request body, approvals, idempotency IDs, routing, result/exception objects and retry policy are unchanged. The facade forwards each RPC exactly once. No animation is awaited before, during or after a tool. Callback errors are isolated, and a hung callback cannot block the RPC caller. This layer cannot approve a tool, submit a new tool, retry it, or invoke an LLM.

## Classification and lifecycle

| Tool | Extra presentation |
| --- | --- |
| screenshot, list_apps | phone_read |
| tap, drag, home, launch_app, type_text | phone_write |
| wait, capabilities, unknown names | none |

Names are exact and case-sensitive; new tools receive no animation until deliberately classified. Arguments, prompt text, app names, screenshots, HMAC material and run/call IDs are never included in emitted events.

- A submitted, queued or approval-pending call does not count as running.
- A confirmed `running` result activates its class. Polling remains the caller's existing responsibility; the presentation layer makes no extra RPCs.
- A confirmed synchronous `succeeded` result may hold the same read/write pose for 350 ms (configurable 0–500 ms), entirely in the display layer. This is not a success/celebration state and does not delay the result.
- Failed/cancelled/expired/unknown results and transport exceptions immediately remove that call's display state. Cancelling a run clears its display before the cancellation request; display clearing is **not** evidence that an already-dispatched action stopped.
- Terminal calls are remembered until their deadline so late `running` replies and duplicate successful results cannot restart their animation.
- Concurrent calls are tracked separately; a running/briefly-held write takes precedence over read. Removing one call leaves other calls intact.
- Deadlines are converted once to monotonic durations, capped at one hour. The extra state expires even if polling stops. Disabling and closing clear local state without joining an external callback.
- One layer belongs to one Client identity. Accidental reuse by another Client gets an independent disabled layer, avoiding ID collisions. Tracking is bounded at 256 records, after which additional presentations are safely skipped rather than affecting tools.

## Event contract and C mapping

Local event example:

```json
{"v":1,"kind":"phone_read","enabled":true,"sequence":17,"ttl_ms":1500}
```

Kinds are only `idle`, `phone_read`, `phone_write`; map them to C enum values NONE=0, READ=1, WRITE=2. `enabled` controls only these extensions. `sequence` orders events within one presentation instance; reconnect/replacement must reset the receiving sink rather than mixing independent producers. Events contain no network destination or credentials.

`NativeAvatarSink` calls only:

```c
actell_avatar_set_enabled(bool enabled);
actell_avatar_set_activity(actell_avatar_activity_t activity, uint32_t ttl_ms);
```

All setters must run through the **same serialized callback worker**. UI/network threads change `ActivityPresentation` state; they must not directly call the C setters. The renderer can read concurrently. The sink rejects stale sequence numbers and clamps leases to 1500 ms. Active states renew every 500 ms. A blocked/lost callback leaves the receiving C state to expire by its own monotonic TTL; the Python tool caller still proceeds. No implementation can force an unresponsive external renderer to clear instantly, so receiver TTL is mandatory.

The C module also supports longer TTLs for other callers; this adapter intentionally sends short leases. It never changes Muse's original mode enum or normal seven-state animation selection.

## Use with the local mock console

```sh
python3 bridge/tools/dev.py console --port 8766
```

Open exactly `http://127.0.0.1:8766`. The optional phone-activity checkbox starts unchecked. Enabling it shows low-sensitivity local event labels as mock list-apps/screenshot tools finish. This is a status display, not evidence of a physical avatar connection. The console remains loopback-only. `/api/presentation` uses the existing same-origin, session and CSRF protections, separate from `/api/rpc`; toggling one browser session does not change another. Presentation polling/toggles have a separate request budget, so they cannot consume the main tool RPC rate allowance. UI sequence and revision guards discard stale responses; a failed toggle is labelled unconfirmed and reconciled with a read instead of falsely claiming that the switch changed.

The console's tools remain functional if the optional module cannot load, the event sink fails, or presentation is unavailable. Its script does not create a second session or store credentials; it uses the already-established console session. There is no localStorage preference, so fresh sessions default off.

## Integrate into an existing verified Python caller

```python
from muse_client.presentation import ActivityPresentation, PresentedClient

# existing_client is already configured through an independently verified transport.
# No credentials or endpoint are created here.
layer = ActivityPresentation()  # disabled, no native/network connection
client = PresentedClient(existing_client, layer)
layer.set_enabled(True)          # explicit opt-in for extra phone states
# Use client.call / client.get_call / client.rpc exactly as before.
# Read layer.snapshot() for a local preview, or supply a local sink below.
layer.set_enabled(False)         # immediate local clear; base animation remains
layer.close()                    # nonblocking cleanup
```

For a trusted, already-loaded local native binding, construct `ActivityPresentation(NativeAvatarSink(native))`, then explicitly enable it. `NativeAvatarSink` does not load libraries or open network connections. A ctypes binding must declare `set_enabled(bool)`, `set_activity(int,uint32_t)` as returning void; its reader methods return bool/int. Do not pass arbitrary downloaded libraries.

### Reproducible same-host C ABI test

```sh
python -m unittest discover -s tests -v
```

`test_presentation_native.py` compiles **only this project's original C activity module** into a temporary shared library with the available host `cc`, then verifies Python's single callback worker produces C READ, WRITE and disabled/NONE states. The test skips explicitly if `cc` is missing. No upstream executable, credentials, external endpoint or hardware is used. This validates a same-process ABI, not ESP32 firmware linkage, image appearance, BLE, Home Link or a Muse VM-to-board event path.

## Real Muse integration is still separate

There is no verified transport here carrying these events from Muse's VM to the gadget. The existing Home Link client integration and model screenshot input remain unverified. The C extension's firmware installation instructions are supplied separately; physical board/display validation remains required. Do not infer that a successful local ABI/mock test connects a real iPhone, updates the board avatar, or completes the original end-to-end tool chain.

A future real transport must preserve ordering, validate the small event schema, send only these presentation fields, dispatch all C setters from one task, and allow receiver TTL expiry. It must remain optional and independent of the main tool channel. This deliverable does not add that transport or claim it exists.

## Rollback

1. Uncheck the console option or call `layer.set_enabled(False)` and `layer.close()`. This does not cancel or replay tools.
2. Use `existing_client` directly instead of `PresentedClient`. No protocol or credential changes are necessary.
3. To remove optional console UI, remove its section and `/presentation.js` script tag from `bridge/host/web_console/index.html`; the two `actell-*` custom-event notifications in `app.js` are isolated and can also be removed. The main console does not depend on a listener.
4. `web_console.py` tolerates the optional `presentation` import being unavailable; the separate presentation endpoint reports unavailable and ordinary tool RPC remains usable.
5. Undo C renderer integration separately following `bridge/extensions/avatar` instructions. Never reset device pairing or modify the tool executor just to remove presentation.
