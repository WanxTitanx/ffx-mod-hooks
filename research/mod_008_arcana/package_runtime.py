#!/usr/bin/env python3
"""Build a private Arcana candidate ZIP; never copies anything into a game folder."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--dll", type=Path)
    args = parser.parse_args()
    catalog = json.loads((HERE / "cards.proposed.json").read_text())["cards"]
    manifest = json.loads((HERE / "assets-manifest.json").read_text())["assets"]
    by_path = {r["path"]: r for r in manifest}
    files = {}
    for card in catalog:
        path = card["asset"]
        data = (ROOT / path).read_bytes()
        if sha(data) != by_path[path]["sha256"]:
            raise SystemExit("Asset drift: " + path)
        files["mods/arcana/cards/" + Path(path).name] = data
    for row in manifest:
        path = Path(row["path"])
        if path.name not in ("tarot-icon-v1.png", "card-back-v1.png"):
            continue
        data = (ROOT / path).read_bytes()
        if sha(data) != row["sha256"]:
            raise SystemExit("Shared asset drift: " + str(path))
        files["mods/arcana/shared/" + path.name] = data
    if len(files) != 80:
        raise SystemExit("Expected 78 selected cards plus icon and back")
    if args.dll:
        data = args.dll.read_bytes()
        if data[:2] != b"MZ":
            raise SystemExit("Expected a Windows DLL candidate")
        files["ffx-hooks.dll"] = data
    files["Arcana-settings.ini.example"] = b"[arcana]\nenabled=0\ndefault_mode=0\n\n[development]\narcana_full_deck=0\n"
    for name in ("effects.v1.json", "acquisition-v1.md"):
        files["Arcana-reference/" + name] = (HERE / name).read_bytes()
    for name in ("LICENSE", "NOTICE"):
        files[name] = (ROOT / name).read_bytes()
    for name in ("polyhook2", "zydis", "zycore", "asmjit", "asmtk", "minhook"):
        files["third-party-licenses/" + name + ".txt"] = (HERE / "distribution-licenses" / (name + ".txt")).read_bytes()
    files["README-Arcana.txt"] = (
        "Jarvis-HOOK / MOD-008 - Spira: Arcana of the Fayth\n"
        "Standalone FFX Hooks module. Spira Reforge and its data packs are not required.\n"
        "Private validation candidate. RT2 gameplay/visual acceptance is pending.\n"
        "Both settings are OFF by default. Enable Arcana and restart for native Main Menu > Equip > Tarot.\n"
        "Use Development > Arcana: full deck for the explicit instant-deck grant. Turning it off keeps earned cards.\n"
        "Normal acquisition uses temple, sidequest and challenge milestones; locked cards show their requirement.\n"
        "With FFX closed, place ffx-hooks.dll and the mods folder inside the game's modules folder.\n"
        "Merge the example settings into the game's _isolated/ffx-hooks.ini; preserve existing settings.\n"
        "The normal FFX Hooks module loader is required. All Arcana runtime art and card definitions are included.\n"
        "Selected art belongs in mods/arcana beside the DLL. This script does not install or launch the game.\n"
        "Keep the .arcana.v1/.arcana.pending.v1/.arcana.previous.v1 extension files with their matching native save.\n"
    ).encode()
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip())
    identity = {"schema": "jarvis.arcana.runtime-package.v1", "source_commit": revision,
                "source_repository": "https://github.com/WanxTitanx/ffx-hooks",
                "working_tree_dirty": dirty, "live_validation": "PENDING_RT2", "files": {n: sha(d) for n, d in sorted(files.items())}}
    files["Arcana-package.json"] = (json.dumps(identity, indent=2) + "\n").encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", zipfile.ZIP_STORED) as archive:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(name, date_time=(2026, 9, 27, 0, 0, 0))
            archive.writestr(info, data)
    print(f"PASS runtime package: {len(files)} members; sha256={sha(args.output.read_bytes())}; {args.output}")


if __name__ == "__main__":
    main()
