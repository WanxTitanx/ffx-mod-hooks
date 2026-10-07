#!/usr/bin/env python3
"""Read-only English/legacy localization corpus. Jarvis-HOOK, 2026-10-03.

Uses the existing GPL-3.0 MOD-006 archive reader and Western byte mapping.
Indexed prefix and weapon-name layouts were checked against the GPL-3.0
FFX Editor readers; provenance is recorded in the accompanying corpus report.
No game-ready resource writer is provided here.
"""
from __future__ import annotations

import argparse
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path
import re
import struct

from asset_io import VbfArchive, digest
from pack import ENCODER
from text_layouts import HELP_ONLY

MASTER = 'ffx_ps2/ffx/master/new_uspc/'
LOCKIT = 'ffx_data/gamedata/ps3data/lockit/ffx_loc_kit_ps3_us.bin'
KERNEL_NAMES = {
    'arms_txt.bin', 'a_ability.bin', 'btlend_txt.bin', 'btl_txt.bin',
    'build_txt.bin', 'command.bin', 'config_txt.bin', 'important.bin',
    'item.bin', 'item_txt.bin', 'menu_txt.bin', 'mmain_txt.bin',
    'monmagic1.bin', 'monmagic2.bin', 'monster1.bin', 'monster2.bin',
    'monster3.bin', 'name_txt.bin', 'panel.bin', 'save_txt.bin',
    'sphere.bin', 'status_txt.bin', 'summon_txt.bin', 'w_name.bin',
}
INVERSE = {value: char for char, value in ENCODER.items() if value < 240}
# Visually verified in the historical package's own font atlas, not inferred
# from spelling: it replaced these four umlaut cells with Portuguese tildes.
LEGACY_TILDES = {166: 'Ã', 180: 'Õ', 189: 'ã', 203: 'õ'}
CONTROL_ONE = {9, 10, 11, 18, 19, 25}
TOKEN = re.compile(r'\{(BYTE|CTRL):([0-9A-F]{2})(?::([0-9A-F]{2}))?\}')


def preview(raw: bytes, *, legacy: bool = False) -> dict:
    """Byte-reversible preview; unproven glyph/control bytes stay explicit."""
    parts, controls, opaque = [], [], False
    inverse = INVERSE | LEGACY_TILDES if legacy else INVERSE
    i = 0
    while i < len(raw):
        value = raw[i]
        if value == 3:
            parts.append('\n')
            controls.append('03')
        elif value in CONTROL_ONE and i + 1 < len(raw):
            pair = raw[i:i + 2]
            parts.append(f'{{CTRL:{pair[0]:02X}:{pair[1]:02X}}}')
            controls.append(pair.hex())
            i += 1
        elif value in inverse:
            char = inverse[value]
            parts.append(char * 2 if char in '{}' else char)
        else:
            parts.append(f'{{BYTE:{value:02X}}}')
            controls.append(f'{value:02x}')
            opaque = True
        i += 1
    text = ''.join(parts)
    if restore_preview(text, legacy=legacy) != raw:
        raise ValueError('Preview is not byte-reversible')
    return {'text': text, 'controls': controls, 'opaque': opaque}


def restore_preview(text: str, *, legacy: bool = False) -> bytes:
    out = bytearray()
    encoder = ENCODER | {v: k for k, v in LEGACY_TILDES.items()} if legacy else ENCODER
    i = 0
    while i < len(text):
        if text[i:i + 2] in ('{{', '}}'):
            out.append(encoder[text[i]])
            i += 2
        elif text[i] == '{':
            match = TOKEN.match(text, i)
            if not match:
                raise ValueError('Invalid preview token')
            out.append(int(match[2], 16))
            if match[3] is not None:
                out.append(int(match[3], 16))
            i = match.end()
        else:
            out.append(3 if text[i] == '\n' else encoder[text[i]])
            i += 1
    return bytes(out)


