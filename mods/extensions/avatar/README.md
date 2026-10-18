# Pebble: Actell v13 avatar and optional phone activity

The default renderer uses rounded, statically shaded v13 160×160 geometry: a milk-white
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
is retained as `tests/reference_v13.c` for exact-v13 fallback pixel checks (rounded face and shading disabled, excluding the intentional quiet-speaking mouth fix). Integration
targets Muse SDK `812c46f9fe42b69d916613a9dc702cbac5ff7b54`.

## Rounded face and static shading

The reviewed rounded/shaded delivery changes the lower-face taper from 0.14 to
0.04 and adds 32 warm skin shades with a 4×4 ordered dither. It preserves the
single slanted brow, facial features, phone/hands, double nods, animation timing
and activity priority. Both exact boundaries and certified interiors share the
same skin fill; the distance prefilter and exact head-cache keys are retained.
No extra timer or full-frame array is introduced. Static local shading still
changes pixels during nods and can increase dirty areas.

These are C compiler definitions, not new sdkconfig/menuconfig options:

- `MUSE_AVATAR_ROUNDED_FACE=0`: restore the earlier narrower chin.
- `MUSE_AVATAR_SHADING=0`: disable added shading, preserving the rounded face.
- `MUSE_AVATAR_DYNAMIC_LIGHT=1`: add pitch-dependent lighting; default is 0.
- `MUSE_AVATAR_EXACT_DISTANCE_PREFILTER=0`: use exact distances for A/B checks.

Default rounded face and shading are enabled. Disable both for the
original face and palette; the independent speaking-mouth behavior below still
applies to the 160-grid UI. Exact original-v13 comparisons use the legacy mouth
channel above its quiet threshold. `CONFIG_MUSE_AVATAR_V2=n` still selects the unchanged 64-grid
character. Offline raw AA is enabled by default for the selected appearance. See the [raw-AA guide](../../../docs/avatar-raw-aa.md) for its separate resource, strict PSRAM requirement, generation and deployment. Missing/unusable assets preserve procedural rendering when PSRAM is available; AA is intentionally not pixel-identical to procedural geometry.

## Stable speaking mouth

The 160-grid mouth uses a dedicated state observer and UI envelope. Every
`muse_state_set_level()` keeps its original shared-level assignment and separately
captures the maximum normalized PCM level since the previous UI read. Levels
above 0.02 update the last-voice timestamp; a brief zero does not clear it. One
UI consumer calls `muse_state_take_mouth_sample()` before menu/image/off-screen
early returns. Its short FreeRTOS critical section copies the sample and clears
only the peak, preventing a producer update from being lost between read/clear.
Mode changes increment a generation and reset voice/peak state, so even an exit
and reentry between UI ticks resets the mouth. OFF checks and mode writes share
the same lock. These are task-context APIs, not ISR interfaces.

`src/actell_mouth_envelope.h` uses 60 ms amplitude attack and 140 ms release, while
silence closure is separate: hold for 60 ms after the last level above 0.02, then
fade the opening baseline to zero over 80 ms. Continuous silence closes at 140 ms
plus UI quantization; asynchronous host schedules measured 140–219 ms. Short
20–80 ms block gaps no longer force a threshold crossing. Quiet input above 0.02
still animates. Sustained input at or below 0.02 can be treated as silence; weak
background energy above it can keep a small mouth. This is energy observation,
not speech recognition, and these are model results rather than device latency.

The independent channel draws each integer mouth height from 1 through 16,
avoiding the former one-line-to-small-ellipse jump. The original UI `s_level`
0.6/0.2 smoothing still drives the meter, eyes and other effects. Voice, audio,
TTS, MP3 and PCM playback code are unchanged; no new recording, frame/audio cache
or heap allocation is introduced. Current level/mode call sites in the combined
SDK are task paths. Lock scheduling cost and actual sound/animation stability
need device acceptance.

