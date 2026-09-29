"""Native visual acceptance; separate from the existing 46 cases."""
import os
from pathlib import Path
import struct
import unittest
from native_harness import DrawFixture, NativeFailure
from pe_image import read_exact

class VisualNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = read_exact(Path(os.environ["FFX_SPHERE_EXE"]))

    def test_visible_geometry(self):
        f = DrawFixture(self.image, 1)
        f.draw(1)
        v = struct.unpack("<12f", f.m.get(f.buffers[0]["positions"].address, 48))
        area = (v[3]-v[0])*(v[7]-v[1])-(v[6]-v[0])*(v[4]-v[1])
        self.assertGreater(abs(area), 1e-6, "native quad is collapsed")
        color = struct.unpack("<16f", f.m.get(f.buffers[0]["colors"].address, 64))
        self.assertTrue(all(x > 0 for x in color), "native quad is transparent")
        uv = struct.unpack("<8f", f.m.get(f.buffers[0]["uv"].address, 32))
        self.assertGreater(max(uv[::2])-min(uv[::2]), 0)
        self.assertGreater(max(uv[1::2])-min(uv[1::2]), 0)
        self.assertTrue(all(0 <= x <= 1 for x in uv))

    def test_collapsed_geometry_rejected(self):
        f = DrawFixture(self.image, 1)
        f.draw(1)
        f.m.put(f.buffers[0]["positions"].address, bytes(48))
        with self.assertRaises(NativeFailure):
            f.result((0,))

    def test_transparent_geometry_rejected(self):
        f = DrawFixture(self.image, 1)
        f.draw(1)
        f.m.put(f.buffers[0]["colors"].address, bytes(64))
        with self.assertRaises(NativeFailure):
            f.result((0,))

    def test_invalid_uv_and_nonfinite_values_rejected(self):
        for name,data in [('uv',struct.pack('<8f',*([2.0]*8))),
                          ('uv',bytes(32)),
                          ('colors',struct.pack('<16f',*([float('nan')]*16))),
                          ('positions',struct.pack('<12f',*([float('inf')]*12)))]:
            with self.subTest(buffer=name,data=data.hex()[:16]):
                f=DrawFixture(self.image,1);f.draw(1)
                f.m.put(f.buffers[0][name].address,data)
                with self.assertRaises(NativeFailure):f.result((0,))

    def test_original_sprite_table_is_required(self):
        f=DrawFixture(self.image,1)
        f.m.allowed_code=[span for span in f.m.allowed_code if span!=(0x684ca0,0x684d82)]
        with self.assertRaises(NativeFailure) as caught:f.draw(1)
        self.assertEqual(caught.exception.evidence['kind'],'unmodelled_native_callee')

    def test_actual_triangle_indices_cannot_collapse_valid_vertices(self):
        f=DrawFixture(self.image,1);f.draw(1)
        # This still references all four vertices, so the old set check alone
        # accepts it even though both triangles repeat an endpoint.
        f.m.put(f.buffers[0]['indices'].address,struct.pack('<6H',0,0,1,2,3,3))
        with self.assertRaises(NativeFailure):f.result((0,))

    def test_index_only_sentinel_rejects_repeated_triangle_vertices(self):
        f=DrawFixture(self.image,1);f.draw(0xffff)
        f.result((0,1),with_geometry=False)
        f.m.put(f.buffers[1]['indices'].address,struct.pack('<6H',0,0,1,2,3,3))
        with self.assertRaises(NativeFailure) as caught:
            f.result((0,1),with_geometry=False)
        self.assertIn('repeated native triangle vertex',str(caught.exception))

    def test_second_layer_nonzero_quad_rejects_repeated_indices(self):
        f=DrawFixture(self.image,2)
        for i in range(2):f.draw(2+i);f.draw(-3)
        f.result((0,1))
        before=f.m.get(f.buffers[1]['indices'].address,24)
        f.m.put(f.buffers[1]['indices'].address+12,struct.pack('<6H',4,4,5,6,7,5))
        with self.assertRaises(NativeFailure) as caught:f.result((0,1))
        self.assertIn('repeated native triangle vertex',str(caught.exception))
        self.assertEqual(f.m.get(f.buffers[1]['indices'].address,12),before[:12])

    def test_actual_indexed_area_is_not_the_canonical_area(self):
        f=DrawFixture(self.image,1);f.draw(1)
        # Canonical triples have nonzero area, but actual triple (0,1,3)
        # is collinear without repeating an index. Check the stored triplets.
        points=(0,0,1, 1,0,1, 0,1,1, 2,0,1)
        f.m.put(f.buffers[0]['positions'].address,struct.pack('<12f',*points))
        f.m.put(f.buffers[0]['indices'].address,struct.pack('<6H',0,1,3,0,3,2))
        with self.assertRaises(NativeFailure) as caught:f.result((0,))
        self.assertIn('collapsed native triangle',str(caught.exception))

    def test_valid_alternate_diagonal_and_order_are_not_rejected(self):
        f=DrawFixture(self.image,1);f.draw(1)
        for indices in ((0,1,3,0,3,2),(3,2,0,3,0,1),(0,2,1,1,2,3)):
            with self.subTest(indices=indices):
                f.m.put(f.buffers[0]['indices'].address,struct.pack('<6H',*indices))
                f.result((0,))

if __name__ == "__main__":
    unittest.main()
