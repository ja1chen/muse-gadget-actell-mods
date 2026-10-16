#!/usr/bin/env python3
"""Regenerate the thin hooks from the verified fixed SDK archive, without editing SDKs."""
import difflib
import hashlib
from pathlib import Path
import tarfile

ROOT = Path(__file__).resolve().parents[4]
archive = ROOT / 'local/upstream/archives/muse-gadget-sdk-812c46f.tar.gz'
assert hashlib.sha256(archive.read_bytes()).hexdigest() == '0a7548404b4aa25ad0cc3ad0f9fee1ad3a3245a03a14b02293da3a83bde69359'
include = '\n#if CONFIG_ACTELL_CAPTION_FONT\n#include "actell_caption_font.h"\n#endif\n'
changes = {
    'muse_ui.c': [
        ('#include "muse_text.h"\n', '#include "muse_text.h"\n' + include),
        ('static const lv_font_t *caption_font(void)\n{', 'static const lv_font_t *caption_font(void)\n{\n#if CONFIG_ACTELL_CAPTION_FONT\n    if (actell_caption_init()) return actell_caption_font();\n#endif'),
        ('int cap_h = 2 * lv_font_get_line_height(&lv_font_unscii_16)', 'int cap_h = 2 * lv_font_get_line_height(caption_font())'),
        ('    int cap_top = cap_bottom - cap_h;', '''#if CONFIG_ACTELL_CAPTION_FONT
    if (actell_caption_active() && !muse_board->round) {
        /* Use the spare bottom margin for taller glyphs before lifting Muse.
         * Keep the original meter/character position when two lines fit. */
        int old_h = 2 * lv_font_get_line_height(&lv_font_unscii_16) + CAPTION_LINE_SPACE;
        cap_bottom += cap_h - old_h;
        if (cap_bottom > s_h / 2 - 3) cap_bottom = s_h / 2 - 3;
    }
#endif
    int cap_top = cap_bottom - cap_h;'''),
        ('    int h = 3 * pitch - CAPTION_LINE_SPACE;', '''    int heard_lines = 3;
#if CONFIG_ACTELL_CAPTION_FONT
    if (actell_caption_active()) heard_lines = 2;
#endif
    int h = heard_lines * pitch - CAPTION_LINE_SPACE;'''),
        ('set_reply_box(l, CAPTION_W / cw, 3, reply_bottom(CAPTION_W, ring_in) - h, cw, pitch);', 'set_reply_box(l, CAPTION_W / cw, heard_lines, reply_bottom(CAPTION_W, ring_in) - h, cw, pitch);'),
        ('set_reply_box(l, c, 3, top, cw, pitch);', 'set_reply_box(l, c, heard_lines, top, cw, pitch);'),
        ('    s_reply_lbl = make_label(face, font, COLOR_CAPTION);', '    s_reply_lbl = make_label(face, font, COLOR_CAPTION);\n#if CONFIG_ACTELL_CAPTION_FONT\n    if (actell_caption_active()) {\n        /* First page can be requested before the first UI tick. */\n        int initial = muse_settings_speaker_on() ? ANSWER_HEARD : ANSWER_READ;\n        actell_caption_set_page(s_answers[initial].w, s_answers[initial].lines);\n    }\n#endif'),
        ('    lv_label_set_long_mode(s_reply_lbl, LV_LABEL_LONG_MODE_WRAP);', '    lv_label_set_long_mode(s_reply_lbl, LV_LABEL_LONG_MODE_WRAP);\n#if CONFIG_ACTELL_CAPTION_FONT\n    if (actell_caption_active()) lv_label_set_long_mode(s_reply_lbl, LV_LABEL_LONG_MODE_CLIP);\n#endif'),
        ('            s_page_for = layout;', '            s_page_for = layout;\n#if CONFIG_ACTELL_CAPTION_FONT\n            if (actell_caption_active()) actell_caption_set_page(s_answers[layout].w, s_answers[layout].lines);\n#endif'),
    ],
    'muse_state.c': [
        ('#include "muse_text.h"\n', '#include "muse_text.h"\n' + include),
        ('    muse_text_to_ascii(buf, sizeof(buf));', '#if CONFIG_ACTELL_CAPTION_FONT\n    if (actell_caption_active()) actell_caption_normalize(buf, buf, sizeof(buf));\n    else\n#endif\n    muse_text_to_ascii(buf, sizeof(buf));'),
    ],
    'muse_chat_text.c': [
        ('#include "muse_text.h"\n', '#include "muse_text.h"\n' + include),
        ('bool muse_hatch_caption_at(const char *text, size_t at, char *out, size_t cap)\n{', 'bool muse_hatch_caption_at(const char *text, size_t at, char *out, size_t cap)\n{\n#if CONFIG_ACTELL_CAPTION_FONT\n    if (actell_caption_active()) return actell_caption_page(text, at, out, cap);\n#endif'),
    ],
    'CMakeLists.txt': [('list(APPEND muse_priv_requires camera)', 'list(APPEND muse_priv_requires camera)\nlist(APPEND muse_priv_requires actell_fonts)')],
}
patch = '# Base: Muse Gadget SDK 812c46f9fe42b69d916613a9dc702cbac5ff7b54\n'
with tarfile.open(archive) as tar:
    for name, replacements in changes.items():
        path = 'esp32/components/muse/' + name
        before = tar.extractfile(path).read().decode()
        after = before
        for old, new in replacements:
            assert after.count(old) == 1, (name, old)
            after = after.replace(old, new, 1)
        patch += ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True), 'a/' + path, 'b/' + path, n=2))
(ROOT / 'bridge/patches/muse-gadget-sdk/caption-font.patch').write_text(patch)
