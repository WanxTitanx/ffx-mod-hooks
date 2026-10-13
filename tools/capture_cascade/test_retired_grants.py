import json
import struct
import unittest
from tools.capture_cascade import author

POLICY = {'remove': [0x800D, 0x8011], 'merge': {'32792': 0x8017}}


def fixture():
    data = bytearray((i * 17) % 256 for i in range(1024))
    struct.pack_into('<9I', data, 0, 8, 0x34, 0x50, 0x80, 0x110, 0x200, 0x320, 0x340, len(data))
    data[0x20B] = 255
    for group in range(14):
        struct.pack_into('<8H', data, 0x232 + group * 16, 0x800D, 0x8011, 0x8018, 0x8017, 0x800C, 0x8019, 0x807A, 0)
    return bytes(data)


def clean(data, policy):
    return getattr(author, 'sanitize_grants', lambda source, _: source)(data, policy)


class RetiredGrantTests(unittest.TestCase):
    def test_profile_cleanup_changes_only_grants(self):
        before = fixture(); after = clean(before, POLICY)
        for group in range(14):
            self.assertEqual(struct.unpack_from('<8H', after, 0x232 + group * 16),
                             (0, 0, 0, 0x8017, 0x800C, 0x8019, 0x807A, 0))
        allowed = {0x232 + group * 16 + offset for group in range(14) for offset in range(6)}
        self.assertTrue(all(a == b or i in allowed for i, (a, b) in enumerate(zip(before, after))))
        self.assertEqual(after[0x20B], 255)
        self.assertEqual(clean(after, POLICY), after)

    def test_merge_does_not_duplicate_and_preserves_capture(self):
        before = bytearray(fixture())
        struct.pack_into('<8H', before, 0x232, 0x8018, 0x8018, 0x800C, 0x8019, 0x807A, 0, 0, 0)
        self.assertEqual(struct.unpack_from('<8H', clean(bytes(before), POLICY), 0x232),
                         (0x8017, 0, 0x800C, 0x8019, 0x807A, 0, 0, 0))

    def test_vanilla_and_other_profile_do_not_inherit_spira_list(self):
        data = fixture()
        self.assertEqual(clean(data, None), data)
        other = clean(data, {'remove': [0x8019], 'merge': {}})
        self.assertEqual(struct.unpack_from('<8H', other, 0x232),
                         (0x800D, 0x8011, 0x8018, 0x8017, 0x800C, 0, 0x807A, 0))

    def test_bounds_rejected(self):
        data = bytearray(fixture());struct.pack_into('<I', data, 24, 0x210)
        with self.assertRaises(ValueError):clean(bytes(data), POLICY)

    def test_author_has_explicit_spira_policy(self):
        from pathlib import Path
        catalog = json.loads(Path(author.__file__).with_name('catalog.json').read_text())
        self.assertEqual(catalog.get('equipmentGrants'), POLICY)

    def test_free_retired_recipe_is_still_a_conflict(self):
        table = bytearray(28)
        struct.pack_into('<I', table, 0, 1)
        struct.pack_into('<HHHI', table, 10, 0, 8, 8, 20)
        struct.pack_into('<4H', table, 20, 1, 0x800D, 0x2000, 0)
        with self.assertRaises(ValueError):
            author.verify_grants(bytes(table), {'remove': [0x800D], 'merge': {}})


if __name__ == '__main__':
    unittest.main()
