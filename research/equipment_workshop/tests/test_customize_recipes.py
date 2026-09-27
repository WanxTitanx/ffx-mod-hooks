from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from generate_customize_recipes import MISSING_NATIVE, parse, render


def fixture():
    rows = [(1, 0x8000 + i, 0x2000 + i % 112, i % 99 + 1)
            for i in range(131) if i not in MISSING_NATIVE]
    header = bytearray(20)
    struct.pack_into('<I', header, 0, 1)
    struct.pack_into('<HHHI', header, 10, len(rows) - 1, 8, len(rows) * 8, 20)
    return bytes(header) + b''.join(struct.pack('<4H', *row) for row in rows)


class RecipeParserTests(unittest.TestCase):
    def test_bounded_native_table(self):
        rows = parse(fixture())
        self.assertEqual(len(rows), 125)
        self.assertEqual(set(range(131)) - rows.keys(), MISSING_NATIVE)

    def test_generated_header_marks_missing_recipes_as_mod(self):
        generated = render(fixture())
        self.assertEqual(generated.count('MOD ONLY'), 6)
        self.assertIn('CustomizeSourceSha256', generated)
        self.assertEqual(render(fixture()), generated)

    def test_truncation_or_extra_bytes_rejected(self):
        for data in (fixture()[:19], fixture()[:-1], fixture() + b'\0'):
            with self.assertRaises(ValueError): parse(data)

    def test_wrong_header_or_stride_rejected(self):
        for at, value in ((0, 2), (12, 9), (16, 21)):
            data = bytearray(fixture()); data[at] = value
            with self.assertRaises(ValueError): parse(data)

    def test_duplicate_ability_rejected(self):
        data = bytearray(fixture()); data[30:32] = data[22:24]
        with self.assertRaises(ValueError): parse(data)

    def test_outside_material_catalog_rejected(self):
        data = bytearray(fixture()); struct.pack_into('<H', data, 24, 0x2070)
        with self.assertRaises(ValueError): parse(data)

    def test_zero_quantity_rejected(self):
        data = bytearray(fixture()); struct.pack_into('<H', data, 26, 0)
        with self.assertRaises(ValueError): parse(data)

    def test_newly_customizable_ability_requires_review(self):
        data = bytearray(fixture()); struct.pack_into('<H', data, 22, 0x8000 + 20)
        with self.assertRaises(ValueError): parse(data)


if __name__ == '__main__': unittest.main()
