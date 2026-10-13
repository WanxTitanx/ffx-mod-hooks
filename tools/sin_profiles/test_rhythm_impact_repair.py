"""Exact private asset repairs preserve authored gameplay and unrelated edits."""
import struct
import unittest
from repair_rhythm_impact import repair, contract
from repair_mana_storm import row_offset


class RhythmImpactRepair(unittest.TestCase):
    def fixture(self):
        data = bytearray((i * 17 + 31) % 256 for i in range(20 + 316 * 92 + 64))
        struct.pack_into('<H', data, 0, 1)
        struct.pack_into('<HHHHI', data, 8, 0, 315, 92, 316 * 92, 20)
        start = row_offset(data, 315)
        struct.pack_into('<hh', data, start + 16, 811, 811)
        data[start + 21] = 3
        return data

    def test_changes_only_the_reviewed_visual_contract(self):
        before = self.fixture()
        snapshot = bytes(before)
        after = repair(before)
        start = row_offset(before, 315)
        self.assertEqual((811, 811, 1), contract(after, start))
        self.assertEqual(snapshot, before)
        self.assertEqual(after, repair(after))
        changed = [i for i, (a, b) in enumerate(zip(before, after)) if a != b]
        self.assertEqual([start + 21], changed)
        self.assertEqual(len(before), len(after))

    def test_restores_own_visual_from_the_previous_physical_fallback(self):
        before = self.fixture()
        start = row_offset(before, 315)
        struct.pack_into('<hh', before, start + 16, 0, 0)
        before[start + 21] = 1
        after = repair(before)
        self.assertEqual((811, 811, 1), contract(after, start))
        self.assertEqual([start + 16, start + 17, start + 18, start + 19],
            [i for i, (a, b) in enumerate(zip(before, after)) if a != b])

    def test_rejects_partial_or_unreviewed_visual_edits(self):
        for offset in (16, 18, 21):
            data = self.fixture()
            data[row_offset(data, 315) + offset] ^= 1
            with self.assertRaises(ValueError):
                repair(data)

    def test_rejects_missing_or_truncated_row(self):
        for size in (0, 19, 20, 20 + 315 * 92):
            with self.assertRaises(ValueError):
                repair(self.fixture()[:size])
        data = self.fixture()
        struct.pack_into('<H', data, 10, 314)
        with self.assertRaises(ValueError):
            repair(data)


if __name__ == '__main__':
    unittest.main()
