#!/usr/bin/env python3
"""Build MOD-006 v2 text packages without modifying native assets. Jarvis-HOOK."""
from __future__ import annotations

import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import struct
import sys
import tempfile

from asset_io import AssetError, VbfArchive, digest, ftc_layout, u16
from font_pack import build_font
from inspect_assets import METRICS, ATLAS_ROOT, ATLAS_NAMES

EXE_SHA = '78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced'
MASTER = 'ffx_ps2/ffx/master/new_uspc/'
KERNEL = MASTER + 'battle/kernel/'
MENU = ('menu_txt.bin', 'mmain_txt.bin', 'config_txt.bin', 'save_txt.bin')
BATTLE = ('arms_txt.bin', 'btl_txt.bin', 'btlend_txt.bin', 'build_txt.bin',
          'item_txt.bin', 'name_txt.bin', 'status_txt.bin', 'summon_txt.bin')
EVENT = re.compile(r'event/(obj_ps3|obj_psv)/[a-z0-9]{2}/(?P<id>[a-z][a-z0-9_-]{0,31})/(?P=id)\.bin\Z')
ASCII = "0123456789 !\"#$%&'()*+,-./:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"
ENCODER = {char: 48 + index for index, char in enumerate(ASCII)}
ENCODER.update(dict(zip('ÀÁÂÄ', range(163, 167))))
ENCODER.update(dict(zip('ÈÉÊËÌÍÎÏÑÒÓÔÖÙÚÛÜßàáâäçèéêëìíîïñòóôöùúûü', range(168, 208))))
ENCODER.update({'Ç': 167, 'ã': 242, 'õ': 243, 'Ã': 244, 'Õ': 245, '…': 211,
                '’': 213, '—': 150, '“': 148, '”': 149})
TOKENS = {'TIDUS': bytes((19, 48)), 'YUNA': bytes((19, 49)),
          'WARN': bytes((10, 67)), 'NORMAL': bytes((10, 65))}
MAX_RECIPE = 1024 * 1024
MAX_WORKING_SET = 256 * 1024 * 1024


def control_allowed(code: int, argument: int) -> bool:
    if code in (9, 11, 25):
        return 48 <= argument <= 255
    if code == 10:
        return argument in (65, 67, 82, 177)
    if code == 18:
        return 48 <= argument <= 57
    if code == 19:
        return 48 <= argument <= 65
    return False


def encode(text: str) -> bytes:
    """Encode supported literals and explicit controls; no silent substitution."""
    if not isinstance(text, str):
        raise AssetError('Translation must be a string')
    result = bytearray()
    at = 0
    while at < len(text):
        char = text[at]
        if char == '\n':
            result.append(3)
        elif text[at:at + 2] in ('{{', '}}'):
            result.append(ENCODER[char])
            at += 1
        elif char == '{':
            end = text.find('}', at + 1)
            token = text[at + 1:end] if end >= 0 else ''
            if token in TOKENS:
                result += TOKENS[token]
            elif re.fullmatch(r'VAR:[0-9]', token):
                result += bytes((18, 48 + int(token[4:])))
            elif re.fullmatch(r'CTRL:[0-9A-F]{2}:[0-9A-F]{2}', token):
                code, argument = (int(value, 16) for value in token[5:].split(':'))
                if not control_allowed(code, argument):
                    raise AssetError('Unsupported native control or argument')
                result += bytes((code, argument))
            else:
                raise AssetError('Unknown or unterminated text token')
            at = end
        else:
            value = ENCODER.get(char)
            if value is None:
                raise AssetError(f'Unmapped character U+{ord(char):04X}')
            result.append(value)
        at += 1
        if len(result) > 2048:
            raise AssetError('Encoded string exceeds 2048 bytes')
    return bytes(result)


def decode(script: bytes) -> str:
    """Round-trip preview for supported bytes. Opaque scripts remain uneditable."""
    inverse = {value: char for char, value in ENCODER.items()}
    out = []
    at = 0
    while at < len(script):
        code = script[at]
        at += 1
        if code == 3:
            out.append('\n')
        elif code in (9, 10, 11, 18, 19, 25):
            if at == len(script) or not control_allowed(code, script[at]):
                raise AssetError('Unsupported native control in preview')
            out.append(f'{{CTRL:{code:02X}:{script[at]:02X}}}')
            at += 1
        elif code in inverse:
            char = inverse[code]
            out.append(char * 2 if char in '{}' else char)
        else:
            raise AssetError(f'Native byte {code:02X} has no editable preview')
    return ''.join(out)


