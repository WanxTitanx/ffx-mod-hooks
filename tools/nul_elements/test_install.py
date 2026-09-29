import json
import tempfile
import unittest
from pathlib import Path
from install import Write, apply, atomic, current

class Installation(unittest.TestCase):
    def test_atomic_backup_and_idempotent_publication(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); old=root/'command.bin'; new=root/'fx/magic.dll'; old.write_bytes(b'old')
            report=apply([Write(old,b'old',b'new'),Write(new,None,b'fx')],root/'backup')
            self.assertEqual(report['state'],'installed')
            self.assertEqual((root/'backup/000.original').read_bytes(),b'old')
            again=apply([Write(old,b'new',b'new'),Write(new,b'fx',b'fx')],root/'again')
            self.assertFalse(any(f['changed'] for f in again['files']))

    def test_drift_refuses_before_any_write(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); file=root/'command.bin'; file.write_bytes(b'other writer')
            with self.assertRaisesRegex(ValueError,'drift'):
                apply([Write(file,b'old',b'new')],root/'backup')
            self.assertFalse((root/'backup').exists())
            self.assertEqual(file.read_bytes(),b'other writer')

    def test_failure_restores_owned_files_and_preserves_newer_writes(self):
        for concurrent in (False,True):
            with self.subTest(concurrent=concurrent),tempfile.TemporaryDirectory() as d:
                root=Path(d); old=root/'command.bin'; new=root/'magic.dll'; last=root/'last.bin'; old.write_bytes(b'old')
                def fail(w):
                    if w.path==last:
                        if concurrent:old.write_bytes(b'concurrent')
                        raise OSError('simulated disk failure')
                    atomic(w)
                with self.assertRaises(OSError):
                    apply([Write(old,b'old',b'new'),Write(new,None,b'fx'),Write(last,None,b'last')],root/'backup',fail)
                self.assertEqual(old.read_bytes(),b'concurrent' if concurrent else b'old')
                self.assertFalse(new.exists())
                report=json.loads((root/'backup/installation.json').read_text())
                self.assertEqual(report['state'],'restore-pending' if concurrent else 'rolled-back')

    def test_symlink_parent_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); (root/'real').mkdir(); (root/'link').symlink_to(root/'real',target_is_directory=True)
            with self.assertRaisesRegex(ValueError,'Symlink'):
                current(root/'link/new.dll')

if __name__=='__main__':unittest.main(verbosity=2)
