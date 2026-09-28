#!/usr/bin/env python3
"""Safely append MOD-002 hook-only auto-ability IDs to pinned local data files.

No gameplay flags are set. The new rows carry names/identity only; a future Hook
must dispatch every effect. Game binaries and backups stay outside Git.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import stat
import struct
import tempfile
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
INVENTORY = HERE / "source_inventory.json"
DEFAULT_IDS = HERE / "default_ids.json"
BACKUP_PARENT = Path("/home/wanderson/.codex/backups")
HEADER_BYTES = 0x14
ABILITY_STRIDE = 0x6C
RATE_STRIDE = 4
FIRST_NEW_ID = 135
LAST_NEW_ID = 147
ASIAN_LOCALES = {"jppc", "new_jppc", "new_chpc", "new_krpc"}

def load_defaults() -> tuple[tuple[tuple[int, str, str], ...], dict[int, str]]:
    manifest = json.loads(DEFAULT_IDS.read_text(encoding="utf-8"))
    if manifest.get("schema") != 1:
        raise ValueError("Unsupported default ID manifest")
    entries = manifest.get("abilities", [])
    if len(entries) != LAST_NEW_ID - FIRST_NEW_ID + 1:
        raise ValueError("Wrong default ID count")
    abilities = tuple((int(row["default_id"]), row["name"], row["gear_type"])
                      for row in entries)
    if [row[0] for row in abilities] != list(range(FIRST_NEW_ID, LAST_NEW_ID + 1)):
        raise ValueError("Default IDs must occupy 135..147 without duplicates")
    if len({row["key"] for row in entries}) != len(entries):
        raise ValueError("Duplicate stable effect key")
    if any(row[2] not in ("weapon", "armor") for row in abilities):
        raise ValueError("Unsupported gear category")
    descriptions = {int(row["default_id"]): row["description"]
                    for row in entries if "description" in row}
    return abilities, descriptions


# A stable effect key may map to a different installed ID in the future Hook
# menu. This authoring tool writes the defaults selected for today's binaries.
ABILITIES, DESCRIPTIONS = load_defaults()
assert [row[0] for row in ABILITIES] == list(range(FIRST_NEW_ID, LAST_NEW_ID + 1))
DEFAULT_DESCRIPTION = "MOD-002 Hook required."


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def get16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def set16(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<H", data, offset, value)


def latin_script(text: str) -> bytes:
    # FFX US single-byte glyphs: digits 0x30..39, uppercase 0x50..69,
    # lowercase 0x70..89; checked against the Editor FfxEncoding.us.cs map.
    result = bytearray()
    for char in text.replace("'", "’"):
        if "0" <= char <= "9":
            result.append(ord(char))
        elif "A" <= char <= "Z":
            result.append(ord(char) + 15)
        elif "a" <= char <= "z":
            result.append(ord(char) + 15)
        elif char == " ":
            result.append(58)
        elif char == "-":
            result.append(71)
        elif char == ".":
            result.append(72)
        elif char == "%":
            result.append(63)
        elif char == "’":
            result.append(65)
        else:
            raise ValueError(f"Unsupported FFX US glyph {char!r}")
    return bytes(result)


def numeric_script(ability_id: int) -> bytes:
    # 0..9 have the same single-byte code in the US and Japanese maps.
    return str(ability_id).encode("ascii")


def script_at(pool: bytes, offset: int) -> bytes:
    if not 0 <= offset < len(pool):
        raise ValueError(f"Text offset {offset} outside pool {len(pool)}")
    end = pool.find(b"\0", offset)
    if end < 0:
        raise ValueError(f"Unterminated string at offset {offset}")
    return pool[offset:end]


def validate_header(data: bytes, stride: int, expected_max: int) -> None:
    if len(data) < HEADER_BYTES:
        raise ValueError("Short table")
    minimum, maximum, row_size, data_size = struct.unpack_from("<HHHH", data, 8)
    if minimum != 0 or maximum != expected_max or row_size != stride:
        raise ValueError(f"Unexpected header: {minimum}..{maximum}, stride={row_size}")
    if data_size != (maximum + 1) * stride:
        raise ValueError("Declared data length differs from row count")
    if HEADER_BYTES + data_size > len(data):
        raise ValueError("Declared data extends beyond EOF")


def append_ability(original: bytes, locale: str, expected_max: int) -> bytes:
    validate_header(original, ABILITY_STRIDE, expected_max)
    if expected_max not in (133, 134):
        raise ValueError(f"Unexpected source last ID {expected_max}")
    old_end = HEADER_BYTES + (expected_max + 1) * ABILITY_STRIDE
    old_data = original[HEADER_BYTES:old_end]
    old_pool = original[old_end:]
    if len(old_pool) >= 0xFFFF:
        raise ValueError("String pool already exceeds 16-bit offsets")

    pool = bytearray(old_pool)
    empty_offset = len(pool)
    pool.append(0)

    def append_text(script: bytes) -> int:
        if not script:
            return empty_offset
        offset = len(pool)
        if offset > 0xFFFF:
            raise ValueError("String offset exceeds 16-bit field")
        pool.extend(script)
        pool.append(0)
        return offset

    template = (old_data[128 * ABILITY_STRIDE:129 * ABILITY_STRIDE]
                if expected_max == 133 else old_data[-ABILITY_STRIDE:])
    keys = tuple(template[pos:pos + 2] for pos in (2, 6, 10, 14))
    rows = bytearray()
    new_names: dict[int, bytes] = {}
    new_descriptions: dict[int, bytes] = {}

    if expected_max == 133:
        # The extracted baseline has no ID 134. A neutral numeric bridge keeps
        # IDs 135..147 aligned with Steam/Spira without copying their custom 134.
        bridge = bytearray(ABILITY_STRIDE)
        bridge_template = old_data[-ABILITY_STRIDE:]
        bridge_keys = tuple(bridge_template[pos:pos + 2]
                            for pos in (2, 6, 10, 14))
        bridge_name = numeric_script(134)
        set16(bridge, 0, append_text(bridge_name))
        set16(bridge, 4, empty_offset)
        set16(bridge, 8, empty_offset)
        set16(bridge, 12, empty_offset)
        for pos, key in zip((2, 6, 10, 14), bridge_keys):
            bridge[pos:pos + 2] = key
        rows.extend(bridge)
        new_names[134] = bridge_name
        new_descriptions[134] = b""

    for ability_id, name, _gear_type in ABILITIES:
        row = bytearray(ABILITY_STRIDE)  # 0x10..0x6B: NO native effects/icons.
        display = (numeric_script(ability_id) if locale in ASIAN_LOCALES
                   else latin_script(name))
        description = (b"" if locale in ASIAN_LOCALES
                       else latin_script(DESCRIPTIONS.get(ability_id, DEFAULT_DESCRIPTION)))
        set16(row, 0, append_text(display))
        set16(row, 4, empty_offset)
        set16(row, 8, append_text(description))
        set16(row, 12, empty_offset)
        for pos, key in zip((2, 6, 10, 14), keys):
            row[pos:pos + 2] = key
        rows.extend(row)
        new_names[ability_id] = display
        new_descriptions[ability_id] = description

    if len(pool) > 0xFFFF:
        raise ValueError("Resulting pool exceeds 16-bit offset range")
    result = bytearray(original[:HEADER_BYTES] + old_data + rows + pool)
    set16(result, 0x0A, LAST_NEW_ID)
    set16(result, 0x0E, (LAST_NEW_ID + 1) * ABILITY_STRIDE)
    validate_header(result, ABILITY_STRIDE, LAST_NEW_ID)
    new_end = HEADER_BYTES + (LAST_NEW_ID + 1) * ABILITY_STRIDE
    if result[HEADER_BYTES:HEADER_BYTES + len(old_data)] != old_data:
        raise AssertionError("Existing ability rows changed")
    if result[new_end:new_end + len(old_pool)] != old_pool:
        raise AssertionError("Existing text pool changed")
    new_pool = result[new_end:]
    for ability_id, expected_script in new_names.items():
        row = result[HEADER_BYTES + ability_id * ABILITY_STRIDE:
                     HEADER_BYTES + (ability_id + 1) * ABILITY_STRIDE]
        if any(row[0x10:]):
            raise AssertionError(f"Hook-only ability {ability_id} has native effects")
        if script_at(new_pool, get16(row, 0)) != expected_script:
            raise AssertionError(f"Wrong name for ID {ability_id}")
        if script_at(new_pool, get16(row, 8)) != new_descriptions[ability_id]:
            raise AssertionError(f"Wrong description for ID {ability_id}")
        if script_at(new_pool, get16(row, 4)) or script_at(new_pool, get16(row, 12)):
            raise AssertionError(f"Unexpected auxiliary text for ID {ability_id}")
    return bytes(result)


def append_rates(original: bytes, expected_max: int) -> bytes:
    validate_header(original, RATE_STRIDE, expected_max)
    if expected_max not in (133, 134):
        raise ValueError(f"Unexpected source rate last ID {expected_max}")
    old_end = HEADER_BYTES + (expected_max + 1) * RATE_STRIDE
    old_data = original[HEADER_BYTES:old_end]
    prefix = original[:HEADER_BYTES]
    suffix = original[old_end:]
    # One neutral bridge rate when extracted ends at 133, then 13 new zero rates.
    count = LAST_NEW_ID - expected_max
    result = bytearray(prefix + old_data + (b"\0" * (count * RATE_STRIDE)) + suffix)
    set16(result, 0x0A, LAST_NEW_ID)
    set16(result, 0x0E, (LAST_NEW_ID + 1) * RATE_STRIDE)
    validate_header(result, RATE_STRIDE, LAST_NEW_ID)
    if result[HEADER_BYTES:HEADER_BYTES + len(old_data)] != old_data:
        raise AssertionError("Existing price rows changed")
    if any(result[HEADER_BYTES + len(old_data):HEADER_BYTES + (LAST_NEW_ID + 1) * RATE_STRIDE]):
        raise AssertionError("New price rows are not zero")
    return bytes(result)


def read_inventory() -> list[dict]:
    inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
    if inventory.get("schema") != 1:
        raise ValueError("Unsupported source inventory schema")
    targets = inventory["targets"]
    abilities = [r for r in targets if r["kind"] == "a_ability"]
    rates = [r for r in targets if r["kind"] == "arms_rate"]
    if len(abilities) != 13 or len(rates) != 12:
        raise ValueError("Expected 13 ability and 12 price files")
    for record in targets:
        path = Path(record["path"])
        if not path.is_file() or path.is_symlink():
            raise ValueError(f"Missing or symlink target: {path}")
        current = path.read_bytes()
        if len(current) != record["size"] or sha256(current) != record["sha256"]:
            raise ValueError(f"Source changed since inventory capture: {path}")
    return targets


def make_plan() -> list[dict]:
    plan = []
    for record in read_inventory():
        path = Path(record["path"])
        before = path.read_bytes()
        if record["kind"] == "a_ability":
            after = append_ability(before, record["locale"], record["max_id"])
        else:
            after = append_rates(before, record["max_id"])
        plan.append({
            "path": path, "group": record["group"], "locale": record["locale"],
            "kind": record["kind"], "before": before, "after": after,
            "before_sha256": sha256(before), "after_sha256": sha256(after),
            "before_size": len(before), "after_size": len(after),
        })
    return plan


def atomic_write(path: Path, data: bytes) -> None:
    original_mode = stat.S_IMODE(path.stat().st_mode)
    fd, temporary = tempfile.mkstemp(prefix=".mod002-", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, original_mode)
        os.replace(temporary, path)
        try:
            directory = os.open(path.parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
        except OSError:
            pass  # Some mounted filesystems do not support directory fsync.
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def create_backup(plan: list[dict], parent: Path) -> Path:
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    backup = parent / f"mod002-autoabilities-{timestamp}"
    backup.mkdir(parents=True, mode=0o700, exist_ok=False)
    records = []
    for entry in plan:
        relative = Path(entry["group"]) / entry["locale"] / f'{entry["kind"]}.bin'
        target = backup / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("xb") as stream:
            stream.write(entry["before"])
            stream.flush()
            os.fsync(stream.fileno())
        if sha256(target.read_bytes()) != entry["before_sha256"]:
            raise IOError(f"Backup verification failed: {target}")
        records.append({
            "path": str(entry["path"]), "backup": str(relative),
            "group": entry["group"], "locale": entry["locale"], "kind": entry["kind"],
            "before_sha256": entry["before_sha256"],
            "after_sha256": entry["after_sha256"],
            "before_size": entry["before_size"], "after_size": entry["after_size"],
        })
    manifest = {
        "schema": 1, "created_utc": timestamp, "ability_ids": [r[0] for r in ABILITIES],
        "records": records,
    }
    with (backup / "manifest.json").open("x", encoding="utf-8") as stream:
        json.dump(manifest, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    return backup


def verify_manifest(manifest_path: Path) -> dict:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema") != 1 or len(manifest.get("records", [])) != 25:
        raise ValueError("Invalid deployment manifest")
    for record in manifest["records"]:
        path = Path(record["path"])
        backup = manifest_path.parent / record["backup"]
        before = backup.read_bytes()
        if sha256(before) != record["before_sha256"]:
            raise ValueError(f"Backup corrupted: {backup}")
        after = (append_ability(before, record["locale"],
                                get16(before, 0x0A))
                 if record["kind"] == "a_ability"
                 else append_rates(before, get16(before, 0x0A)))
        if sha256(after) != record["after_sha256"]:
            raise ValueError(f"Planned output drifted: {path}")
        current = path.read_bytes()
        if len(current) != record["after_size"] or sha256(current) != record["after_sha256"]:
            raise ValueError(f"Target readback differs: {path}")
    return manifest


def rollback(manifest_path: Path) -> None:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema") != 1 or len(manifest.get("records", [])) != 25:
        raise ValueError("Invalid rollback manifest")
    restored_bytes: dict[Path, bytes] = {}
    for record in manifest["records"]:
        path = Path(record["path"])
        data = (manifest_path.parent / record["backup"]).read_bytes()
        if sha256(data) != record["before_sha256"]:
            raise ValueError(f"Backup corrupted: {record['backup']}")
        restored_bytes[path] = data
        current_sha = sha256(path.read_bytes())
        if current_sha not in (record["before_sha256"], record["after_sha256"]):
            raise ValueError(f"Refusing to overwrite independently changed file: {path}")
    for record in manifest["records"]:
        path = Path(record["path"])
        if sha256(path.read_bytes()) == record["before_sha256"]:
            continue
        atomic_write(path, restored_bytes[path])
        if sha256(path.read_bytes()) != record["before_sha256"]:
            raise IOError(f"Rollback readback failed: {path}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--apply", action="store_true")
    modes.add_argument("--verify", type=Path, metavar="BACKUP_MANIFEST")
    modes.add_argument("--rollback", type=Path, metavar="BACKUP_MANIFEST")
    parser.add_argument("--backup-parent", type=Path, default=BACKUP_PARENT)
    args = parser.parse_args()
    if args.verify:
        manifest = verify_manifest(args.verify)
        print(f"VERIFIED {len(manifest['records'])} installed files against {args.verify}")
        return
    if args.rollback:
        rollback(args.rollback)
        print(f"ROLLED_BACK 25 files using {args.rollback}")
        return

    plan = make_plan()
    summary = {
        "mode": "apply" if args.apply else "dry_run",
        "ability_ids": [row[0] for row in ABILITIES],
        "file_count": len(plan),
        "groups": {group: sum(r["group"] == group for r in plan)
                   for group in ("steam", "extracted", "spira_reforge")},
        "planned": [{"group": r["group"], "locale": r["locale"],
                     "kind": r["kind"], "before_sha256": r["before_sha256"],
                     "after_sha256": r["after_sha256"],
                     "before_size": r["before_size"], "after_size": r["after_size"]}
                    for r in plan],
    }
    if not args.apply:
        print(json.dumps(summary, ensure_ascii=False, indent=2))
        return
    backup = create_backup(plan, args.backup_parent)
    changed = []
    try:
        for entry in plan:
            if sha256(entry["path"].read_bytes()) != entry["before_sha256"]:
                raise ValueError(f"Target changed between planning and write: {entry['path']}")
            atomic_write(entry["path"], entry["after"])
            changed.append(entry)
            if sha256(entry["path"].read_bytes()) != entry["after_sha256"]:
                raise IOError(f"Write readback failed: {entry['path']}")
        verify_manifest(backup / "manifest.json")
    except Exception:
        for entry in reversed(changed):
            path = entry["path"]
            if sha256(path.read_bytes()) == entry["after_sha256"]:
                atomic_write(path, entry["before"])
        raise
    summary["backup_manifest"] = str(backup / "manifest.json")
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
