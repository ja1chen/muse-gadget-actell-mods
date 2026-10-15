"""Adversarial bounds and compatibility checks for shared resource packages."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pack


class ResourcePackageTests(unittest.TestCase):
    def setUp(self):
        self.image = pack.create([('caption.font', 'test.font', b'synthetic-font'),
                                  ('image.logo', 'image.png', b'synthetic-image')], 'synthetic')

    def rewrite_index(self, change):
        fields = list(pack.HEADER.unpack_from(self.image))
        index = json.loads(self.image[pack.HEADER.size:pack.HEADER.size + fields[2]])
        change(index)
        encoded = pack.canonical(index)
        fields[2] = len(encoded)
        fields[5] = hashlib.sha256(encoded).digest()
        image = bytearray(self.image)
        image[:pack.HEADER.size] = pack.HEADER.pack(*fields)
        image[pack.HEADER.size:pack.HEADER.size + len(encoded)] = encoded
        return image

    def test_shared_assets_and_determinism(self):
        directory = pack.verify(self.image, {'caption.font': 'test.font', 'image.logo': 'image.png'})
        self.assertEqual(len(directory['entries']), 2)
        self.assertEqual(self.image, pack.create([('image.logo', 'image.png', b'synthetic-image'),
                                                ('caption.font', 'test.font', b'synthetic-font')], 'synthetic'))

    def test_payload_corruption_and_truncation(self):
        corrupt = bytearray(self.image)
        corrupt[pack.ALIGNMENT] ^= 1
        for image in [corrupt, self.image[:-1], self.image + b'\0', self.image[:32]]:
            with self.subTest(size=len(image)), self.assertRaises(ValueError):
                pack.verify(image)

    def test_directory_corruption(self):
        image = bytearray(self.image)
        image[pack.HEADER.size] ^= 1
        with self.assertRaises(ValueError):
            pack.verify(image)

    def test_bounds_and_overlap_even_with_valid_directory_hash(self):
        for key, value in [('offset', 0), ('offset', 65537), ('offset', 2**32),
                           ('offset', -65536), ('length', 2**32), ('length', -1),
                           ('offset', True)]:
            image = self.rewrite_index(lambda d: d['entries'][0].update({key: value}))
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                pack.verify(image)
        image = self.rewrite_index(lambda d: d['entries'][1].update(offset=pack.ALIGNMENT))
        with self.assertRaises(ValueError):
            pack.verify(image)

    def test_rollback_contract(self):
        for required in [{'caption.font': 'new.font.abi'}, {'animation.avatar': 'animation.v1'}]:
            with self.assertRaises(ValueError):
                pack.verify(self.image, required)
        image = self.rewrite_index(lambda d: d.update(reader_abi=2))
        with self.assertRaises(ValueError):
            pack.verify(image)

    def test_unknown_header_and_duplicates(self):
        for offset, value in [(8, 2), (12, pack.MAX_INDEX + 1), (16, 129)]:
            image = bytearray(self.image)
            struct.pack_into('<I', image, offset, value)
            with self.assertRaises(ValueError):
                pack.verify(image)
        image = self.rewrite_index(lambda d: d['entries'][1].update(id='caption.font'))
        with self.assertRaises(ValueError):
            pack.verify(image)
        with self.assertRaises(ValueError):
            pack.create([('duplicate', 'test', b'a'), ('duplicate', 'test', b'b')], 'synthetic')

    def test_font_table_and_glyph_offset_bounds(self):
        # Synthetic table headers only; this tests structure, not glyph decoding.
        head = bytearray(48)
        struct.pack_into('<I4sIHH', head, 0, 48, b'head', 1, 3, 24)
        head[34], head[35], head[37] = 1, 1, 4
        cmap = struct.pack('<I4s', 8, b'cmap')
        loca = struct.pack('<I4sIII', 20, b'loca', 2, 8, 9)
        glyf = struct.pack('<I4sI', 12, b'glyf', 0)
        binary = bytes(head) + cmap + loca + glyf
        self.assertEqual(pack.verify_binary_font(binary), {'glyphs': 1, 'size': 24, 'bpp': 4})
        for offset, value in [(0, 0xFFFFFFFC), (48 + 8 + 12, 999), (8, 2)]:
            corrupt = bytearray(binary)
            struct.pack_into('<I', corrupt, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                pack.verify_binary_font(corrupt)


if __name__ == '__main__':
    unittest.main()
