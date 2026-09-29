"""Offline packaging regression; no SSH, compiler or game execution."""
import importlib.util
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
class PackageTests(unittest.TestCase):
    def test_shared_header_and_source_only(self):
        spec=importlib.util.spec_from_file_location('vm_package',Path(__file__).resolve().parents[1]/'run_vm.py')
        vm=importlib.util.module_from_spec(spec);spec.loader.exec_module(vm)
        with tempfile.TemporaryDirectory() as temporary:
            vm.ROOT=Path(temporary)
            wanted={'src/runtime/FfxHooksDll/dllmain.cpp','src/runtime/FfxDinput8Probe/ffx_probe_block.h','src/runtime/BattlePhotoMode/PhotoModeActions.h'}
            forbidden={'src/runtime/FfxHooksDll/bin/Release/ffx-hooks.dll','src/runtime/FfxDinput8Probe/bin/ffx-probe.dll','src/runtime/FfxHooksDll/vcpkg_installed/dependency.h','.superpowers/private.txt'}
            for name in wanted|forbidden:
                path=vm.ROOT/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('// fixture')
            archive=vm.package()
            with tarfile.open(archive) as tar:self.assertEqual(set(tar.getnames()),wanted)
            manifest=json.loads((archive.parent/'source-manifest.json').read_text())
            self.assertEqual({item['path'] for item in manifest},wanted)
            self.assertTrue(all(len(item['sha256'])==64 for item in manifest))
if __name__=='__main__':unittest.main()
