"""Compile the Windows runner's actual source packet without using SSH."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("workshop_run_checks", ROOT / "run_checks.py")
CHECKS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKS)
COMPILER = shutil.which("i686-w64-mingw32-g++")


class PacketCompiled(Exception):
    """Stop after validating the packet, before any remote build or download."""


class WindowsPacketTests(unittest.TestCase):
    @unittest.skipUnless(COMPILER, "MinGW x86 compiler is required for the Windows packet check")
    def test_transferred_packet_compiles_without_checkout_headers(self):
        with tempfile.TemporaryDirectory(prefix="workshop-packet-") as temporary:
            directory = Path(temporary)
            packet = directory / "packet"
            packet.mkdir()
            build = directory / "build"
            build.mkdir()
            fixture = directory / "fixture.bin"
            fixture.write_bytes(b"packet test; not executed")
            args = argparse.Namespace(windows="unit-test-only", pe=fixture,
                                      kernel=fixture, snapshot=None)

            def transfer(command, **kwargs):
                self.assertEqual(command[:2], ["scp", "-q"])
                self.assertEqual(len(command), 4)
                self.assertTrue(command[3].startswith("unit-test-only:C:/VMTasks/"))
                destination = packet / PurePosixPath(command[3].split(":", 1)[1]).name
                shutil.copyfile(command[2], destination)
                return ""

            def powershell(host, code):
                self.assertEqual(host, "unit-test-only")
                if code.startswith("New-Item "):
                    return ""
                if code.startswith("(Get-FileHash "):
                    filename = PurePosixPath(code.split("'")[1]).name
                    return hashlib.sha256((packet / filename).read_bytes()).hexdigest()
                if code.startswith("Remove-Item "):
                    return "WORKSHOP_TEMP_REMOVED"
                self.assertTrue(code.startswith("& "), code)
                compiled = subprocess.run(
                    [COMPILER, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I.",
                     "-fsyntax-only", "workshop.cpp", "effects.cpp", "lifecycle.cpp",
                     "test_workshop.cpp", "native_effects.cpp"],
                    cwd=packet, capture_output=True, text=True, timeout=60)
                self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
                raise PacketCompiled

            with mock.patch.object(CHECKS, "BUILD", build), \
                    mock.patch.object(CHECKS, "run", side_effect=transfer), \
                    mock.patch.object(CHECKS, "ps", side_effect=powershell):
                with self.assertRaises(PacketCompiled):
                    CHECKS.windows(args)


if __name__ == "__main__":
    unittest.main()
