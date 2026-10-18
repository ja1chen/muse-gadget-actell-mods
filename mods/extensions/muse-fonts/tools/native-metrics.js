// SPDX-License-Identifier: Apache-2.0
'use strict';

// Match the original unscii-16's 17px line pitch. Keep ordinary glyph rasters
// intact; fit only taller glyphs, without clipping or dropping code points.
function nativeMetrics(font) {
  let resized = 0, shifted = 0;
  for (const glyph of font.glyphs) {
    const box = glyph.bbox;
    if (box.height > 17) {
      const oldHeight = box.height, oldWidth = box.width;
      const width = Math.max(1, Math.round(oldWidth * 17 / oldHeight));
      const pixels = Array.from({length: 17}, (_, y) => Array.from({length: width}, (_, x) => {
        const x0 = x * oldWidth / width, x1 = (x + 1) * oldWidth / width;
        const y0 = y * oldHeight / 17, y1 = (y + 1) * oldHeight / 17;
        let sum = 0;
        for (let sy = Math.floor(y0); sy < Math.ceil(y1); sy++) {
          for (let sx = Math.floor(x0); sx < Math.ceil(x1); sx++) {
            const area = (Math.min(x1, sx + 1) - Math.max(x0, sx)) *
                         (Math.min(y1, sy + 1) - Math.max(y0, sy));
            sum += glyph.pixels[sy][sx] * area;
          }
        }
        return Math.round(sum / ((x1 - x0) * (y1 - y0)));
      }));
      // Keep the original horizontal advance and center the fitted bitmap.
      box.x += Math.floor((oldWidth - width) / 2);
      box.width = width; box.height = 17; glyph.pixels = pixels;
      resized++;
    }
    const y = Math.max(-3, Math.min(14 - box.height, box.y));
    if (y !== box.y) shifted++;
    box.y = y;
  }
  font.ascent = 14; font.descent = -3;
  return {line_height: 17, baseline: 3, resized_glyphs: resized, shifted_glyphs: shifted,
          policy: '16px em; 17px line; area-fit tall glyphs; retain advances and coverage'};
}
module.exports = nativeMetrics;
