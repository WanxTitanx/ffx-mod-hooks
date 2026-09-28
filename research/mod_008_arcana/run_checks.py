#!/usr/bin/env python3
"""Jarvis-HOOK: portable Arcana RT0/isolated-store checks (never launches FFX)."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import re

ROOT = Path(__file__).resolve().parents[2]
DLL = ROOT / "src/runtime/FfxHooksDll"
CASES = {
    "ArcanaStartupRt0": [],
    "ArcanaCoreRt0": ["ArcanaCore"],
    "ArcanaAcquisitionRt0": ["ArcanaCore", "ArcanaAcquisition"],
    "ArcanaUiRt0": ["ArcanaCore", "ArcanaUiCore"],
    "ArcanaNativeEffectsRt0": ["ArcanaCore", "ArcanaNativeEffects"],
    "ArcanaCombatCoreRt0": ["ArcanaCore", "ArcanaCombatCore"],
    "ArcanaStoreRt1": ["ArcanaCore", "ArcanaStore"],
    "NativeSaveEventsRt0": [],
    "NativeSaveProjectionRt0": [],
    "NativeGameplayEventsRt0": [],
}


def startup_adapter(output):
    source = (DLL / "dllmain.cpp").read_text()
    start = source.index("static bool StartNativeTextOutlineGuard(")
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    calls = re.findall(r"const bool textReady=(.*?);", source)
    assert len(calls) == 1, "Require exactly one production Arcana drawing dependency"
    worker = source[source.index("static DWORD WINAPI HooksWorkerThread(LPVOID)"):]
    scan_at = worker.find("FfxHooks::InstallElementHook(")
    arcana_at = worker.find("FfxHooks::Arcana::NativeUi::Start(")
    requested_at = worker.find("if(FfxHooks::Arcana::Runtime::Requested())")
    scan_first = 0 <= scan_at < requested_at < arcana_at
    target = output / "ArcanaStartupAdapter.inc"
    target.write_text(source[start:end] + "\nstatic bool ArcanaEarlyTextReady(uintptr_t arcanaBase,bool combatReady){\n"
                      "(void)arcanaBase; return " + calls[0] + ";\n}\n" +
                      "static constexpr bool kScanBeforeArcana = " + str(scan_first).lower() + ";\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--output", type=Path, default=ROOT / "work/mod008/portable")
    parser.add_argument("--case", choices=CASES)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    startup_adapter(args.output)
    subprocess.run([sys.executable, str(Path(__file__).with_name("compile_catalog.py")), "--check"], check=True)
    subprocess.run([sys.executable, str(Path(__file__).with_name("check_balance.py"))], check=True)
    compiler = os.environ.get("CXX", "g++")
    flags = ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread", "-I" + str(args.output)]
    flags += ["-g", "-O1", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"] if args.sanitize else ["-O2"]
    for name, sources in CASES.items():
        if args.case and args.case != name:
            continue
        executable = args.output / (name + ("-san" if args.sanitize else ""))
        source = [DLL / "tests" / (name + ".cpp")] + [DLL / "hooks" / (s + ".cpp") for s in sources]
        subprocess.run([compiler, *flags, *map(str, source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
    print("PASS Arcana portable checks; native Windows, graphics and live gameplay are separate gates", flush=True)


if __name__ == "__main__":
    main()
