"""Borrowed native buffers retain allocation identity, including same-scene ABA."""
import os
from pathlib import Path
import unittest
from native_harness import DrawFixture, NativeFailure
from pe_image import read_exact

class NativeOwnershipTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = read_exact(Path(os.environ['FFX_SPHERE_EXE']))

    def test_same_generation_recycle_does_not_revive_a_borrower(self):
        f = DrawFixture(self.image, 1)
        old = f.buffers[0]['colors']
        f.m.release(old)
        new = f.m.recycle(old, generation=f.m.generation)
        self.assertNotEqual(old.serial, new.serial)
        with self.assertRaises(NativeFailure) as caught:
            f.draw(0)
        self.assertEqual(caught.exception.evidence['kind'], 'allocation_serial_mismatch')
        self.assertEqual(f.m.get(new.address, new.length), bytes(new.length))

    def test_advancing_epoch_does_not_adopt_an_old_borrower(self):
        f = DrawFixture(self.image, 1)
        old = f.buffers[0]['colors']
        f.m.release(old)
        f.m.generation = 2
        new = f.m.recycle(old, generation=2)
        with self.assertRaises(NativeFailure) as caught:
            with f.m.borrowed((old,)):
                f.m.get(new.address, 4)
        self.assertEqual(caught.exception.evidence['kind'], 'generation_mismatch')
        self.assertEqual(f.m.get(new.address, new.length), bytes(new.length))

    def test_descriptor_cannot_redirect_into_an_unowned_allocation(self):
        f = DrawFixture(self.image, 1)
        other = f.m.alloc(64, 'unrelated allocation')
        f.m.put(other.address, b'\xa5'*64)
        f.m.set32(f.layers+0x14, other.address)
        with self.assertRaises(NativeFailure) as caught:
            f.draw(0)
        self.assertEqual(caught.exception.evidence['kind'], 'unowned_allocation')
        self.assertEqual(f.m.get(other.address,64), b'\xa5'*64)

    def test_empty_scope_does_not_admit_heap_access(self):
        f = DrawFixture(self.image, 1)
        with self.assertRaises(NativeFailure) as caught:
            with f.m.borrowed(()):
                f.m.get(f.sprite.address,4)
        self.assertEqual(caught.exception.evidence['kind'], 'unowned_allocation')

    def test_recycle_during_native_callback_invalidates_remaining_access(self):
        f = DrawFixture(self.image, 1)
        old = f.buffers[0]['colors']
        original = f.m.external[0x639180]
        replacement = []
        def resolver():
            f.m.release(old)
            replacement.append(f.m.recycle(old, generation=f.m.generation))
            original()
        f.m.external[0x639180] = resolver
        with self.assertRaises(NativeFailure) as caught:
            f.draw(0)
        self.assertEqual(caught.exception.evidence['kind'], 'allocation_serial_mismatch')
        # Teardown of the failed call must clear its access scope, not permanently
        # poison legitimate setup for a separately admitted future owner.
        f.m.put(replacement[0].address, b'\x5a')
        self.assertEqual(f.m.get(replacement[0].address, 1), b'\x5a')

    def test_nested_callback_cannot_acquire_foreign_memory(self):
        f=DrawFixture(self.image,1)
        other=f.m.alloc(64,'foreign nested allocation')
        with self.assertRaises(NativeFailure) as caught:
            with f.m.borrowed((f.sprite,)):
                with f.m.borrowed((other,)):pass
        self.assertEqual(caught.exception.evidence['kind'],'unowned_allocation')
        f.m.put(other.address,b'\x5a')
        self.assertEqual(f.m.get(other.address,1),b'\x5a')

    def test_scope_cleanup_does_not_restore_permissions_to_released_memory(self):
        f=DrawFixture(self.image,1)
        old=f.buffers[0]['colors'];f.m.release(old)
        with self.assertRaises(NativeFailure):f.draw(0)
        covering=[flags for start,end,flags in f.m.cpu.mem_regions()
                  if start<=old.address<=end]
        self.assertEqual(covering,[f.m.u.UC_PROT_NONE])

if __name__ == '__main__':unittest.main()
