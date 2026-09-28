"""Jarvis-HOOK: full native builds must not lose shared source dependencies."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock
from types import SimpleNamespace
import zipfile
import re

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("mod_runtime_checks", ROOT / "tools/run_mod_runtime_checks.py")
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


class SourcePacketTests(unittest.TestCase):
    def test_complete_native_source_packet_excludes_private_outputs(self):
        with tempfile.TemporaryDirectory(prefix="mod007-packet-") as temporary:
            path = Path(temporary) / "source.zip"
            manifest = RUNNER.snapshot(path, None)
            names = {entry["path"] for entry in manifest}
            required = {
                "src/runtime/FfxDinput8Probe/ffx_probe_block.h",
                "src/runtime/NativeMenuShell/NativeMenuShell.h",
                "src/runtime/BattlePhotoMode/PhotoModeActions.h",
                "src/runtime/FfxHooksDll/hooks/ArenaSceneryCatalog.inc",
                "src/runtime/FfxHooksDll/dllmain.cpp",
                "src/runtime/FfxHooksDll/ffx-hooks.ini",
            }
            self.assertTrue(required <= names, "Missing native build input: " + repr(required - names))
            self.assertFalse(any(Path(name).suffix in {".dll", ".exe", ".bin", ".obj"} for name in names))
            self.assertEqual({name for name in names if Path(name).suffix == ".ini"},
                             {"src/runtime/FfxHooksDll/ffx-hooks.ini"})
            with zipfile.ZipFile(path) as archive:
                self.assertEqual(names | {"source-manifest.json"}, set(archive.namelist()))
            # Check every resolvable project-local quoted include as well as the
            # initial regressions. Windows SDK/third-party system includes are
            # supplied by the explicitly selected toolchain, not this packet.
            for name in names:
                source = ROOT / name
                if source.suffix not in {".h", ".hpp", ".cpp", ".inl", ".inc"}:
                    continue
                for include in re.findall(r'^\s*#\s*include\s+"([^"]+)"', source.read_text(errors="strict"), re.M):
                    options = (source.parent / include, ROOT / "src/runtime/FfxHooksDll" / include)
                    found = next((candidate.resolve() for candidate in options if candidate.is_file()), None)
                    if found is not None and found.is_relative_to(ROOT):
                        self.assertIn(found.relative_to(ROOT).as_posix(), names, "Unpackaged include from " + name)

    def test_default_template_does_not_admit_private_runtime_configurations(self):
        with tempfile.TemporaryDirectory(prefix="mod007-private-packet-") as temporary:
            root = Path(temporary)
            prefix = "src/runtime/FfxHooksDll/"
            names = [prefix + name for name in
                     ("ffx-hooks.ini", "user.ini", "config/ffx-hooks.ini", "private.bin", "hooks/Test.cpp")]
            for name in names:
                file = root / name
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_text("private fixture\n" if name != names[0] else "[spira]\nenabled=0\n")
            listing = SimpleNamespace(stdout="\0".join(names) + "\0")
            with mock.patch.object(RUNNER, "ROOT", root), mock.patch.object(RUNNER, "checked", return_value=listing):
                manifest = RUNNER.snapshot(root / "packet.zip", None)
            self.assertEqual({entry["path"] for entry in manifest},
                             {prefix + "ffx-hooks.ini", prefix + "hooks/Test.cpp"})


if __name__ == "__main__":
    unittest.main()
