#!/usr/bin/env python3
"""Install reviewed Nul assets into three explicit roots, with drift checks/backups.

Default is a dry run. This does not install ffxhooks.dll, change configuration,
open a game, modify a save, or grant commands. Close the game and Editor first.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import stat
import tempfile
from dataclasses import dataclass
from pathlib import Path
from commands import SPELLS, build

def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def current(path: Path) -> bytes | None:
    for part in (path, *path.parents):
        if part.is_symlink():
            raise ValueError(f'Symlink target is not admitted: {part}')
    if not path.exists():
        return None
    if not path.is_file():
        raise ValueError(f'Target is not a regular file: {path}')
    return path.read_bytes()

@dataclass
class Write:
    path: Path
    before: bytes | None
    after: bytes

def plan(commands: Path, animations: Path, roots: tuple[Path, Path, Path]) -> list[Write]:
    if len({str(root.resolve()) for root in roots}) != 3:
        raise ValueError('Steam, Spira and Extracted must be three distinct roots')
    command = (commands/'command.bin').read_bytes()
    receipt = json.loads((commands/'command-receipt.json').read_text())
    if sha(command) != receipt['output_sha256'] or build(command)[0] != command:
        raise ValueError('Command stage does not match its reviewed recipe')
    animation_receipt = json.loads((animations/'animation-receipt.json').read_text())
    files = animation_receipt['files']
    expected = {f'magicFiles/FFX/magic_{s.animation:04d}.dll' for s in SPELLS}
    for s in SPELLS:
        prefix = f'data/mods/FFX_Data/GameData/PS3Data/magic/magic_{s.animation:04d}/'
        expected.update(prefix+t['path'] for t in json.loads(Path(__file__).with_name('holy_donor_recipe.json').read_text())['textures'])
    if {f['path'] for f in files} != expected or len(files) != 54:
        raise ValueError('Animation stage has missing, duplicate or unreviewed leaves')
    payload = {}
    for item in files:
        data = (animations/item['path']).read_bytes()
        if sha(data) != item['sha256']:
            raise ValueError('Animation stage drift: '+item['path'])
        payload[item['path']] = data
    result = []
    for index, root in enumerate(roots):
        if not root.is_dir():
            raise ValueError('Missing explicit root: '+str(root))
        kernel = root/('ffx_ps2/ffx/master' if index == 2 else 'data/mods/ffx_ps2/ffx/master')
        for locale in ('jppc','new_uspc'):
            path = kernel/locale/'battle/kernel/command.bin'
            before = current(path)
            if before is None or sha(before) not in (receipt['sha256'], sha(command)):
                raise ValueError('Command bank changed since review: '+str(path))
            result.append(Write(path, before, command))
        for relative, data in payload.items():
            mapped = relative.replace('data/mods/FFX_Data/GameData/PS3Data/magic/',
                                      'ffx_data/gamedata/ps3data/magic/') if index == 2 else relative
            path = root/mapped
            before = current(path)
            if before is not None and before != data:
                raise ValueError('Clone identity is already occupied: '+str(path))
            result.append(Write(path, before, data))
    if len({str(w.path) for w in result}) != len(result):
        raise ValueError('Duplicate target in installation plan')
    return result

def atomic(write: Write) -> None:
    if current(write.path) != write.before:
        raise ValueError('Target drift before publication: '+str(write.path))
    write.path.parent.mkdir(parents=True, exist_ok=True)
    mode = stat.S_IMODE(write.path.stat().st_mode) if write.before is not None else 0o644
    fd, temporary = tempfile.mkstemp(prefix='.nul-install-', dir=write.path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(write.after); stream.flush(); os.fsync(stream.fileno())
        os.chmod(temporary, mode)
        if current(write.path) != write.before:
            raise ValueError('Target drift during publication: '+str(write.path))
        if write.before is None:
            # link is create-only: another writer can never be silently replaced.
            os.link(temporary, write.path)
        else:
            os.replace(temporary, write.path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)

def apply(writes: list[Write], backup: Path, publish=atomic) -> dict:
    for w in writes:
        if current(w.path) != w.before:
            raise ValueError('Target drift before transaction: '+str(w.path))
    backup.mkdir(parents=True, exist_ok=False)
    report = {'producer':'Jarvis-HOOK', 'state':'prepared', 'files':[]}
    for index, w in enumerate(writes):
        entry = {'path':str(w.path), 'before_sha256':sha(w.before) if w.before is not None else None,
                 'after_sha256':sha(w.after), 'bytes':len(w.after), 'changed':w.before != w.after}
        if w.before is not None and w.before != w.after:
            name = f'{index:03d}.original'; (backup/name).write_bytes(w.before); entry['backup'] = name
        report['files'].append(entry)
    receipt = backup/'installation.json'
    receipt.write_text(json.dumps(report, indent=2)+'\n')
    completed = []
    try:
        for w in writes:
            if w.before == w.after:
                continue
            publish(w); completed.append(w)
        if any(current(w.path) != w.after for w in writes):
            raise ValueError('Installed readback mismatch')
    except BaseException as error:
        problems = []
        for w in reversed(completed):
            try:
                if current(w.path) != w.after:
                    raise ValueError('Newer target preserved: '+str(w.path))
                if w.before is None:
                    w.path.unlink()
                else:
                    atomic(Write(w.path, w.after, w.before))
            except Exception as restore_error:
                problems.append(str(restore_error))
        report.update(state='restore-pending' if problems else 'rolled-back', error=str(error), restore_errors=problems)
        receipt.write_text(json.dumps(report, indent=2)+'\n')
        raise
    report['state'] = 'installed'
    receipt.write_text(json.dumps(report, indent=2)+'\n')
    return report

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('commands','animations','steam','spira','extracted'):
        parser.add_argument('--'+name, required=True, type=Path)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--backup', type=Path)
    args = parser.parse_args()
    writes = plan(args.commands.resolve(), args.animations.resolve(),
                  (args.steam.absolute(), args.spira.absolute(), args.extracted.absolute()))
    if args.apply:
        if args.backup is None:
            parser.error('--apply requires a new --backup directory')
        report = apply(writes, args.backup)
        print(report['state'], len(writes), 'verified leaves;', sum(f['changed'] for f in report['files']), 'changed')
    else:
        print('DRY RUN', len(writes), 'leaves;', sum(w.before != w.after for w in writes), 'changes; no writes')

if __name__ == '__main__':
    main()
