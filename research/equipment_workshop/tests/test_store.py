import ctypes
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
from bridge import Core, Request, OPS, WorkshopError
from store import Store, SAVE_SIZE, GEAR_BASE, ID_BASE, QTY_BASE, sha

def synthetic():
    data = bytearray([0xCC] * SAVE_SIZE)
    for i in range(256):
        struct.pack_into("<H", data, ID_BASE + 2*i, 0x2000+i if i<112 else 0xFFFF)
        data[QTY_BASE+i]=99 if i<112 else 0
    data[GEAR_BASE:GEAR_BASE+4400]=bytes(4400)
    for slot in range(2):
        p=GEAR_BASE+slot*22
        data[p+2]=1;data[p+6]=255;data[p+11]=4
        for i in range(4):struct.pack_into("<H",data,p+14+2*i,0x8064)
    return bytes(data)

class StoreTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.source=self.root/'original';self.source.write_bytes(synthetic())
        self.core=Core();self.store=Store(self.root/'workspace',self.core);self.store.create(self.source,123)
    def tearDown(self):self.temp.cleanup()
    def plan(self, op='mode', value=1):
        raw,meta,state,_=self.store.load();r=Request();r.op=OPS[op];r.revision=state.revision;r.pieceId=state.pieces[0].id;r.value=value
        return raw,meta,self.core.preview(state,r).after
    def test_roundtrip(self):
        original=self.source.read_bytes();self.store.commit(*self.plan())
        self.store.commit(*self.plan('refine'))
        raw,_,state,_=self.store.load();self.assertEqual(state.pieces[0].rank,1)
        self.assertEqual(state.items[87],95);self.assertEqual(self.source.read_bytes(),original)
        changed={i for i,(a,b) in enumerate(zip(original,raw)) if a!=b}
        self.assertEqual(changed,{QTY_BASE+87})
    def test_stale_preview(self):
        plan=self.plan();self.store.commit(*plan)
        with self.assertRaises(WorkshopError):self.store.commit(*plan)
        self.assertEqual(self.store.load()[2].revision,1)
    def test_crash_points(self):
        for phase in ('prepared','native_written','sidecar_written'):
            with self.subTest(phase=phase):
                store=Store(self.root/phase,self.core);store.create(self.source,123)
                raw,meta,s,_=store.load();r=Request();r.op=OPS['mode'];r.value=1;r.pieceId=s.pieces[0].id
                state=self.core.preview(s,r).after
                def fault(at):
                    if at==phase:raise OSError('injected interruption')
                store.fault=fault
                with self.assertRaises(OSError):store.commit(raw,meta,state)
                # Mode does not change native bytes. A before native image and an
                # after metadata image still resolve by the complete pair.
                recovered=store.load()[2]
                self.assertEqual(recovered.revision,1 if phase=='sidecar_written' else 0)
                self.assertFalse((store.path/'pending.json').exists())
    def test_paid_recovery_completes_once(self):
        self.store.commit(*self.plan());plan=self.plan('refine')
        def fault(at):
            if at=='native_written':raise OSError('power loss')
        self.store.fault=fault
        with self.assertRaises(OSError):self.store.commit(*plan)
        state=self.store.load()[2];self.assertEqual(state.items[87],95);self.assertEqual(state.pieces[0].rank,1)
        self.assertEqual(self.store.load()[2].items[87],95)
    def test_foreign_write_is_preserved(self):
        self.store.commit(*self.plan());plan=self.plan('refine')
        def fault(at):
            if at=='native_written':raise OSError('power loss')
        self.store.fault=fault
        with self.assertRaises(OSError):self.store.commit(*plan)
        foreign=bytearray((self.store.path/'native.bin').read_bytes());foreign[20]^=1
        (self.store.path/'native.bin').write_bytes(foreign)
        with self.assertRaises(WorkshopError):self.store.load()
        self.assertEqual((self.store.path/'native.bin').read_bytes(),foreign)
    def test_copied_workspace_is_foreign(self):
        target=self.root/'copy';shutil.copytree(self.store.path,target)
        with self.assertRaises(WorkshopError):Store(target,self.core).load()
    def test_unobserved_identical_replacement_cannot_be_inferred(self):
        # No stable ID is derived from a fingerprint. Changed native bytes reject
        # all extension effects; byte-identical unobserved replacement requires
        # the future native lifecycle observer and is explicitly not detectable.
        data=bytearray((self.store.path/'native.bin').read_bytes());data[GEAR_BASE]^=1
        (self.store.path/'native.bin').write_bytes(data)
        with self.assertRaises(WorkshopError):self.store.load()
    def test_corrupt_missing_and_oversized_sidecar(self):
        good=(self.store.path/'sidecar.json').read_bytes()
        for bad in (b'',b'{',bytes(256001)):
            (self.store.path/'sidecar.json').write_bytes(bad)
            with self.assertRaises(WorkshopError):self.store.load()
        (self.store.path/'sidecar.json').write_bytes(good);(self.store.path/'sidecar.json').unlink()
        with self.assertRaises(WorkshopError):self.store.load()
    def test_backup_requires_exact_native_pair(self):
        self.store.commit(*self.plan());(self.store.path/'sidecar.json').write_bytes(b'corrupt')
        self.store.recover_sidecar_backup();self.assertEqual(self.store.load()[2].revision,0)
        self.store.commit(*self.plan());self.store.commit(*self.plan('refine'))
        with self.assertRaises(WorkshopError):self.store.recover_sidecar_backup()
    def test_actual_fixture_if_supplied(self):
        import os
        path=os.environ.get('WORKSHOP_SAVE_FIXTURE')
        if not path:self.skipTest('private fixture not supplied')
        source=Path(path);before=sha(source.read_bytes());store=Store(self.root/'real',self.core);store.create(source,1)
        self.assertEqual(sha(source.read_bytes()),before);self.assertEqual(store.load()[2].version,1)

if __name__=='__main__':unittest.main()
