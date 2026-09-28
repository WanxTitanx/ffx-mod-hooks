#!/usr/bin/env python3
"""Create a hash-bound public beta ZIP; never deploys or launches the game."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import posixpath
import re
import struct
import subprocess
from urllib.parse import quote
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--version', default='v0.6.0-beta')
    args = parser.parse_args()
    evidence = json.loads((ROOT / 'docs/releases/v0.6.0-beta-inputs.json').read_text())
    if args.version != evidence['release']:
        parser.error('This release recipe is bound to its recorded version')
    dll = args.dll.read_bytes()
    if len(dll) != evidence['dll_bytes'] or sha(dll) != evidence['dll_sha256']:
        parser.error('DLL does not match the validated release candidate')
    pe = struct.unpack_from('<I', dll, 0x3C)[0]
    if dll[:2] != b'MZ' or dll[pe:pe + 4] != b'PE\0\0' or struct.unpack_from('<H', dll, pe + 4)[0] != 0x14C:
        parser.error('Release requires the recorded PE32/i386 DLL')
    for entry in evidence['production_inputs']:
        path = ROOT / entry['path']
        if not path.is_file() or sha(path.read_bytes()) != entry['sha256']:
            parser.error('Production source drift: ' + entry['path'])
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    if subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=normal'], cwd=ROOT).strip():
        parser.error('Commit the complete reviewed source before packaging')

    files = {'ffx-hooks.dll': dll}
    arcana = ROOT / 'research/mod_008_arcana'
    cards = json.loads((arcana / 'cards.proposed.json').read_text())['cards']
    art = json.loads((arcana / 'assets-manifest.json').read_text())['assets']
    assets = {entry['path']: entry for entry in art}
    for card in cards:
        path = card['asset']
        data = (ROOT / path).read_bytes()
        if sha(data) != assets[path]['sha256']:
            parser.error('Card asset drift: ' + path)
        files['mods/arcana/cards/' + Path(path).name] = data
    for entry in art:
        name = Path(entry['path']).name
        if name in ('tarot-icon-v1.png', 'card-back-v1.png'):
            data = (ROOT / entry['path']).read_bytes()
            if sha(data) != entry['sha256']:
                parser.error('Shared art drift: ' + name)
            files['mods/arcana/shared/' + name] = data
    if len(cards) != 78 or len(files) != 81:
        parser.error('Expected DLL plus 78 cards and two shared runtime images')
    for name in ('LICENSE', 'NOTICE', 'README.md', 'docs/INSTALL.md',
                 'docs/INSTALACAO_PT-BR.md', 'docs/ROADMAP.md'):
        files[name] = (ROOT / name).read_bytes()
    files['examples/ffx-hooks.ini.example'] = (ROOT / 'src/runtime/FfxHooksDll/ffx-hooks.ini').read_bytes()
    files['examples/monster-rewards-v1.tsv.example'] = b'ffx.monster-rewards.v1\n'
    files['examples/Arcana-settings.ini.example'] = b'[arcana]\nenabled=0\ndefault_mode=0\n\n[development]\narcana_full_deck=0\n'
    for name in ('effects.v1.json', 'acquisition-v1.md'):
        files['Arcana-reference/' + name] = (arcana / name).read_bytes()
    for name in ('polyhook2', 'zydis', 'zycore', 'asmjit', 'asmtk', 'minhook'):
        files['third-party-licenses/' + name + '.txt'] = (arcana / 'distribution-licenses' / (name + '.txt')).read_bytes()
    # The binary package links to the exact public source for documents/assets
    # shipped only in the source archive, rather than leaving broken local links.
    for name in list(files):
        if not name.endswith('.md'):
            continue
        def source_link(match: re.Match[str]) -> str:
            target = match.group(1).strip('<>')
            leaf, separator, anchor = target.partition('#')
            if not leaf or re.match(r'^[a-zA-Z]+:', leaf) or leaf.startswith('/'):
                return match.group(0)
            resolved = posixpath.normpath(posixpath.join(posixpath.dirname(name), leaf))
            if resolved in files:
                return match.group(0)
            url = 'https://github.com/WanxTitanx/ffx-mod-hooks/blob/' + revision + '/' + quote(resolved, safe='/')
            return '](' + url + (separator + anchor if separator else '') + ')'
        files[name] = re.sub(r'\]\(([^)]+)\)', source_link, files[name].decode()).encode()
    files['SOURCE.md'] = f'''# FFX Hooks {args.version} — source and binary identity

Jarvis-HOOK. Public source: https://github.com/WanxTitanx/ffx-mod-hooks
Source tag: {args.version}
Public source commit: {revision}
Runtime source checkpoint: {evidence['runtime_source_commit']}
DLL: {len(dll)} bytes, SHA-256 `{sha(dll)}`.
PE resource version: 0.2.0.0; package/tag version: {args.version}.

This is the previously validated Windows x86 Release DLL, also verified under
Proton. Its production inputs are byte-identical to the public source entries
recorded in docs/releases/v0.6.0-beta-inputs.json. Public documentation, packaging
metadata and one test-only whitespace correction are not DLL code changes.
The source archive contains the exact public tagged tree. A new compiler/linker
run may produce a different PE hash; that does not identify this tested binary.

Windows RT0/RT1 and 22 same-binary Proton cases passed for the candidate; the
public source also has portable/sanitized Workshop, Arcana and text checks.
This beta release does not assert complete live RT2 or Production acceptance.
Game executable, native save/PE fixtures, loaders and game-derived mod/translation
packs are not redistributed. Arcana's original selected runtime artwork is included.
Close the game before installation; follow docs/INSTALL.md. Preserve settings and
save sidecars. All editable F8 boolean options default OFF.
'''.encode()
    receipt = dict(schema='ffx.public-release.v1', producer='Jarvis-HOOK',
                   version=args.version, source_repository='https://github.com/WanxTitanx/ffx-mod-hooks',
                   source_commit=revision, runtime_source_commit=evidence['runtime_source_commit'],
                   dll_sha256=sha(dll), dll_bytes=len(dll), live_validation='PENDING_RT2',
                   files={name: dict(bytes=len(data), sha256=sha(data)) for name, data in sorted(files.items())})
    files['release-manifest.json'] = (json.dumps(receipt, indent=2) + '\n').encode()
    files['CHECKSUMS.sha256'] = ''.join(f'{sha(data)}  {name}\n' for name, data in sorted(files.items())).encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(name, date_time=(2026, 9, 28, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)
    with zipfile.ZipFile(args.output) as archive:
        if archive.testzip() is not None or set(archive.namelist()) != set(files):
            raise RuntimeError('Archive integrity/readback failed')
        for name, data in files.items():
            if archive.read(name) != data:
                raise RuntimeError('Archive content mismatch: ' + name)
    print(json.dumps(dict(result='PASS', source_commit=revision, members=len(files),
                         dll_sha256=sha(dll), zip_sha256=sha(args.output.read_bytes()),
                         zip_bytes=args.output.stat().st_size)))


if __name__ == '__main__':
    main()
