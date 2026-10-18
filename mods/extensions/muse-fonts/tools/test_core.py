"""Small synthetic binary profile: exercise the actual C parser/decoder without font downloads."""
import ctypes as C
import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('font_host_types', HERE / 'run-font-host.py')
types = importlib.util.module_from_spec(spec)
spec.loader.exec_module(types)


def synthetic():
    head = bytearray(48)
    struct.pack_into('<I4sIHH', head, 0, 48, b'head', 1, 3, 24)
    struct.pack_into('<H', head, 16, 3)
    struct.pack_into('<H', head, 30, 2)
    head[34], head[35], head[37], head[38], head[39] = 1, 1, 4, 4, 4
    cmap = struct.pack('<I4sIIIHHHBB', 28, b'cmap', 1, 0, 63, 1, 1, 0, 2, 0)
    loca = struct.pack('<I4sIII', 20, b'loca', 2, 8, 8)
    return bytes(head) + cmap + loca + struct.pack('<I4s', 12, b'glyf') + bytes([0, 0x22, 0x05, 0xaf])


class CoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        output = Path(cls.temp.name) / 'core.dylib'
        extension = HERE.parent
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                        '-I' + str(extension / 'include'), str(extension / 'src/actell_font_core.c'),
                        str(extension / 'src/actell_caption_layout.c'), '-o', str(output)], check=True)
        cls.lib = C.CDLL(str(output))
        cls.lib.actell_font_open.argtypes = [C.POINTER(types.Font), C.c_void_p, C.c_size_t]
        cls.lib.actell_font_open.restype = C.c_bool
        cls.lib.actell_font_glyph.argtypes = [C.POINTER(types.Font), C.c_uint32, C.POINTER(types.Glyph)]
        cls.lib.actell_font_glyph.restype = C.c_bool
        cls.lib.actell_font_decode.argtypes = [C.POINTER(types.Font), C.POINTER(types.Glyph), C.c_void_p, C.c_size_t, C.c_size_t]
        cls.lib.actell_font_decode.restype = C.c_bool
        cls.lib.actell_font_normalize.argtypes = [C.POINTER(types.Font), C.c_char_p, C.c_void_p, C.c_size_t]
        cls.lib.actell_font_normalize.restype = C.c_size_t
        cls.lib.actell_font_page.argtypes = [C.POINTER(types.Font), C.c_char_p, C.c_size_t, C.c_void_p, C.c_size_t, C.c_int, C.c_int]
        cls.lib.actell_font_page.restype = C.c_bool

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.bytes = C.create_string_buffer(synthetic())
        self.font, self.glyph = types.Font(), types.Glyph()
        self.assertTrue(self.lib.actell_font_open(C.byref(self.font), self.bytes, len(self.bytes) - 1))

    def test_real_pixels_and_capacity(self):
        self.assertTrue(self.lib.actell_font_glyph(C.byref(self.font), 63, C.byref(self.glyph)))
        out = C.create_string_buffer(4)
        self.assertTrue(self.lib.actell_font_decode(C.byref(self.font), C.byref(self.glyph), out, 4, 2))
        self.assertEqual(out.raw, bytes([0, 85, 170, 255]))
        self.assertFalse(self.lib.actell_font_decode(C.byref(self.font), C.byref(self.glyph), out, 3, 2))
        self.assertFalse(self.lib.actell_font_glyph(C.byref(self.font), 0x20000, C.byref(self.glyph)))

    def test_utf8_and_missing_glyph_normalization(self):
        for text, expected in [(b'?', b'?'), ('😀中文'.encode(), b'???'), (b'\xe4\xb8', b'??'),
                               (b'\xed\xa0\x80', b'???'), (b'\xc0\x80', b'??'), (b'\n', b'\n')]:
            out = C.create_string_buffer(20)
            self.lib.actell_font_normalize(C.byref(self.font), text, out, 20)
            self.assertEqual(out.value, expected)
        out = C.create_string_buffer(2)
        self.lib.actell_font_normalize(C.byref(self.font), '中文'.encode(), out, 2)
        self.assertEqual(out.value, b'?')

    def test_pages_and_overlap(self):
        out = C.create_string_buffer(40)
        self.assertTrue(self.lib.actell_font_page(C.byref(self.font), b'??????', 0, out, 40, 4, 2))
        self.assertEqual(out.value, b'??\n??')
        self.assertTrue(self.lib.actell_font_page(C.byref(self.font), b'??????', 4, out, 40, 4, 2))
        self.assertEqual(out.value, b'??')
        self.assertTrue(self.lib.actell_font_page(C.byref(self.font), b'?\n\n?', 0, out, 40, 4, 3))
        self.assertEqual(out.value, b'?\n\n?')
        self.assertTrue(self.lib.actell_font_page(C.byref(self.font), b'\n\n?', 0, out, 40, 4, 3))
        self.assertEqual(out.value, b'\n\n?')

    def test_truncation_and_header_corruption(self):
        for size in range(len(self.bytes) - 1):
            self.assertFalse(self.lib.actell_font_open(C.byref(self.font), self.bytes, size))
        raw = bytearray(synthetic())
        raw[34] = 255
        self.assertFalse(self.lib.actell_font_open(C.byref(self.font), C.create_string_buffer(bytes(raw)), len(raw)))


if __name__ == '__main__':
    unittest.main()
