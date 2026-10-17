# Flat tools / normalized-display-v1

Status: Actell source implemented; Gadget integration in progress. No deployment or physical-device acceptance is implied. This extends remote execution and does not change the byte-identical SLAN/1 pairing contract.

## Negotiation

`capabilities` retains its legacy pixel-based `tools` schema. It additionally returns:

- `input_contracts`: `image-pixels-v1`, `normalized-display-v1`.
- `independent_input`: `true`.
- `input_tools.normalized-display-v1`: schemas with integer coordinate minimum 0 and maximum 1000. All other tool limits retain their existing validation.
- `image_retention_seconds`: `1800`.

Open a run with `input_contract: "normalized-display-v1"` and `image_contract: "thumbnail-v2"`. Omission of input_contract preserves legacy pixel behavior. Unknown contracts are rejected. Reopening an existing ID with a different contract or deadline is an idempotency conflict.

The input contract is fixed for the run, included in the resolved execution digest and cannot be inferred from coordinate magnitude. New input rejects observation; callers cannot supply private underscore-prefixed execution fields. Legacy observation ownership, generation, geometry and 60-second admission checks remain intact.

## Input

`tap` and `drag` accept integers in the inclusive range 0–1000, endpoint aligned to the current full interactive display. They require no image ID or prior screenshot. Mapping to the existing HID range is `(value * 65535 + 500) / 1000` with integer division. Local Actell model tools keep their existing pixel interface.

The developer-service backend queries authenticated `getdisplayinfo` through a new read-only native command (action 12, result kind 15), then verifies its size against existing decoder geometry and the native geometry generation. It never calls capture or JPEG encoding for this query. Existing media preparation/decoding is still required for the current CoreDevice input route. Missing or inconsistent geometry fails as geometry_unavailable, rather than guessing from UIKit, a model table or an old screenshot.

The approval record binds the raw arguments, mapped coordinates, route/session and geometry token. Independent authorization is consumed once. Geometry is checked after authorization and again in the existing native dequeue/Down fence. Geometry changes after sending Down cannot be atomically excluded; incomplete or uncertain input remains unknown and is never replayed. Physical orientation correctness on the target device remains an acceptance item.

## Run ownership

A run is an internal execution ownership scope, not an Agent conversation. There is no new 60-second run maximum or five-second idle timeout. The existing explicit run/call deadlines and one-hour run admission ceiling remain.

Authenticated remote run admission does not require a foreground scene, earlier foreground invocation, a matching continuation client or a preexisting background-source receipt. Requests that reach the active service are admitted to the existing execution pipeline, subject to protected data, live client permissions, process-wide ownership and durable-state checks. The runtime starts with the actual scene and allows background task creation; it neither pretends to be foreground nor bypasses the shared execution lifecycle. Connection, selected background drivers and ordinary deadlines determine execution readiness and return their normal outcomes.

A service continuation demand may retain the user's already-selected continuous mechanisms (audio, PiP, location) between invocations, without holding phone execution ownership. A newly admitted background request may establish that demand too; it is a resource demand, never an extra admission prerequisite. It requests no location permission, enables no unselected mechanism and changes no idle-residency preference. Service off, revocation, lock, explicit stop, setup failure or external execution stop releases it. Existing per-run system-task cancellation and actual execution leases remain under the common lifecycle. Local service activation/configuration still uses its existing unlocked-foreground setup checks; those are separate from incoming run admission.

`runs.get` and `runs.cancel` additionally return `execution_released: boolean`. A closed run can still be draining while its input/media cleanup finishes; callers poll this read-only flag within their remaining platform budget before assuming a following run can acquire the phone. They never resubmit tools.call to wait for cleanup. A cleanup delay does not erase a confirmed action result or imply nonexecution.

The current Gadget SDK exposes an invoke ID but no platform task identity or cancellation notification. The fallback is one run per invoke, closed by the owner after its call reaches a terminal state. New invokes can continue without a screenshot or pairing. Existing pending/unknown calls are reconciled using original IDs; a new invoke must not be used to replay them. Task-level grouping and immediate platform cancellation are not advertised as implemented.

## Results

Call receipts preserve run_id, call_id, state, result and error. `effect` supplements state:

- `not_sent`: no input dispatch was recorded.
- `acknowledged`: input service acknowledgement, not proof of visual goal completion.
- `may_have_occurred`: dispatch or uncertain delivery; do not replay.
- `not_applicable`: screenshot/read/wait rather than an input effect.

Legacy journal entries can omit effect. Missing effect is not proof that an action was never sent. Interrupted journal entries are restored as unknown. Error codes geometry_unavailable and geometry_changed are preserved when raised by the independent input path; public error text still uses the normal safe tool-result path.

For screenshots, the result retains image_id, generation, width, height, geometry, image_contract, mime_type, byte_count and expires_at_ms, and adds captured_at_ms and image_status. A normalized run also returns coordinate_space=normalized-display-v1. `calls.get` refreshes image_status to available/unavailable without executing another capture. Capture success and image-delivery failure are separate outcomes; the Gadget must not present metadata-only results as image success.

## Historical image retrieval

thumbnail-v2 results are retained for 30 minutes from original capture time in a protected, backup-excluded disk cache. images.get uses the original run_id/image_id/generation and live authentication/screenshot permissions. It can read after run closure, a later capture or process restart, without acquiring execution ownership.

Each atomic record includes original identity, dimensions, transform, time, JPEG and SHA-256. Bounds: 64 unexpired records, 256 KiB per JPEG, 16 MiB total JPEG budget; JSON/base64 metadata overhead is additional. Capacity is checked before capture. Unexpired results are not evicted to make room; a full cache rejects a new screenshot with image_storage_full. Expired records are removed on access/admission. Revocation clears that client's cached results; permission changes use the existing revocation fence. Storage faults fail closed.

Responses retain the existing JPEG identity/hash headers and additionally report X-Actell-Captured-At-Ms and X-Actell-Expires-At-Ms. Unavailable originals return image_unavailable; they are never recaptured implicitly. Historical availability does not extend action deadlines or legacy observation admission.

## Remaining platform/storage boundaries

Gadget must bind each platform invoke to durable original run/call IDs before sending actions. A repeated invoke must query the original call, never create a replacement. Neither a transport timeout nor not_found establishes nonexecution. The platform has not confirmed a model/executor recovery route when the entire first receipt is lost.

Actell retains accepted run/call identities in its existing durable journal with 10,000-entry bounds. This change does not silently prune those identities or claim unbounded operation. A capacity/archival scheme that preserves rejection of retired identities remains required for long-term use. Gadget persistence and registration rollout must be reviewed against its actual storage budget before declaring end-to-end completion.

## Validation

See [the implementation record](FLAT-TOOLS-IMPLEMENTATION.md). Host and simulator tests are distinct from actual JPEG visibility in Muse, physical input delivery, target orientation and visual task success.
