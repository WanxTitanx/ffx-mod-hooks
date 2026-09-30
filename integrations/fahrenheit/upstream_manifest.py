"""Verify a checkout against the complete reviewed Fahrenheit Git tree."""
from __future__ import annotations

from collections.abc import Callable, Collection, Mapping
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re


HERE = Path(__file__).resolve().parent
MANIFEST_PATH = HERE / "upstream-manifest.json"
TRUSTED_ORIGIN = "https://github.com/fahrenheit-crew/fahrenheit.git"
TRUSTED_REVISION = "cdb145d93295c1c6e2bf4766fda5a12877369f54"
TRUSTED_TREE = "4f4faeed98a1f0c808ce3fa08366a6e23b5d6d15"
TRUSTED_LICENSE = "LGPL-3.0-or-later"
TRUSTED_FILE_COUNT = 501
TRUSTED_MANIFEST_SHA256 = "7eb700c21069ea117eee5e19262fd3467f742817ffa3d820f967caafdd59a676"
GENERATED_OUTPUT_ROOTS = frozenset({
    ".git",
    ".vs",
    "artifacts",
})
# Fahrenheit redirects normal outputs to the repository-level artifacts tree.
# Its evaluated Compile glob still admits C# files placed in bin/obj directories.
GENERATED_OUTPUT_DIRECTORY_NAMES = frozenset()
_SHA256 = re.compile(r"[0-9a-f]{64}")
RecoverBytes = Callable[[str, bytes], bytes]


@dataclass(frozen=True)
class UpstreamManifest:
    origin: str
    revision: str
    tree: str
    license: str
    files: Mapping[str, str]


def _reject_duplicate_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
    document: dict[str, object] = {}
    for name, value in pairs:
        if name in document:
            raise ValueError(f"Duplicate key in Fahrenheit manifest: {name}")
        document[name] = value
    return document


def _validate_relative_file(path: str) -> None:
    parsed = PurePosixPath(path)
    if (
        not path
        or "\\" in path
        or parsed.is_absolute()
        or parsed.as_posix() != path
        or any(part in {"", ".", ".."} for part in parsed.parts)
    ):
        raise ValueError(f"Invalid path in Fahrenheit manifest: {path!r}")
    if (
        parsed.parts[0] in GENERATED_OUTPUT_ROOTS
        or any(part in GENERATED_OUTPUT_DIRECTORY_NAMES for part in parsed.parts)
    ):
        raise ValueError(f"Manifest file is hidden by a generated-output rule: {path}")


