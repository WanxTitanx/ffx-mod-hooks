"""Packed ABMAP contract tests; synthetic fixtures, not gameplay acceptance."""
from __future__ import annotations
import importlib.util
import random
import struct
import sys
import unittest
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
sys.path.append( str(BASE))
try:
    import codec as C
except ModuleNotFoundError:
    C = None


def fixture(count=860, *, content=35, tail=b''):
    # Deliberately independent encoders, including nonzero opaque header fields.
    layout = struct.pack('<8H', 49, 1, count, max(0, count-1), 0x1234, 0x5678, 3, 0x5000)
    layout += struct.pack('<hh6H', 0, 0, 0x6789, 0, 0xABCD, 0xBCDE, 0xCDEF, 0xDEF0)
    for index in range(count):
        x, y = (index % 40)*16, (index//40)*16
        cell = (x+2560)//256 + 20*((y+2336)//256)
        layout += struct.pack('<hh4H', x, y, 0x1357, content, 0, cell)
    for index in range(count-1):
        layout += struct.pack('<4H', index, index+1, 0xFFFF, 0x2468)
    contents = struct.pack('<4H', 49, count, 0xBEEF, 0xCAFE) + bytes([content])*count + tail
    return layout, contents


class CodecTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(C, 'The strict packed-pair compiler is not implemented')

    def test_mutable_model_containers_are_refused(self):
        from dataclasses import replace
        grid=C.parse(*fixture(4))
        for field in ('layout_header','contents_header','clusters','nodes','links'):
            with self.subTest(field=field),self.assertRaises(C.FormatError):
                C.encode(replace(grid,**{field:list(getattr(grid,field))}))

    def test_unknown_cluster_radius_type_is_refused(self):
        a,b=fixture(4)
        for value in (8,255,65535):
            bad=bytearray(a);struct.pack_into('<H',bad,16+6,value)
            with self.subTest(value=value),self.assertRaises(C.FormatError):C.parse(bytes(bad),b)

    def test_known_cluster_radius_types_roundtrip(self):
        a,b=fixture(4)
        for value in range(8):
            changed=bytearray(a);struct.pack_into('<H',changed,16+6,value)
            pair=(bytes(changed),b)
            self.assertEqual(C.encode(C.parse(*pair)),pair)

    def test_activatable_empty_type_is_not_the_null_sentinel(self):
        grid=C.parse(*fixture(4))
        new=C.append_node(grid,donor=0,connect_to=0,x=900,y=900,content=1)
        self.assertEqual(new.nodes[-1].type_word,1)
        self.assertEqual(C.parse(*C.encode(new)).nodes[-1].content,1)
        null=C.append_node(grid,donor=0,connect_to=0,x=900,y=900,content=255)
        self.assertEqual(null.nodes[-1].type_word,65535)

    def test_no_edit_pair_is_byte_identical(self):
        for count in (1, 828, 860, 861, 1024):
            with self.subTest(count=count):
                pair = fixture(count)
                self.assertEqual(C.encode(C.parse(*pair)), pair)

    def test_append_updates_both_header_counts_and_payload(self):
        original = fixture()
        grid = C.parse(*original)
        grown = C.append_node(grid, donor=859, connect_to=859, x=800, y=400, content=35)
        layout, contents = C.encode(grown)
        self.assertEqual(struct.unpack_from('<H', layout, 4)[0], 861)
        self.assertEqual(struct.unpack_from('<H', contents, 2)[0], 861)
        self.assertEqual(len(contents), 869)
        self.assertEqual(struct.unpack_from('<H', layout, 6)[0], 860)
        self.assertEqual(C.encode(grid), original, 'Appending must not mutate the input')

    def test_stale_contents_count_is_rejected(self):
        layout, contents = fixture(861)
        stale = bytearray(contents)
        struct.pack_into('<H', stale, 2, 860)
        with self.assertRaisesRegex(C.FormatError, 'contents.*count'):
            C.parse(layout, bytes(stale))

    def test_appended_byte_without_layout_change_is_rejected(self):
        a, b = fixture()
        with self.assertRaisesRegex(C.FormatError, 'contents.*length'):
            C.parse(a, b+b'\x23')

    def test_truncated_inputs_are_rejected(self):
        a, b = fixture(5)
        for cut in (0, 1, 7, 8, 15, 16, len(a)-1):
            with self.subTest(layout_cut=cut), self.assertRaises(C.FormatError):
                C.parse(a[:cut], b)
        for cut in (0, 1, 7, 8, len(b)-1):
            with self.subTest(contents_cut=cut), self.assertRaises(C.FormatError):
                C.parse(a, b[:cut])

    def test_trailing_layout_bytes_are_not_silently_ignored(self):
        a, b = fixture()
        with self.assertRaisesRegex(C.FormatError, 'layout.*length'):
            C.parse(a+b'bad', b)

    def test_wrong_magic_is_rejected(self):
        a, b = fixture(3)
        for changed in (0, 1):
            inputs = [bytearray(a), bytearray(b)]
            inputs[changed][0] = 48
            with self.subTest(changed=changed), self.assertRaises(C.FormatError):
                C.parse(*map(bytes, inputs))

    def test_donor_and_all_opaque_bytes_are_preserved(self):
        original = fixture(8)
        grid = C.parse(*original)
        grown = C.append_node(grid, donor=3, connect_to=7, x=700, y=333, content=36)
        a, b = C.encode(grown)
        self.assertEqual(grown.clusters, grid.clusters)
        self.assertEqual(grown.nodes[:-1], grid.nodes)
        self.assertEqual(grown.links[:-1], grid.links)
        self.assertEqual(grown.nodes[-1].opaque, grid.nodes[3].opaque)
        self.assertEqual(a[8:16], original[0][8:16])
        self.assertEqual(b[4:8], original[1][4:8])
        self.assertEqual(b[8:-1], original[1][8:])

    def test_no_node_and_zero_cluster_are_rejected(self):
        for offset in (2, 4):
            a, b = fixture(3)
            bad = bytearray(a); struct.pack_into('<H', bad, offset, 0)
            with self.subTest(offset=offset), self.assertRaises(C.FormatError):
                C.parse(bytes(bad), b)

    def test_dangling_link_is_rejected(self):
        a, b = fixture(3); bad = bytearray(a)
        struct.pack_into('<H', bad, 16+16+3*12, 3)
        with self.assertRaisesRegex(C.FormatError, 'link.*node'):
            C.parse(bytes(bad), b)

    def test_dangling_anchor_is_rejected(self):
        a, b = fixture(3); bad = bytearray(a)
        struct.pack_into('<H', bad, 16+16+3*12+4, 3)
        with self.assertRaisesRegex(C.FormatError, 'anchor'):
            C.parse(bytes(bad), b)

    def test_self_link_is_rejected(self):
        a, b = fixture(3); bad = bytearray(a)
        struct.pack_into('<H', bad, 16+16+3*12+2, 0)
        with self.assertRaisesRegex(C.FormatError, 'self'):
            C.parse(bytes(bad), b)

    def test_reversed_duplicate_link_is_rejected(self):
        a, b = fixture(3); bad = bytearray(a)
        struct.pack_into('<4H', bad, 16+16+3*12+8, 1, 0, 0xFFFF, 0)
        with self.assertRaisesRegex(C.FormatError, 'duplicate'):
            C.parse(bytes(bad), b)

    def test_sixth_link_to_one_node_is_rejected_without_mutation(self):
        grid = C.parse(*fixture(2))
        for i in range(4):
            grid = C.append_node(grid, donor=0, connect_to=0, x=100+i*20, y=500, content=35)
        before = C.encode(grid)
        with self.assertRaisesRegex(C.FormatError, 'degree|five'):
            C.append_node(grid, donor=0, connect_to=0, x=200, y=500, content=35)
        self.assertEqual(C.encode(grid), before)

    def test_fixed_native_node_array_limit_rejects_1025(self):
        grid = C.parse(*fixture(1024))
        with self.assertRaisesRegex(C.FormatError, '1024|node capacity'):
            C.append_node(grid, donor=1023, connect_to=1023, x=900, y=900, content=35)

    def test_offline_large_graph_does_not_claim_native_support(self):
        limits = C.AUTHORING_LIMITS
        for count in (1025, 2048, 4096, 16384):
            with self.subTest(count=count):
                a, b = fixture(count)
                grid = C.parse(a, b, limits=limits)
                self.assertEqual(len(grid.nodes), count)
                self.assertTrue(C.native_blockers(grid))
                with self.assertRaises(C.FormatError):
                    C.encode(grid)
                self.assertEqual(C.encode(grid, limits=limits), (a,b))

    def test_native_type_count_is_separate_from_node_count(self):
        grid = C.parse(*fixture(3))
        with self.assertRaisesRegex(C.FormatError, 'node type'):
            C.append_node(grid, donor=0, connect_to=2, x=500, y=500, content=130)

    def test_absent_panel_row_is_rejected_when_catalog_is_supplied(self):
        a,b = fixture(3)
        with self.assertRaisesRegex(C.FormatError, 'panel'):
            C.parse(a,b,panel_ids=frozenset({1,2,3}))

    def test_empty_layout_sentinel_is_ffff_not_00ff(self):
        grid = C.parse(*fixture(2))
        grid = C.append_node(grid, donor=0, connect_to=1, x=500, y=500, content=255)
        self.assertEqual(grid.nodes[-1].type_word, 0xFFFF)
        a,b = C.encode(grid)
        self.assertEqual(b[-1],255)
        self.assertEqual(C.encode(C.parse(a,b)),(a,b))

    def test_content_mismatch_is_rejected(self):
        a,b = fixture(2); bad=bytearray(b);bad[8]=36
        with self.assertRaisesRegex(C.FormatError, 'content'):
            C.parse(a,bytes(bad))

    def test_invalid_cluster_is_rejected(self):
        a,b=fixture(3);bad=bytearray(a);struct.pack_into('<H',bad,16+16+8,1)
        with self.assertRaisesRegex(C.FormatError,'cluster'):
            C.parse(bytes(bad),b)

    def test_coordinates_use_native_truncation_not_floor(self):
        self.assertEqual(C.cell(-2561,-2337),0)
        self.assertEqual(C.cell(-2817,-2593),(-21)&0xffff)
        self.assertEqual(C.cell(0,0),190)
        for coordinate in (-32769,32768,False,1.5,float('nan')):
            with self.subTest(coordinate=coordinate), self.assertRaises(C.FormatError):
                C.cell(coordinate,0)

    def test_out_of_range_fields_do_not_wrap(self):
        grid=C.parse(*fixture(3))
        for value in (-1,256,65535,True,1.5):
            with self.subTest(content=value),self.assertRaises(C.FormatError):
                C.append_node(grid,donor=0,connect_to=2,x=500,y=500,content=value)
        for index in (-1,3,65536,True):
            with self.subTest(donor=index),self.assertRaises(C.FormatError):
                C.append_node(grid,donor=index,connect_to=2,x=500,y=500,content=35)

    def test_random_append_round_trips_and_keeps_canary_input(self):
        rng=random.Random(0x861)
        for trial in range(150):
            count=rng.randrange(1,100)
            original=fixture(count)
            grid=C.parse(*original)
            content=rng.choice([1,2,35,96,127,255])
            grown=C.append_node(grid,donor=rng.randrange(count),connect_to=count-1,
                                x=rng.randrange(700,1200),y=rng.randrange(700,1200),content=content)
            packed=C.encode(grown)
            self.assertEqual(C.encode(C.parse(*packed)),packed)
            self.assertEqual(C.encode(grid),original)

    def test_arbitrary_short_bytes_cannot_escape_structural_validation(self):
        rng=random.Random(0xC0FFEE)
        for _ in range(1000):
            a=rng.randbytes(rng.randrange(0,128));b=rng.randbytes(rng.randrange(0,64))
            with self.assertRaises(C.FormatError): C.parse(a,b)


if __name__=='__main__':unittest.main()
