import struct
import unittest
from repair_silica_shards import repair
from repair_mana_storm import row_offset


class SilicaShardsRepair(unittest.TestCase):
    def fixture(self):
        data = bytearray((i * 23) % 256 for i in range(20 + 304 * 92 + 64))
        struct.pack_into('<H', data, 0, 1)
        struct.pack_into('<HHHHI', data, 8, 0, 303, 92, 304 * 92, 20)
        start = row_offset(data, 303)
        data[start + 0x46], data[start + 0x53] = 100, 5
        return data

    def test_only_unintended_slow_changes(self):
        before = self.fixture()
        snapshot = bytes(before)
        after = repair(before)
        start = row_offset(before, 303)
        self.assertEqual([start + 0x46, start + 0x53],
            [i for i, (a, b) in enumerate(zip(before, after)) if a != b])
        self.assertEqual(snapshot, before)
        self.assertEqual(after, repair(after))
        self.assertEqual(len(before), len(after))

    def test_unreviewed_fields_and_truncation_fail_closed(self):
        for field in (0x46, 0x53):
            data = self.fixture()
            data[row_offset(data, 303) + field] ^= 1
            with self.assertRaises(ValueError):
                repair(data)
        with self.assertRaises(ValueError):
            repair(self.fixture()[:20 + 303 * 92])


if __name__ == '__main__':
    unittest.main()