def kernel_layout(source: bytes):
    if len(source) < 21:
        raise AssetError('Truncated kernel table')
    low, high, stride, block = struct.unpack_from('<HHHH', source, 8)
    if high < low or stride not in (8, 16) or (high - low + 1) * stride > block or 20 + block >= len(source):
        raise AssetError('Invalid kernel table bounds')
    return low, high, stride, 20 + block


def script_at(data: bytes, at: int) -> bytes:
    if not 0 <= at < len(data):
        raise AssetError('Text offset is out of bounds')
    end = data.find(b'\0', at)
    if end < 0:
        raise AssetError('Text terminator is missing')
    return data[at:end]


def checked_edits(edits, low: int, high: int, slots: int):
    if not isinstance(edits, list):
        raise AssetError('Text edits must be an array')
    used = set()
    for edit in edits:
        if not isinstance(edit, dict) or set(edit) != {'row', 'slot', 'text'}:
            raise AssetError('Edit requires exactly row, slot, text')
        row, slot = edit['row'], edit['slot']
        if type(row) is not int or type(slot) is not int or not low <= row <= high or not 0 <= slot < slots:
            raise AssetError('Invalid row or slot')
        if (row, slot) in used:
            raise AssetError('Duplicate text edit')
        used.add((row, slot))
        yield edit, encode(edit['text'])


def append_kernel(source: bytes, edits) -> bytes:
    low, high, stride, pool = kernel_layout(source)
    output = bytearray(source)
    for edit, replacement in checked_edits(edits, low, high, 4):
        at = 20 + (edit['row'] - low) * stride + edit['slot'] * (2 if stride == 8 else 4)
        before = script_at(source, pool + u16(source, at))
        if replacement == before:
            continue
        offset = len(output) - pool
        if offset + len(replacement) + 1 > 65536:
            raise AssetError('Translation exceeds u16 text pool')
        struct.pack_into('<H', output, at, offset)
        output += replacement + b'\0'
    return bytes(output)


def field_layout(source: bytes) -> int:
    if not 9 <= len(source) <= 65536:
        raise AssetError('Field table exceeds the native u16 address space')
    header = u16(source, 0)
    if header < 8 or header % 8 or header >= len(source):
        raise AssetError('Invalid implicit field header')
    return header


