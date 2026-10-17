#!/usr/bin/env node
// Independent tooling: Apache-2.0. Generated glyph assets: OFL-1.1.
'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const root = path.resolve(__dirname, '../../../..');
const here = path.resolve(__dirname, '..');
const converter = path.join(root, 'local/cache/caption-font-tools/node_modules/lv_font_conv');
const collect = require(path.join(converter, 'lib/collect_font_data'));
const writeLVGL = require(path.join(converter, 'lib/writers/lvgl'));
const writeBin = require(path.join(converter, 'lib/writers/bin'));
const setDepth = require(path.join(converter, 'lib/utils')).set_depth;
const opentype = require(path.join(converter, 'node_modules/opentype.js'));
const { PNG } = require(path.join(converter, 'node_modules/pngjs'));
const nativeMetrics = require('./native-metrics');
const manifest = JSON.parse(fs.readFileSync(path.join(here, 'manifest.json')));
const charset = JSON.parse(fs.readFileSync(path.join(here, 'charset/dynamic.json')));
const sample = fs.readFileSync(path.join(here, 'charset/preview.txt'), 'utf8');
const sha = data => crypto.createHash('sha256').update(data).digest('hex');
const output = path.join(root, 'local/build/caption-fonts');
const reportDir = path.join(root, 'local/validation/caption-fonts');
fs.mkdirSync(output, { recursive: true });
fs.mkdirSync(reportDir, { recursive: true });
function checked(name) {
  const bytes = fs.readFileSync(path.join(root, 'local/cache', name));
  if (sha(bytes) !== manifest.files[name].sha256) throw Error(`Hash mismatch: ${name}`);
  return bytes;
}

// Draw the same FreeType raster and quantization supplied to the LVGL writer.
// No browser OTF rendering, scaling, or substitute glyphs in these previews.
function preview(font, bpp, filename) {
  const png = new PNG({ width: 480, height: 480 });
  for (let i = 0; i < png.data.length; i += 4) {
    png.data[i] = 15; png.data[i + 1] = 18; png.data[i + 2] = 28; png.data[i + 3] = 255;
  }
  const glyphs = new Map(font.glyphs.map(g => [g.code, setDepth(g, bpp)]));
  const pitch = font.ascent - font.descent + 4;
  let x = 16, baseline = 16 + font.ascent;
  for (const ch of sample) {
    const glyph = glyphs.get(ch.codePointAt(0)) || glyphs.get(63);
    if (ch === '\n' || x + glyph.advanceWidth > 464) {
      x = 16; baseline += pitch;
      if (ch === '\n') continue;
    }
    if (baseline - font.descent >= 480) break;
    for (let y = 0; y < glyph.bbox.height; y++) {
      for (let gx = 0; gx < glyph.bbox.width; gx++) {
        const px = Math.round(x) + glyph.bbox.x + gx;
        const py = baseline - glyph.bbox.y - glyph.bbox.height + y;
        if (px < 0 || px >= 480 || py < 0 || py >= 480) continue;
        const i = (py * 480 + px) * 4, a = glyph.pixels[y][gx] / 255;
        [216, 210, 255].forEach((c, k) => { png.data[i + k] = Math.round(png.data[i + k] * (1 - a) + c * a); });
      }
    }
    x += glyph.advanceWidth;
  }
  fs.writeFileSync(filename, PNG.sync.write(png));
}

