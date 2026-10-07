#!/usr/bin/env python3
"""Prepare a bounded private source snapshot for TextLanguageValidate; never installs a pack."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import tempfile

from asset_io import AssetError, VbfArchive, digest
from inspect_assets import METRICS, ATLAS_ROOT, ATLAS_NAMES
from pack import MASTER, SUPPORTED_EXECUTABLES, MAX_MANIFEST, MAX_WORKING_SET, load_recipe, publish_directory
from text_layouts import describe_resource


def safe_path(path):
    if not isinstance(path, str) or not 1 <= len(path) <= 240:
        return False
    for part in path.split('/'):
        if not re.fullmatch(r'[A-Za-z0-9_.-]+', part) or part in ('.', '..') or part.endswith('.'):
            return False
        if re.fullmatch(r'con|prn|aux|nul|com[1-9]|lpt[1-9]', part.split('.')[0], re.I):
            return False
    return True


def archive_name(resource, schema):
    request = resource.get('request', '')
    if not isinstance(request, str):
        raise AssetError('Resource request must be a string')
    request = request.replace('\\', '/').lower()
    if request.startswith('../../../'):
        request = request[9:]
    request = request.removeprefix('/')
    if not safe_path(request):
        raise AssetError('Invalid virtual resource request')
    family = resource.get('family')
    if request.startswith('ffx_data/ffx_ps2/'):
        name = request[len('ffx_data/'):]
        if family == 'font_metrics' and name == METRICS:
            return name
        if name.startswith(MASTER):
            layout = describe_resource(name[len(MASTER):])
            if layout.family == family and schema >= layout.minimum_api:
                return layout.member
    if family == 'font_atlas' and request.startswith(ATLAS_ROOT) and request[len(ATLAS_ROOT):] in ATLAS_NAMES:
        return request
    if family=='ui_texture' and schema>=4:
        profiles=json.loads(Path(__file__).with_name('graphics_profiles.json').read_text())['entries']
        if any(p['request']=='/'+request for p in profiles):return request
    raise AssetError('Resource is outside the supported source families')


def prepare(vbf: Path, package: Path, output: Path):
    vbf = vbf.resolve(strict=True)
    package = package.resolve(strict=True)
    output = output.resolve()
    install = vbf.parent.parent if vbf.parent.name.lower() == 'data' and (vbf.parent.parent/'FFX.exe').is_file() else vbf.parent
    if output.exists() or output == install or install in output.parents or output == package or package in output.parents:
        raise AssetError('Use a new output outside the installation and package')
    manifest = load_recipe(package/'manifest.json',MAX_MANIFEST)
    if not isinstance(manifest, dict) or type(manifest.get('schema_version')) is not int or type(manifest.get('hook_api')) is not int or (manifest.get('schema_version'), manifest.get('hook_api')) not in ((1, 1), (2, 2), (3, 3), (4, 4)) or manifest.get('executable_sha256') not in SUPPORTED_EXECUTABLES:
        raise AssetError('Unsupported package version or executable profile')
    resources = manifest.get('resources')
    if not isinstance(resources, list) or not 1 <= len(resources) <= 4096:
        raise AssetError('Invalid resource count')
    paths, total, planned = set(), 0, []
    for resource in resources:
        if not isinstance(resource, dict):
            raise AssetError('Resource must be an object')
        path, size, fingerprint = resource.get('path'), resource.get('source_size'), resource.get('source_sha256')
        if not safe_path(path) or path.lower() in paths or type(size) is not int or not 1 <= size <= 64*1024*1024 or not isinstance(fingerprint, str) or not re.fullmatch('[0-9a-f]{64}', fingerprint):
            raise AssetError('Invalid source extent, path or fingerprint')
        paths.add(path.lower())
        total += size
        if total > MAX_WORKING_SET:
            raise AssetError('Source snapshot exceeds its working-set bound')
        planned.append((archive_name(resource, manifest['schema_version']), path, size, fingerprint))
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.mod006-reference-', dir=output.parent) as temporary:
        staging = Path(temporary)/'reference'
        staging.mkdir()
        with VbfArchive(vbf) as archive:
            for name, path, size, fingerprint in planned:
                data = archive.read(name, limit=size)
                if len(data) != size or digest(data) != fingerprint:
                    raise AssetError(f'Native source changed: {name}. Rebase the translation against the matching source.')
                target = staging/path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
        publish_directory(staging, output)
    return dict(reference=str(output), resources=len(planned), source_bytes=total)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vbf', type=Path, required=True)
    parser.add_argument('--pack', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(prepare(args.vbf, args.pack, args.output), indent=2))
    except (AssetError, OSError, ValueError, TypeError) as error:
        parser.exit(1, f'Source snapshot rejected: {error}\n')
