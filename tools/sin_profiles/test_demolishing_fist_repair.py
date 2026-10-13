import struct
import unittest
from repair_demolishing_fist import repair
from repair_mana_storm import row_offset


class DemolishingFistRepair(unittest.TestCase):
    def fixture(self):
        data = bytearray((i * 29) % 256 for i in range(20 + 319 * 92 + 64))
        struct.pack_into('<H', data, 0, 1)
        struct.pack_into('<HHHHI', data, 8, 0, 318, 92, 319 * 92, 20)
        start = row_offset(data, 318)
        struct.pack_into('<hh', data, start + 16, 814, 814)
        data[start + 21] = 3
        return data

    def test_only_caster_changes_and_own_visual_is_preserved(self):
        before = self.fixture()
        snapshot = bytes(before)
        after = repair(before)
        start = row_offset(before, 318)
        self.assertEqual([start + 21],
            [i for i, (a, b) in enumerate(zip(before, after)) if a != b])
        self.assertEqual(10, after[start + 21])
        self.assertEqual((814, 814), struct.unpack_from('<hh', after, start + 16))
        self.assertEqual(snapshot, before)
        self.assertEqual(after, repair(after))
        self.assertEqual(len(before), len(after))

    def test_unreviewed_contract_and_truncation_fail_closed(self):
        for offset in (16, 18, 21):
            data = self.fixture()
            data[row_offset(data, 318) + offset] ^= 1
            with self.assertRaises(ValueError):
                repair(data)
        with self.assertRaises(ValueError):
            repair(self.fixture()[:20 + 318 * 92])


if __name__ == '__main__':
    unittest.main()
