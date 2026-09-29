"""Byte-transaction tests, not a proof of native instruction execution."""
from pathlib import Path
import importlib.util
import struct
import sys
import unittest
sys.path.append( str(Path(__file__).resolve().parents[1]))

class PatchTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(importlib.util.find_spec('emulator_patches'),
                             'Emulator-only atomic byte plan is not implemented')
        import emulator_patches as p
        self.p = p

    def seed(self):
        image = bytearray(0x700000)
        for patch in self.p.render_plan(861):
            image[patch.rva:patch.rva+len(patch.expected)] = patch.expected
        image[0x65a49f:0x65a4a4] = bytes.fromhex('e95c030000')
        return image

    def test_exact_stock_plan_is_identity(self):
        image = self.seed()
        result = self.p.apply_private_image(image, self.p.render_plan(861))
        self.assertEqual(result, image)
        self.assertIsNot(result, image)

    def test_capacity_counts_allocations_and_zeroing_agree(self):
        for capacity in (1, 860, 861, 862, 1024, 1025, 2048, 4096, 16384):
            with self.subTest(capacity=capacity):
                plan = self.p.render_plan(capacity)
                values = {patch.label: patch.new_value for patch in plan}
                self.assertEqual(values['vertices'], 4*capacity)
                self.assertEqual(values['indices_count'], 6*capacity)
                for field,stride in [('positions',48),('colors',64),('uv',32),('indices',12)]:
                    self.assertEqual(values[f'allocate_{field}'], capacity*stride)
                    self.assertEqual(values[f'clear_{field}'], capacity*stride)
                self.assertEqual(values['writer_positive_boundary'], capacity)
                self.assertEqual(values['writer_positive_decode'], capacity)
                self.assertEqual(values['writer_negative_append'], -capacity-1)
                self.assertEqual(values['producer_positive_bias'], capacity-1)
                self.assertEqual(values['producer_negative_append'], -capacity-1)

    def test_only_documented_immediate_bytes_change(self):
        image = self.seed(); plan = self.p.render_plan(4096)
        result = self.p.apply_private_image(image, plan)
        permitted = {patch.rva+patch.immediate+i for patch in plan for i in range(4)}
        actual = {i for i,(a,b) in enumerate(zip(image,result)) if a != b}
        self.assertTrue(actual)
        self.assertLessEqual(actual, permitted)
        self.assertEqual(result[0x65a49f:0x65a4a4], bytes.fromhex('e95c030000'))
        for patch in plan:
            self.assertEqual(struct.unpack_from('<I',result,patch.rva+patch.immediate)[0],
                             patch.new_value & 0xffffffff)

    def test_corrupt_each_instruction_refuses_entire_plan(self):
        plan = self.p.render_plan(1024)
        for index,patch in enumerate(plan):
            with self.subTest(site=patch.label):
                image = self.seed(); image[patch.rva] ^= 0xff; before = bytes(image)
                with self.assertRaises(self.p.PatchError):
                    self.p.apply_private_image(image,plan)
                self.assertEqual(image,before)

    def test_short_image_rejected_without_mutation(self):
        image = bytearray(32); before = image[:]
        with self.assertRaises(self.p.PatchError):
            self.p.apply_private_image(image,self.p.render_plan(1024))
        self.assertEqual(image,before)

    def test_duplicate_patch_rejected(self):
        plan = self.p.render_plan(861)
        with self.assertRaises(self.p.PatchError):
            self.p.apply_private_image(self.seed(),plan+(plan[0],))

    def test_overlap_even_with_different_label_rejected(self):
        a = self.p.Patch('a',8,b'\x90'*8,1,1)
        b = self.p.Patch('b',10,b'\x90'*8,1,2)
        with self.assertRaises(self.p.PatchError):
            self.p.apply_private_image(bytearray(b'\x90'*64),(a,b))

    def test_out_of_range_immediate_rejected(self):
        for value in (-0x80000001, 0x100000000, 1.5, True):
            with self.assertRaises(self.p.PatchError):
                self.p.Patch('bad',8,b'\x90'*5,1,value)

    def test_bad_patch_spans_rejected(self):
        for rva,expected,offset in [(-1,b'12345',1),(8,b'',0),(8,b'12345',2),(8,b'12345',-1)]:
            with self.assertRaises(self.p.PatchError):
                self.p.Patch('bad',rva,expected,offset,0)

    def test_empty_patch_plan_is_not_a_successful_install(self):
        with self.assertRaises(self.p.PatchError):
            self.p.apply_private_image(bytearray(64),())

    def test_capacity_above_index_width_rejected(self):
        for count in (0,-1,16385,0x7fffffff,True):
            with self.assertRaises(ValueError): self.p.render_plan(count)

    def test_native_file_or_process_writer_is_not_exposed(self):
        self.assertFalse(hasattr(self.p,'apply_to_process'))
        self.assertFalse(hasattr(self.p,'write_executable'))
        self.assertEqual(len(self.p.render_plan(1024)),15)

if __name__ == '__main__': unittest.main()
