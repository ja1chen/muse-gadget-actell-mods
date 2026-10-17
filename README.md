# Muse × Actell Bridge

English | [简体中文](README.zh-CN.md)

An independent Actell extension project for Muse Gadget SDK, targeting Waveshare ESP32-S3-Touch-AMOLED-2.16 with an optional headless configuration. Muse handles planning; the device component calls Actell over HTTPS/HMAC, using Actell's authorization, approval, and execution queue.

## Repository layout

| Directory | Contents |
|---|---|
| `bridge/` | Project implementation: host, extensions (including skills), patches, and tools |
| `upstream/` | Muse Gadget SDK URL, pinned commit, and archive hash; full SDK source is excluded |
| `docs/` | Protocols, architecture, and usage guides |
| `local/` | Local material, entirely Git-ignored and excluded from sharing |

See [bridge/README.md](bridge/README.md) for the internal layout and [AGENTS.md](AGENTS.md) for engineering conventions. `LICENSE` and [THIRD_PARTY.md](THIRD_PARTY.md) live at the repository root and document licensing and third-party provenance. Local logs, validation reports, caches, builds, upstream archives, credentials, and real screenshots belong in their respective `local/` subdirectories. Historical material is retained there; paths in old reports describe the environment at the time. Avatar's `bridge/extensions/avatar/preview/` is also entirely ignored and kept locally; do not commit or upload it.

## Local development

Requires Python 3.10+. The full host test suite also requires Node.js and a C compiler. The Python client uses only the standard library.

```sh
python3 bridge/tools/dev.py test
python3 bridge/tools/dev.py console --port 8766
# Or start only the protocol mock:
python3 bridge/tools/dev.py mock
```

Open the web console at `http://127.0.0.1:8766`. These entry points use synthetic screenshots and mock tools; they do not control a phone. The natural-language interface explicitly reports that it is not connected to a real Muse session. Tests create and close temporary loopback services. Logs go to `local/logs/current/`; Python caches and temporary builds go to `local/cache/` and `local/build/` respectively.

TTS transport configuration checks run with the ESP-IDF Python environment: `python3 bridge/tools/validation/run-tts-kconfig.py`; see the [TTS guide](docs/doubao-tts.md) for worker and target validation.

For other Python usage, run modules from `bridge/host/` or set `PYTHONPATH=bridge/host`. See the [host client guide](docs/host-client.md) for API examples.

## Upstream and project boundaries

- **Upstream**: official account/pairing flows, the Noise control channel, board drivers, and base Muse UI. The pinned version is recorded in the [source manifest](upstream/sources.json). Full source and development/test keys are kept only under `local/upstream/`.
- **Project extensions**: [gadget-actell](bridge/extensions/gadget-actell/README.md), [avatar](bridge/extensions/avatar/README.md), the [headless console](docs/headless-console.md), and optional [Doubao reply TTS](docs/doubao-tts.md). Their source is maintained in `bridge/extensions/`; staged SDK copies are used for build validation. TTS defaults to verified bidirectional WebSocket streaming, with HTTP streaming selected by disabling `CONFIG_DOUBAO_TTS_BIDIRECTIONAL`, and uses existing PCM playback; cloud and physical playback require separate acceptance.
- **SDK changes**: [patches](bridge/patches/README.md) contains only Muse Gadget SDK integration and adaptation patches. New headless files are kept synchronized between the extension directory and the patch.
- **Actell application**: connection management, authorization, and phone execution are developed and verified directly in the separate Actell project. This repository does not provide an Actell installation patch; the historical V1 patch is no longer part of the current integration workflow.
- **Host prototype**: `bridge/host/` supports protocol and UI regression checks. The current device component uses HTTPS directly over Wi-Fi and does not require the Muse VM Home Link transport proposed by the historical prototype.

The avatar defaults to rounded, statically shaded 160×160 v13 rendering, with independent shading/shape switches and a compile-time 64×64 fallback. Speaking uses peak capture, mode generations and a dedicated mouth envelope to bridge short audio gaps and close smoothly, while preserving the UI volume envelope; offline antialiasing is deferred. Head-distance prefiltering reduces listening render work while preserving sampled pixels between prefilter-off/on variants; see the [performance guide](docs/avatar-listening-performance.md). Staging preserves the 320 px layout and composes with caption fonts/TTS; host pixel checks and target builds do not establish device appearance or concurrent performance. See the [avatar guide](bridge/extensions/avatar/README.md).