async function main() {
  if (require(path.join(converter, 'package.json')).version !== manifest.converter.version) throw Error('Wrong converter version');
  checked('SourceHanSans-OFL.txt');
  const reports = [];
  const variants = process.argv.slice(2);
  for (const variant of variants.length ? variants : ['Regular:16:4']) {
    const [weight, px, depth] = variant.split(':');
    if (!['Regular', 'Medium'].includes(weight) || !['16', '24', '28'].includes(px) || !['2', '4'].includes(depth)) throw Error('Use Regular|Medium:16|24|28:2|4');
    const size = Number(px), bpp = Number(depth);
    const name = `actell_device_sans_${weight.toLowerCase()}_${size}_${bpp}`;
    const source = `SourceHanSansSC-${weight}.otf`, bytes = checked(source);
    const font = opentype.parse(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength));
    const requested = charset.ranges.flatMap(([lo, hi]) => Array.from({ length: parseInt(hi, 16) - parseInt(lo, 16) + 1 }, (_, i) => parseInt(lo, 16) + i));
    const missing = requested.filter(cp => !font.charToGlyphIndex(String.fromCodePoint(cp)));
    const args = {
      font: [{ source_path: source, source_bin: bytes, ranges: [{ range: charset.ranges.flatMap(([lo, hi]) => [parseInt(lo, 16), parseInt(hi, 16), parseInt(lo, 16)]) }] }],
      size, bpp, no_kerning: true, no_compress: false, no_prefilter: false,
      lv_include: 'lvgl.h', lv_font_name: name, output: `${name}.c`, opts_string: `Actell Device Sans; ${variant}; charset/dynamic.json; no kerning; compressed`
    };
    console.log(`Generating ${variant}; ${requested.length - missing.length} cmap glyphs`);
    const data = await collect(args);
    const metricsPolicy = size === 16 ? nativeMetrics(data) : null;
    // Independent oracle for the bounded C decoder: exact quantized A8 pixels
    // and integer advances from the converter before binary serialization.
    const oracle = data.glyphs.map(g => ({
      code: g.code, advance: Math.round(g.advanceWidth),
      width: g.bbox.width, height: g.bbox.height, x: g.bbox.x, y: g.bbox.y,
      sha256: sha(Buffer.from(setDepth(g, bpp).pixels.flat()))
    }));
    fs.writeFileSync(path.join(reportDir, name + '.oracle.json'), JSON.stringify(oracle));
    const generated = writeLVGL(args, data)[args.output];
    const c = `/* Derived from Source Han Sans SC ${manifest.upstream_version}.\n * Copyright 2014-2025 Adobe. Modified font: Actell Device Sans.\n * SPDX-License-Identifier: OFL-1.1\n * See licenses/SourceHanSans-OFL.txt and manifest.json.\n */\n${generated}`;
    fs.writeFileSync(path.join(output, args.output), c);
    const binArgs = { ...args, output: `${name}.font` };
    const binary = writeBin(binArgs, data)[binArgs.output];
    fs.writeFileSync(path.join(output, binArgs.output), binary);
    preview(data, bpp, path.join(reportDir, name + '.png'));
    const report = { name, source, source_sha256: sha(bytes), size, bpp, metrics_policy: metricsPolicy, glyphs: data.glyphs.length, missing: missing.map(cp => cp.toString(16).toUpperCase()), line_height: data.ascent - data.descent, baseline: -data.descent,
      output_sha256: sha(c), c_source_bytes: Buffer.byteLength(c), binary_sha256: sha(binary), binary_bytes: binary.length, binary_format: 'lv_font_conv-bin-1.5.3', charset_sha256: sha(fs.readFileSync(path.join(here, 'charset/dynamic.json'))), preview_sample_sha256: sha(sample),
      converter: manifest.converter, reproduction: `node bridge/extensions/muse-fonts/tools/generate.js ${variant}`,
      preview: 'Output raster at selected bpp, 1:1 pixels; 16px applies documented line fitting; not device acceptance or LVGL line-layout validation' };
    fs.writeFileSync(path.join(reportDir, name + '.json'), JSON.stringify(report, null, 2) + '\n');
    reports.push(report);
  }
  fs.mkdirSync(path.join(output, 'licenses'), { recursive: true });
  fs.copyFileSync(path.join(here, 'licenses/SourceHanSans-OFL.txt'), path.join(output, 'licenses/SourceHanSans-OFL.txt'));
  fs.copyFileSync(path.join(here, 'manifest.json'), path.join(output, 'manifest.json'));
  fs.writeFileSync(path.join(reportDir, 'summary.json'), JSON.stringify(reports, null, 2) + '\n');
}
main().catch(error => { console.error(error); process.exitCode = 1; });