def load_manifest(path: Path = MANIFEST_PATH) -> UpstreamManifest:
    document = json.loads(
        path.read_text(encoding="utf-8"), object_pairs_hook=_reject_duplicate_keys
    )
    canonical = json.dumps(
        document, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")
    if hashlib.sha256(canonical).hexdigest() != TRUSTED_MANIFEST_SHA256:
        raise ValueError("Fahrenheit manifest content differs from the reviewed official snapshot")
    if not isinstance(document, dict) or document.get("schema") != 1:
        raise ValueError("Unsupported Fahrenheit upstream manifest schema")
    expected_identity = {
        "origin": TRUSTED_ORIGIN,
        "revision": TRUSTED_REVISION,
        "tree": TRUSTED_TREE,
        "license": TRUSTED_LICENSE,
        "file_count": TRUSTED_FILE_COUNT,
    }
    for name, expected in expected_identity.items():
        if document.get(name) != expected:
            raise ValueError(f"Fahrenheit manifest {name} is not the reviewed value")
    generated_roots = document.get("generated_output_roots")
    generated_names = document.get("generated_output_directory_names")
    if not isinstance(generated_roots, list) or set(generated_roots) != GENERATED_OUTPUT_ROOTS:
        raise ValueError("Fahrenheit generated-output roots changed")
    if (
        not isinstance(generated_names, list)
        or set(generated_names) != GENERATED_OUTPUT_DIRECTORY_NAMES
    ):
        raise ValueError("Fahrenheit generated-output exclusions changed")
    files = document.get("files")
    if not isinstance(files, dict) or len(files) != TRUSTED_FILE_COUNT:
        raise ValueError("Fahrenheit manifest is incomplete")
    checked: dict[str, str] = {}
    for name, digest in files.items():
        if not isinstance(name, str) or not isinstance(digest, str):
            raise ValueError("Fahrenheit manifest entries must be string pairs")
        _validate_relative_file(name)
        if not _SHA256.fullmatch(digest):
            raise ValueError(f"Invalid SHA-256 for Fahrenheit file: {name}")
        checked[name] = digest
    return UpstreamManifest(
        origin=document["origin"],
        revision=document["revision"],
        tree=document["tree"],
        license=document["license"],
        files=checked,
    )


def _source_inventory(root: Path) -> tuple[set[str], set[str]]:
    if root.is_symlink() or not root.is_dir():
        raise ValueError(f"Fahrenheit source root is not a regular directory: {root}")
    files: set[str] = set()
    special: set[str] = set()

    def raise_walk_error(error: OSError) -> None:
        raise error

    for current, directory_names, file_names in os.walk(
        root, topdown=True, onerror=raise_walk_error, followlinks=False
    ):
        current_path = Path(current)
        retained_directories = []
        for name in sorted(directory_names):
            path = current_path / name
            relative = path.relative_to(root).as_posix()
            relative_parts = PurePosixPath(relative).parts
            if (
                len(relative_parts) == 1 and name in GENERATED_OUTPUT_ROOTS
            ) or name in GENERATED_OUTPUT_DIRECTORY_NAMES:
                continue
            if path.is_symlink():
                special.add(relative + "/")
                continue
            retained_directories.append(name)
        directory_names[:] = retained_directories
        for name in sorted(file_names):
            path = current_path / name
            relative = path.relative_to(root).as_posix()
            if relative == ".git":
                continue
            if path.is_symlink() or not path.is_file():
                special.add(relative)
                continue
            files.add(relative)
    return files, special


def _format_paths(label: str, paths: Collection[str], limit: int = 20) -> str:
    ordered = sorted(paths)
    shown = ordered[:limit]
    suffix = f" (+{len(ordered) - limit} more)" if len(ordered) > limit else ""
    return f"{label}: {', '.join(shown)}{suffix}"


def verify_manifest(
    root: Path,
    *,
    recover_bytes: RecoverBytes | None = None,
    required_extra_files: Collection[str] = (),
) -> UpstreamManifest:
    manifest = load_manifest()
    root = Path(root)
    extras = set(required_extra_files)
    for name in extras:
        _validate_relative_file(name)
        if name in manifest.files:
            raise ValueError(f"Overlay extra duplicates an upstream file: {name}")

    actual, special = _source_inventory(root)
    expected = set(manifest.files) | extras
    failures: list[str] = []
    unexpected = actual - expected
    missing = expected - actual
    if unexpected:
        failures.append(_format_paths("Unexpected source/build inputs", unexpected))
    if missing:
        failures.append(_format_paths("Missing reviewed source files", missing))
    if special:
        failures.append(_format_paths("Unsupported source filesystem entries", special))

    modified: list[str] = []
    for name, expected_hash in manifest.files.items():
        if name not in actual:
            continue
        try:
            data = (root / name).read_bytes()
            if recover_bytes is not None:
                data = recover_bytes(name, data)
        except (OSError, UnicodeError, ValueError) as error:
            failures.append(f"{name}: cannot verify source bytes: {error}")
            continue
        if hashlib.sha256(data).hexdigest() != expected_hash:
            modified.append(name)
    if modified:
        failures.append(_format_paths("Source differs from official reviewed commit", modified))
    if failures:
        raise ValueError("\n".join(failures))
    return manifest
