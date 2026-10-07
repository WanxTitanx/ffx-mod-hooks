#!/usr/bin/env python3
"""Build source-bound MOD-006 text packages without modifying native assets. Jarvis-HOOK."""
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
from text_layouts import MASTER, KERNEL, MENU, BATTLE, EVENT, Layout, describe_resource
from extended_containers import rebuild_macro, scripts as macro_scripts

EXE_SHA = '78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced'
STEAM_EXE_SHA = '0537b2a1047f3266e73495cd4e35f63f0777f4231d417699f979954686da686d'
SUPPORTED_EXECUTABLES = {EXE_SHA:10675712, STEAM_EXE_SHA:10687744}
ASCII = "0123456789 !\"#$%&'()*+,-./:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"
ENCODER = {char: 48 + index for index, char in enumerate(ASCII)}
ENCODER.update(dict(zip('ÀÁÂÄ', range(163, 167))))
ENCODER.update(dict(zip('ÈÉÊËÌÍÎÏÑÒÓÔÖÙÚÛÜßàáâäçèéêëìíîïñòóôöùúûü', range(168, 208))))
ENCODER.update({'Ç': 167, 'ã': 242, 'õ': 243, 'Ã': 244, 'Õ': 245, '…': 211,
                '’': 213, '—': 150, '“': 148, '”': 149, 'º':246, 'ª':247})
TOKENS = {'TIDUS': bytes((19, 48)), 'YUNA': bytes((19, 49)),
          'WARN': bytes((10, 67)), 'NORMAL': bytes((10, 65))}
MAX_MANIFEST = 1024 * 1024
MAX_RECIPE = 32 * 1024 * 1024
MAX_WORKING_SET = 256 * 1024 * 1024


def executable_identity(path: Path) -> str:
    """Bind authoring to actual bytes; the unchanged VBF does not identify a PE."""
    size = path.stat().st_size
    if size not in SUPPORTED_EXECUTABLES.values():
        raise AssetError('Unsupported executable size')
    identity = digest(path.read_bytes())
    if SUPPORTED_EXECUTABLES.get(identity) != size:
        raise AssetError('Unsupported executable fingerprint')
    return identity


PAIR_CONTROLS = {7,9,10,11,14,16,18,19,*range(20,36)}
# Existing native symbols retained without assigning an unverified Unicode name.
NATIVE_GLYPHS = {0x8F,0x90,0x9C,0xD4}


def control_allowed(code: int, argument: int, api: int = 3) -> bool:
    if api >= 4:
        if code in (7,14) and 0 <= argument <= 255:return True
        if code == 18 and 48 <= argument <= 255:return True
        if code == 11 and argument == 32:return True
    if api >= 3:
        if (code == 16 or 20 <= code <= 35) and 48 <= argument <= 255:
            return True
        if code == 19 and argument in (66,67):
            return True
        if code == 10 and argument in (136,148,151,161):
            return True
    if code in (9, 11, 25):
        return 48 <= argument <= 255
    if code == 10:
        return argument in (65, 67, 82, 177)
    if code == 18:
        return 48 <= argument <= 57
    if code == 19:
        return 48 <= argument <= 65
    return False


def control_api(controls: bytes) -> int:
    at, api = 0, 2
    while at < len(controls):
        code = controls[at]
        at += 1
        if code == 3:
            continue
        if code == 1:
            api = max(api,3)
            continue
        if at == len(controls) or not control_allowed(code,controls[at],4):
            raise AssetError('Malformed control fingerprint')
        if not control_allowed(code,controls[at],3):
            api = max(api,4)
        elif not control_allowed(code,controls[at],2):
            api = max(api,3)
        at += 1
    return api


