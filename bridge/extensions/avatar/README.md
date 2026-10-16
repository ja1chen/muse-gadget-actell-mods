# Pebble: Actell v13 avatar and optional phone activity

The default renderer uses the approved v13 160×160 geometry: a milk-white
floating head, round cream ears, curved chin and short angled phone, without
a ground shadow. Seven Muse states plus phone reading/writing are rendered by
C code. `CONFIG_MUSE_AVATAR_V2=y` defaults on; disable it and rebuild to select
the original 64×64 character. The legacy source remains byte-identical.
This compile-time flag is independent of the runtime phone-activity switch.
The SDK's state enum and UI timer cadence are unchanged.

Thinking holds on each side for about 1.1533 seconds with a 0.18-second transition
and no center hold. Reading follows a four-second Z cycle with upper-left and
upper-right holds; listening retains two consecutive nods. Phone feedback
applies only during IDLE/THINKING. Voice, boot, shutdown and error states take
priority. Tool completion adds no celebration or replay.

This is original geometric artwork, licensed under the root Apache-2.0 license;
no Jollybot code or imagery is included. The supplied unoptimized v13 renderer
is retained as `tests/reference_v13.c` for pixel equivalence checks. Integration
targets Muse SDK `812c46f9fe42b69d916613a9dc702cbac5ff7b54`.

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
API. The host state uses lock-free 32-bit C11 atomics and a single-pass snapshot;
ESP32 uses a short FreeRTOS critical snapshot, with rendering and clock queries
outside the critical section. On the host,
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

## Stage and downgrade

Use a separate verified SDK copy under `local/build/`. Apply the gadget hooks,
then stage avatar, caption fonts and optional TTS in that order (see
[patch instructions](../../patches/README.md)). Existing custom avatars must be
preserved outside the SDK component before staging; never overwrite them.

```sh
python3 bridge/extensions/avatar/tools/stage.py --sdk-root local/build/sdk-copy
```

The original SDK CMake override still builds one translation unit. The installed
dispatcher includes only the selected renderer and the activity controller.
Thin header/UI/Kconfig edits separate the native grid from the original 64-unit
layout: the large avatar stays 320 display pixels. Miniatures below 160 display
pixels use full invalidation; the v13 dirty-rectangle scratch is static rather
than task-stack storage. SDK edits are indexed in
[`avatar.patch`](../../patches/muse-gadget-sdk/avatar.patch), tested for exact
equivalence to staging. Do not apply this patch separately from `stage.py`.

Staging is idempotent while installed files match and rejects conflicting files
or unknown anchors before writing. Preserve `.actell-avatar-v2-stage.json`, which
journals originals before mutation. A partial install is refused on rerun;
restore the recorded originals manually before retrying.

To downgrade, use menuconfig to disable `CONFIG_MUSE_AVATAR_V2`, then reconfigure
and build. A defaults file alone does not replace an existing sdkconfig value.
This selects the old Actell character, not the official SDK avatar.

```sh
python3 bridge/extensions/avatar/tools/stage.py --sdk-root local/build/sdk-copy --rollback
```

Rollback restores the three verified SDK files and removes only unchanged staged
files. It refuses if later font/UI patches changed them. For a combined SDK,
recreate a clean staged copy in the documented order to omit avatar; do not force
rollback through later edits. Restore the backed-up custom avatar if needed.

## Memory and build validation

V13 uses a 25,600-byte framebuffer, a 25,600-byte exact head-geometry cache and a
1,024-byte scale table. Only enabling ESP-IDF
`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` places the two large arrays in
external BSS (51,200 bytes). Otherwise they consume internal static RAM; there is
no runtime allocation fallback for these arrays. Enable external BSS only on a
PSRAM board with working PSRAM initialization. The SDK dirty RGB565 heap cache
grows from 8,192 to 51,200 bytes and retains its full-redraw allocation fallback.
Including dirty scratch, the nominal increase is about 92 KiB, before target
alignment/control state. Geometry changes still run the original float-distance
algorithm. No device FPS, peak heap or power claim follows from host tests.

