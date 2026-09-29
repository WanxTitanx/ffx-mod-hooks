#!/usr/bin/env python3
"""Emit the actual save-session adapter with explicit RT1 platform endpoints."""
from pathlib import Path
import argparse
import hashlib
import json

ROOT = Path(__file__).resolve().parents[2]
MOD = ROOT / "src/runtime/FfxHooksDll"
CASES = (
    "off invalid missing validate profile pin gateway full stop-before stop-during-start "
    "load sort-key grid-key io-start-late preview null-source wrong-thread wrong-capture-thread "
    "nested reset-pending reset stop-during-reset stop-during-readback stop-pending stop-cleanup "
    "captured-after-remove producer-lost gateway-lost replay crc-cleared primary capacity "
    "source-fault copy-exception invalid-crc readback-fault corrupt-first corrupt-last incomplete "
    "stop-during-capture producer-during-readback gateway-during-readback producer-reset gateway-reset"
).split()


def emit(output: Path) -> dict:
    adapter = MOD / "hooks/SeymourSessionRuntime.cpp"
    template = MOD / "tests/SeymourSessionAdapterRt1.inl"
    source = adapter.read_text(encoding="utf-8")
    for name in ("GridTeachHook.h", "RonsoPoolRuntime.h", "RecoveryNative.h", "../shared/Config.h"):
        token = f'#include "{name}"'
        if source.count(token) != 1:
            raise ValueError(f"endpoint include changed: {name}")
        source = source.replace(token, "// Explicit RT1 platform endpoint supplied above.")
    if source.count("GetCurrentThreadId()") != 4:
        raise ValueError("thread-ID endpoint count changed; review its interleaving probes")
    source = source.replace("GetCurrentThreadId()", "Test::ThreadId()")
    body = template.read_text(encoding="utf-8")
    marker = "// INSERT ACTUAL SESSION ADAPTER"
    if body.count(marker) != 1:
        raise ValueError("session template marker changed")
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8") as file:
        file.write(body.replace(marker, source))
    dependencies = [adapter, template, Path(__file__), MOD / "shared/Config.h"]
    dependencies += [MOD / "hooks" / name for name in (
        "SeymourSessionRuntime.h", "SeymourSessionCore.h", "SeymourActiveLoadCore.h",
        "NativeSaveEvents.h", "NativeSaveLoadEvents.h", "RonsoPoolSave.h", "RonsoPoolSave.cpp", "RonsoPoolCore.h"
    )]
    report = {
        "source_hashes": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in dependencies},
        "generated_sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
        "cases": CASES,
        "scope": "actual adapter, observer registry and save core; explicit simulated configuration/profile/copy/producer endpoints",
        "thread_endpoint": "real Windows thread ID (portable surrogate on Linux), with deterministic stop interleaving",
        "native_install_or_game_proven": False,
    }
    with output.with_suffix(".manifest.json").open("x", encoding="utf-8") as file:
        json.dump(report, file, indent=2)
        file.write("\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    print(json.dumps(emit(parser.parse_args().output), indent=2))
