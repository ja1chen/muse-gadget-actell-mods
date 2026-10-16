#!/usr/bin/env python3
"""Install isolated custom avatar with verified reversible SDK edits.
Never changes sdkconfig or unrelated patches. No build, flash or network.
"""
from pathlib import Path
import argparse, json, hashlib
ROOT=Path(__file__).resolve().parents[1]
def digest(b): return hashlib.sha256(b).hexdigest()
def edit_once(s,a,b):
    if s.count(a)!=1: raise ValueError('Unsupported SDK anchor: '+a[:80])
    return s.replace(a,b)
def main():
    p=argparse.ArgumentParser();p.add_argument('--sdk-root',type=Path,required=True);p.add_argument('--rollback',action='store_true');a=p.parse_args()
    c=a.sdk_root/'esp32/components/muse';target=c/'avatar';record=c/'.actell-avatar-v2-stage.json'
    if a.rollback:
        if not record.exists():p.error('No staged installation record')
        data=json.loads(record.read_text())
        for f,v in data['edits'].items():
            if digest((c/f).read_bytes())!=v['after']:p.error('Changed since staging; refusing rollback: '+f)
        for f,h in data['files'].items():
            if not (target/f).exists() or digest((target/f).read_bytes())!=h:p.error('Custom avatar changed; refusing rollback')
        for f,v in data['edits'].items():(c/f).write_text(v['before'])
        for f in data['files']:(target/f).unlink()
        record.unlink();print('Rolled back only verified avatar installation.');return
    if record.exists():
        data=json.loads(record.read_text())
        if all(digest((c/f).read_bytes())==v['after'] for f,v in data['edits'].items()) and all((target/f).is_file() and digest((target/f).read_bytes())==h for f,h in data['files'].items()):
            print('Already staged; no changes.');return
        p.error('Existing stage differs; inspect before updating')
    if (target/'muse_pixel.c').exists():p.error('Existing custom avatar: preserve/move it explicitly before staging')
    h=(c/'muse_pixel.h').read_text();ui=(c/'muse_ui.c').read_text();kc=(c/'Kconfig.projbuild').read_text() if (c/'Kconfig.projbuild').exists() else None
    # Registration belongs to component Kconfig (name varies between snapshots).
    kname='Kconfig.projbuild' if kc is not None else 'Kconfig'
    if kc is None:kc=(c/kname).read_text()
    before={'muse_pixel.h':h,'muse_ui.c':ui,kname:kc}
    h=edit_once(h,'#define MUSE_PX_W 64\n#define MUSE_PX_H 64','''#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
/* ACTELL_AVATAR_V2: source grid and UI layout are intentionally independent. */
#define MUSE_LAYOUT_PX 64
#if defined(CONFIG_MUSE_AVATAR_V2) && CONFIG_MUSE_AVATAR_V2
#define MUSE_PX_W 160
#define MUSE_PX_H 160
#else
#define MUSE_PX_W 64
#define MUSE_PX_H 64
#endif''')
    # These uses are layout units; dirty detection continues using native W/H.
    layout_lines = [
        '    int cell = s_canvas_px / MUSE_PX_W - 1;',
        '    l->px = MUSE_PX_W * cell;',
        '    l->px = MUSE_PX_W * MINI_CELL_PX;',
        '    s_big_y = s_small ? 0 : art_bottom - (s_canvas_px / 2 - ART_BLANK_ROWS * (s_canvas_px / MUSE_PX_W));',
        '    s_canvas_px = s_small ? s_h * 3 / 4 : MUSE_PX_W * 5;',
        '        s_canvas_px = s_w / MUSE_PX_W * MUSE_PX_W;',
        '        s_canvas_px = (MUSE_PX_W * 5 + 2 * s_dy) / MUSE_PX_W * MUSE_PX_W;',
    ]
    for line in layout_lines:ui=edit_once(ui,line,line.replace('MUSE_PX_W','MUSE_LAYOUT_PX'))
    ui=edit_once(ui,'    lv_area_t r[MUSE_PX_H];   /* in cells */','''#if defined(CONFIG_MUSE_AVATAR_V2) && CONFIG_MUSE_AVATAR_V2
    static lv_area_t r[MUSE_PX_H];   /* UI thread only; keep native-grid scratch off task stack. */
#else
    lv_area_t r[MUSE_PX_H];   /* legacy stack layout */
#endif''')
    ui=edit_once(ui,'    if (!s_cells) {','    if (!s_cells || (int)s_muse_src.header.w < MUSE_PX_W) {')
    kc+='''\n# ACTELL_AVATAR_V2_BEGIN
config MUSE_AVATAR_V2
    bool "Actell high-resolution avatar (160 px; off uses original 64 px)"
    depends on MUSE_ENABLED
    default y
    help
        Selects the staged Actell v13 renderer. Reconfigure/rebuild after changing.
        Disable to compile only the legacy Actell renderer. Remove the stage to
        restore the SDK official/custom avatar selection behavior.
# ACTELL_AVATAR_V2_END
'''
    after={'muse_pixel.h':h,'muse_ui.c':ui,kname:kc}
    # SDK custom-avatar CMake source is a single TU. Keep original source files
    # independent, using a tiny dispatcher without editing SDK CMake.
    files={'muse_pixel.c':(ROOT/'src/muse_pixel_dispatch.c').read_bytes().replace(b'"muse_pixel.c"',b'"muse_pixel_legacy.c"'),
           'muse_pixel_legacy.c':(ROOT/'src/muse_pixel.c').read_bytes(),
           'muse_pixel_160.c':(ROOT/'src/muse_pixel_160.c').read_bytes(),
           'actell_avatar.c':(ROOT/'src/actell_avatar.c').read_bytes(),
           'actell_avatar.h':(ROOT/'include/actell_avatar.h').read_bytes()}
    for f in files:
        if (target/f).exists():p.error('Would overwrite '+str(target/f))
    data={'edits':{f:{'before':before[f],'after':digest(s.encode())} for f,s in after.items()},'files':{f:digest(b) for f,b in files.items()}}
    # Recovery journal is persisted before any mutation. A partial apply is
    # intentionally refused on the next run; originals remain in this journal.
    record.write_text(json.dumps(data,indent=2))
    target.mkdir(exist_ok=True)
    for f,b in files.items():(target/f).write_bytes(b)
    for f,s in after.items():(c/f).write_text(s)
    record.write_text(json.dumps(data,indent=2));print('Staged v13; CONFIG_MUSE_AVATAR_V2 defaults y. No build or flash.')
if __name__=='__main__':main()
