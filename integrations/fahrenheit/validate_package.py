"""Validate the actual SDK output before distributing the Fahrenheit bridge."""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def validate(directory: Path) -> int:
    assembly = 'ffxhooks_fahrenheit'
    required = {assembly + suffix for suffix in (
        '.dll', '.manifest.json', '.deps.json', '.runtimeconfig.json'
    )}
    actual = {path.name for path in directory.iterdir() if path.is_file()}
    if not required <= actual:
        raise ValueError(f'Missing package members: {sorted(required - actual)}')
    forbidden = {name for name in actual if name.lower().endswith('.dll') and name != assembly + '.dll'}
    if forbidden:
        raise ValueError(f'The bridge must not distribute private copies of framework/native DLLs: {sorted(forbidden)}')
    manifest = json.loads((directory / (assembly + '.manifest.json')).read_text(encoding='utf-8-sig'))
    for field in ('Id', 'Name', 'Desc', 'Authors', 'Version', 'Link', 'Flags'):
        if not isinstance(manifest.get(field), str) or not manifest[field].strip():
            raise ValueError(f'Manifest field {field} must be a nonempty string')
    if manifest['Id'] != assembly or manifest['Flags'] != 'NONE':
        raise ValueError('Unexpected bridge identity or unimplemented save-set flag')
    for field in ('Dependencies', 'LoadAfter'):
        if not isinstance(manifest.get(field), list) or any(not isinstance(v, str) for v in manifest[field]):
            raise ValueError(f'Manifest field {field} must be an array of strings')
    for suffix in ('.deps.json', '.runtimeconfig.json'):
        json.loads((directory / (assembly + suffix)).read_text(encoding='utf-8-sig'))
    with (directory / (assembly + '.dll')).open('rb') as binary:
        if binary.read(2) != b'MZ':
            raise ValueError('Managed assembly is not a PE binary')
    print(f'Fahrenheit bridge package: PASS ({len(required)} required members, valid JSON, no bundled framework DLLs)')
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', nargs='?', type=Path, default=Path(__file__).parent / 'bin' / 'publish')
    try:
        raise SystemExit(validate(parser.parse_args().directory))
    except (OSError, ValueError) as error:
        parser.exit(1, f'Fahrenheit bridge package: FAIL: {error}\n')
