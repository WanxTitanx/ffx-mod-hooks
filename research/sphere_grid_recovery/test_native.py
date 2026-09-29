"""Dedicated native Sphere Grid regressions. Set FFX_SPHERE_EXE explicitly."""
import os
import unittest
from native_fixture import SphereMachine, exact_image


class NativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = os.environ.get('FFX_SPHERE_EXE')
        if not path:
            raise RuntimeError('FFX_SPHERE_EXE is required; native tests must not silently skip')
        cls.raw = exact_image(path)

    def test_real_constructor_and_real_counter_reset(self):
        m = SphereMachine(self.raw)
        manager = m.manager()
        owner, layers = m.sphere_layers(manager)
        for layer in layers:
            self.assertEqual(m.read32(layer+4), 3444)
            self.assertEqual(m.read32(layer+8), 5166)
            for offset, length in [(12,41328),(20,55104),(24,27552),(28,10332)]:
                self.assertEqual(m.stats[m.read32(layer+offset)]['bytes'], length)
        m.reset_sphere(manager)
        self.assertEqual(m.native_calls['0x685370'], 1)
        self.assertEqual(m.stats[owner]['bytes'], 0xd0)

    def test_real_resolver_and_861_writes_then_boundary_failure(self):
        m = SphereMachine(self.raw)
        manager = m.manager()
        m.reset_sphere(manager)
        sprite, packet = m.sprite(), m.packet()
        for i in range(861):
            m.draw(sprite, packet, 861+i)
        self.assertEqual(m.native_calls['0x684e70'], 861)
        with self.assertRaises(AssertionError):
            m.draw(sprite, packet, 1722)
        self.assertIn('overrun', m.failure['kind'])
        self.assertEqual(m.failure['eip'], '0x7f5622')

    def test_real_deferred_retirement_does_not_release_on_first_frame(self):
        m = SphereMachine(self.raw)
        manager = m.manager(full=True)
        before = m.live_bytes
        m.invoke(0x685950, this=manager)
        self.assertEqual(m.read32(manager+4), 2)
        m.invoke(0x6859e0, this=manager)
        self.assertEqual(m.read32(manager+4), 3)
        self.assertEqual(m.live_bytes, before)
        m.invoke(0x6859e0, this=manager)
        self.assertEqual(m.read32(manager+4), 4)
        self.assertEqual(m.read32(manager+0x94), 0)
        self.assertEqual(m.live_bytes, 0xe8)


if __name__ == '__main__':
    unittest.main()
