"""Jarvis-HOOK: read-only Last Seal data binding; no game/package writes."""
from pathlib import Path
import argparse
import hashlib
import json
import struct

IDS = (100, 911, 912, 913, 914)
COMMANDS = (0x410F, 0x4022, 0x4023, 0x4067, 0x61CB, 0x61DD, 0x61D8, 0x61D6, *range(0x61E1, 0x61EE))


def fnv(data):
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016x}"


def rows(path, wanted):
    data = path.read_bytes()
    if len(data) < 20:
        raise ValueError(f"Truncated kernel: {path}")
    first, previous, last, width = (struct.unpack_from('<H', data, n)[0] for n in (0, 8, 10, 12))
    offset = struct.unpack_from('<I', data, 16)[0]
    if first != 1 or previous or width != 92 or offset < 20 or offset + (last + 1) * width > len(data):
        raise ValueError(f"Invalid native command table: {path}")
    result = {}
    for command in wanted:
        index = command & 4095
        if index > last:
            raise ValueError(f"Missing command {command:04x}")
        row = data[offset + index * 92:offset + (index + 1) * 92]
        result[command] = row[16:].hex()
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--native-kernel', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args(); manifest = json.loads(a.manifest.read_text())
    if not manifest.get('passed'):
        raise ValueError('Verified DATA candidate required')
    stage = Path(manifest['stage']); evidence = []
    for relative, record in manifest['outputs'].items():
        path = stage / relative; data = path.read_bytes()
        if len(data) != record['length'] or hashlib.sha256(data).hexdigest() != record['sha256']:
            raise ValueError(f'DATA candidate drift: {relative}')
    master = stage / 'data/mods/ffx_ps2/ffx/master'
    assets = []
    for mid in IDS:
        path = master / f'jppc/battle/mon/_m{mid:03}/m{mid:03}.bin'
        data = path.read_bytes()
        if len(data) < 36 or struct.unpack_from('<I', data)[0] != 8 or struct.unpack_from('<I', data, 32)[0] != len(data):
            raise ValueError('Invalid monster container')
        stat = struct.unpack_from('<I', data, 12)[0]
        if struct.unpack_from('<H', data, stat+0x72)[0] != mid:
            raise ValueError('Monster resource identity mismatch')
        assets.append({'id': mid, 'size': len(data), 'hash': fnv(data)})
        evidence.append({'path': str(path), 'sha256': hashlib.sha256(data).hexdigest()})
    formation = master / 'jppc/battle/btl/omeg01_10/omeg01_10.bin'; data = formation.read_bytes()
    slots = struct.unpack_from('<I', data, 12)[0]+12
    if struct.unpack_from('<8H', data, slots) != tuple(0x1000+i for i in IDS)+(65535,)*3:
        raise ValueError('The exact five reserved actors are required')
    frame = {'size': len(data), 'hash': fnv(data)}
    evidence.append({'path': str(formation), 'sha256': hashlib.sha256(data).hexdigest()})
    expected = None
    for path in sorted(master.glob('*/battle/kernel/monmagic2.bin')):
        current = rows(path, [command for command in COMMANDS if command >> 12 == 6])
        if expected is not None and current != expected:
            raise ValueError('Localized gameplay payloads disagree')
        expected = current
        evidence.append({'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    if expected is None:
        raise ValueError('Monster magic tables absent')
    expected.update(rows(a.native_kernel, [command for command in COMMANDS if command >> 12 == 4]))
    package = {'schema': 'ffx.spira.omega-last-seal.v1', 'enabled': False,
               'monsters': assets, 'formation': frame,
               'commands': [{'id': command, 'gameplay_hex': expected[command]} for command in COMMANDS]}
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(package, indent=2)+'\n')
    a.output.with_suffix('.sources.json').write_text(json.dumps({'data_manifest': str(a.manifest.resolve()), 'data_manifest_sha256': hashlib.sha256(a.manifest.read_bytes()).hexdigest(), 'resources': evidence}, indent=2)+'\n')
    print(f'Bound five monsters, exact story formation and {len(COMMANDS)} commands; disabled output only.')


if __name__ == '__main__':
    main()
