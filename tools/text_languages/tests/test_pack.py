"""End-to-end contracts use private fixtures only when explicitly supplied."""
import importlib.util
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from asset_io import AssetError


class AuthoringTests(unittest.TestCase):
    def test_producer_exists(self):
        self.assertIsNotNone(importlib.util.find_spec('pack'), 'Pack authoring module must exist')

    def test_append_preserves_aliases_and_headers(self):
        if importlib.util.find_spec('pack') is None:
            self.fail('Pack authoring is not implemented')
        from pack import append_kernel, encode
        original = bytearray(37)
        struct.pack_into('<HHHH', original, 8, 0, 0, 16, 16)
        struct.pack_into('<H', original, 20, 1)
        struct.pack_into('<H', original, 24, 1)
        original += encode('Formation') + b'\0'
        result = append_kernel(bytes(original), [{'row': 0, 'slot': 0, 'text': 'Formação'}])
        self.assertEqual(result[:20], original[:20])
        self.assertEqual(result[22:len(original)], original[22:])
        self.assertEqual(struct.unpack_from('<H', result, 24)[0], 1)
        self.assertTrue(result.endswith(encode('Formação') + b'\0'))

    def test_encoding_is_lossless_and_bounded(self):
        if importlib.util.find_spec('pack') is None:
            self.fail('Pack authoring is not implemented')
        from pack import encode
        self.assertEqual(encode('ãõÃÕ'), bytes((242, 243, 244, 245)))
        self.assertEqual(encode('a\n{TIDUS}'), bytes((112, 3, 19, 48)))
        for text in ('bad\x00', '😀', '{UNKNOWN}', 'a' * 2049):
            with self.subTest(text=text[:20]), self.assertRaises(AssetError):
                encode(text)


if __name__ == '__main__':
    unittest.main()