def _row(data: bytes, row: int, slot: int, offset: int, flags: int,
         pool: int, *, null: bool, legacy: bool = False,
         preserve_invalid: bool = False) -> dict:
    absolute = pool + offset
    invalid = not null and not pool <= absolute < len(data)
    if invalid and preserve_invalid:
        return {'row': row, 'slot': slot, 'offset': absolute, 'flags': flags,
                'null_pointer': False, 'invalid_pointer': True, 'raw_hex': '',
                'text': '', 'controls': [], 'opaque': True}
    if null:
        raw = b''
    else:
        if not pool <= absolute < len(data):
            raise ValueError(f'Text pointer outside pool: row {row}, slot {slot}')
        end = data.find(b'\0', absolute)
        if end < 0:
            raise ValueError('Unterminated text')
        raw = data[absolute:end]
    decoded = preview(raw, legacy=legacy)
    return {'row': row, 'slot': slot, 'offset': absolute, 'flags': flags,
            'null_pointer': null, 'invalid_pointer': False, 'raw_hex': raw.hex(), **decoded}


def parse_kernel(data: bytes, name: str, *, legacy: bool = False) -> list[dict]:
    if len(data) < 20:
        raise ValueError('Truncated indexed table')
    low, high, stride, block = struct.unpack_from('<4H', data, 8)
    count, pool = high - low + 1, 20 + block
    if count <= 0 or stride < 8 or count * stride > block or pool >= len(data):
        raise ValueError('Invalid indexed table bounds')
    if name == 'w_name.bin':
        if stride != 72:
            raise ValueError('Unsupported weapon-name stride')
        slots, step = 14, 4
    elif name == 'btl_txt.bin':
        if stride != 8:
            raise ValueError('Unsupported battle-text stride')
        # Each offset is followed by native layout metadata. Treating that
        # halfword as an offset invents valid-looking suffixes from the pool.
        slots, step = 2, 4
    elif name in HELP_ONLY:
        if stride != 16:
            raise ValueError('Unsupported help-only table stride')
        slots, step = 2, 4
    else:
        if stride < 16:
            raise ValueError('Truncated name/description prefix')
        slots, step = 4, 4
    result = []
    for index in range(count):
        for slot in range(slots):
            at = 20 + index * stride + slot * step
            offset = struct.unpack_from('<H', data, at)[0]
            flags = struct.unpack_from('<H', data, at + 2)[0] if step == 4 else 0
            result.append(_row(data, low + index, slot, offset, flags, pool,
                               null=False, legacy=legacy, preserve_invalid=True))
    return result


