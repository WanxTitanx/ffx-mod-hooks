import struct
import unittest
from repair_dragging_strike import repair
from repair_mana_storm import row_offset


class DraggingStrikeRepair(unittest.TestCase):
    def fixture(self):
        data = bytearray((i * 31) % 256 for i in range(20 + 310 * 92 + 64))
        struct.pack_into('<H', data, 0, 1)
        struct.pack_into('<HHHHI', data, 8, 0, 309, 92, 310 * 92, 20)
        start = row_offset(data, 309)
        struct.pack_into('<hh', data, start + 16, 805, 805)
        data[start + 21] = 3
        return data

    def test_own_cast_survives_and_only_impact_and_caster_change(self):
        before = self.fixture()
        snapshot = bytes(before)
        after = repair(before)
        start = row_offset(before, 309)
        self.assertEqual((805, 876), struct.unpack_from('<hh', after, start + 16))
        self.assertEqual(23, after[start + 21])
        self.assertEqual([start + 18, start + 21],
            [i for i, (a, b) in enumerate(zip(before, after)) if a != b])
        self.assertEqual(snapshot, before)
        self.assertEqual(after, repair(after))
        self.assertEqual(len(before), len(after))

    def test_unreviewed_and_incomplete_inputs_fail_closed(self):
        for offset in (16, 18, 21):
            data = self.fixture()
            data[row_offset(data, 309) + offset] ^= 1
            with self.assertRaises(ValueError):
                repair(data)
        with self.assertRaises(ValueError):
            repair(self.fixture()[:20 + 309 * 92])


if __name__ == '__main__':
    unittest.main()
