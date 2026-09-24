"""Recoverable per-save Workshop sandbox. Original saves are only read.

This is a host prototype, not the game's save callback. Two atomic renames are
not one transaction: a durable journal resolves a known before/after pair.
Unknown writes quarantine the session instead of guessing piece identities.
"""
from __future__ import annotations
import base64
import contextlib
import fcntl
import hashlib
import json
import os
from pathlib import Path
import secrets
import struct
import uuid

from bridge import Core, State, WorkshopError

SAVE_SIZE, GEAR_BASE, ID_BASE, QTY_BASE = 0x6900, 0x44DC, 0x3F0C, 0x410C
MAX_DOCUMENT = 256_000

def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def canonical(value) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("ascii")

def envelope(value) -> bytes:
    return canonical({"payload": value, "sha256": sha(canonical(value))})

def unpack(raw: bytes):
    if len(raw) > MAX_DOCUMENT:
        raise WorkshopError("Oversized document")
    try:
        obj = json.loads(raw)
        if set(obj) != {"payload", "sha256"} or sha(canonical(obj["payload"])) != obj["sha256"]:
            raise ValueError("Integrity mismatch")
        return obj["payload"]
    except (ValueError, TypeError, KeyError) as e:
        raise WorkshopError("Corrupt document; recovery is required") from e

def decode64(value: str) -> bytes:
    try:
        return base64.b64decode(value, validate=True)
    except (ValueError, TypeError) as e:
        raise WorkshopError("Corrupt binary field") from e

def inventory(raw: bytes):
    if len(raw) != SAVE_SIZE:
        raise WorkshopError("Expected a 0x6900-byte PC FFX save copy")
    items, positions = [0] * 112, {}
    for slot in range(256):
        word = struct.unpack_from("<H", raw, ID_BASE + 2 * slot)[0]
        qty = raw[QTY_BASE + slot]
        if 0x2000 <= word < 0x2070:
            item = word - 0x2000
            if item in positions:
                raise WorkshopError("Ambiguous duplicate inventory item")
            positions[item] = slot
            items[item] = qty
    return raw[GEAR_BASE:GEAR_BASE + 4400], items, positions

def render_copy(raw: bytes, state: State) -> bytes:
    _, before, positions = inventory(raw)
    result = bytearray(raw)
    for slot, piece in enumerate(state.pieces):
        result[GEAR_BASE + slot * 22:GEAR_BASE + (slot + 1) * 22] = bytes(piece.native)
    for item, qty in enumerate(state.items):
        if item not in positions:
            if qty != before[item]:
                raise WorkshopError("An absent native material cannot be manufactured")
        else:
            result[QTY_BASE + positions[item]] = qty
    return bytes(result)

