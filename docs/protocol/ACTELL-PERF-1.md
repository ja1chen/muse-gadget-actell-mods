# Actell screenshot transport performance

This optional extension preserves the existing RPC envelope, HMAC, original run/call
IDs, deadlines and [flat-tool outcome contract](FLAT-TOOLS-1.md). It changes transport
reuse and bounded waiting; it does not grant permission to replay operations.

## Sequential HTTP connections

Muse requests HTTP persistence within one outer invocation. It reuses a connection
only after fully consuming a successful, bounded response and confirming HTTP
persistence. Responses with `Connection: close` retain the old behavior. The client
is closed on invocation completion, failure, network/identity change, four seconds
of idle time or sixteen requests. No HTTP pipelining or cross-invocation TLS session
pool is introduced. Every RPC has a fresh nonce, timestamp and HMAC. Every new
connection uses the saved hostname and pinned certificate, never mDNS as trust.

The IDF 6 streaming client uses `prepare` and `request_send` on a retained connection.
Calling `open` again would reconnect in this SDK version. Complete length, JPEG,
identity, dimensions and SHA-256 validation remain mandatory before upload.

Muse keeps one successful network locator for at most thirty seconds from discovery,
bound to saved origin, client, certificate fingerprint and the Wi-Fi network epoch,
local address, subnet and gateway. Wi-Fi reconnect/roaming and IP changes advance the
epoch, including a reconnect that receives the same IP. Cache hits still check LAN
scope. A transport/protocol failure invalidates the locator. A cached connection
attempt is capped at two seconds; only failure before any request-body write attempt
can fall back once to discovery and a new authenticated connection within the request
budget. Failure after a write attempt, even a write returning an error, never causes
resubmission. Existing read-only original-status recovery remains unchanged.

## Negotiated bounded waits

`capabilities` may return this object in its ordinary result:

```json
{
  "performance": {
    "http_keep_alive": true,
    "tools_call_wait_ms_max": 1500,
    "runs_cancel_wait_ms_max": 500
  }
}
```

Muse sends optional `params.wait_ms` only when that operation's numeric, finite,
positive integer limit is advertised. It clamps tool waiting to 1500 ms and release
waiting to 500 ms, further constrained by the remaining local budget with a 250 ms
margin. Missing or malformed limits use the legacy path. Capability results are not
cached across invocations: no capability-version invalidation contract exists yet.

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

Actell records control preparation, media start, fresh-frame wait, original/thumbnail
encoding, cache write, result readiness and actual release. Platform integration must
separately record result receipt, image readiness and next LLM request (optionally
first output), retaining the same operation ID. Platform code is outside this SDK
adapter; these events are not inferred from upload completion.

Summarize Muse UART logs and Actell's filtered diagnostic report:

```sh
python3 bridge/tools/analyze-actell-latency.py \
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
