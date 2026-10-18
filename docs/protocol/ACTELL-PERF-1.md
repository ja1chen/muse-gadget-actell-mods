# Actell screenshot transport performance

This optional extension preserves the existing RPC envelope, HMAC, original run/call
IDs, deadlines and [flat-tool outcome contract](FLAT-TOOLS-1.md). It changes transport
reuse and bounded waiting; it does not grant permission to replay operations.

## Sequential HTTP connections

Muse requests sequential HTTP persistence within and between outer invocations. It reuses a connection
only after fully consuming a successful, bounded response and confirming HTTP
persistence. Responses with `Connection: close` retain the old behavior. The client
can move into one worker-owned idle slot after a successful invocation; the next
invocation can borrow it only for the same saved identity, Wi-Fi network and Muse
control generation. Failure, network/identity/generation change, ten minutes of
business-idle time or sixteen total requests close it. The worker wakes at the idle deadline
even without another request, and network/control changes wake it for cleanup.
Pairing actions discard the slot. Background hello borrows an eligible socket with
its own request fence, without changing the business owner or refreshing business
idle time. A cold hello closes its own socket. The worker wakes at most every thirty
seconds for pairing maintenance even while a socket is retained. Local tool waits
also reap expired idle connections.
There is no HTTP pipelining or concurrent connection pool. Every RPC has a fresh
nonce, timestamp and HMAC, and replaces or removes the previous operation trace.
Every new connection uses the saved hostname and pinned certificate, never mDNS as trust.

The IDF 6 streaming client uses `prepare` and `request_send` on a retained connection.
Calling `open` again would reconnect in this SDK version. Complete length, JPEG,
identity, dimensions and SHA-256 validation remain mandatory before upload.

Muse keeps one successful network locator for at most thirty seconds from discovery,
bound to saved origin, client, certificate fingerprint and the Wi-Fi network epoch,
local address, subnet and gateway. Wi-Fi reconnect/roaming and IP changes advance the
epoch, including a reconnect that receives the same IP. Cache hits still check LAN
scope. This TTL governs discovery before opening a new connection, not the lifetime
of an already authenticated live connection. Active connections remain subject to
identity, network, generation, idle and request limits across the TTL boundary.
A transport/protocol failure invalidates the locator and idle slot. A cached connection
attempt is capped at two seconds; only failure before any request-body write attempt
can fall back once to discovery and a new authenticated connection within the request
budget. Failure after a write attempt, even a write returning an error, never causes
resubmission. Existing read-only original-status recovery remains unchanged.

Before retaining a completed connection, the worker requires at least 24 KiB of
free DMA-capable internal memory and a 2 KiB contiguous block. It closes the
optional idle connection below either threshold; idle maintenance applies the
same check. This leaves 8 KiB above the SDK's unchanged 16 KiB send reserve.
The check never interrupts a borrowed/in-flight request, sends no extra RPC,
and preserves the thirty-second locator cache. Low-memory workloads may need
another authenticated TLS connection on the next invocation.

## Optional idle-release hint

Resource retention is independent of the platform chat subscription. Neither opening,
closing, reconnecting nor timing out that subscription changes TLS/media ownership.
No subscription is started or kept alive for this feature. The ten-minute idle limit
works even when no platform task events are received.

An existing authenticated event stream may deliver `task.status` with
`payload.status == "completed"`. Its adapter posts a nonblocking advisory hint before
local-chat/sequence filtering. The bridge accepts it only with no queued or active
jobs; a new admitted job or control generation invalidates a pending hint. Busy hints
are discarded rather than applied after an unrelated job finishes. The single worker
owns cleanup, so a request admitted after cleanup starts executes afterward.

If a successful `capabilities` response for this control generation advertises
`performance.idle_release: true`, and there has been phone-tool work within the idle
window, the worker may send this ordinary authenticated RPC, once, with a fresh
request ID and a two-second total budget:

```json
{"v":1,"client_id":"<paired-client>","request_id":"<fresh-id>",
 "op":"companion.release_idle","params":{}}
```

The ordinary success envelope contains `result.released` as a boolean. This is a
best-effort request to retire idle resources, not a task cancellation, execution
receipt or permission to replay. Actell independently checks all local and remote
users, queued work and active media operations. A busy peer ignores it. Muse closes
its idle TLS after the attempt; a lost response is not retried. Old peers receive no
new RPC. A repeated hint without new phone-tool work does not resend it. Address
cache, pairing and original operation history survive normal reclamation.

Actell retains Muse-created media separately from run ownership: normal run release
must finish promptly, while media can remain reusable for up to ten minutes of
business inactivity. Status queries and hello do not extend media retention. New
media use invalidates a pending cleanup; cleanup completion is fenced to its original
generation. Normal Muse release still uses `runs.cancel`; Actell permits retention
only when the run has accepted calls and every call has succeeded. Interrupted,
failed, unknown or expired work, explicit stop and loss of execution eligibility
retain their existing cleanup rules. Retention does not grant background
execution permission or keep a run authorized.

