"""Real temporary filesystem and CLI tests. No game files are opened."""
from pathlib import Path
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.append(str(Path(__file__).resolve().parents[1]))
from test_codec import fixture
import codec

class BundleTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(importlib.util.find_spec('bundle'), 'Research bundle writer is not implemented')
        import bundle
        self.b = bundle
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.layout,self.contents = fixture(860)
        self.grid = codec.parse(self.layout,self.contents)

    def test_new_bundle_is_complete_lossless_and_never_runtime_approved(self):
        target = self.root/'result'
        manifest = self.b.export_new(target,self.grid)
        self.assertEqual((target/'layout.dat').read_bytes(),self.layout)
        self.assertEqual((target/'contents.dat').read_bytes(),self.contents)
        self.assertEqual(json.loads((target/'manifest.json').read_text()),manifest)
        self.assertFalse(manifest['production_ready'])
        self.assertFalse(manifest['runtime_verified'])
        self.assertFalse(manifest['panel_catalog_verified'])
        self.assertEqual(manifest['counts']['nodes'],860)
        self.assertEqual(len(manifest['outputs']['layout.dat']['sha256']),64)
        self.assertTrue(manifest['unverified_boundaries'])

    def test_existing_directory_even_empty_is_never_overwritten(self):
        target = self.root/'result'; target.mkdir()
        with self.assertRaises(FileExistsError): self.b.export_new(target,self.grid)
        self.assertEqual(list(target.iterdir()),[])

    def test_existing_contents_survive_rejected_output(self):
        target = self.root/'result';target.mkdir();(target/'layout.dat').write_bytes(b'KEEP')
        with self.assertRaises(FileExistsError): self.b.export_new(target,self.grid)
        self.assertEqual((target/'layout.dat').read_bytes(),b'KEEP')
        self.assertFalse((target/'manifest.json').exists())

    def test_output_file_collision_is_refused(self):
        target=self.root/'result';target.write_bytes(b'KEEP')
        with self.assertRaises(FileExistsError): self.b.export_new(target,self.grid)
        self.assertEqual(target.read_bytes(),b'KEEP')

    def test_symlink_output_is_refused(self):
        real=self.root/'real';real.mkdir();alias=self.root/'alias';alias.symlink_to(real,target_is_directory=True)
        with self.assertRaises(FileExistsError): self.b.export_new(alias,self.grid)
        self.assertEqual(list(real.iterdir()),[])

    def test_invalid_input_creates_no_output(self):
        from dataclasses import replace
        invalid = replace(self.grid,nodes=())
        with self.assertRaises(codec.FormatError): self.b.export_new(self.root/'result',invalid)
        self.assertFalse((self.root/'result').exists())

    def test_write_failure_never_leaves_success_manifest(self):
        actual=self.b._write_exclusive
        def failing(path,data):
            if path.name=='contents.dat': raise OSError('injected full disk')
            return actual(path,data)
        with patch.object(self.b,'_write_exclusive',failing):
            with self.assertRaises(OSError):self.b.export_new(self.root/'result',self.grid)
        self.assertFalse((self.root/'result'/'manifest.json').exists())
        self.assertEqual((self.root/'result'/'layout.dat').read_bytes(),self.layout)

    def test_io_error_does_not_delete_concurrently_created_data(self):
        actual=self.b._write_exclusive
        def failing(path,data):
            if path.name=='contents.dat':
                path.write_bytes(b'FOREIGN')
                raise PermissionError('injected competing writer')
            return actual(path,data)
        with patch.object(self.b,'_write_exclusive',failing):
            with self.assertRaises(OSError):self.b.export_new(self.root/'result',self.grid)
        self.assertEqual((self.root/'result'/'contents.dat').read_bytes(),b'FOREIGN')
        self.assertFalse((self.root/'result'/'manifest.json').exists())

    def test_partial_manifest_is_never_the_completion_marker(self):
        actual=self.b._write_exclusive
        def failing(path,data):
            if path.name=='.manifest.prepared':
                path.write_bytes(b'{"schema":')
                raise OSError('injected interrupted manifest')
            return actual(path,data)
        with patch.object(self.b,'_write_exclusive',failing):
            with self.assertRaises(OSError):self.b.export_new(self.root/'result',self.grid)
        self.assertFalse((self.root/'result'/'manifest.json').exists())

    def test_authoring_large_bundle_records_native_blockers(self):
        layout,contents=fixture(4096)
        grid=codec.parse(layout,contents,limits=codec.AUTHORING_LIMITS)
        with self.assertRaises(codec.FormatError):self.b.export_new(self.root/'blocked',grid)
        manifest=self.b.export_new(self.root/'research',grid,limits=codec.AUTHORING_LIMITS)
        self.assertTrue(any('1024' in s for s in manifest['native_blockers']))
        self.assertFalse(manifest['production_ready'])

    def test_bounded_read_rejects_large_or_short_files(self):
        path=self.root/'large';path.write_bytes(b'x'*(codec.MAX_INPUT_BYTES+1))
        with self.assertRaises(codec.FormatError):self.b.read_input(path)

    def test_cli_audit_is_read_only(self):
        layout=self.root/'source.layout';contents=self.root/'source.contents'
        layout.write_bytes(self.layout);contents.write_bytes(self.contents)
        before = {p.name:p.read_bytes() for p in self.root.iterdir()}
        command=[sys.executable,str(Path(self.b.__file__)),'audit',str(layout),str(contents)]
        result=subprocess.run(command,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        report=json.loads(result.stdout)
        self.assertEqual(report['counts']['nodes'],860)
        self.assertEqual({p.name:p.read_bytes() for p in self.root.iterdir()},before)

    def test_cli_appends_both_counts_and_preserves_source(self):
        layout=self.root/'source.layout';contents=self.root/'source.contents'
        layout.write_bytes(self.layout);contents.write_bytes(self.contents)
        command=[sys.executable,str(Path(self.b.__file__)),'append',str(layout),str(contents),
                 '--output',str(self.root/'appended'),'--donor','0','--connect-to','0',
                 '--x','256','--y','256','--content','0x23']
        result=subprocess.run(command,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        target=self.root/'appended'
        grid=codec.parse((target/'layout.dat').read_bytes(),(target/'contents.dat').read_bytes())
        self.assertEqual(len(grid.nodes),861)
        self.assertEqual(grid.contents_header[1],861)
        self.assertEqual(layout.read_bytes(),self.layout)
        self.assertEqual(contents.read_bytes(),self.contents)

    def test_cli_stale_count_failure_is_nonzero_and_explained(self):
        import struct
        layout,contents=fixture(861);contents=bytearray(contents);struct.pack_into('<H',contents,2,860)
        a=self.root/'a';b=self.root/'b';a.write_bytes(layout);b.write_bytes(contents)
        result=subprocess.run([sys.executable,str(Path(self.b.__file__)),'audit',str(a),str(b)],
                              capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0)
        self.assertIn('count',result.stderr.lower())

if __name__=='__main__':unittest.main()
