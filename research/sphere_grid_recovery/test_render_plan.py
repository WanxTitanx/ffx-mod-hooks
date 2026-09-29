"""Capacity/protocol regressions; every modified instruction exists only in an emulator."""
import os
import unittest
from native_fixture import SphereMachine, exact_image
from render_plan import plan


def apply_plan(machine, capacity, allocations_only=False):
    if machine.native_calls or machine.services:
        raise ValueError('This fixture only permits changes before native initialization')
    changes = [p for p in plan(capacity) if not allocations_only or p.family == 'allocation']
    for p in changes:
        if bytes(machine.cpu.mem_read(p.va, len(p.before))) != p.before:
            raise ValueError('instruction mismatch at ' + hex(p.va))
    for p in changes:
        machine.cpu.mem_write(p.va, p.after)


class RenderPlanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw = exact_image(os.environ['FFX_SPHERE_EXE'])

    def test_reject_overflow_and_unsafe_counts(self):
        for count in (-1, 0, 860, 16385, 65535, True, 1.5):
            with self.subTest(count=count):
                with self.assertRaises(ValueError):
                    plan(count)
        self.assertEqual(len(plan(1024)), 15)

    def test_no_partial_change_on_instruction_mismatch(self):
        m = SphereMachine(self.raw)
        changes = plan(1024)
        bad = changes[-1]
        m.cpu.mem_write(bad.va, b'\xcc')
        before = [bytes(m.cpu.mem_read(p.va, len(p.before))) for p in changes]
        with self.assertRaises(ValueError):
            apply_plan(m, 1024)
        self.assertEqual(before, [bytes(m.cpu.mem_read(p.va, len(p.before))) for p in changes])

    def test_raw_860_jump_displacement_is_not_modified(self):
        m = SphereMachine(self.raw)
        before = bytes(m.cpu.mem_read(0xa5a49f, 5))
        self.assertEqual(before.hex(), 'e95c030000')
        apply_plan(m, 1024)
        self.assertEqual(bytes(m.cpu.mem_read(0xa5a49f, 5)), before)

    def test_allocations_alone_do_not_fix_positive_or_negative_protocol_collision(self):
        for negative in (False, True):
            with self.subTest(negative=negative):
                m = SphereMachine(self.raw)
                apply_plan(m, 1024, allocations_only=True)
                manager = m.manager()
                m.reset_sphere(manager)
                sprite, packet = m.sprite(), m.packet()
                for i in range(1024):
                    m.draw(sprite, packet, -862 if negative else 861+i)
                with self.assertRaises(AssertionError):
                    m.draw(sprite, packet, -862 if negative else 861)
                self.assertIn('overrun', m.failure['kind'])

    def test_1024_capacity_including_both_fixed_update_collisions(self):
        m = SphereMachine(self.raw)
        apply_plan(m, 1024)
        manager = m.manager(full=True)
        m.reset_sphere(manager)
        sprite, packet = m.sprite(), m.packet()
        for i in range(1024):
            m.draw(sprite, packet, 1024+i)
            m.draw(sprite, packet, -1025)
        layers = m.sphere_layers(manager)[1]
        for layer in layers:
            self.assertEqual((m.read32(layer+4), m.read32(layer+8)), (4096,6144))
        for index in (0,859,860,861,1023):
            m.draw(sprite, packet, index)
            m.draw(sprite, packet, -1-index)
        for layer in layers:
            self.assertEqual((m.read32(layer+4), m.read32(layer+8)), (4096,6144))
        m.invoke(0x685950, this=manager)
        m.invoke(0x6859e0, this=manager)
        m.invoke(0x6859e0, this=manager)
        self.assertEqual(m.live_bytes, 0xe8+0x200+0x80+64)

    def test_16384_quad_index_width_boundary(self):
        m = SphereMachine(self.raw)
        apply_plan(m, 16384)
        manager = m.manager()
        m.reset_sphere(manager)
        sprite, packet = m.sprite(), m.packet()
        layers = m.sphere_layers(manager)[1]
        for layer in layers:
            m.set32(layer+4, 16383*4)
            m.set32(layer+8, 16383*6)
        m.draw(sprite, packet, 16384+16383)
        m.draw(sprite, packet, -16385)
        for layer in layers:
            self.assertEqual(m.read32(layer+4), 65536)
            start = m.read32(layer+28)
            raw = bytes(m.cpu.mem_read(start+16383*12, 12))
            values = [int.from_bytes(raw[i:i+2], 'little') for i in range(0,12,2)]
            self.assertEqual(min(values), 65532)
            self.assertEqual(max(values), 65535)


if __name__ == '__main__':
    unittest.main()