Actell HTTP uses a separate idle budget only after a successful authenticated RPC.
The first/partial request, executing handler and response write retain their short
budgets. Connection capacity and request limits remain bounded; idle authenticated
sessions may be evicted for a new connection without cancelling active handlers.

Task IDs need not identify a unique run for this optimization. An unrelated or late
completion can discard an unused cache and cause a cold start; it cannot cancel a
running operation. Cold start/stop may still fail, so this is not a promise of equal
latency or success. No cloud-origin text display or chat sequence-routing change is
part of this policy.

## Negotiated bounded waits

`capabilities` may return this object in its ordinary result:

```json
{
  "performance": {
    "http_keep_alive": true,
    "idle_release": true,
    "tools_call_wait_ms_max": 1500,
    "runs_cancel_wait_ms_max": 500
  }
}
```

Muse sends optional `params.wait_ms` only when that operation's numeric, finite,
positive integer limit is advertised. It clamps tool waiting to 1500 ms and release
waiting to 500 ms, further constrained by the remaining local budget with a 250 ms
margin. Missing or malformed limits use the legacy path. Capability results are not
cached across invocations for wait negotiation. The advisory idle-release flag is
retained only for the matching control generation and the recent phone-work window;
it is cleared on pairing actions and refreshed by each capability response.

- `tools.call`: persist and submit exactly once, then wait at most `wait_ms` for the
  original receipt. A terminal receipt avoids the first `calls.get` and 500 ms pause.
  Pending/approval/timeout responses retain the existing original-ID query path.
- `runs.cancel`: cancellation acknowledgement remains distinct from actual release.
  Wait at most `wait_ms` for owner cleanup and return `execution_released` truthfully.
  Muse uses `runs.get` if release is still unconfirmed. A status read never acquires
  execution ownership or cancels another run.

Old peers receive neither new wait parameter. A lost response does not trigger a
retry with fewer parameters or a new tool submission.

## Measurement and privacy

The optional `X-Actell-Operation-Id` header carries the existing 64-character lowercase
hex operation digest for diagnostic correlation only. It is not signed independently
and must never be used for authorization, deduplication, ownership or outcome lookup.
The authenticated envelope's request ID remains authoritative. Each side computes
local durations using its monotonic clock; hosts' timestamps are never subtracted.

Muse emits operation/request correlation, queue wait, discovery/cache/connection
attempts, body download, image validation and SDK upload start/end/abort. Upload
completion means the SDK sent its body, not that the platform accepted or displayed
the image. Logs contain fixed phases, opaque operation/request IDs and numeric counts;
no images, tool arguments, credentials, device names or network addresses are added.
Keep raw device logs and timing reports under ignored `local/` with owner-only access.
`http_timing.connection_borrowed` identifies an attempt using the prior invocation's
idle connection; `connection_reused` also includes reuse within the same invocation.
Broad receive-payload metadata/fingerprint tracing is not installed. Only fixed
resource-reclamation reasons and the existing timing measurements remain.
The analyzer reports borrowed and new connection attempts separately, including
failed borrowed attempts that fall back before the request body is written.

Actell records control preparation, media start, fresh-frame wait, original/thumbnail
encoding, cache write, result readiness and actual release. Platform integration must
separately record result receipt, image readiness and next LLM request (optionally
first output), retaining the same operation ID. Platform code is outside this SDK
adapter; these events are not inferred from upload completion.

Summarize Muse UART logs and Actell's filtered diagnostic report:

```sh
python3 mods/tools/analyze-actell-latency.py \
  local/logs/performance/muse.log \
  --actell-report local/validation/performance/actell-diagnostics.json \
  --output local/validation/performance/timing-report.json
```

The Actell report is produced by its `tools/muse-remote/collect_diagnostics.py`.
Only known `performance.*` stages, validated operation IDs, success and byte counts
are imported; nanosecond durations become milliseconds. Diagnostic timestamps are
not used as durations. Optional normalized JSONL events contain `source` (`actell`
or `platform`), `operation_id`, `phase`
and explicit local `elapsed_ms`. Recognized phases are listed in the analyzer.
The analyzer only exports allowlisted identifiers, phases and numeric measurements,
separates cold/cache/locator-recovery paths, and reports sample count, median and
nearest-rank P95. Control recovery is grouped separately by observed Actell native
spans; missing spans are not proof that recovery did not occur. Repeated/status-only
or incomplete invocations are excluded from capture latency.
Do not add overlapping phase durations to estimate the total. Compare at least thirty
ordinary captures per variant, keep recovery cases separate, and record failures,
image bytes/resolution and network conditions in private acceptance records.

## Validation boundary

The current implementation covers Muse and Actell. Platform/LLM scheduling
instrumentation is deferred; normalized platform events are only a future input seam.

Host tests execute the real receiver, flat executor and patched SDK sender with
synthetic transports. They cover persistent/closing responses, fresh signing,
TTL/identity/network changes, bounded pre-body fallback, ambiguous writes, truncation,
chunked/large replies, wait negotiation, real release confirmation and image ownership.
Target compilation and physical interoperability/performance are separate gates.
Binary attachments, overlapping download/upload, direct phone upload and thumbnail
pipeline changes remain contingent on measured bottlenecks; none is enabled here.
