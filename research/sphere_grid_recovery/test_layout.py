"""Exclusive offline Sphere Grid format regressions; fixtures contain no game assets."""
import random
import struct
import unittest

import layout


def fixture(count=3):
    header = struct.pack('<8H', 0x31, 1, count, count-1, 0, 0, 3, 0x5000)
    cluster = struct.pack('<8H', 0, 0, 0x1234, 3, 0x5678, 0, 0, 0)
    nodes = b''.join(struct.pack('<hh4H', i*16, 0, 0x4321, 0x23, 0,
                                (i*16+2560)//256+20*(2336//256)) for i in range(count))
    links = b''.join(struct.pack('<4H', i, i+1, 0xffff, 0x4567) for i in range(count-1))
    contents = struct.pack('<4H', 0x31, count, 0x9876, 0x3456) + b'\x23'*count
    return header+cluster+nodes+links, contents


class FormatTests(unittest.TestCase):
    def test_roundtrip_preserves_unknown_fields(self):
        pair = fixture()
        self.assertEqual(layout.parse_pair(*pair).encode(), pair)

    def test_append_updates_both_declared_counts_and_preserves_old_records(self):
        source = layout.parse_pair(*fixture())
        result = source.append_node(template=2, x=64, y=0, content=0xff, connect_to=2)
        a, b = result.encode()
        self.assertEqual(struct.unpack_from('<H', a, 4)[0], 4)
        self.assertEqual(struct.unpack_from('<H', b, 2)[0], 4)
        self.assertEqual(len(b), 12)
        self.assertEqual(source.nodes, result.nodes[:-1])
        self.assertEqual(source.links, result.links[:-1])
        self.assertEqual(result.nodes[-1].content_word, 0xffff)
        self.assertEqual(b[-1], 0xff)
        self.assertEqual(source.contents_header[4:], b[4:8])
        self.assertEqual(layout.parse_pair(a, b).encode(), (a,b))

    def test_historical_extra_payload_with_stale_contents_count_is_rejected(self):
        result = layout.parse_pair(*fixture()).append_node(template=2, x=64, y=0,
                                                          content=0x23, connect_to=2)
        a, b = result.encode()
        broken = bytearray(b)
        struct.pack_into('<H', broken, 2, 3)
        with self.assertRaisesRegex(ValueError, 'contents count'):
            layout.parse_pair(a, bytes(broken))

    def test_truncated_and_trailing_data_are_rejected(self):
        a,b = fixture()
        for bad_a,bad_b in [(a[:-1],b),(a+b'\0',b),(a,b[:-1]),(a,b+b'\0')]:
            with self.subTest(lengths=(len(bad_a),len(bad_b))):
                with self.assertRaises(ValueError):layout.parse_pair(bad_a,bad_b)

    def test_bad_references_and_self_link_are_rejected(self):
        a,b=fixture()
        for first,second,anchor in [(3,0,0xffff),(0,3,0xffff),(0,1,3),(0,0,0xffff)]:
            broken=bytearray(a)
            struct.pack_into('<3H',broken,16+16+3*12,first,second,anchor)
            with self.assertRaises(ValueError):layout.parse_pair(bytes(broken),b)

    def test_sixth_incident_link_cannot_overwrite_neighboring_node_fields(self):
        source=layout.parse_pair(*fixture(2))
        for i in range(4):source=source.append_node(template=0,x=64+i*16,y=32,content=0x23,connect_to=0)
        with self.assertRaisesRegex(ValueError,'degree'):
            source.append_node(template=0,x=144,y=32,content=0x23,connect_to=0)

    def test_content_copy_and_spatial_bucket_mismatch_are_rejected(self):
        a,b=fixture()
        for offset,value in [(32+6,0xffff),(32+10,0xffff)]:
            broken=bytearray(a);struct.pack_into('<H',broken,offset,value)
            with self.assertRaises(ValueError):layout.parse_pair(bytes(broken),b)

    def test_fixed_native_limits_are_distinct_from_render_capacity(self):
        source=layout.parse_pair(*fixture())
        self.assertEqual(source.capacity_report()['logical_node_limit'],1024)
        self.assertEqual(source.capacity_report()['unmodified_render_quads'],861)
        self.assertFalse(source.capacity_report()['live_accepted'])

    def test_boundary_860_861_862_1024_and_reject_1025(self):
        for count in (860,861,862,1024):
            with self.subTest(count=count):
                source=layout.parse_pair(*fixture(count))
                self.assertEqual(source.node_count,count)
                self.assertEqual(source.encode(),fixture(count))
        with self.assertRaisesRegex(ValueError,'node capacity'):
            layout.parse_pair(*fixture(1025))

    def test_append_is_transactional_on_bad_input(self):
        source=layout.parse_pair(*fixture());before=source.encode()
        for kwargs in [dict(x=40000),dict(content=256),dict(template=999),dict(connect_to=999)]:
            args=dict(template=2,x=64,y=0,content=0x23,connect_to=2);args.update(kwargs)
            with self.assertRaises(ValueError):source.append_node(**args)
            self.assertEqual(source.encode(),before)

    def test_bad_counts_do_not_trigger_unbounded_allocation(self):
        a,b=fixture()
        for offset,value in [(2,129),(4,65535),(6,1025)]:
            broken=bytearray(a);struct.pack_into('<H',broken,offset,value)
            with self.assertRaises(ValueError):layout.parse_pair(bytes(broken),b)

    def test_random_payload_damage_is_validated_without_crashing(self):
        rng=random.Random(861)
        a,b=fixture()
        for _ in range(1000):
            damaged=bytearray(a);damaged[rng.randrange(len(a))]=rng.randrange(256)
            try:
                result=layout.parse_pair(bytes(damaged),b)
            except ValueError:
                continue
            self.assertEqual(result.encode(),(bytes(damaged),b))


if __name__=='__main__':unittest.main()
