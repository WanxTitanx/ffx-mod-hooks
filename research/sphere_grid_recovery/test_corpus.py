"""Read-only round-trips of caller-supplied original Sphere Grid assets."""
import hashlib
import os
from pathlib import Path
import struct
import unittest
from layout import parse_pair

BASELINES = {
    1: ('ae286e2d9a6501e3aef3f17ae438c6bf3de5c96b18e88cd8c25081ce8b7e214d',
        '8c6c652f52a0f8b23403f4cc63d61482bbf3f41527800cf6d7e2c09dcf74b64f'),
    2: ('1303519cfc2dc4a6eb3975392ea2639f147272d816dcceea877e4157d77ba990',
        '542ce37678fe4ec97d7c3ffd2403061de7ade1bc5a4b6b2de2a7cfdf90745206'),
    3: ('d0befd7fd6b8d7ce18c661aea5f46328ef6bf63d670501229999740e2d4fae5e',
        'ce89d25bcfac2394ce4d7e7e7f8d5887448c62e646dd264ec3786c12f575cf37'),
}


class CorpusTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = os.environ.get('FFX_SPHERE_ASSETS')
        if not path:
            raise RuntimeError('FFX_SPHERE_ASSETS must name the original jppc/menu/abmap directory')
        cls.pairs = {}
        for index, hashes in BASELINES.items():
            a = (Path(path) / f'dat{index:02}.dat').read_bytes()
            b = (Path(path) / f'dat{index+8:02}.dat').read_bytes()
            if (hashlib.sha256(a).hexdigest(), hashlib.sha256(b).hexdigest()) != hashes:
                raise ValueError(f'Original corpus identity mismatch: pair {index}')
            cls.pairs[index] = a, b

    def test_all_original_pairs_roundtrip_byte_exactly(self):
        for index, pair in self.pairs.items():
            with self.subTest(grid=index):
                self.assertEqual(parse_pair(*pair).encode(), pair)

    def test_append_is_paired_and_preserves_all_prior_records(self):
        for index, pair in self.pairs.items():
            with self.subTest(grid=index):
                source = parse_pair(*pair)
                degree = [0] * source.node_count
                for link in source.links:
                    degree[link.first] += 1
                    degree[link.second] += 1
                donor = next(i for i, value in enumerate(degree) if value < 5)
                node = source.nodes[donor]
                result = source.append_node(template=donor, x=node.x+16, y=node.y,
                                            content=0x23, connect_to=donor)
                a, b = result.encode()
                self.assertEqual(result.nodes[:-1], source.nodes)
                self.assertEqual(result.links[:-1], source.links)
                self.assertEqual(len(a), len(pair[0])+20)
                self.assertEqual(len(b), len(pair[1])+1)
                self.assertEqual(struct.unpack_from('<H', b, 2)[0], source.node_count+1)
                self.assertEqual(parse_pair(a, b), result)

    def test_historical_contents_header_bug_is_rejected_on_real_pair(self):
        a, b = self.pairs[2]
        with self.assertRaises(ValueError):
            parse_pair(a, b + b'\x23')
        broken = bytearray(b)
        struct.pack_into('<H', broken, 2, 861)
        with self.assertRaises(ValueError):
            parse_pair(a, bytes(broken))


if __name__ == '__main__':
    unittest.main()
