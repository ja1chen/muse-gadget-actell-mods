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

def integrate_mouth(ui, header):
    """Apply only the mouth channel additions to a v2-staged UI/header pair.

    Called by clean staging and the hash-guarded updater so both paths produce
    byte-identical SDK integration. Unknown or repeated anchors are refused.
    Existing level/meter smoothing is deliberately preserved.
    """
    ui=edit_once(ui, '#include "muse_pixel.h"\n',
                 '#include "muse_pixel.h"\n#include "avatar/actell_mouth_envelope.h"\n')
    ui=edit_once(ui, 'esp_err_t muse_ui_start(void)\n{', 'esp_err_t muse_ui_start(void)\n{\n    muse_avatar_aa_init();')
    ui=edit_once(ui, 'static float s_level;\n',
                 'static float s_level;\nstatic actell_mouth_envelope_t s_mouth;\n')
    ui=edit_once(ui, '    float now = (float)esp_timer_get_time() / 1e6f;',
                 '    int64_t mouth_now_us = esp_timer_get_time();\n'
                 '    float now = (float)mouth_now_us / 1e6f;\n'
                 '    muse_mouth_sample_t mouth_sample = muse_state_take_mouth_sample();\n'
                 '    float mouth_level = actell_mouth_step(&s_mouth, &mouth_sample, mouth_now_us);')
    ui=edit_once(ui, '        .happy = muse_state_happiness(),',
                 '        .happy = muse_state_happiness(),\n'
                 '        .mouth_level = mouth_level,\n'
                 '        .mouth_level_valid = true,')
    header=edit_once(header, '    float happy;     /* 0..1 pet reaction */',
                    '    float happy;     /* 0..1 pet reaction */\n'
                    '    float mouth_level; /* 0..1 dedicated fast-release mouth channel */\n'
                    '    bool mouth_level_valid; /* false: renderer falls back to level */')
    header=edit_once(header, 'void muse_pixel_render(const muse_pose_t *pose);', 'void muse_pixel_render(const muse_pose_t *pose);\nvoid muse_avatar_aa_init(void); /* Startup only, before decoder/draw publication. */')
    return ui, header


def integrate_mouth_state(source, header):
    """Add an independent, spinlock-protected observer; keep global level intact."""
    source=edit_once(source, '#include <stdarg.h>\n', '#include <math.h>\n#include <stdarg.h>\n')
    source=edit_once(source, 'static volatile float s_level;\n',
        'static volatile float s_level;\n'
        'static portMUX_TYPE s_mouth_lock = portMUX_INITIALIZER_UNLOCKED;\n'
        'static muse_mouth_sample_t s_mouth_sample;\n')
    source=edit_once(source, 'void muse_state_set_mode(muse_mode_t mode)\n{\n    if (mode == s_mode) {\n        return;\n    }\n    /* Shutting down wins over the voice pipeline finishing a turn. */\n    if (s_mode == MUSE_MODE_OFF && mode != MUSE_MODE_IDLE) {\n        return;\n    }\n    s_mode_since_us = esp_timer_get_time();\n    s_mode = mode;\n}\n',
        'void muse_state_set_mode(muse_mode_t mode)\n{\n    portENTER_CRITICAL(&s_mouth_lock);\n    if (mode == s_mode) {\n        portEXIT_CRITICAL(&s_mouth_lock);\n        return;\n    }\n    /* Shutting down wins over the voice pipeline finishing a turn. */\n    if (s_mode == MUSE_MODE_OFF && mode != MUSE_MODE_IDLE) {\n        portEXIT_CRITICAL(&s_mouth_lock);\n        return;\n    }\n    s_mouth_sample = (muse_mouth_sample_t){\n        .generation = s_mouth_sample.generation + 1u,\n        .speaking = mode == MUSE_MODE_SPEAKING,\n    };\n    s_mode_since_us = esp_timer_get_time();\n    s_mode = mode;\n    portEXIT_CRITICAL(&s_mouth_lock);\n}\n')
    source=edit_once(source, '    s_level = level < 0 ? 0 : (level > 1 ? 1 : level);',
        '    s_level = level < 0 ? 0 : (level > 1 ? 1 : level);\n'
        '    float observer_level = isfinite(level) ? fminf(fmaxf(level, 0), 1) : 0;\n'
        '    portENTER_CRITICAL(&s_mouth_lock);\n'
        '    if (s_mouth_sample.speaking) {\n'
        '        if (observer_level > s_mouth_sample.peak) {\n'
        '            s_mouth_sample.peak = observer_level;\n'
        '        }\n'
        '        if (observer_level > 0.02f) {\n'
        '            s_mouth_sample.last_voice_us = esp_timer_get_time();\n'
        '            s_mouth_sample.heard_voice = true;\n'
        '        }\n'
        '    }\n'
        '    portEXIT_CRITICAL(&s_mouth_lock);')
    source=edit_once(source, 'float muse_state_level(void)\n',
        'muse_mouth_sample_t muse_state_take_mouth_sample(void)\n'
        '{\n'
        '    portENTER_CRITICAL(&s_mouth_lock);\n'
        '    muse_mouth_sample_t sample = s_mouth_sample;\n'
        '    s_mouth_sample.peak = 0;\n'
        '    portEXIT_CRITICAL(&s_mouth_lock);\n'
        '    return sample;\n'
        '}\n\n'
        'float muse_state_level(void)\n')
    header=edit_once(header, 'float muse_state_level(void);\n',
        'float muse_state_level(void);\n\n'
        '/* Independent mouth observer. Single UI consumer takes and clears peak.\n'
        ' * Last voice timestamp and speaking generation persist until mode change. */\n'
        'typedef struct {\n'
        '    float peak;\n'
        '    int64_t last_voice_us;\n'
        '    uint32_t generation;\n'
        '    bool speaking;\n'
        '    bool heard_voice;\n'
        '} muse_mouth_sample_t;\n'
        'muse_mouth_sample_t muse_state_take_mouth_sample(void);\n')
    return source, header

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
    state_c=(c/'muse_state.c').read_text();state_h=(c/'muse_state.h').read_text()
    before={'muse_pixel.h':h,'muse_ui.c':ui,kname:kc,'muse_state.c':state_c,'muse_state.h':state_h}
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
    ui,h=integrate_mouth(ui,h)
    state_c,state_h=integrate_mouth_state(state_c,state_h)
    after={'muse_pixel.h':h,'muse_ui.c':ui,kname:kc,'muse_state.c':state_c,'muse_state.h':state_h}
    # SDK custom-avatar CMake source is a single TU. Keep original source files
    # independent, using a tiny dispatcher without editing SDK CMake.
    files={'muse_pixel.c':(ROOT/'src/muse_pixel_dispatch.c').read_bytes().replace(b'"muse_pixel.c"',b'"muse_pixel_legacy.c"'),
           'muse_pixel_legacy.c':(ROOT/'src/muse_pixel.c').read_bytes(),
           'muse_pixel_160.c':(ROOT/'src/muse_pixel_160.c').read_bytes(),
           'actell_mouth_envelope.h':(ROOT/'src/actell_mouth_envelope.h').read_bytes(),
           **{f:(ROOT/'src'/f).read_bytes() for f in ['muse_pixel_aa_assets.h','muse_pixel_aa_resource.h','muse_pixel_aa_fallback.inc','muse_aa_partition.c','muse_aa_partition.h','muse_pixel_aa_init.c']},
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