With the existing ESP-IDF 6.0.1 environment activated and all extensions staged,
build both Waveshare profiles using public synthetic configuration:

```sh
python3 bridge/extensions/avatar/tools/build.py --sdk local/build/sdk-copy \
  --components-dir local/build/resolved-sdk/esp32/managed_components --mode v13
python3 bridge/extensions/avatar/tools/build.py --sdk local/build/sdk-copy \
  --components-dir local/build/resolved-sdk/esp32/managed_components --mode legacy
```

Replace the components directory with existing resolved components. For these public profiles, the intentionally invalid TTS key only enables
compilation. No services are called. Both profiles compile fonts/TTS/Actell and explicitly opt
into the board's PSRAM BSS. `--internal-bss` checks internal-RAM linkage separately.
Builds/logs/reports go under `local/`; no flashing is performed. These synthetic
images are compilation artifacts, not credential-configured device releases.

For a locally configured device build, add `--secrets /path/outside/project/device-secrets.conf`
to the v13 command. Only `bridge/tools/apply-secrets.py` reads that explicit input;
normal import enables SDK authentication and TTS together. Private builds use
`local/build/avatar-v13/private-v13/`, owner-only artifacts/logs, and compiler-only
feature checks with configuration contents withheld. They never print private
headers, firmware contents or build logs. No flash or account/service call is
performed by building.

## Reproduce host tests and previews

```sh
python3 bridge/tools/dev.py avatar
python3 bridge/extensions/muse-fonts/tools/check-stage.py --with-avatar --with-tts
```

The first command verifies the pinned archive and both direct/staged renderer
branches with ASan/UBSan: each renders 1,680 frames at eight sizes and exercises
strips, TTL, toggle, clock wrap, invalid values and voice priority. It compares
2,400 v13 cases against the original oracle, checks 199 dirty-frame pairs, ESP
interface compilation, patch equivalence, idempotence and protected rollback.
LeakSanitizer is disabled; this is not a passed leak check. Reports are written
to `local/validation/current/`.

Optional previews require Pillow and render actual C output:

```sh
python3 bridge/extensions/avatar/tools/validate.py \
  --sdk-archive local/upstream/archives/muse-gadget-sdk-812c46f.tar.gz
```

Add `--legacy` for the original character and use a separate `--output` directory
to retain both previews. New PNGs/GIFs default to
`local/validation/avatar-preview/`. Existing `preview/` stays ignored and local.

Run `python3 bridge/extensions/avatar/tools/check-builds.py --compiler /path/to/xtensa-esp32s3-elf-gcc`
after both builds. It verifies linked feature symbols, one custom renderer, cache
sizes/placement, image budget, scratch storage and identical partition tables.

Both public Waveshare configurations have passed ESP-IDF 6.0.1 target builds.
The v13 map places exactly 51,200 bytes of avatar buffers in external BSS and
2,560 bytes of dirty scratch in internal BSS. Compared with legacy under the
same external-BSS configuration, total internal BSS decreases by 480 bytes,
external BSS increases by 51,200 bytes, and flash text increases by 3,724 bytes.
The runtime RGB565 dirty cache additionally grows by 43,008 bytes. Both padded
application images are 3,084,288 bytes, leaving 1,110,016 bytes in a 4 MiB slot;
identical padded sizes do not imply identical machine code. Partition tables
match, and disabled v13 links no head cache or scale table. These figures apply
to the synthetic build profile, not measured runtime heap.

Target ELF/map evidence is recorded locally under `local/validation/avatar-v13/`.
The private v13 image passed app-only flash/readback and a 45-second boot
observation with no panic or watchdog, with caption resources and the Muse
session connected. Device use has shown visible stutter; its cause and actual
frame rate remain unmeasured. Physical AMOLED appearance, settings/caption
layouts, simultaneous voice/Actell performance, packet/heap behavior and real
read/write animation events still require acceptance. Live TTS playback has
not been tested.
