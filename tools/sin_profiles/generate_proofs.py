#!/usr/bin/env python3
"""Jarvis-HOOK: reproduce hash-only admission proofs from private S.I.N. inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def generate(manifest_path, pack_path, command_path):
    manifest = json.loads(manifest_path.read_text())
    pack = pack_path.read_bytes()
    if hashlib.sha256(pack).hexdigest().upper() != manifest['packSha256']:
        raise ValueError('Pack does not match the export manifest')
    profiles = manifest['profiles']
    indices=sorted({index for profile in profiles for index in profile.get('requiredCommands',(268,269,270,271))})
    if len(indices)>64:raise ValueError('Dependency mask capacity exceeded')
    positions={index:n for n,index in enumerate(indices)}
    legacy_mask=sum(1<<positions[index] for index in (268,269,270,271))
    if any(profile['curse']>8 for profile in profiles):
        catalog=json.loads((Path(__file__).parent/'catalog.json').read_text())
        expected={(entry['monster'],curse) for entry in catalog['monsters'] for curse in entry['allowed']}
        if len(profiles)!=len(expected) or {(p['monster'],p['curse']) for p in profiles}!=expected:
            raise ValueError('Expanded profiles must exactly implement the reviewed per-monster matrix')
    if pack[:8] != b'SINAI001' or struct.unpack_from('<I', pack, 8)[0] != len(profiles):
        raise ValueError('Invalid profile pack header')
    cursor = 12
    seen = set()
    rows = []
    for profile in profiles:
        monster, curse, reserved, original, size, digest = struct.unpack_from('<HBBQIQ', pack, cursor)
        cursor += 24
        data = pack[cursor:cursor + size]
        cursor += size
        key = (monster, curse)
        if (reserved or key in seen or key != (profile['monster'], profile['curse'])
                or not 0 < size <= 65536 or len(data) != size
                or size != profile['aiLength'] or fnv(data) != digest
                or digest != int(profile['aiHash'], 16)
                or original != int(profile['originalAiHash'], 16)):
            raise ValueError('Invalid or mismatched profile record')
        seen.add(key)
        mask=sum(1<<positions[index] for index in set(profile.get('requiredCommands',(268,269,270,271))))
        rows.append(f"    {{{monster}, {curse}, {profile['originalAiLength']}, {size}, 0x{original:016X}ULL, 0x{digest:016X}ULL, 0x{mask:X}ULL}},")
    if cursor != len(pack):
        raise ValueError('Unexpected trailing pack data')

    commands = command_path.read_bytes()
    segments = struct.unpack_from('<H', commands)[0]
    if not 0 < segments <= 16 or len(commands) < 8 + 12 * segments:
        raise ValueError('Invalid command table header')
    command_rows = []
    for index in indices:
        matches = []
        for segment in range(segments):
            first, last, size, _, offset = struct.unpack_from('<HHHHI', commands, 8 + segment * 12)
            if first > last or last > 4095 or size != 92 or offset < 8 + segments * 12:
                raise ValueError('Invalid command segment')
            if first <= index <= last:
                entry = offset + (index - first) * size
                data = commands[entry:entry + size]
                if len(data) != size:
                    raise ValueError('Truncated command table')
                matches.append(f"    {{{index}, {size}, 0x{fnv(data):016X}ULL}},")
        if len(matches) != 1:
            raise ValueError(f'Command {index} must have exactly one definition')
        command_rows.extend(matches)

    return '\n'.join([
        '#pragma once', '#include <array>', '#include <cstdint>',
        '// Jarvis-HOOK. Hash-only identities for privately generated Editor profiles.',
        '// No monster data or bytecode is embedded in this source file.',
        'namespace FfxHooks::SinAi {',
        'struct Proof {std::uint16_t monster;std::uint8_t curse;std::uint32_t originalSize,size;std::uint64_t originalHash,hash,commands;};',
        f'inline constexpr std::array<Proof,{len(rows)}> kProofs={{{{', *rows, '}};',
        'struct CommandProof {std::uint16_t index;std::uint16_t size;std::uint64_t hash;};',
        f'inline constexpr std::array<CommandProof,{len(command_rows)}> kCommands={{{{', *command_rows, '}};',
        f'inline constexpr std::uint64_t kLegacyCommandMask=0x{legacy_mask:X}ULL;',
        '} // namespace FfxHooks::SinAi', '',
    ])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('manifest', 'pack', 'commands', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    if args.output.resolve() in {p.resolve() for p in (args.manifest, args.pack, args.commands)}:
        parser.error('Output must differ from every input')
    result = generate(args.manifest, args.pack, args.commands)
    args.output.write_text(result)
    print('S.I.N. hash-only proofs generated; pack and command dependencies verified.')
