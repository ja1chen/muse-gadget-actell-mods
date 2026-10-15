# Third-party provenance

- Muse SDK: https://github.com/facebookincubator/muse-gadget-sdk.git at `812c46f9fe42b69d916613a9dc702cbac5ff7b54`, Apache-2.0. Used as the fixed integration baseline. Full source and Git history are excluded from this Git repository; local archives are ignored under `local/upstream/`. LICENSE is the standard Apache-2.0 license text copied from that checkout.
- Actell: https://github.com/ja1chen/actell.git, GNU GPL version 3. It is a separately maintained companion application. Its integration source is developed and verified in the Actell repository; this bridge does not distribute an Actell source archive or adapter patch. Historical local materials retain their original license and are excluded from Git sharing.
- Python uses standard-library modules only. The deterministic PNG is a 1×1 test fixture, not a user screenshot.

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
