"""Reject unreviewed Fahrenheit source/build inputs without starting the game."""
from __future__ import annotations

import importlib
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest


HERE = Path(__file__).resolve()
ROOT = HERE.parents[3]
VERIFIER = HERE.parents[1] / "verify_upstream.py"
MANIFEST = HERE.parents[1] / "upstream-manifest.json"
REFERENCE = ROOT / "work/fahrenheit/upstream"
SCRATCH = ROOT / "work/fahrenheit-services"


def load_verifier():
    spec = importlib.util.spec_from_file_location("fahrenheit_source_verifier", VERIFIER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_helper():
    return importlib.import_module("upstream_manifest")


class UpstreamVerifierTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.verifier = load_verifier()
        cls.helper = load_helper()
        if not REFERENCE.is_dir():
            raise unittest.SkipTest(f"reviewed Fahrenheit checkout is missing: {REFERENCE}")
        SCRATCH.mkdir(parents=True, exist_ok=True)

    def copy_reference(self, temporary: str) -> Path:
        destination = Path(temporary) / "fahrenheit"
        shutil.copytree(
            REFERENCE,
            destination,
            ignore=shutil.ignore_patterns(".git", "artifacts", "bin", "obj", "__pycache__"),
        )
        return destination

    def test_rejects_unreviewed_core_source(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ffx-fh-verify-", dir=SCRATCH) as temporary:
            checkout = self.copy_reference(temporary)
            (checkout / "src/core/ReviewExtra.cs").write_text("internal static class ReviewExtra {}\n")
            with self.assertRaisesRegex(ValueError, "ReviewExtra.cs"):
                self.verifier.verify(checkout)

    def test_rejects_modified_original_core_source(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ffx-fh-verify-", dir=SCRATCH) as temporary:
            checkout = self.copy_reference(temporary)
            source = checkout / "src/core/alloc.cs"
            source.write_bytes(source.read_bytes() + b"\n// unreviewed change\n")
            with self.assertRaisesRegex(ValueError, "src/core/alloc.cs"):
                self.verifier.verify(checkout)

    def test_rejects_injected_directory_build_target(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ffx-fh-verify-", dir=SCRATCH) as temporary:
            checkout = self.copy_reference(temporary)
            target = checkout / "src/core/Directory.Build.targets"
            target.write_text("<Project><Target Name=\"Injected\" BeforeTargets=\"Build\" /></Project>\n")
            with self.assertRaisesRegex(ValueError, "Directory.Build.targets"):
                self.verifier.verify(checkout)

    def test_generated_output_name_does_not_hide_nested_core_source(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ffx-fh-verify-", dir=SCRATCH) as temporary:
            checkout = self.copy_reference(temporary)
            source = checkout / "src/core/artifacts/ReviewExtra.cs"
            source.parent.mkdir(parents=True)
            source.write_text("internal static class ReviewExtra {}\n")
            with self.assertRaisesRegex(ValueError, "src/core/artifacts/ReviewExtra.cs"):
                self.verifier.verify(checkout)

    def test_rejects_bin_and_obj_sources_compiled_by_msbuild(self) -> None:
        sources = (
            "src/core/bin/ReviewExtra.cs",
            "src/core/obj/ReviewExtra.cs",
            "src/core/nested/bin/ReviewExtra.cs",
            "src/core/nested/obj/ReviewExtra.cs",
        )
        for relative in sources:
            with self.subTest(relative=relative):
                with tempfile.TemporaryDirectory(
                    prefix="ffx-fh-verify-", dir=SCRATCH
                ) as temporary:
                    checkout = self.copy_reference(temporary)
                    source = checkout / relative
                    source.parent.mkdir(parents=True)
                    source.write_text("internal static class ReviewExtra {}\n")
                    with self.assertRaisesRegex(ValueError, relative):
                        self.verifier.verify(checkout)

    def test_allows_actual_generated_output_roots(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ffx-fh-verify-", dir=SCRATCH
        ) as temporary:
            checkout = self.copy_reference(temporary)
            generated = [
                checkout / "artifacts/obj/Fahrenheit/generated.props",
                checkout / "artifacts/build/Fahrenheit/Debug/net10.0/fh.dll",
                checkout / ".vs/Fahrenheit/v17/.suo",
            ]
            for path in generated:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("generated\n")
            self.assertEqual(0, self.verifier.verify(checkout))

    def test_rejects_rewritten_manifest_hash(self) -> None:
        document = json.loads(MANIFEST.read_text(encoding="utf-8"))
        document["files"]["src/core/alloc.cs"] = "0" * 64
        with tempfile.TemporaryDirectory(prefix="ffx-fh-manifest-", dir=SCRATCH) as temporary:
            rewritten = Path(temporary) / "upstream-manifest.json"
            rewritten.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "manifest content differs"):
                self.helper.load_manifest(rewritten)


if __name__ == "__main__":
    unittest.main()
