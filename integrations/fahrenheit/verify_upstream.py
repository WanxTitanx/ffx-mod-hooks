"""Check the complete Fahrenheit source tree reviewed for this bridge."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))
from upstream_manifest import verify_manifest  # noqa: E402


def verify(root: Path) -> int:
    reference = json.loads(HERE.joinpath("upstream-reference.json").read_text(encoding="utf-8"))
    manifest = verify_manifest(root)
    failures: list[str] = []
    if reference["revision"] != manifest.revision:
        failures.append("Protocol reference revision differs from the complete manifest")
    runtime_sources = {
        name for name in manifest.files if name.startswith("src/runtime/") and name.endswith(".cs")
    }
    if runtime_sources != set(reference["runtime_sources"]):
        failures.append("Protocol runtime inventory differs from the complete manifest")
    for name, expected in reference["sha256"].items():
        if manifest.files.get(name) != expected:
            failures.append(f"{name}: protocol hash differs from the complete manifest")
    if failures:
        raise ValueError("\n".join(failures))
    print(f"Fahrenheit reference: PASS ({len(manifest.files)} files, revision {manifest.revision})")
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    try:
        raise SystemExit(verify(parser.parse_args().root))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f'Fahrenheit reference: FAIL: {error}\n')
