"""Jarvis-HOOK: bind approved IDs to read-only Editor kernel fixtures.

Writes only the requested output manifest; never updates the game or Editor.
IDs below were supplied by the Editor handoff, not allocated by this tool.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

COMMANDS = {
    "abyssal_pulse": 0x61B3, "rupture": 0x61B4, "bastion": 0x61B5,
    "venom_hex": 0x61B6, "shattering_blow": 0x61B7,
    "darkja": 0x6174, "aegis": 0x61B8,
}


def bind(paths):
    expected = None
    evidence = []
    for path in paths:
        data = path.read_bytes()
        if len(data) < 20:
            raise ValueError(f"Truncated kernel: {path}")
        first, previous, last, width = (struct.unpack_from("<H", data, n)[0] for n in (0, 8, 10, 12))
        offset = struct.unpack_from("<I", data, 16)[0]
        if (first, previous, width) != (1, 0, 92) or offset < 20 or offset + (last + 1) * width > len(data):
            raise ValueError(f"Invalid native monmagic2 layout: {path}")
        commands = []
        for key, encoded in COMMANDS.items():
            index = encoded & 0xFFF
            if index > last:
                raise ValueError(f"Unresolved {key}: row {index} absent in {path}")
            row = data[offset + index * width:offset + (index + 1) * width]
            commands.append({"key": key, "id": encoded, "gameplay_hex": row[16:].hex()})
        if expected is not None and expected != commands:
            raise ValueError(f"Gameplay/VFX mismatch between locales: {path}")
        expected = commands
        evidence.append({"path": str(path.resolve()), "sha256": hashlib.sha256(data).hexdigest()})
    if expected is None:
        raise ValueError("No command tables provided")
    return {"schema": "ffx.inside-sin.v1", "enabled": False, "commands": expected}, evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest, evidence = bind(args.kernel)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, indent=2) + "\n")
    args.output.with_suffix(".sources.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"Bound {len(COMMANDS)} approved commands across {len(evidence)} tables; activation remains OFF.")


if __name__ == "__main__":
    main()
