# Caption font candidates

Experimental **Actell Device Sans**, derived from Adobe Source Han Sans SC
2.005R. This directory contains generation tools, the explicit dynamic-text
coverage policy, pinned input hashes, and the complete upstream OFL notice.
The default-off device reader, bounded per-glyph decoder and pixel pagination
are implemented through thin SDK hooks. External shared storage, binary packages
and budgets are available. Initial deployment and firmware startup checks passed; visual/performance acceptance and rollback validation remain pending. See [resource packages](../../../docs/read-only-resources.md)
and [the implementation report](../../../docs/caption-fonts.md).

## Reproduce

Requires Node.js, npm and Python 3.10+. Run from the repository root:

```sh
python3 bridge/extensions/muse-fonts/tools/fetch.py
npm install --prefix local/cache/caption-font-tools --cache local/cache/npm \
  --ignore-scripts --no-audit --no-fund lv_font_conv@1.5.3
node bridge/extensions/muse-fonts/tools/generate.js
```

The generator verifies font and license hashes before conversion. Its default
matrix is Regular/Medium at 24px/4bpp, 28px/4bpp and 24px/2bpp. To regenerate
one candidate, append e.g. `Regular:24:4`. That run's `summary.json` lists only
the candidates requested in that invocation; individual candidate reports persist.

Generated C data, standard binary `.font` data and distribution notices go to `local/build/caption-fonts/`;
coverage reports and 480×480 PNGs go to `local/validation/caption-fonts/`.
PNGs show converter raster data at its actual quantized bit depth and 1:1
pixels, without browser OTF text. They are glyph comparisons, not the Muse
screen layout, original-font comparisons, LVGL decoder tests or device acceptance.
The preview sample is synthetic and is never used to restrict the dynamic charset.

Build a shared resource container (font, coverage/provenance and complete license):

```sh
python3 bridge/extensions/muse-fonts/tools/pack.py build --font actell_device_sans_regular_24_4
```

Containers go to `local/build/read-only-resources/`; their budgets go to
`local/validation/read-only-resources/`. `pack.py verify` checks ABI, resource
bounds, hashes and required IDs/formats. Packaging does not demonstrate target
font decoding or zero-copy loading. The default proposed shared region is
7.8125MiB; it holds one candidate and future assets, not all six candidates.

Measure a candidate with the existing target compiler and LVGL 9.5.0 source:

```sh
python3 bridge/extensions/muse-fonts/tools/budget.py \
  --compiler "$XTENSA_CC" --lvgl-root "$LVGL_ROOT"
```

Object compilation uses `LV_CONF_SKIP`, `LV_FONT_FMT_TXT_LARGE=1` and
`LV_USE_FONT_COMPRESSED=1`. It tests font-format compatibility and allocated
font sections, not the application's actual configuration, linker map or app.bin.
Reports contain no private configuration. The default screen is a 4MiB app
partition with 256KiB reserved free; these are explicit screening inputs and can
be changed with `--app-capacity` and `--reserve` after checking the actual target.

## Coverage and rendering policy

`charset/dynamic.json` requests broad Unicode ranges including basic CJK,
Extension A, traditional characters, kana, punctuation and extended Latin.
Every cmap-supported glyph in those ranges is retained; holes are enumerated
in each report. No claim covers all Chinese, all Unicode, emoji or combining
sequences. Coverage is not a language-localization feature.

Compression is enabled. Kerning is explicitly disabled: the converter's full
pair scan is quadratic for this 29k-glyph profile. Any subsequent renderer and
pagination must use the emitted advances with the same no-kerning policy.
No fallback font is silently embedded. The integrated normalizer preserves covered characters and LF, maps tab/CR to
spaces, and uses a visible question mark for unsupported or invalid text. Missing
or incompatible resources keep the original font and pagination path. The emitted line height is 46px at size 24 and 54px at size
28, reflecting all retained glyph extrema rather than assuming size equals pitch.

## Licensing

Independent tools follow the repository Apache-2.0 license. Font inputs and
derived glyph resources remain **OFL-1.1**, an exception to the root code license.
Retain `licenses/SourceHanSans-OFL.txt`, `manifest.json` and the per-candidate
report with any distribution of generated fonts or firmware containing them.
Use the modified name Actell Device Sans, without the reserved name “Source”.
Do not sell the font by itself or imply Adobe endorsement. The converter's MIT
license does not replace OFL. This project has not created or published a font
or firmware release; whole-video, character, image and audio permissions require
their own provenance review.

The per-glyph decoder adapts LVGL's MIT RLE state machine; retain
`licenses/LVGL-MIT.txt` with distributions of that implementation.

## Firmware and validation

See [caption integration](../../../docs/caption-font-integration.md) for the
default-off `CONFIG_ACTELL_CAPTION_FONT`, thin patch staging, public-config
on/off/headless builds and validation scope. `partitions.resources.csv` adds
the generic resource region while preserving every original partition entry;
it does not migrate or write a device. Resource initialization and normal
operation provide no Flash-writing or credential-management interface.

`dev.py test` runs synthetic container and real C core checks without downloaded
fonts. `run-font-host.py` checks all six actual binary candidates against exact
converter pixel/metric oracles and runs ASan/UBSan. `run-resource-port.py` uses
the actual device source with read-only host partition and crypto substitutes;
these results are separate from physical-device acceptance.
