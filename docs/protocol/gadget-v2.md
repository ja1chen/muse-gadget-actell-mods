# Gadget command adapter and thumbnail-v2

The RPC envelope and exact-byte ACTELL1 authentication remain frozen v1. This
addendum applies to the independent ESP gadget component and iPhone companion.
It changes neither official Muse account/BLE pairing nor `/link-control`.
No `/link-tunnel` or Python-injected Muse VM network transport is used.

- Bonjour `_actell-muse._tcp` locates a candidate. Public TXT `identity` is the
  provisioned leaf DER SHA256; its match is never sufficient authentication.
- Trusted USB invitation supplies endpoint TLS hostname/port, self-issued non-CA
  leaf PEM and SHA256, client ID, 32-byte secret, and pending expiry. Local TLS
  hostname/key/validity verification and exact leaf trust remain mandatory.
- `companion.hello {device_name,protocol:1}` is authenticated for a pending client;
  its result state is `pending_authorization|authorized|unavailable`, plus scopes.
  iPhone authorization grants independent screenshot/list_apps/control scopes.
- Gadget `runs.open` negotiates `image_contract:"thumbnail-v2"`.
- Screenshots use one cached decoded raster and a bounded JPEG (≤256 KiB, long
  edge≤1280), not an 8-MiB original in a gadget/base64 envelope. Existing v1 clients
  remain separate and do not silently adopt new coordinates.
- `width,height` denote delivered pixels. `geometry` includes `revision`, original
  and delivered sizes, full-raster crop, zero delivery rotation, no mirror/padding,
  `coordinate_space:"delivered-image-pixels"`, endpoint-aligned-nearest mapping,
  and `device_orientation:"unknown"`. A zero delivery rotation does not claim
  that physical phone orientation or underlying screen cropping is known.
- `images.get` includes existing binary image headers plus
  `X-Actell-Image-Contract: thumbnail-v2` and `X-Actell-Geometry-Revision`.
- Coordinate actions supply original `image_id,generation,geometry_revision` with
  delivered-image integer pixels. Actell validates the cached geometry/frame
  identity at actual dispatch, then maps coordinates once. Caller never rescales.
- `actell.image` returns the upstream camera JPEG-base64 payload only when the
  explicit experimental config is enabled. Actual Muse model visibility is an
  unverified acceptance gate and must not be inferred from successful RPC delivery.

After timeout, reconnect or restart, writes are never automatically replayed.
Original run/call IDs are the only reconciliation route; a missing record does not
prove the original submission cannot execute. Local completion is one submission
attempt, not exactly-once delivery across the upstream lossy/disconnected queue.
