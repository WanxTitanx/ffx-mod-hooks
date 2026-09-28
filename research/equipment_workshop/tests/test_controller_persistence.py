"""Jarvis-HOOK: accepted outcomes survive a fresh standalone controller."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'host'))
from bridge import WorkshopError
from server import Workshop
from store import Store, native_gil
import test_server as fixtures


class ControllerPersistenceTests(unittest.TestCase):
    def setUp(self):
        fixtures.ControllerTests.setUp(self)

    def tearDown(self):
        fixtures.ControllerTests.tearDown(self)

    def request(self, op='mode', **extra):
        return fixtures.ControllerTests.request(self, op, **extra)

    def test_accepted_random_outcome_survives_restart(self):
        quote = self.app.preview(self.request('refine'))
        before_raw, _, before, _ = self.store.load()
        self.assertIsNone(quote['after'])
        self.app.confirm({'confirmation': quote['confirmation']})
        accepted = self.store.load()
        self.assertEqual(accepted[2].rolls, before.rolls + 1)
        self.assertLess(native_gil(accepted[0]), native_gil(before_raw))
        self.store = Store(self.store.path, self.core)
        self.app = Workshop(self.store)
        recovered = self.store.load()
        self.assertEqual(recovered[:2], accepted[:2])
        self.assertEqual(bytes(recovered[2]), bytes(accepted[2]))
        with self.assertRaises(WorkshopError):
            self.app.confirm({'confirmation': quote['confirmation']})
        next_quote = self.app.preview(self.request('refine'))
        self.assertGreater(next_quote['gil_debit'], 0)
        self.assertIsNone(next_quote['after'])
        self.assertEqual(self.store.load()[:2], accepted[:2])

    def test_interrupted_acceptance_recovers_once(self):
        quote = self.app.preview(self.request('refine'))
        before_raw, _, before, _ = self.store.load()

        def interrupt(phase):
            if phase == 'native_written':
                raise OSError('private loss after the paid native image')

        self.store.fault = interrupt
        with self.assertRaises(OSError):
            self.app.confirm({'confirmation': quote['confirmation']})
        self.assertIsNone(self.app.pending)
        self.store = Store(self.store.path, self.core)
        self.app = Workshop(self.store)
        recovered = self.store.load()
        self.assertEqual(recovered[2].rolls, before.rolls + 1)
        self.assertEqual(native_gil(recovered[0]), native_gil(before_raw) - quote['gil_debit'])
        self.assertEqual(sum(recovered[2].pieces[0].ranks), 1)
        self.assertEqual(self.store.load()[:2], recovered[:2])
        with self.assertRaises(WorkshopError):
            self.app.confirm({'confirmation': quote['confirmation']})

    def test_cancelled_preview_does_not_publish_outcome(self):
        before = self.store.load()
        quote = self.app.preview(self.request('refine'))
        self.assertIsNone(quote['after'])
        self.app.pending = None
        self.store = Store(self.store.path, self.core)
        self.app = Workshop(self.store)
        self.assertEqual(self.store.load()[:2], before[:2])
        with self.assertRaises(WorkshopError):
            self.app.confirm({'confirmation': quote['confirmation']})


if __name__ == '__main__':
    unittest.main()