Appended `muse_pose_t.mouth_level` and `mouth_level_valid` fields require a complete
component rebuild. Zero-initialized callers without a valid channel preserve the
previous quiet-mouth mapping at `pose.level <= 0.05`. The 64-grid renderer ignores
the channel and retains its old appearance. A late asynchronous callback from an
old audio session still cannot be attributed to that session without an upstream
session token; the observer follows the mode at the time of the callback.

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
serial dispatch. See [`mods/host/muse_client/presentation.py`](../../host/muse_client/presentation.py)
and [activity integration](../../../docs/optional-avatar-events.md) for the separate
Python activity wrapper. A real VM-to-device event transport is NOT implemented by
this renderer; the existing phone-control/voice path must not wait for it.

## Stage and downgrade

Use a separate verified SDK copy under `local/build/`. Apply the gadget hooks,
then stage avatar, caption fonts and optional TTS in that order (see
[patch instructions](../../patches/README.md)). Existing custom avatars must be
preserved outside the SDK component before staging; never overwrite them.

```sh
python3 mods/extensions/avatar/tools/stage.py --sdk-root local/build/sdk-copy
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
python3 mods/extensions/avatar/tools/stage.py --sdk-root local/build/sdk-copy --rollback
```

Rollback restores the five verified SDK files and removes only unchanged staged
files. It refuses if later font/UI patches changed them. For a combined SDK,
recreate a clean staged copy in the documented order to omit avatar; do not force
rollback through later edits. Restore the backed-up custom avatar if needed.

## Memory and build validation

The default raw-AA renderer uses a 51,200-byte framebuffer and 51,200-byte head cache, both required in external PSRAM, plus small control/scale buffers. It refuses an internal-memory image-buffer fallback. The procedural geometry reference uses a 25,600-byte framebuffer, a 25,600-byte exact head-geometry cache and a
1,024-byte scale table. For the procedural reference, enabling ESP-IDF
`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` places the two large arrays in
external BSS (51,200 bytes). This option is mandatory for production 160-grid raw-AA builds. In the standalone historical reference there is
no runtime allocation fallback for these arrays. Enable external BSS only on a
PSRAM board with working PSRAM initialization. The SDK dirty RGB565 heap cache
grows from 8,192 to 51,200 bytes and retains its full-redraw allocation fallback.
For the historical procedural renderer, the nominal increase over the 64-grid path is about 92 KiB including dirty scratch, before target alignment/control state. Its squared-distance prefilter and original boundary-band distances remain the geometry reference. Raw-AA cache keys use the documented quantized poses; dirty/flush area can increase with coverage pixels, while animation timing formulas stay unchanged.
The shaded update adds 64 bytes of palette RAM and 144 bytes of palette/threshold constants; no additional full-frame buffer or heap allocation is introduced. No device FPS, peak heap or power claim follows from host tests.

With the existing ESP-IDF 6.0.1 environment activated and all extensions staged,
build all three Waveshare profiles using public synthetic configuration:

```sh
python3 mods/extensions/avatar/tools/build.py --sdk local/build/sdk-copy \
  --components-dir local/build/resolved-sdk/esp32/managed_components --mode v13
python3 mods/extensions/avatar/tools/build.py --sdk local/build/sdk-copy \
  --components-dir local/build/resolved-sdk/esp32/managed_components --mode v13 --shading off
python3 mods/extensions/avatar/tools/build.py --sdk local/build/sdk-copy \
  --components-dir local/build/resolved-sdk/esp32/managed_components --mode legacy
```

Replace the components directory with existing resolved components. For these public profiles, the intentionally invalid TTS key only enables
compilation. No services are called. All three profiles compile fonts/TTS/Actell and explicitly opt
into the board's PSRAM BSS. `--internal-bss` checks internal-RAM linkage separately.
Builds use separate `local/build/avatar-v13/{v13-shaded,v13-flat,legacy}` directories, with corresponding logs/reports under `local/`; no flashing is performed. When the current 2.16 controls header is staged, the builder includes its required Montserrat 24 overlay. These synthetic
images are compilation artifacts, not credential-configured device releases.