class Store:
    def __init__(self, directory: Path, core: Core | None = None, fault=None):
        self.path = directory.resolve()
        self.core = core or Core()
        self.fault = fault or (lambda _: None)
        self.key = sha(os.fsencode(self.path))

    @contextlib.contextmanager
    def locked(self):
        # This prototype host is Linux-only. The core/native harness is Win32.
        self.path.mkdir(parents=True, exist_ok=True)
        with (self.path / ".lock").open("a+b") as f:
            fcntl.flock(f, fcntl.LOCK_EX)
            try:
                yield
            finally:
                fcntl.flock(f, fcntl.LOCK_UN)

    def write(self, name: str, data: bytes):
        target = self.path / name
        temporary = self.path / (name + ".tmp-" + secrets.token_hex(6))
        try:
            with temporary.open("xb") as f:
                f.write(data)
                f.flush()
                os.fsync(f.fileno())
            os.replace(temporary, target)
            self.sync_directory()
        finally:
            temporary.unlink(missing_ok=True)

    def sync_directory(self):
        directory = os.open(self.path, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)

    def read(self, name: str, maximum=MAX_DOCUMENT) -> bytes:
        path = self.path / name
        if path.is_symlink() or not path.is_file() or path.stat().st_size > maximum:
            raise WorkshopError("Missing, linked or oversized workspace file: " + name)
        return path.read_bytes()

    def sidecar(self, raw: bytes, state: State, save_id: str) -> bytes:
        return envelope({"schema": 1, "save_id": save_id, "workspace_key": self.key,
                         "native_sha256": sha(raw), "state": base64.b64encode(bytes(state)).decode("ascii")})

    def pair(self, raw: bytes, meta: bytes):
        obj = unpack(meta)
        if (obj.get("schema") != 1 or obj.get("workspace_key") != self.key
                or obj.get("native_sha256") != sha(raw)):
            raise WorkshopError("Save/sidecar association changed; metadata is quarantined")
        if not isinstance(obj.get("save_id"), str) or len(obj["save_id"]) != 32:
            raise WorkshopError("Invalid save identity")
        state = self.core.decode(decode64(obj["state"]))
        records, items, _ = inventory(raw)
        if records != b"".join(bytes(p.native) for p in state.pieces) or items != list(state.items):
            raise WorkshopError("Native readback disagrees with the extension")
        return state, obj["save_id"]

    def create(self, source: Path, seed: int | None = None):
        original = source.read_bytes()
        records, items, _ = inventory(original)
        state = self.core.import_records(records, items, seed if seed is not None else secrets.randbits(64))
        with self.locked():
            if any((self.path / n).exists() for n in ("native.bin", "sidecar.json", "pending.json", "origin.json")):
                raise WorkshopError("Import requires a new workspace")
            identity = uuid.uuid4().hex
            # Origin is diagnostic only. It is never a write destination.
            self.write("origin.json", envelope({"source": str(source.resolve()), "source_sha256": sha(original), "save_id": identity}))
            self.write("native.bin", original)
            self.write("sidecar.json", self.sidecar(original, state, identity))
            self.pair(self.read("native.bin"), self.read("sidecar.json"))

    def recover(self):
        pending = self.path / "pending.json"
        if not pending.exists():
            return
        journal = unpack(self.read("pending.json"))
        if journal.get("schema") != 1 or journal.get("workspace_key") != self.key:
            raise WorkshopError("Foreign recovery journal")
        pairs = {}
        for phase in ("before", "after"):
            section = journal[phase]
            pairs[phase] = (decode64(section["native"]), decode64(section["sidecar"]))
            self.pair(*pairs[phase])
        before_state, before_id = self.pair(*pairs["before"])
        after_state, after_id = self.pair(*pairs["after"])
        if before_id != after_id or after_state.revision != before_state.revision + 1:
            raise WorkshopError("Invalid journal lineage")
        current = self.read("native.bin"), self.read("sidecar.json")
        if any(current[i] not in (pairs["before"][i], pairs["after"][i]) for i in (0, 1)):
            raise WorkshopError("Foreign change during transaction; automatic recovery refused")
        # Once either after-image exists, finish the durable transaction. If both
        # before-images remain, no debit happened and the prepared plan is aborted.
        target = pairs["before"] if current == pairs["before"] else pairs["after"]
        self.write("native.bin", target[0])
        self.write("sidecar.json", target[1])
        self.pair(self.read("native.bin"), self.read("sidecar.json"))
        pending.unlink()
        self.sync_directory()

    def load(self):
        with self.locked():
            self.recover()
            raw, meta = self.read("native.bin"), self.read("sidecar.json")
            state, save_id = self.pair(raw, meta)
            return raw, meta, state, save_id

    def commit(self, expected_raw: bytes, expected_meta: bytes, state: State):
        with self.locked():
            self.recover()
            before = self.read("native.bin"), self.read("sidecar.json")
            if before != (expected_raw, expected_meta):
                raise WorkshopError("The inventory changed; review a new preview")
            old, save_id = self.pair(*before)
            if state.revision != old.revision + 1:
                raise WorkshopError("Invalid transaction revision")
            self.core.decode(bytes(state))
            new_raw = render_copy(before[0], state)
            after = new_raw, self.sidecar(new_raw, state, save_id)
            self.pair(*after)
            journal = {"schema": 1, "workspace_key": self.key}
            for phase, pair in (("before", before), ("after", after)):
                journal[phase] = {"native": base64.b64encode(pair[0]).decode("ascii"),
                                  "sidecar": base64.b64encode(pair[1]).decode("ascii")}
            self.write("native.bin.bak", before[0])
            self.write("sidecar.json.bak", before[1])
            self.write("pending.json", envelope(journal))
            self.fault("prepared")
            self.write("native.bin", after[0])
            self.fault("native_written")
            self.write("sidecar.json", after[1])
            self.fault("sidecar_written")
            readback = self.read("native.bin"), self.read("sidecar.json")
            if readback != after:
                raise WorkshopError("Transaction readback failed; recovery journal retained")
            self.pair(*readback)
            (self.path / "pending.json").unlink()
            self.sync_directory()

    def recover_sidecar_backup(self):
        with self.locked():
            self.recover()
            raw, backup = self.read("native.bin"), self.read("sidecar.json.bak")
            self.pair(raw, backup)  # Exact native binding; never roll back unrelated gear.
            self.write("sidecar.json", backup)