def rebuild_field(source: bytes, edits) -> bytes:
    """Keep the implicit first offset/count and flags; repoint both string variants."""
    header = field_layout(source)
    translated = {(edit['row'], edit['slot']): value
                  for edit, value in checked_edits(edits, 0, header // 8 - 1, 2)}
    if not translated:
        return source
    strings = []
    changed = False
    for row in range(header // 8):
        for slot in range(2):
            at = row * 8 + slot * 4
            old_offset = u16(source, at)
            if old_offset < header:
                raise AssetError('Opaque/null field pointer: this table is preserve-only')
            original = script_at(source, old_offset)
            value = translated.get((row, slot), original)
            changed |= value != original
            strings.append(value)
    if not changed:
        return source
    result = bytearray(source[:header])
    known = {}
    for index, value in enumerate(strings):
        if value not in known:
            if len(result) + len(value) + 1 > 65536:
                raise AssetError('Translation exceeds u16 field table')
            known[value] = len(result)
            result += value + b'\0'
        struct.pack_into('<H', result, index * 4, known[value])
    return bytes(result)


def measure(script: bytes, widths: bytes):
    if len(script) > 2048 or len(widths) < 208:
        raise AssetError('Script or glyph metrics exceed the supported bounds')
    controls = bytearray()
    lines = [0]
    at = 0
    while at < len(script):
        code = script[at]
        at += 1
        if code == 3:
            controls.append(code)
            lines.append(0)
        elif code in (9, 10, 11, 18, 19, 25):
            if at == len(script) or not control_allowed(code, script[at]):
                raise AssetError('Unsupported or truncated control')
            controls += bytes((code, script[at]))
            at += 1
        elif code < 48 or code in (240, 241) or code > 245 or not widths[code - 48]:
            raise AssetError('Unsupported glyph or control in edited string')
        else:
            lines[-1] += widths[code - 48]
    return bytes(controls), lines


def validate_edits(source: bytes, target: bytes, edits, widths: bytes, *, field=False):
    if field:
        field_layout(source)
        field_layout(target)
    else:
        low, _, stride, pool = kernel_layout(source)
    audit = []
    for edit in edits:
        if field:
            at = edit['row'] * 8 + edit['slot'] * 4
            before = script_at(source, u16(source, at))
            after = script_at(target, u16(target, at))
        else:
            at = 20 + (edit['row'] - low) * stride + edit['slot'] * (2 if stride == 8 else 4)
            before = script_at(source, pool + u16(source, at))
            after = script_at(target, pool + u16(target, at))
        if before == after:
            audit.append({**edit, 'changed': False})
            continue
        if len(after) > len(before):
            raise AssetError(f'Encoded byte capacity exceeded at {edit["row"]}:{edit["slot"]}')
        old_control, old_width = measure(before, widths)
        new_control, new_width = measure(after, widths)
        if old_control != new_control or len(old_width) != len(new_width):
            raise AssetError(f"Controls changed at {edit['row']}:{edit['slot']}")
        if any(new > old for new, old in zip(new_width, old_width)):
            raise AssetError(f"Width overflow at {edit['row']}:{edit['slot']}: {new_width} > {old_width}")
        audit.append({**edit, 'changed': True, 'before_width': old_width,
                      'after_width': new_width, 'controls': new_control.hex(), 'encoded': after.hex()})
    return audit


def load_recipe(path: Path):
    def unique(pairs):
        obj = {}
        for key, value in pairs:
            if key in obj:
                raise AssetError(f'Duplicate JSON key: {key}')
            obj[key] = value
        return obj
    with path.open('rb') as stream:
        data = stream.read(MAX_RECIPE + 1)
    if len(data) > MAX_RECIPE:
        raise AssetError('Recipe exceeds 1 MiB')
    return json.loads(data.decode('utf-8'), object_pairs_hook=unique)


def publish_directory(source: Path, destination: Path):
    """An existing destination is never replaced, including a racing empty directory."""
    if os.name == 'nt':
        os.rename(source, destination)  # Windows rename fails if the destination exists.
        return
    if sys.platform.startswith('linux'):
        library = ctypes.CDLL(None, use_errno=True)
        rename = getattr(library, 'renameat2', None)
        if rename is None:
            raise AssetError('Atomic no-replace publication is unavailable on this platform')
        rename.argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint)
        rename.restype = ctypes.c_int
        if rename(-100, os.fsencode(source), -100, os.fsencode(destination), 1):
            code = ctypes.get_errno()
            raise OSError(code, os.strerror(code), str(destination))
        return
    raise AssetError('Atomic no-replace publication requires Windows or Linux')


def build(vbf: Path, output: Path, edits, reference: Path | None = None):
    vbf = vbf.resolve(strict=True)
    output = output.resolve()
    install = vbf.parent.parent if vbf.parent.name.lower() == 'data' and (vbf.parent.parent / 'FFX.exe').is_file() else vbf.parent
    if output.exists() or output == install or install in output.parents:
        raise AssetError('Use a new isolated output outside the installation')
    if reference:
        reference = reference.resolve()
        if (reference.exists() or reference == output or reference in output.parents or output in reference.parents
                or reference == install or install in reference.parents):
            raise AssetError('Use a new independent reference directory outside the installation')
    if not isinstance(edits, dict) or set(edits) != {'locale', 'display_name', 'pack_version', 'resources'} or edits['locale'] != 'pt-BR':
        raise AssetError('Version 2 requires the documented pt-BR font profile')
    name, version = edits['display_name'], edits['pack_version']
    if not isinstance(name, str) or not name or len(name.encode('utf-8')) > 96 or any(ord(c) < 32 or 127 <= ord(c) < 160 for c in name):
        raise AssetError('Invalid display name')
    if not isinstance(version, str) or not re.fullmatch(r'[0-9]{1,9}\.[0-9]{1,9}\.[0-9]{1,9}', version):
        raise AssetError('Invalid package version')
    if not isinstance(edits['resources'], dict) or not 1 <= len(edits['resources']) <= 4091:
        raise AssetError('A package requires a bounded set of translated resources')
    resources, outputs, originals, audit = [], {}, {}, []
    used = set()

    def add(identity, family, request, path, source, target, font=None):
        item = {'id': identity, 'family': family, 'request': request, 'path': path,
                'source_size': len(source), 'size': len(target),
                'source_sha256': digest(source), 'sha256': digest(target)}
        if font:
            item['font'] = font
        resources.append(item)
        outputs[path], originals[path] = target, source

    with VbfArchive(vbf) as archive:
        fi = {'base.ftc': archive.read(METRICS)}
        fi.update({name: archive.read(ATLAS_ROOT + name) for name in ATLAS_NAMES})
        fo, glyphs, font_audit = build_font(fi)
        layout = ftc_layout(fo['base.ftc'])
        widths = fo['base.ftc'][layout.metrics:layout.metrics + layout.count]
        for name, entries in edits['resources'].items():
            if not isinstance(name, str) or name.lower() in used or not isinstance(entries, list) or not entries:
                raise AssetError('Invalid/duplicate text resource or empty edits')
            used.add(name.lower())
            event = EVENT.fullmatch(name.lower()) is not None
            if not event and name not in MENU + BATTLE:
                raise AssetError(f'Unsupported text resource: {name}')
            source = archive.read((MASTER + name) if event else (KERNEL + name))
            target = rebuild_field(source, entries) if event else append_kernel(source, entries)
            checked = validate_edits(source, target, entries, widths, field=event)
            if source == target:
                raise AssetError(f'Resource has no actual translation: {name}')
            identity = 'event-' + digest(name.lower().encode())[:24] if event else name.replace('.', '-')
            family = 'events' if event else ('menu' if name in MENU else 'battle')
            request = '/FFX_Data/' + (MASTER + name if event else KERNEL + name)
            path = 'text/' + name
            add(identity, family, request, path, source, target, 'western')
            audit.append({'resource': name, 'edits': checked})
        add('metrics', 'font_metrics', '/FFX_Data/' + METRICS, 'font/base.ftc', fi['base.ftc'], fo['base.ftc'])
        atlases = []
        for name in ATLAS_NAMES:
            identity = name.split('.')[0].replace('_', '-')
            atlases.append(identity)
            add(identity, 'font_atlas', '/' + ATLAS_ROOT + name, 'font/' + name, fi[name], fo[name])
    if sum(len(data) for data in outputs.values()) + sum(len(data) for data in originals.values()) > MAX_WORKING_SET:
        raise AssetError('Source and output working set exceeds 256 MiB')
    coverage = {family: 'partial' if any(r['family'] == family for r in resources) else 'unavailable'
                for family in ('menu', 'battle', 'events')}
    coverage.update(subtitles=coverage['events'], texture_text='unavailable')
    manifest = {'schema_version': 2, 'capability': 'ffx.text-locale', 'hook_api': 2,
                'locale': edits['locale'], 'display_name': edits['display_name'], 'pack_version': edits['pack_version'],
                'base_locale': 1, 'fallback': 'native', 'activation': 'restart', 'executable_sha256': EXE_SHA,
                'coverage': coverage, 'resources': resources,
                'fonts': [{'id': 'western', 'encoding': 'ffx-western-v1', 'preserve_native': True,
                           'metrics': 'metrics', 'atlases': atlases, 'glyphs': glyphs}]}
    manifest_text = json.dumps(manifest, ensure_ascii=False, indent=2) + '\n'
    if len(manifest_text.encode('utf-8')) > MAX_RECIPE:
        raise AssetError('Manifest exceeds 1 MiB')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.mod006-', dir=output.parent) as temp:
        staging = Path(temp) / 'pack'
        staging.mkdir()
        for path, data in outputs.items():
            target = staging / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        (staging / 'manifest.json').write_text(manifest_text, encoding='utf-8')
        report = {'coverage': coverage, 'translations': audit, 'font': font_audit, 'rt2': 'not-run'}
        (staging / 'coverage.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        publish_directory(staging, output)
    if reference:
        reference.mkdir(parents=True)
        for path, data in originals.items():
            target = reference / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    return {'output': str(output), 'resources': len(resources), 'coverage': coverage,
            'translated_slots': sum(sum(edit['changed'] for edit in r['edits']) for r in audit),
            'manifest_sha256': digest((output / 'manifest.json').read_bytes())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vbf', type=Path, required=True)
    parser.add_argument('--edits', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reference', type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(build(args.vbf, args.output, load_recipe(args.edits), args.reference), indent=2))
        return 0
    except (AssetError, OSError, ValueError, TypeError) as exc:
        parser.exit(1, f'Pack build rejected: {exc}\n')


if __name__ == '__main__':
    raise SystemExit(main())
