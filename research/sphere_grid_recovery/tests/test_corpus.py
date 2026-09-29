"""Corpus audit tests use explicitly synthetic files in temporary directories."""
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest
sys.path.append(str(Path(__file__).resolve().parents[1]))
from test_codec import fixture

class CorpusTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(importlib.util.find_spec('verify'),'Verification/corpus runner is not implemented')
        import verify
        self.v=verify
        self.directory=tempfile.TemporaryDirectory();self.addCleanup(self.directory.cleanup)
        self.root=Path(self.directory.name)

    def seed(self):
        for layout,contents,count in [('dat01.dat','dat09.dat',828),('dat02.dat','dat10.dat',860),
                                      ('dat03.dat','dat11.dat',828)]:
            a,b=fixture(count);(self.root/layout).write_bytes(a);(self.root/contents).write_bytes(b)

    def test_all_three_pairs_roundtrip_without_modification(self):
        self.seed();before={p.name:p.read_bytes() for p in self.root.iterdir()}
        result=self.v.audit_corpus(self.root)
        self.assertEqual(result['pairs_passed'],3)
        self.assertFalse(result['production_ready'])
        self.assertTrue(all(p['byte_identical_roundtrip'] for p in result['pairs']))
        self.assertEqual({p.name:p.read_bytes() for p in self.root.iterdir()},before)
        self.assertFalse(result['pairs'][1]['matches_recorded_vanilla_standard'])

    def test_missing_pair_is_not_counted_as_success(self):
        self.seed();(self.root/'dat10.dat').unlink()
        result=self.v.audit_corpus(self.root)
        self.assertEqual(result['pairs_passed'],2)
        self.assertEqual(result['pairs'][1]['status'],'failed')

    def test_stale_header_is_reported_per_pair(self):
        self.seed();p=self.root/'dat10.dat';bad=bytearray(p.read_bytes());struct.pack_into('<H',bad,2,859);p.write_bytes(bad)
        result=self.v.audit_corpus(self.root)
        self.assertEqual(result['pairs_passed'],2)
        self.assertIn('count',result['pairs'][1]['error'])

    def test_missing_corpus_is_an_explicit_unverified_boundary(self):
        result=self.v.audit_corpus(None)
        self.assertEqual(result['status'],'not_run')
        self.assertEqual(result['pairs_passed'],0)

    def test_extra_byte_is_rejected_without_repairing_source(self):
        self.seed();p=self.root/'dat10.dat';bad=p.read_bytes()+b'\x23';p.write_bytes(bad)
        result=self.v.audit_corpus(self.root)
        self.assertEqual(result['pairs_passed'],2)
        self.assertEqual(p.read_bytes(),bad)

    def test_verification_output_never_overwrites_an_existing_directory(self):
        (self.root/'KEEP').write_bytes(b'KEEP')
        with self.assertRaises(FileExistsError):self.v.make_output(self.root)
        self.assertEqual((self.root/'KEEP').read_bytes(),b'KEEP')

if __name__=='__main__':unittest.main()
