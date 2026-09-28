#!/usr/bin/env python3
"""Create original, synthetic file-IO fixtures; these are NOT game language packs."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

from pack import EXE_SHA


def create(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=False)
    resources = []
    def add(identity, family, request, path, data, font=None):
        digest = hashlib.sha256(data).hexdigest()
        resource = dict(id=identity, family=family, request=request, path=path,
                        source_size=len(data), size=len(data), source_sha256=digest, sha256=digest)
        if font:
            resource['font'] = font
        target = output / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        resources.append(resource)
    add('menu', 'menu', '/FFX_Data/ffx_ps2/ffx/master/new_uspc/battle/kernel/menu_txt.bin',
        'text/menu.bin', b'Synthetic test resource; never load in FFX.', 'western')
    add('metrics', 'font_metrics', '/FFX_Data/ffx_ps2/ffx/master/uspc/menu/base.ftc',
        'font/base.ftc', b'Synthetic metric identity, deliberately incompatible.')
    atlases = []
    for role in ('font', 'shadow'):
        for parity in (0, 1):
            identity = f'{role}-{parity}'
            atlases.append(identity)
            filename = f'{role}_0_{parity}.dds.phyre'
            add(identity, 'font_atlas', '/FFX_Data/GameData/PS3Data/menu_us/base_ftc/D3D11/' + filename,
                'font/' + filename, f'Synthetic {identity}; no glyph pixels.'.encode('ascii'))
    manifest = dict(schema_version=1, capability='ffx.text-locale', hook_api=1,
                    locale='pt-BR', display_name='Synthetic IO test - not a game package', pack_version='1.0.0',
                    base_locale=1, fallback='native', activation='restart', executable_sha256=EXE_SHA,
                    coverage=dict(menu='partial', battle='unavailable', events='unavailable', texture_text='unavailable'),
                    resources=resources, fonts=[dict(id='western', encoding='ffx-western-v1', preserve_native=True,
                    metrics='metrics', atlases=atlases, glyphs=[])])
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    create(parser.parse_args().output)
