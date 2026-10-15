# Pebble: original Muse avatar + optional phone activity

Original milk-white floating round head with small rounded cream ears and
simple round hands, rendered in C on the Muse 64×64 pixel grid. No torso or
legs. The ear cavities and roots use a continuous, restrained warm shading
surface; the upper roots stay cream and connected to the head, while the
outer silhouette keeps a dark outline. This is the approved preview-v7 design.

This directory contains no Jollybot code or assets. Licensed under
the repository's Apache-2.0 license. Targets the renderer contract at SDK commit
`812c46f9fe42b69d916613a9dc702cbac5ff7b54`.

## Nine animations

| Input | Original animation |
|---|---|
| BOOT | Rises into frame, eyes open, amber boot dots appear |
| IDLE | Gentle floating bob and blink |
| LISTENING | Paw cups ear, round mouth, blue marks react to microphone level |
| THINKING | Paw at chin, gaze aside, cycling thought dots |
| SPEAKING | Audio-level mouth and bob, waving paw |
| ERROR | Initial side shake, X eyes, red alert |
| OFF | Goodbye wave, eyes close, entire character fades to black by 1.3 s |
| phone_read | Two hands cradle a phone, head leans down and closer to observe |
| phone_write | One small hand holds the phone, the other makes a simple small-amplitude gesture beside its edge; no long arm or elbow |

Existing `happy` pet input causes a brief happy-eye hop only when supplied by
Muse. Completion of phone tools never sets happy and has no celebration.
The two phone activities apply only while Muse is IDLE or THINKING. Boot,
shutdown, errors and active voice conversation keep their own base animation.
This is an independent presentation layer; Muse's seven-state enum is unchanged.

The narrow 13×23-pixel phone always shows its flat matte BACK and small rear
camera to the viewer; its screen faces the character. There is no light side
rail, screen UI or cropped bottom edge. Read and write are distinguished through posture and
hand motion, never screen UI. The small moving hand can tuck behind the foreground
phone edge; the gesture is intentionally simple rather than a jointed arm simulation.
`preview/phone_action_phases.png` compares six actual C frames for each action.

## Optional, isolated activity layer

The original seven animations always render when this avatar is installed.
`actell_avatar_set_enabled(false)` (default) disables only phone activity.
The setter also clears pending activity; re-enabling does not replay it.

`include/actell_avatar.h` provides three operations and a read accessor:

- `actell_avatar_set_enabled(bool)`
- `actell_avatar_set_activity(ACTELL_AVATAR_READ / WRITE / NONE, ttl_ms)`
- `actell_avatar_enabled()` / `actell_avatar_activity()`

All setters MUST execute on one serialized event-dispatch task. UI toggles
must enqueue onto that same dispatcher, not call setters from another task.
Any number of activity-accessor readers may run concurrently. This is not a multiwriter
API. The state uses only lock-free 32-bit C11 atomics and a single-pass snapshot;
a frame racing a writer uses the base pose rather than waiting. Expiry uses a
64-bit monotonic clock split across atomic words. No heap, network, sleeps,
queues or Muse state mutations exist in this C module.

TTL zero and NONE clear. TTL is capped at 60 seconds; normal bridge events use
1500 ms and a 500 ms heartbeat. Failure to refresh returns to the current base
animation, not a task-success animation. Disable/error/cancel must also send
NONE. The transport is responsible for validation, sequence filtering and
serial dispatch. See [`bridge/host/muse_client/presentation.py`](../../host/muse_client/presentation.py)
and [activity integration](../../../docs/optional-avatar-events.md) for the separate
Python activity wrapper. A real VM-to-device event transport is NOT implemented by
this renderer; the existing phone-control/voice path must not wait for it.

## Build without editing upstream source

Keep this directory in the external project. Optional staging into an SDK
checkout uses its documented, gitignored override path:

```sh
python3 bridge/extensions/avatar/tools/stage.py --sdk-root /path/to/muse-gadget-sdk
```

This writes only `esp32/components/muse/avatar/muse_pixel.c` (combined original
renderer + controller) and `esp32/components/muse/avatar/actell_avatar.h`. It refuses existing overrides and
does not change SDK CMake, build or flash. The unmodified SDK CMake compiles this
single override source. Device glue can include `avatar/actell_avatar.h` from
the Muse component. No device glue is installed automatically.

Removing these two staged files restores the default SDK renderer on the next
build. Removing this entire extension and omitting staging leaves upstream
firmware and the main phone tool chain unchanged.

## Reproduce host tests and previews

Requires an existing C compiler and Pillow. No upstream firmware scripts run.

```sh
python3 bridge/extensions/avatar/tools/validate.py \
  --sdk-archive local/upstream/archives/muse-gadget-sdk-812c46f.tar.gz
```

The test extracts only the two actual pinned SDK headers into a temporary
folder, compiles our C with warnings-as-errors, AddressSanitizer and UBSan, then
runs TTL/toggle/64-bit-clock/invalid-input/strip tests and 1680 rendered frames
at eight sizes. LeakSanitizer is disabled because this container is traced;
no renderer or controller heap allocation exists. PNGs and GIFs are produced
from the actual C render/scale functions, not an independently drawn mockup.
`preview/states.png` shows all nine; `preview/all_states.gif` cycles through all;
individual state GIFs preserve 40 ms frames. New previews and their `validation.json` are written to `local/validation/avatar-preview/` by default.
The existing `preview/` directory is local-only and entirely Git-ignored; do not commit or upload its contents. Historical reports are also local-only.

A controller-only shared library can be built for a local adapter test:

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -shared -fPIC \
  -I bridge/extensions/avatar/include bridge/extensions/avatar/src/actell_avatar.c \
  -o local/build/actell-avatar.so
```

## Limits and unverified work

Static 4096-byte palette-index framebuffer plus palette; nearest-neighbor
64×64 to up-to-512×512 RGB565 strip scaling. Render functions run only on the
UI thread, matching upstream. No frame timing claims have been measured on
ESP32-S3. No ESP-IDF build, physical AMOLED check, flashing, or actual
Actell-to-device animation transport has been completed. Verify those before
calling this a board-ready firmware release.

## ESP-IDF validation update (2026-10-12)

The approved v7 `src/muse_pixel.c` and preview pixels remain byte-identical.
Real ESP32-S3 compilation exposed the host-only assumption that
`ATOMIC_INT_LOCK_FREE == 2`. Only the optional activity-state sink now has an
ESP-specific short FreeRTOS critical snapshot; clock queries/rendering/allocation/
networking stay outside it. The original host atomic implementation remains.
The same host harness passes both branches. Full target build evidence and the
optional default-off gadget activity adapter are in
`local/validation/gadget-actell/` (logs in `local/logs/gadget-actell/`) and `../gadget-actell/README.md`.
The seven original Muse states and two optional read/write animations are reused;
no new completion or celebration animation was added.