Before building, prepare a separate copy of the pinned SDK under `local/build/`, follow the patch instructions, and stage the extensions. Keep this separate from Actell's existing USB-C/NCM forwarding firmware. Target compilation, real Muse account access, USB setup, phone execution, and model image input each require separate verification; host tests do not establish physical-device acceptance.

Keep the private configuration at a user-selected path, preferably outside this project. `--secrets` is required; there is no default project-local path. Create a placeholder with `python3 bridge/tools/apply-secrets.py --secrets /path/outside/project/device-secrets.conf --init`, then edit it locally. Only this tool may read the user's secrets; agents must not inspect, display or copy the source file into the project. Import with `python3 bridge/tools/apply-secrets.py --secrets /path/outside/project/device-secrets.conf --sdkconfig local/build/sdk-copy/esp32/build-tts/sdkconfig`. Only `CONFIG_GADGET_SDK_TOKEN` and optional `CONFIG_DOUBAO_TTS_API_KEY` belong in this file. A nonempty API key enables TTS automatically; resource and voice are fixed in the component. Values are hidden; see the [configuration guide](docs/doubao-tts.md). The optional `--sdk-token-only` mode is for isolated font builds and removes provider settings; omit it when preparing TTS or combined font/TTS firmware.

Optional [Waveshare 2.16 controls](bridge/extensions/waveshare-216-controls/README.md) separate left BOOT speaker mute, middle PWR sleep/wake and hardware-long shutdown, and right KEY3 Talk. Apply this extension last, after caption fonts; use its display-only overlay for the native 24px power icon. Existing touch settings remain available. Authorized device builds use the font/TTS runner with `--with-controls`; the extension also provides a checked inactive-slot app update tool. The controls regression in `dev.py test` requires the hash-verified pinned SDK archive under `local/upstream/archives/`. Basic button behavior and icon positions have been confirmed on hardware. Measured timing, wake power cost, real audio and extended display acceptance remain pending.

## Protocols and references

- [Muse Gadget SDK changes](docs/muse-sdk-changes.md): a brief index of SDK adaptations by category, with links to details.
- [Muse Gadget SDK extensions](docs/muse-sdk-extensions.md): a brief index of added capabilities, with links to details.
- [Gadget V2](docs/protocol/gadget-v2.md) / [Host V1](docs/protocol/v1.md)
- [Optional avatar activity](docs/optional-avatar-events.md)
- [Muse skill](bridge/extensions/skills/gadget-actell-remote/SKILL.md)
- [Provenance and licensing](THIRD_PARTY.md)

Maintain both indexes alongside implementation changes. Keep entries brief and organized by category, without dates or numbering; implementation, setup, and validation details belong in the linked guides.

The project's independent code follows the root Apache-2.0 LICENSE. Actell remains a separate application under its own GPL-3.0 license. Public synthetic authentication vectors are test data; project-local tokens, pairing information, and private keys belong in `local/credentials/`. User-selected secrets source files may remain outside the project.

Experimental [caption fonts and shared resources](docs/caption-font-integration.md) provide a default-off reader, bounded per-glyph decoding, pixel pagination, original-font fallback and public-config firmware builds. [Resource packages](docs/read-only-resources.md) contain Actell Device Sans candidates derived from pinned Adobe fonts; font assets retain OFL-1.1 separately from the code license. Generated data stays under `local/`. The font build runner also supports protected combined firmware with `--mode on --with-tts --secrets`; both extensions must be staged first. Add `--tts-transport http` to build the original HTTP backend into isolated `private-fonts-tts-http/` outputs; omitting it keeps bidirectional as the default. The proposed resource layout preserves original data partitions and dual OTA slots. Initial resource deployment and firmware startup checks have passed. Final font selection, visual/performance acceptance and rollback validation remain pending.

## Before sharing through Git

```sh
python3 bridge/tools/check-share.py
git status --short
```

The checker scans tracked and non-ignored files, reporting paths and issue types without printing credentials. It does not replace manual review. Generate distributable archives from reviewed Git commits and write them to `local/`; do not archive the entire working directory.

For a comparison with the actual credentials, run `python3 bridge/tools/apply-secrets.py --secrets /path/outside/project/device-secrets.conf --audit-share`. This tool checks Git-visible working files and staged contents, reporting sanitized locations only; it does not reveal values or matching lines.