def encode(text: str, *, api: int = 3) -> bytes:
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
            elif token == 'CTRL:01':
                result.append(1)
            elif api>=4 and re.fullmatch(r'GLYPH:[0-9A-F]{2}',token) and int(token[6:],16) in NATIVE_GLYPHS:
                result.append(int(token[6:],16))
            elif re.fullmatch(r'VAR:[0-9]', token):
                result += bytes((18, 48 + int(token[4:])))
            elif re.fullmatch(r'CTRL:[0-9A-F]{2}:[0-9A-F]{2}', token):
                code, argument = (int(value, 16) for value in token[5:].split(':'))
                if not control_allowed(code, argument, api):
                    raise AssetError('Unsupported native control or argument')
                result += bytes((code, argument))
            else:
                raise AssetError('Unknown or unterminated text token')
            at = end
        else:
            value = ENCODER.get(char)
            if value is None or value >= 246 and api < 4:
                raise AssetError(f'Unmapped character U+{ord(char):04X}')
            result.append(value)
        at += 1
        if len(result) > 2048:
            raise AssetError('Encoded string exceeds 2048 bytes')
    return bytes(result)


def decode(script: bytes, *, api: int = 3) -> str:
    """Round-trip preview for supported bytes. Opaque scripts remain uneditable."""
    inverse = {value: char for char, value in ENCODER.items()}
    out = []
    at = 0
    while at < len(script):
        code = script[at]
        at += 1
        if code == 3:
            out.append('\n')
        elif code == 1:
            out.append('{CTRL:01}')
        elif code in PAIR_CONTROLS:
            if at == len(script) or not control_allowed(code, script[at],api):
                raise AssetError('Unsupported native control in preview')
            out.append(f'{{CTRL:{code:02X}:{script[at]:02X}}}')
            at += 1
        elif api>=4 and code in NATIVE_GLYPHS:
            out.append(f'{{GLYPH:{code:02X}}}')
        elif code in inverse:
            char = inverse[code]
            out.append(char * 2 if char in '{}' else char)
        else:
            raise AssetError(f'Native byte {code:02X} has no editable preview')
    return ''.join(out)


def kernel_layout(source: bytes, layout: Layout | None = None):
    if len(source) < 21:
        raise AssetError('Truncated kernel table')
    low, high, stride, block = struct.unpack_from('<HHHH', source, 8)
    allowed = (layout.stride,) if layout is not None else (8, 16)
    if high < low or stride not in allowed or (high - low + 1) * stride > block or 20 + block >= len(source):
        raise AssetError('Invalid kernel table bounds')
    return low, high, stride, 20 + block


def script_at(data: bytes, at: int) -> bytes:
    if not 0 <= at < len(data):
        raise AssetError('Text offset is out of bounds')
    end = data.find(b'\0', at)
    if end < 0:
        raise AssetError('Text terminator is missing')
    return data[at:end]


def checked_edits(edits, low: int, high: int, slots: int, *, api: int = 3):
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
        yield edit, encode(edit['text'],api=api)


def append_kernel(source: bytes, edits, layout: Layout | None = None, *, api: int = 3) -> bytes:
    low, high, stride, pool = kernel_layout(source, layout)
    slots, step = (layout.slots, layout.offset_step) if layout else (2 if stride == 8 else 4, 4)
    output = bytearray(source)
    appended = {}
    for edit, replacement in checked_edits(edits, low, high, slots,api=api):
        at = 20 + (edit['row'] - low) * stride + edit['slot'] * step
        before = script_at(source, pool + u16(source, at))
        if replacement == before:
            continue
        offset = appended.get(replacement)
        if offset is None:
            offset = len(output) - pool
            if offset + len(replacement) + 1 > 65536:
                raise AssetError('Translation exceeds u16 text pool')
            appended[replacement] = offset
            output += replacement + b'\0'
        struct.pack_into('<H', output, at, offset)
    # Some original optional references are deliberately out of range. Growing
    # the pool must not turn one into a new, unintended live string.
    for row in range(high-low+1):
        for slot in range(slots):
            at = 20+row*stride+slot*step
            before, after = pool+u16(source,at), pool+u16(output,at)
            if before == after and len(source) <= before < len(output):
                raise AssetError('Appended pool activates an invalid native reference')
    return bytes(output)


def field_layout(source: bytes) -> int:
    if not 9 <= len(source) <= 65536:
        raise AssetError('Field table exceeds the native u16 address space')
    header = u16(source, 0)
    if header < 8 or header % 8 or header >= len(source):
        raise AssetError('Invalid implicit field header')
    return header