def parse_field(data: bytes, *, legacy: bool = False) -> list[dict]:
    if not 9 <= len(data) <= 65536:
        raise ValueError('Field size outside u16 format')
    if not any(data):
        return []
    header = struct.unpack_from('<H', data)[0]
    if header < 8 or header % 8 or header >= len(data):
        raise ValueError('Invalid field header')
    result = []
    for row in range(header // 8):
        for slot in range(2):
            offset, flags = struct.unpack_from('<HH', data, row * 8 + slot * 4)
            if offset and offset < header:
                raise ValueError('Field pointer overlaps header')
            result.append(_row(data, row, slot, offset, flags, 0, null=offset == 0,
                               legacy=legacy))
    return result


def parse_macro(data: bytes, *, legacy: bool = False) -> list[dict]:
    if len(data) < 64:
        raise ValueError('Truncated macro dictionary')
    offsets = struct.unpack_from('<16I', data)
    if any(off and not 64 <= off < len(data) for off in offsets):
        raise ValueError('Macro chunk outside container')
    result = []
    for chunk, offset in enumerate(offsets):
        if not offset:
            continue
        end = min((x for x in offsets if x > offset), default=len(data))
        part = data[offset:end]
        if len(part) < 2:
            raise ValueError('Truncated macro chunk')
        header = struct.unpack_from('<H', part)[0]
        if not 4 <= header < len(part) or header % 4:
            raise ValueError('Invalid macro row table')
        for index in range(header // 4):
            for slot in range(2):
                pointer = struct.unpack_from('<H', part, index * 4 + slot * 2)[0]
                if pointer and pointer < header:
                    raise ValueError('Macro pointer overlaps header')
                row = _row(part, chunk * 65536 + index, slot, pointer, 0, 0,
                           null=pointer==0, legacy=legacy, preserve_invalid=True)
                if pointer:row['offset'] += offset
                row.update(chunk=chunk, row_in_chunk=index, macro_id=chunk * 256 + index)
                result.append(row)
    return result


def parse_lockit(data: bytes, *, legacy: bool = False) -> list[dict]:
    if b'\0' in data or b'\r' in data.replace(b'\r\n', b'') or b'\n' in data.replace(b'\r\n', b''):
        raise ValueError('Unsupported lockit line format')
    result, offset = [], 0
    for index, raw in enumerate(data.split(b'\r\n')):
        try:
            utf8 = raw.decode('utf-8','strict')
        except UnicodeError:
            utf8 = None
        result.append({'row': index, 'slot': 0, 'offset': offset, 'flags': 0,
                       'null_pointer': False, 'invalid_pointer': False,
                       'raw_hex': raw.hex(), 'encoding':'mixed_unresolved',
                       'utf8_candidate':utf8, **preview(raw, legacy=legacy)})
        offset += len(raw) + 2
    return result


def family_for(name: str) -> str | None:
    if name == LOCKIT:
        return 'lockit'
    if not name.startswith(MASTER):
        return None
    relative = name[len(MASTER):]
    if relative == 'menu/macrodic.dcp':
        return 'macro'
    if relative == 'menu/menumain.bin':
        return 'field'
    if relative.startswith('battle/kernel/') and Path(name).name in KERNEL_NAMES:
        return 'kernel'
    if relative.endswith('.bin') and relative.startswith(
            ('event/obj_ps3/', 'event/obj_psv/', 'battle/btl/')):
        return 'field'
    return None


def parse(data: bytes, name: str, family: str, *, legacy: bool = False) -> list[dict]:
    if family == 'macro':
        return parse_macro(data, legacy=legacy)
    if family == 'lockit':
        return parse_lockit(data, legacy=legacy)
    return (parse_kernel(data, Path(name).name, legacy=legacy) if family == 'kernel'
            else parse_field(data, legacy=legacy))


def check_destination(vbf: Path, legacy: Path, output: Path) -> None:
    vbf, legacy, output = vbf.resolve(), legacy.resolve(), output.resolve()
    install = vbf.parent.parent if vbf.parent.name.lower() == 'data' else vbf.parent
    if output.exists() or any(output == p or p in output.parents for p in (install, legacy)):
        raise ValueError('Use a new output directory outside both source trees')


def _json(path: Path, value) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def export(vbf: Path, legacy: Path, output: Path,
           legacy_label: str = 'User-supplied translation, version unspecified') -> dict:
    check_destination(vbf, legacy, output)
    output.mkdir(parents=True)
    manifest, records, failures, issues = [], [], [], []
    with VbfArchive(vbf) as archive:
        selected = sorted(n for n in archive.names if family_for(n))
        native_hashes = {}
        for name in selected:
            family = family_for(name)
            source = archive.read(name)
            sha = digest(source)
            native_hashes[name] = sha
            item = {'resource': name, 'source_sha256': sha, 'source_size': len(source),
                    'family': family}
            manifest.append(item)
            try:
                english = parse(source, name, family)
            except ValueError as exc:
                failures.append({'resource': name, 'side': 'english', 'error': str(exc)})
                continue
            old_path = legacy / name
            old_name, alignment = name, 'exact_location'
            if not old_path.is_file() and '/obj_psv/' in name:
                candidate = name.replace('/obj_psv/', '/obj_ps3/')
                if (legacy / candidate).is_file() and candidate in native_hashes and native_hashes[candidate] == sha:
                    old_path, old_name = legacy / candidate, candidate
                    alignment = 'source_identical_platform_variant'
            old_rows = {}
            if old_path.is_file():
                old = old_path.read_bytes()
                item.update(legacy_resource=old_name, legacy_sha256=digest(old), legacy_size=len(old))
                try:
                    old_rows = {(r['row'], r['slot']): r for r in parse(old, old_name, family, legacy=True)}
                    if set(old_rows) != {(r['row'], r['slot']) for r in english}:
                        issues.append({'resource': name, 'kind': 'row_set_mismatch'})
                        alignment = 'row_set_mismatch_manual_alignment_required'
                except ValueError as exc:
                    failures.append({'resource': name, 'side': 'legacy', 'error': str(exc)})
            for en in english:
                old_row = old_rows.get((en['row'], en['slot']))
                key = f'{name}#{en["row"]}:{en["slot"]}'
                flags = []
                if family == 'lockit':
                    flags.append('mixed_encoding_requires_per_row_review')
                if en['invalid_pointer'] or old_row and old_row['invalid_pointer']:
                    flags.append('invalid_text_reference_preserved')
                if old_row:
                    if en['controls'] != old_row['controls']:
                        flags.append('control_sequence_differs')
                    if en['flags'] != old_row['flags']:
                        flags.append('table_flags_differ')
                    if en['text'] and en['text'] == old_row['text']:
                        flags.append('unchanged_from_english')
                    if en['text'] and not old_row['text']:
                        flags.append('legacy_text_empty')
                elif en['text']:
                    flags.append('legacy_entry_missing')
                if en['opaque'] or old_row and old_row['opaque']:
                    flags.append('opaque_bytes_need_codec_review')
                if 'mismatch' in alignment:
                    flags.append('alignment_requires_review')
                record = {'id': key, 'resource': name, 'family': family,
                          'row': en['row'], 'slot': en['slot'], 'english': en,
                          'legacy': old_row, 'legacy_resource': old_name if old_row else None,
                          'alignment': alignment if old_row else 'missing',
                          'proposed_pt_br': None, 'review_status': 'unreviewed', 'qa_flags': flags}
                records.append(record)
        _json(output / 'archive_identity.json', {
            'path': str(vbf), 'size': archive.size, 'header_size': archive.header_size,
            'header_sha256': _header_sha(vbf, archive.header_size),
            'integrity': 'VBF header MD5 verified by existing reader; selected members SHA-256 recorded',
            'legacy_root': str(legacy), 'source_locale': 'English', 'target_locale': 'pt-BR',
            'legacy_version': legacy_label})
    with (output / 'entries.jsonl').open('w', encoding='utf-8') as f:
        for row in records:
            f.write(json.dumps(row, ensure_ascii=False) + '\n')
    with (output / 'review.tsv').open('w', encoding='utf-8', newline='') as f:
        writer = csv.writer(f, delimiter='\t')
        writer.writerow(['id', 'english', 'legacy_pt_br', 'proposed_pt_br', 'review_status', 'qa_flags'])
        for row in records:
            writer.writerow([row['id'], row['english']['text'],
                             row['legacy']['text'] if row['legacy'] else '', '', 'unreviewed',
                             '|'.join(row['qa_flags'])])
    legacy_assets = []
    for path in sorted(legacy.rglob('*')):
        if path.is_file():
            name = path.relative_to(legacy).as_posix()
            legacy_assets.append({'resource': name, 'size': path.stat().st_size,
                                  'sha256': digest(path.read_bytes()),
                                  'parser_family': family_for(name),
                                  'coverage': 'selected' if name in selected else 'inventory_only'})
    summary = {'selected_resources': len(selected), 'entries': len(records),
               'nonempty_english_entries': sum(bool(r['english']['text']) for r in records),
               'aligned_legacy_entries': sum(r['legacy'] is not None for r in records),
               'unique_nonempty_english': len({r['english']['text'] for r in records if r['english']['text']}),
               'qa_flags': dict(Counter(flag for r in records for flag in r['qa_flags'])),
               'legacy_assets': len(legacy_assets), 'parse_failures': len(failures),
               'reviewed_entries': 0, 'complete_localization': False}
    _json(output / 'resource_manifest.json', manifest)
    _json(output / 'legacy_assets.json', legacy_assets)
    _json(output / 'parse_failures.json', failures)
    _json(output / 'resource_issues.json', issues)
    _json(output / 'summary.json', summary)
    return summary


def _header_sha(path: Path, size: int) -> str:
    with path.open('rb') as stream:
        return hashlib.sha256(stream.read(size)).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vbf', type=Path, required=True)
    parser.add_argument('--legacy-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--legacy-label', default='User-supplied translation, version unspecified')
    args = parser.parse_args()
    if not args.vbf.is_file() or not args.legacy_root.is_dir():
        parser.error('Input VBF and extracted legacy directory must exist')
    print(json.dumps(export(args.vbf, args.legacy_root, args.output, args.legacy_label), indent=2))


if __name__ == '__main__':
    main()
