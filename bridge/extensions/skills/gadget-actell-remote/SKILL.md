---
name: gadget-actell-remote
description: Use a trusted Actell companion through the independent gadget commands while Muse remains the sole planner.
---

# Actell remote tools (independent community extension)

This extension uses the official Muse gadget command registration/invoke/result
mechanism; it is not an official Muse product integration. The gadget directly
contacts the paired iPhone on Wi-Fi using verified HTTPS and ACTELL1 HMAC. No VM
LAN socket, Home Link adapter or `/link-tunnel` is needed. Do not run Actell's local
AI planner for this workflow; retain its existing approval/execution queue.

## Preconditions

1. The user must have completed official Muse setup and separately authorized the
   Actell companion through trusted USB bootstrap and the iPhone per-device scopes.
   Never request connection secrets in a prompt, put them in a command, or use the
   public Python mock secret against a real device. Do not bypass certificate checks.
2. Use only the gadget's actually advertised `actell.rpc` / `actell.image` commands.
   Do not pretend they are installed merely because this skill exists. If absent,
   explain the required firmware/setup step instead of inventing a proxy transport.
3. `actell.rpc` takes string `op` and string `params_json` containing an object.
   Allowed operations: companion.hello, capabilities, runs.open, runs.get,
   runs.cancel, tools.call and calls.get. Obtain capabilities and honor exact
   tool schemas and the app's approval UI. Never auto-approve phone actions.
4. Read docs/protocol/v1.md and docs/protocol/gadget-v2.md. The gadget negotiates
   thumbnail-v2 for new runs. The old Python client remains a host test tool and
   is not the gadget runtime.

## Execution

- Open a unique run with an absolute future deadline (maximum one hour). Bind
  actions to stable run_id + call_id. Submit once; retain original IDs and params.
- Poll calls.get until terminal. queued and pending_approval are not execution.
  On failure/timeout/reconnect, preserve unknown outcomes. Reconcile original IDs;
  never use a new ID to repeat an uncertain action. not_found is not proof that
  an earlier request cannot still execute.
- A screenshot returns image_id, generation, delivered dimensions and geometry.
  The separately advertised experimental actell.image command accepts params_json
  {run_id,image_id,generation} and returns a capped JPEG in the official
  camera.capture wire form, plus identity/geometry metadata. It is default off.
- A delivered JPEG payload does not prove the Muse model can see it. Require a
  successful real runtime visual-input validation before vision-based planning.
  If image visibility is unavailable or uncertain, stop coordinate planning and
  explain the limitation. Do not guess from metadata or an image path.
- After that validation, tap/drag use integer delivered-image pixels and
  observation {image_id,generation,geometry_revision}. Never normalize or rescale
  on the caller. Actell validates geometry/frame freshness at actual dispatch and
  maps once to the original raster. Refresh on stale_observation.
- runs.cancel prevents new actions and requests cancellation; already-dispatched
  actions may finish. Never promise rollback. Do not relight an operation based
  on a late result from a cancelled run.

## Boundaries

Official `/link-control` Noise transport and Muse account pairing stay upstream.
The Actell credential/authorization is independent. BLE companion transport is
not implemented. The default-off headless LAN page remains an isolated-development
text interface to real Muse typed-turn/cancel APIs, never a secret provisioning
page. The optional approved avatar uses only confirmed running/completed read or
write activity; presentation never controls execution or waits before results.

Target compilation has passed in the cloud; no hardware was flashed and no real
account/device/image-visibility test has been claimed. See the local-only reports under `local/validation/`; these are not part of the shared repository.
