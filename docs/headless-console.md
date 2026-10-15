# ESP32-S3 N16R8 headless development path

## Two deliberately separate running modes

1. **Cloud/host simulator (runnable now):** `python3 bridge/tools/dev.py console --port 8766`, then open exactly `http://127.0.0.1:8766`. It provides explicit text submission, run cancellation, simulated `list_apps` and `screenshot`, and logs. The text endpoint returns HTTP 501 because this Python adapter has no real Muse session. It does not fabricate an Agent response or control an iPhone. `localhost` is intentionally rejected; use the numeric loopback URL.
2. **Board Muse text console (ESP-IDF target build verified, not flashed):** the development-only firmware patch below calls the real Muse typed-turn API and shows its actual response events in a LAN web page. It needs ESP-IDF, verified board hardware, successful official pairing, Wi-Fi, and a reachable Muse account/VM. No account or hardware was contacted during development.

Neither mode proves that Muse can reach a real Actell endpoint or see screenshot pixels. The new independent gadget command component reaches Actell directly over Wi-Fi HTTPS/HMAC and requires trusted USB bootstrap plus Actell per-device authorization. It does not need a VM Home Link network adapter or `/link-tunnel`. Real device execution and model image visibility still require runtime validation. The board page does not contain Actell keys or directly call Actell.

## Verified upstream facts

Source: `https://github.com/facebookincubator/muse-gadget-sdk`, pinned commit `812c46f9fe42b69d916613a9dc702cbac5ff7b54`.

- `esp32/components/muse/muse_chat.h`: `muse_hatch_text_turn(char *)` takes ownership of a malloc-allocated string; `muse_hatch_text_cancel()` requests cancellation.
- `muse_chat_session.cpp`: typed text uses the existing authenticated HTTP-over-Noise session, `POST /chat/stream`, and replies on `POST /chat/subscribe`. These are internal device session paths, not a newly invented anonymous HTTP API.
- `muse_chat_text.c`: typed replies become serial `@chat` JSON events. Types include `sent`, `busy`, `text`, `final`, `message_done`, `done`, and `error`. The existing `tools/muse/chat.py` consumes these on normal supported Muse builds.
- The generic upstream S3 DevKit overlay targets **N8R8** and assumes RGB GPIO38/BOOT GPIO0. It is not a verified match for an unknown N16R8 carrier.
- There is no full generic headless Muse UI board overlay in this pinned checkout. The Voice PE path already compiles just the chat session without the LVGL/display application. This patch reuses that source selection and `main/voice_muse_chat.c`'s paired-account/Wi-Fi settings shim; it does not select a made-up screen board.

## Delivered firmware files

`bridge/patches/muse-gadget-sdk/headless.patch` applies to the pinned repository root. It adds the following narrowly scoped changes:

- A `MUSE_HEADLESS` Kconfig option for S3 + PSRAM + `MUSE_BOARD_NONE`, mutually exclusive with the voice-board mode.
- Existing Muse chat/session/text components and `voice_muse_chat.c`, without the display, codec, microphone, speaker, LVGL application, or a USB-NCM implementation.
- A callback before the existing `@chat` serial write. The old serial output is preserved. A bounded 64 × 400-byte ring in PSRAM retains event chunks, reports missed history, and never waits for a browser in the Muse task.
- A server-owned one-turn gate, capped 4096 UTF-8-byte input, body/time/socket limits, separate poll/cancel rate limits, and a one-second text submission limit. Browser refresh only polls state; it never resubmits a turn.
- Actual `muse_hatch_text_turn` / `muse_hatch_text_cancel` calls. HTTP 202 means queued/requested, not remote completion. Cancellation does not undo actions already executed by Muse or Actell.
- A token-authenticated development HTTP page, **disabled by default**. The 128-bit random per-boot token is printed only to serial and kept in browser memory after manual entry. It is never placed in URLs, localStorage, cookies, source configuration, or Actell requests. Host is restricted to the station's numeric IP, and API Origin must match that HTTP origin; there is no permissive CORS.

The C source/HTML files under `bridge/extensions/headless-console/` are readable copies of the patch's new files, not an additional standalone replacement firmware. Do not copy them into old USB-NCM firmware.

## Security and pairing prerequisites

**The board console is a plaintext development service, not production-safe LAN remote control.** Both the temporary token and chat contents travel over HTTP. A person observing the LAN could steal the token and issue chat requests. Use an isolated, trusted development Wi-Fi network; do not expose port 80 to a public network, use port forwarding, or enter highly sensitive information. This warning applies to the optional browser text console only. The separate gadget→Actell connection uses its own trusted TLS/HMAC bootstrap and never exposes those credentials through this page. Reboot rotates the token. Keep serial logs private and do not share token-bearing screenshots.

The server is compiled off unless `CONFIG_MUSE_HEADLESS_HTTP_DEBUG=y` is explicitly selected. Even then, security checks do not make an untrusted LAN safe. The Python simulator remains numeric-loopback-only and is not a board-to-host LAN adapter.

