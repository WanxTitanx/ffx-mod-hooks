"""Exercise the pinned overlay and reject tampered provider inputs; no game starts."""
from pathlib import Path
import hashlib, importlib.util, json, tempfile, unittest

HERE=Path(__file__).resolve()
ROOT=HERE.parents[4]
BUILDER=HERE.parents[1]/'build_overlay.py'

class OverlayTests(unittest.TestCase):
    def test_pinned_copy_and_tamper_rejection(self):
        self.assertTrue(BUILDER.is_file(),'cooperative provider overlay builder is missing')
        spec=importlib.util.spec_from_file_location('cooperative_overlay',BUILDER)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        reference=ROOT/'work/fahrenheit/upstream'
        before={p:hashlib.sha256((reference/p).read_bytes()).hexdigest() for p in module.PATCHED}
        with tempfile.TemporaryDirectory(prefix='ffx-overlay-',dir=ROOT/'work/fahrenheit-services') as temporary:
            destination=Path(temporary)/'provider'
            module.create_overlay(reference,destination)
            module.verify_overlay(destination)
            self.assertFalse((destination/'artifacts').exists())
            self.assertTrue((destination/'src/core/ffx_cooperative_services.cs').is_file())
            extra=destination/'src/core/ReviewExtra.cs';extra.write_text('internal static class ReviewExtra {}\n')
            with self.assertRaisesRegex(ValueError,'ReviewExtra.cs'):module.verify_overlay(destination)
            extra.unlink()
            alloc=destination/'src/core/alloc.cs';alloc_original=alloc.read_bytes();alloc.write_bytes(alloc_original+b'\n// unreviewed change\n')
            with self.assertRaisesRegex(ValueError,'src/core/alloc.cs'):module.verify_overlay(destination)
            alloc.write_bytes(alloc_original)
            target=destination/'src/core/Directory.Build.targets';target.write_text('<Project><Target Name="Injected" BeforeTargets="Build" /></Project>\n')
            with self.assertRaisesRegex(ValueError,'Directory.Build.targets'):module.verify_overlay(destination)
            target.unlink()
            save=destination/'src/runtime/save_impl.cs';original=save.read_bytes()
            save.write_bytes(original.replace(b'TryWrite(',b'BrokenWrite(',1))
            with self.assertRaises(ValueError):module.verify_overlay(destination)
            save.write_bytes(original)
            marker=destination/module.MARKER;document=json.loads(marker.read_text());document['protocol']=999;marker.write_text(json.dumps(document))
            with self.assertRaises(ValueError):module.verify_overlay(destination)
            with self.assertRaises(FileExistsError):module.create_overlay(reference,destination)
        after={p:hashlib.sha256((reference/p).read_bytes()).hexdigest() for p in module.PATCHED}
        self.assertEqual(before,after,'the unmodified reviewed reference must remain untouched')

if __name__=='__main__':unittest.main()
