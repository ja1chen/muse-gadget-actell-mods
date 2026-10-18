"""Synthetic checks for the native-size line policy; no font download required."""
from pathlib import Path
import subprocess
import unittest


class NativeMetricsTests(unittest.TestCase):
    def test_fit_preserves_normal_glyphs_coverage_and_ink(self):
        script = r'''
const assert = require('assert');
const fit = require('./native-metrics');
const glyph = (code, w, h, y, value=255) => ({code, advanceWidth:16,
  bbox:{x:0,y,width:w,height:h},pixels:Array.from({length:h},()=>Array(w).fill(value))});
const normal = glyph(0x4f60,16,16,-2);
const tall = glyph(0x3031,12,31,-9);
const descender = glyph(103,9,13,-4);
const original = JSON.stringify(normal);
const font = {glyphs:[normal,tall,descender],ascent:22,descent:-9};
const report = fit(font);
assert.equal(JSON.stringify(normal), original);
assert.equal(font.ascent-font.descent,17);
assert.equal(report.resized_glyphs,1);
assert.equal(font.glyphs.length,3);
assert.equal(tall.advanceWidth,16);
assert.equal(tall.bbox.height,17);
assert(tall.pixels.flat().every(x=>x===255));
assert.equal(descender.bbox.y,-3);
for(const g of font.glyphs){
 assert(g.bbox.y>=-3 && g.bbox.y+g.bbox.height<=14);
 assert.equal(g.pixels.length,g.bbox.height);
 assert(g.pixels.every(row=>row.length===g.bbox.width));
}
const gradient = glyph(0x100,8,34,-10);
gradient.pixels=Array.from({length:34},(_,y)=>Array(8).fill(y*6));
fit({glyphs:[gradient]});
assert.deepEqual(gradient.pixels.map(row=>row[0]),Array.from({length:17},(_,y)=>y*12+3));
'''
        subprocess.run(['node', '-e', script], cwd=Path(__file__).parent, check=True)