**Physical pairing approval is preserved.** The default `CONFIG_MUSE_HEADLESS_BUTTON_GPIO=-1` disables button initialization, so a fresh board cannot complete official pairing until you have verified and configured its actual BOOT/button GPIO. No webpage remotely confirms pairing or installs credentials. N16R8 identifies memory size, not carrier wiring. A board without an identified usable button needs a separately reviewed physical confirmation solution; do not bypass confirmation.

## Build preparation, without touching the user's board

Do this only in a separate checkout/project. No existing USB-NCM project is edited. These are instructions for a future explicitly authorized build/flash session, not commands already executed against hardware.

1. Verify the module marking and vendor schematic. Confirm ESP32-S3, 16 MiB QSPI flash, 8 MiB octal PSRAM, flash voltage/mode, USB programming connector, and an available physical pairing button. Do not infer GPIO from memory size. The supplied memory settings are a candidate N16R8 profile; verify before use.
2. Obtain the official Muse repository at the pinned commit. Its `esp32/AGENTS.md` requires ESP-IDF **v6.0.1**; do not run an unreviewed upstream setup script. Keep this checkout separate from all previous NCM firmware.
3. At that repository root, run `git apply --check /path/to/muse-headless.patch`, then apply the patch only in the new independent checkout. The check was verified against the pinned source; applying to another version is not supported without review.
4. Stage the new component after this patch using `bridge/extensions/gadget-actell/tools/stage.py`. Copy `bridge/extensions/gadget-actell/overlays/sdkconfig.headless-s3` and `sdkconfig.bridge` into the new `esp32/` working directory. For isolated LAN testing only, explicitly enable `CONFIG_MUSE_HEADLESS_HTTP_DEBUG=y`. Use a fresh build directory and generated sdkconfig. Do not reuse a display-board sdkconfig or layer `devices/sdkconfig.muse`.
5. With the verified ESP-IDF installation active, configure from the `esp32/` directory, for example:

   `idf.py -B build-headless -D SDKCONFIG=sdkconfig.headless -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.headless-s3;sdkconfig.bridge" set-target esp32s3`

   `idf.py -B build-headless -D SDKCONFIG=sdkconfig.headless menuconfig`

   In the Muse menu set the **verified physical pairing button GPIO**. Leave unknown pins disabled. Configure SDK/account setup only through the official supported local process, never by committing real tokens/passwords into these files. Inspect the resulting config: `MUSE_BOARD_NONE=y`, `MUSE_ENABLED` off, `HOMEHUB_VOICE` off, `HOMEHUB_LED_BACKEND_NONE=y`, correct flash/PSRAM, and explicit debug HTTP choice.

   `idf.py -B build-headless -D SDKCONFIG=sdkconfig.headless build`

6. Before any flash, preserve the old firmware/recovery procedure and confirm the exact port, partition layout and module. **Flashing this board as Muse temporarily removes its old USB-C/NCM bridge role.** For Actell end-to-end tests, the iPhone must still have its independently verified execution/VPN route; retain a second old bridge board if its phone-control path needs that board. The new command bridge itself uses direct LAN HTTPS. This patch cannot provide both firmware roles at once.
7. Flash only after explicit hardware access approval in the appropriate environment. Complete official Muse BLE provisioning and its physical-button confirmation. No display/microphone/speaker is required. Use the numeric station IP from the Wi-Fi/router/serial output to open `http://<board-ip>/`, enter the per-boot console token, and explicitly submit text.
8. Test real statuses and event completion, cancel an active turn, disconnect/reconnect Wi-Fi, reload the page, reboot to check token rotation, and verify Muse app history agrees. Text chunks are bounded history; the Muse app remains the authoritative full conversation if the page reports lost events.

The event ring budgets 25.6 KiB of PSRAM and fails closed if allocation fails. Polling copies one 400-byte event at a time rather than allocating a second full ring; JSON response memory remains bounded by 64 events. The original Muse session still allocates some voice-related buffers/decoder code even though no audio device is initialized. This is a first headless integration, not a measured minimum-RAM port. Flash size, heap headroom, Wi-Fi/BLE coexistence, sustained output, must be measured on the actual board. The new Actell profile disables the L3 tunnel and retains `/link-control`.

## Validation status

- Passed: patch clean-application check against the exact pinned source; JavaScript syntax check; simulator contract/security tests reported in the package validation notes.
- Source reviewed: real typed/cancel API and serial reply path, no unknown GPIO initialization by default, no browser Actell key, explicit mock/real separation.
- Historical build record (2026-10-12): ESP-IDF v6.0.1/Xtensa ESP32-S3 compile/link with headless + independent gadget command bridge (image output off, binary 0x161000 bytes, 66% of 4-MiB app slot free); separate bridge-disabled + headless HTTP_DEBUG build (0x151000 bytes, 67% free). Reports are local-only under `local/validation/gadget-actell/`, with logs in `local/logs/gadget-actell/`. Re-run target builds before using this as current build evidence.
- **Not run:** flashing, physical pairing, board HTTP runtime, real Muse subscription, trusted USB/TLS/Bonjour interoperation with an iPhone, real Actell execution/VPN route, visual-model image input and hardware resource measurements. No SDK token or production credential was used; official pairing still requires the user's supported local setup.

Do not describe this patch as ready-to-flash or a verified end-to-end Muse × Actell integration until those checks pass.
