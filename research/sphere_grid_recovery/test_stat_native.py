"""Native A54860 sums, distinct from the downstream player-stat application."""
import importlib.util
import os
from pathlib import Path
import unittest
from pe_image import read_exact

class StatNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = read_exact(Path(os.environ['FFX_SPHERE_EXE']))

    def fixture(self, count):
        self.assertIsNotNone(importlib.util.find_spec('stat_fixture'),
                             'The native stat consumer fixture is missing')
        from stat_fixture import StatFixture
        return StatFixture(self.image, count)

    def test_appended_hp_node_reaches_native_summary(self):
        f = self.fixture(1024)
        f.activate(860, 0, 35)
        result = f.recompute()
        self.assertEqual(result[0], (6, 0, 0, 0, 0, 0, 0, 0, 0, 0))
        self.assertEqual(result[1:], [(0,)*10]*6)
        self.assertEqual(f.downstream_apply_calls, 1)

    def test_seven_masks_and_idempotent_native_recompute(self):
        f = self.fixture(1024)
        for character in range(7):
            f.activate(860+character, character, 5)
        first = f.recompute()
        self.assertEqual(first, [(0,0,4,0,0,0,0,0,0,0)]*7)
        self.assertEqual(f.recompute(), first)
        self.assertEqual(f.downstream_apply_calls, 2)

    def test_extended_count_visits_nodes_beyond_1024(self):
        for count in (1025, 4096):
            with self.subTest(count=count):
                f = self.fixture(count)
                f.activate(count-1, 6, 35)
                result = f.recompute()
                self.assertEqual(result[:6], [(0,)*10]*6)
                self.assertEqual(result[6][0], 6)
                self.assertEqual(f.node_count, count)

    def test_unknown_panel_or_out_of_range_identity_does_not_mutate(self):
        f = self.fixture(1024)
        before = f.state_bytes()
        for args in ((1024,0,35),(-1,0,35),(860,7,35),(860,0,129)):
            with self.subTest(args=args), self.assertRaises(ValueError):
                f.activate(*args)
            self.assertEqual(f.state_bytes(), before)

if __name__ == '__main__':unittest.main()
