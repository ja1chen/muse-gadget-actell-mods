# Third-party provenance

- Muse SDK: https://github.com/facebookincubator/muse-gadget-sdk.git at `812c46f9fe42b69d916613a9dc702cbac5ff7b54`, Apache-2.0. Used as the fixed integration baseline. Full source and Git history are excluded from this Git repository; local archives are ignored under `local/upstream/`. LICENSE is the standard Apache-2.0 license text copied from that checkout.
- Actell: https://github.com/ja1chen/actell.git, GNU GPL version 3. It is a separately maintained companion application. Its integration source is developed and verified in the Actell repository; this bridge does not distribute an Actell source archive or adapter patch. Historical local materials retain their original license and are excluded from Git sharing.
- Python uses standard-library modules only. The deterministic PNG is a 1×1 test fixture, not a user screenshot.
- Caption font tooling: Adobe Source Han Sans SC 2.005R, OFL-1.1. Modified glyph resources are named Actell Device Sans and remain OFL-1.1, separately from the root code license. Font inputs and generated resources are excluded from Git under `local/`; the complete original notice is retained in `bridge/extensions/muse-fonts/licenses/SourceHanSans-OFL.txt`. Pinned URLs and input SHA-256 hashes are in that extension's `manifest.json`; generated hashes and coverage are in local candidate reports. The lv_font_conv 1.5.3 converter is MIT-licensed and installed only in the local cache, not vendored. Font licensing does not cover other demo media or characters.
- The bounded per-glyph RLE decoder follows LVGL 9.5.0 fmt_txt compression semantics and adapts its state machine, copyright 2025 LVGL Kft, MIT. The full notice is retained in `bridge/extensions/muse-fonts/licenses/LVGL-MIT.txt`. The reader adds explicit input bounds and uses per-call decoder state without whole-font bitmap allocation. LVGL source is resolved locally rather than vendored as project code.

For reproducible Muse SDK acquisition, use `upstream/sources.json`, check out its exact commit, verify `git rev-parse HEAD`, and follow the pinned SDK's build instructions. Only Muse Gadget SDK uses this project's upstream patch workflow. Verify the independently maintained Actell integration separately from the actual source tree in use.

The independent gadget component additionally resolves Espressif mDNS 1.10.0
(Apache-2.0) and the upstream-compatible Espressif cJSON component via ESP-IDF's
component manager. Their source is not vendored here. Resolved build dependency
locks are retained locally under `local/validation/gadget-actell/`; the SDK and managed
components retain their upstream licenses. No production credential is included.

The optional Doubao TTS extension calls Volcengine's documented HTTP API and
uses ESP-IDF HTTP/TLS plus the same resolved Espressif cJSON component. It does
not vendor a Doubao SDK or copy its sample client. Host worker tests compile
the locally resolved cJSON source with its original license unchanged.
