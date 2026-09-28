"""Jarvis-HOOK: source preflight fails before publishing incompatible snapshots."""
from pathlib import Path
import copy
import hashlib
import json
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import pack
import reference
from asset_io import AssetError
from test_asset_io import archive_bytes


class PreflightTests(unittest.TestCase):
    def test_portuguese_uppercase_cedilla_has_native_glyph(self):
        self.assertEqual(pack.encode('Çç'), bytes((167, 190)))
        self.assertEqual(pack.decode(bytes((167, 190))), 'Çç')

    def test_path_rejections(self):
        for name in ('../file', '/tmp/file', 'C:/file', 'file:ads', 'a//b', 'a/../b',
                     'NUL.txt', 'aux', 'x/COM1.bin', 'trailing.', 'unicode/ação'):
            with self.subTest(name=name):
                self.assertFalse(reference.safe_path(name))
        self.assertTrue(reference.safe_path('font/base.ftc'))

    def test_resource_family_routes(self):
        for filename in pack.MENU:
            resource = {'family': 'menu', 'request': '/FFX_Data/' + pack.KERNEL + filename}
            self.assertEqual(reference.archive_name(resource, 2), pack.KERNEL + filename)
            resource['family'] = 'events'
            with self.assertRaises(AssetError):
                reference.archive_name(resource, 2)
        name = pack.MASTER + 'event/obj_ps3/ss/ssbt0000/ssbt0000.bin'
        resource = {'family': 'events', 'request': '../../../FFX_Data/' + name}
        self.assertEqual(reference.archive_name(resource, 2), name)
        with self.assertRaises(AssetError):
            reference.archive_name(resource, 1)
        with self.assertRaises(AssetError):
            reference.archive_name({'family': 'menu', 'request': '/FFX_Data/Sound/Voice/JP/a.bin'}, 2)

    def case(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        native = root/'native'; native.mkdir()
        package = root/'pack'; package.mkdir()
        payload = b'Private source fixture'
        name = pack.KERNEL + 'menu_txt.bin'
        vbf = native/'FFX_Data.vbf'
        vbf.write_bytes(archive_bytes(payload, name))
        resource = dict(family='menu', request='/FFX_Data/'+name, path='text/menu.bin',
                        source_size=len(payload), source_sha256=hashlib.sha256(payload).hexdigest())
        manifest = dict(schema_version=2, hook_api=2, executable_sha256=pack.EXE_SHA, resources=[resource])
        return root, vbf, package, manifest, payload

    def test_verified_snapshot_is_published_and_original_is_preserved(self):
        root, vbf, package, manifest, payload = self.case()
        before = vbf.read_bytes()
        (package/'manifest.json').write_text(json.dumps(manifest))
        result = reference.prepare(vbf, package, root/'reference')
        self.assertEqual(result['resources'], 1)
        self.assertEqual((root/'reference/text/menu.bin').read_bytes(), payload)
        self.assertEqual(vbf.read_bytes(), before)
        with self.assertRaises(AssetError):
            reference.prepare(vbf, package, root/'reference')
        self.assertEqual((root/'reference/text/menu.bin').read_bytes(), payload)

    def test_invalid_sources_never_publish_output(self):
        root, vbf, package, baseline, _ = self.case()
        mutations = [
            lambda m: m['resources'][0].update(source_sha256='a'*64),
            lambda m: m['resources'][0].update(source_size=1),
            lambda m: m['resources'][0].update(path='../escape'),
            lambda m: m['resources'].append(copy.deepcopy(m['resources'][0])),
            lambda m: m.update(schema_version=True, hook_api=True),
            lambda m: m.update(hook_api=999),
        ]
        for index, mutate in enumerate(mutations):
            manifest = copy.deepcopy(baseline); mutate(manifest)
            (package/'manifest.json').write_text(json.dumps(manifest))
            output = root/('rejected-'+str(index))
            with self.subTest(index=index), self.assertRaises(AssetError):
                reference.prepare(vbf, package, output)
            self.assertFalse(output.exists())

    def test_reference_destination_cannot_be_the_installation(self):
        root, vbf, package, manifest, _ = self.case()
        (package/'manifest.json').write_text(json.dumps(manifest))
        for output in (vbf.parent/'extra', package/'extra'):
            with self.assertRaises(AssetError):
                reference.prepare(vbf, package, output)
            self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