def rebuild_field(source: bytes, edits, *, api: int = 3) -> bytes:
    """Keep the implicit first offset/count and flags; repoint both string variants."""
    header = field_layout(source)
    translated = {(edit['row'], edit['slot']): value
                  for edit, value in checked_edits(edits, 0, header // 8 - 1, 2,api=api)}
    if not translated:
        return source
    strings = []
    changed = False
    for row in range(header // 8):
        for slot in range(2):
            at = row * 8 + slot * 4
            old_offset = u16(source, at)
            if old_offset == 0:
                if (row, slot) in translated:
                    raise AssetError('Cannot edit an inactive field variant')
                strings.append(None)
                continue
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
        if value is None:
            struct.pack_into('<H', result, index * 4, 0)
            continue
        if value not in known:
            if len(result) + len(value) + 1 > 65536:
                raise AssetError('Translation exceeds u16 field table')
            known[value] = len(result)
            result += value + b'\0'
        struct.pack_into('<H', result, index * 4, known[value])
    return bytes(result)


def rebuild_resource(source: bytes, edits, layout: Layout, *, api: int = 3) -> bytes:
    if layout.container == 'indexed':
        return append_kernel(source, edits, layout,api=api)
    if layout.container == 'field':
        return rebuild_field(source, edits,api=api)
    if layout.container == 'macro':
        replacements = {(e['row'],e['slot']):value for e,value in
                        checked_edits(edits,0,16*65536-1,2,api=api)}
        return rebuild_macro(source,replacements)
    raise AssetError('No admitted authoring layout')


def edit_script(data: bytes, edit, layout: Layout) -> bytes:
    if layout.container == 'macro':
        value = macro_scripts(data).get((edit['row'],edit['slot']))
        if value is None:
            raise AssetError('Macro edit targets an inactive reference')
        return value
    if layout.container == 'field':
        return script_at(data,u16(data,edit['row']*8+edit['slot']*4))
    low, _, stride, pool = kernel_layout(data,layout)
    at = 20+(edit['row']-low)*stride+edit['slot']*layout.offset_step
    return script_at(data,pool+u16(data,at))


def measure(script: bytes, widths: bytes, *, api: int = 3):
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
        elif code == 1:
            controls.append(code)
        elif code in PAIR_CONTROLS:
            if at == len(script) or not control_allowed(code, script[at],api):
                raise AssetError('Unsupported or truncated control')
            controls += bytes((code, script[at]))
            at += 1
        elif code < 48 or code in (240, 241) or code > (247 if api>=4 else 245) or not widths[code - 48]:
            raise AssetError('Unsupported glyph or control in edited string')
        else:
            lines[-1] += widths[code - 48]
    return bytes(controls), lines

def _control_tokens(wire):
    result=[];at=0
    while at<len(wire):
        size=1 if wire[at] in (1,3) else 2
        result.append(wire[at:at+size]);at+=size
    return result

def compatible_controls(source: bytes,target: bytes, *, api: int = 3) -> bool:
    a,_=measure(source,bytes([1]*230),api=api);b,_=measure(target,bytes([1]*230),api=api)
    if api<4:return a==b
    x,y=_control_tokens(a),_control_tokens(b)
    if source[:1] in (b'\x13',b'\x19') and source[2:3]==b'\x03' and source[:3]!=target[:3]:return False
    if any(t[0] in (7,16) for t in x):return x==y
    # Reflow within each pause-delimited page, preserving every native control
    # and its argument. Position/choice-bearing scripts keep their exact wire.
    def pages(tokens):
        output=[[]]
        for token in tokens:
            if token==b'\x01':output.append([])
            else:output[-1].append(token)
        return [(p.count(b'\x03'),[t for t in p if t!=b'\x03']) for p in output]
    return pages(x)==pages(y)


def validate_edits(source: bytes, target: bytes, edits, widths: bytes, *, field=False, layout: Layout | None = None, api: int = 3):
    macro_before = macro_scripts(source) if layout and layout.container == 'macro' else None
    macro_after = macro_scripts(target) if macro_before is not None else None
    field_needs_api3 = (field or layout is not None and layout.container == 'field') and any(
        u16(source,at)==0 for at in range(0,field_layout(source),4))
    if layout is not None:
        pass  # The descriptor-specific reader checks every requested identity below.
    elif field:
        field_layout(source)
        field_layout(target)
    else:
        low, _, stride, pool = kernel_layout(source)
    audit = []
    for edit in edits:
        if macro_before is not None:
            identity = (edit['row'],edit['slot'])
            before, after = macro_before.get(identity), macro_after.get(identity)
            if before is None or after is None:
                raise AssetError('Macro edit targets an inactive reference')
        elif layout is not None:
            before, after = edit_script(source,edit,layout), edit_script(target,edit,layout)
        elif field:
            at = edit['row'] * 8 + edit['slot'] * 4
            before = script_at(source, u16(source, at))
            after = script_at(target, u16(target, at))
        else:
            at = 20 + (edit['row'] - low) * stride + edit['slot'] * 4
            before = script_at(source, pool + u16(source, at))
            after = script_at(target, pool + u16(target, at))
        if before == after:
            audit.append({**edit, 'changed': False})
            continue
        if api<4 and len(after) > len(before):
            raise AssetError(f'Encoded byte capacity exceeded at {edit["row"]}:{edit["slot"]}')
        old_control, old_width = measure(before, widths,api=api)
        new_control, new_width = measure(after, widths,api=api)
        if not compatible_controls(before,after,api=api) or len(old_width) != len(new_width):
            raise AssetError(f"Controls changed at {edit['row']}:{edit['slot']}")
        if api<4 and any(new > old for new, old in zip(new_width, old_width)):
            raise AssetError(f"Width overflow at {edit['row']}:{edit['slot']}: {new_width} > {old_width}")
        audit.append({**edit, 'changed': True, 'before_width': old_width,
                      'after_width': new_width, 'controls': new_control.hex(), 'encoded': after.hex(),
                      'layout_review_required':len(after)>len(before) or any(new>old for new,old in zip(new_width,old_width)),
                      'required_api':4 if api>=4 else max(control_api(new_control),3 if field_needs_api3 else 2)})
    return audit


def load_recipe(path: Path, max_bytes: int = MAX_RECIPE):
    def unique(pairs):
        obj = {}
        for key, value in pairs:
            if key in obj:
                raise AssetError(f'Duplicate JSON key: {key}')
            obj[key] = value
        return obj
    with path.open('rb') as stream:
        data = stream.read(max_bytes + 1)
    if len(data) > max_bytes:
        raise AssetError(f'JSON input exceeds its {max_bytes}-byte bound')
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


def build(vbf: Path, output: Path, edits, reference: Path | None = None,
          executable: Path | None = None, *, hook_api: int | None = None,
          graphics: Path | None = None):
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
        raise AssetError('Authoring requires the documented pt-BR recipe and font profile')
    name, version = edits['display_name'], edits['pack_version']
    if not isinstance(name, str) or not name or len(name.encode('utf-8')) > 96 or any(ord(c) < 32 or 127 <= ord(c) < 160 for c in name):
        raise AssetError('Invalid display name')
    if not isinstance(version, str) or not re.fullmatch(r'[0-9]{1,9}\.[0-9]{1,9}\.[0-9]{1,9}', version):
        raise AssetError('Invalid package version')
    if not isinstance(edits['resources'], dict) or not 1 <= len(edits['resources']) <= 4091:
        raise AssetError('A package requires a bounded set of translated resources')
    executable = executable or (install / 'FFX.exe')
    if not executable.is_file():
        raise AssetError('Supply --exe when the VBF is outside a supported game installation')
    executable_sha256 = executable_identity(executable)
    resources, outputs, originals, audit = [], {}, {}, []
    used = set()
    if hook_api not in (None,2,3,4):raise AssetError('Unsupported authoring API')
    if graphics is not None and hook_api!=4:raise AssetError('UI texture compilation requires API 4')
    api = hook_api or 2
    authoring_api=hook_api or 3

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
        fo, glyphs, font_audit = build_font(fi,ordinals=authoring_api>=4)
        layout = ftc_layout(fo['base.ftc'])
        widths = fo['base.ftc'][layout.metrics:layout.metrics + layout.count]
        for name, entries in edits['resources'].items():
            if not isinstance(name, str) or not isinstance(entries, list) or not entries:
                raise AssetError('Invalid/duplicate text resource or empty edits')
            layout = describe_resource(name)
            if hook_api is not None and layout.minimum_api>hook_api:
                raise AssetError('Resource requires a newer authoring API')
            if layout.member in used:
                raise AssetError('Duplicate canonical text resource, including aliases')
            used.add(layout.member)
            api = max(api,layout.minimum_api)
            source = archive.read(layout.member)
            target = rebuild_resource(source,entries,layout,api=authoring_api)
            checked = validate_edits(source,target,entries,widths,layout=layout,api=authoring_api)
            api = max([api]+[edit.get('required_api',2) for edit in checked])
            if hook_api is not None and api>hook_api:
                raise AssetError(f'Resource requires API {api}; explicit authoring API is {hook_api}')
            if source == target:
                raise AssetError(f'Resource has no actual translation: {name}')
            identity = ('text-'+digest(layout.member.encode())[:24] if '/' in layout.key
                        else layout.key.replace('.','-'))
            path = 'text/' + layout.key
            add(identity,layout.family,layout.request,path,source,target,'western')
            audit.append({'resource':layout.key,'container':layout.container,'edits':checked})
        if graphics is not None:
            graphics=graphics.resolve(strict=True)
            approved={r['request']:r for r in json.loads(Path(__file__).with_name('graphics_profiles.json').read_text())['entries']}
            compiled=json.loads((graphics/'graphics-manifest.json').read_text())
            if len(compiled['records'])!=len(approved):raise AssetError('Incomplete examined UI texture set')
            seen_graphics=set()
            for row in compiled['records']:
                request=row['request'];profile=approved.get(request)
                if profile is None or request in seen_graphics:raise AssetError('Unexamined or duplicate UI texture')
                seen_graphics.add(request)
                p=(graphics/row['path']).resolve(strict=True)
                if graphics not in p.parents:raise AssetError('Compiled UI texture escaped its root')
                target=p.read_bytes();source=archive.read(row['resource'])
                if (len(source)!=profile['size'] or len(target)!=profile['size'] or
                    digest(source)!=profile['source_sha256'] or digest(target)!=profile['sha256']):
                    raise AssetError('UI texture differs from its examined compilation')
                # Input locations never select publication destinations. The
                # closed request catalogue supplies unique relative paths.
                path='graphics/'+request.removeprefix('/')
                add('graphic-'+digest(request.encode())[:24],'ui_texture',request,path,source,target)
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
    coverage.update(subtitles=coverage['events'], texture_text='partial' if graphics is not None else 'unavailable')
    manifest = {'schema_version': api, 'capability': 'ffx.text-locale', 'hook_api': api,
                'locale': edits['locale'], 'display_name': edits['display_name'], 'pack_version': edits['pack_version'],
                'base_locale': 1, 'fallback': 'native', 'activation': 'restart', 'executable_sha256': executable_sha256,
                'coverage': coverage, 'resources': resources,
                'fonts': [{'id': 'western', 'encoding': 'ffx-western-v2' if api>=4 else 'ffx-western-v1', 'preserve_native': True,
                           'metrics': 'metrics', 'atlases': atlases, 'glyphs': glyphs}]}
    manifest_text = json.dumps(manifest, ensure_ascii=False, indent=2) + '\n'
    if len(manifest_text.encode('utf-8')) > MAX_MANIFEST:
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
        report = {'coverage': coverage, 'translations': audit, 'font': font_audit,
                  'executable_sha256': executable_sha256, 'rt2': 'not-run'}
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
    parser.add_argument('--exe', type=Path, help='Exact supported FFX.exe; defaults to the VBF installation')
    parser.add_argument('--hook-api',type=int,choices=(2,3,4),help='Explicit capability version; default retains legacy inference')
    parser.add_argument('--graphics',type=Path,help='Examined native UI compilation; requires API 4')
    args = parser.parse_args()
    try:
        print(json.dumps(build(args.vbf,args.output,load_recipe(args.edits),args.reference,args.exe,hook_api=args.hook_api,graphics=args.graphics),indent=2))
        return 0
    except (AssetError, OSError, ValueError, TypeError) as exc:
        parser.exit(1, f'Pack build rejected: {exc}\n')


if __name__ == '__main__':
    raise SystemExit(main())