For a locally configured device build, add `--secrets /path/outside/project/device-secrets.conf`
to the v13 command. Only `mods/tools/apply-secrets.py` reads that explicit input;
normal import enables SDK authentication and TTS together. Private builds use
`local/build/avatar-v13/private-v13-shaded/`, owner-only artifacts/logs, and compiler-only
feature checks with configuration contents withheld. They never print private
headers, firmware contents or build logs. No flash or account/service call is
performed by building.

## Reproduce host tests and previews

```sh
python3 mods/tools/dev.py avatar
python3 mods/extensions/muse-fonts/tools/check-stage.py --with-avatar --with-tts
```

The first command verifies the pinned archive and direct/staged legacy, shaded, flat and dynamic-light renderer
branches with ASan/UBSan: each renders 1,680 frames at eight sizes and exercises
strips, TTL, toggle, clock wrap, invalid values and voice priority. It compares
2,400 v13 cases against the original oracle, using speaking levels above the new closure threshold, checks 199 dirty-frame pairs, ESP
interface compilation, patch equivalence, idempotence and protected rollback.
Dedicated mouth checks verify threshold-adjacent float32 levels, 9×1 closure, non-finite normalization, 3,000 unchanged non-speaking frames, 1,000 unchanged open-speaking frames, 200 quiet frames unchanged outside the mouth, and 1,000 independent-channel frames with unchanged pixels outside the mouth, 1,000 exact legacy-channel fallback frames, and all mouth heights 1–16. Sanitizer envelope tests cover 100/200/500 ms pauses, generation/reentry, non-finite input and clock boundaries. Observer tests extract the actual staged state functions: 1,280 asynchronous producer/UI schedule and phase cases, plus 10,000 concurrent producer updates using a host pthread mutex. These tests do not model ESP32 dual-core scheduling. The shaded checks compare 18,000 pairs for static/dynamic prefilter equivalence and original-v13 fallback, and confirm 74 head-cache rebuilds in 200 listening frames. It also compares 12,000 complete 160×160 frames with the distance prefilter
off/on and records separate operation counts and uninstrumented host timings.
See [listening performance](../../../docs/avatar-listening-performance.md) for
limits, diagnosis and target A/B steps. LeakSanitizer is disabled; this is not a passed leak check. Reports are written
to `local/validation/current/`. When local raw assets are present, additional bound-AA sanitizers, PSRAM failure mocks, 2,000 unbound procedural comparisons, 2,160 bound frames, and 100 rebind cycles run; geometric prefilter/oracle checks explicitly use the retained procedural geometry source.

Optional previews require Pillow and render actual C output:

```sh
python3 mods/extensions/avatar/tools/validate.py \
  --sdk-archive local/upstream/archives/muse-gadget-sdk-812c46f.tar.gz
```

Add `--legacy` for the original character and use a separate `--output` directory
to retain both previews. New PNGs/GIFs default to
`local/validation/avatar-preview/`. Existing `preview/` stays ignored and local.

Run `python3 mods/extensions/avatar/tools/check-builds.py --compiler /path/to/xtensa-esp32s3-elf-gcc`
after all three builds. It verifies linked feature symbols, one custom renderer, cache
sizes/placement, image budget, scratch storage and identical partition tables.

The earlier pre-shading v13 and legacy configurations passed target builds and
physical boot/voice observations; those records do not validate the new shaded
renderer. Current build/map reports live under `local/validation/avatar-v13/`.
The build matrix checks the same partition table, both 25,600-byte arrays in
external BSS, unchanged scale table and dirty scratch, no AA symbols, and a
64-byte internal-BSS increase for shading over the same rounded flat renderer.
The earlier private shaded image, before the quiet-speaking fix, passed inactive-slot writing and readback, with the
previous app, NVS, pairing and shared resources preserved. A 45-second startup
observation confirmed font readiness and the Muse session, without panic or
watchdog. These checks do not establish appearance or concurrent performance.
New shaded appearance, recording/listening concurrency, actual display transfer,
frame timing and heap/stack high-water marks need separate device acceptance.
