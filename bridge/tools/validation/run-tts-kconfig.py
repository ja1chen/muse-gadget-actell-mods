#!/usr/bin/env python3
"""Check TTS transport defaults with ESP-IDF's kconfiglib (synthetic values only)."""
from pathlib import Path
import tempfile
try:
    import kconfiglib
except ImportError:
    raise SystemExit("Run this check with the activated ESP-IDF Python environment.") from None

ROOT = Path(__file__).resolve().parents[3]
TTS = ROOT / 'bridge/extensions/doubao-tts/components/doubao_tts/Kconfig'
PLATFORM = '''config MUSE_HATCH
    bool
    default y
config MUSE_ENABLED
    bool
    default y
'''
TRANSPORT = '''config WS_TRANSPORT
    bool "Websocket transport"
    default y
config WS_BUFFER_SIZE
    int "Upgrade buffer"
    default 1024
    depends on WS_TRANSPORT
config WS_DYNAMIC_BUFFER
    bool "Dynamic upgrade buffer"
    default n
    depends on WS_TRANSPORT
'''


def main():
    tmp = ROOT / 'local/build/tmp'
    tmp.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=tmp) as folder:
        path = Path(folder) / 'Kconfig'
        for transport_first in [False, True]:
            include = f'source "{TTS}"\n'
            path.write_text(PLATFORM + (TRANSPORT + include if transport_first else include + TRANSPORT))
            for key, bidi in [('', False), ('SYNTHETIC_KEY', False), ('SYNTHETIC_KEY', True)]:
                k = kconfiglib.Kconfig(str(path), warn_to_stderr=False)
                k.syms['DOUBAO_TTS_API_KEY'].set_value(key)
                if not bidi:
                    k.syms['DOUBAO_TTS_BIDIRECTIONAL'].set_value('n')
                assert k.syms['DOUBAO_TTS'].str_value == ('y' if key else 'n')
                assert k.syms['DOUBAO_TTS_BIDIRECTIONAL'].str_value == ('y' if bidi else 'n')
                assert k.syms['WS_DYNAMIC_BUFFER'].str_value == ('y' if bidi else 'n')
                assert k.syms['WS_BUFFER_SIZE'].str_value == ('4096' if bidi else '1024')
                if bidi:
                    # Existing sdkconfig values below the bound must not leave
                    # the old too-small buffer active after upgrading firmware.
                    k.syms['WS_BUFFER_SIZE'].set_value('1024')
                    assert k.syms['WS_BUFFER_SIZE'].str_value == '4096'
                    k.syms['WS_BUFFER_SIZE'].set_value('8192')
                    assert k.syms['WS_BUFFER_SIZE'].str_value == '8192'
    print('TTS Kconfig: default bidirectional, key gating, cached buffer migration, dynamic release and HTTP defaults passed.')


if __name__ == '__main__':
    main()
