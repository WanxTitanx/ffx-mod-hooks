#!/usr/bin/env python3
"""Inspect only MOD-006's Western font inputs from a user's existing VBF."""
from __future__ import annotations

import argparse
from dataclasses import asdict
import io
import json
from pathlib import Path
import struct

from asset_io import AssetError, VbfArchive, digest, ftc_layout, phyre_layout

METRICS = 'ffx_ps2/ffx/master/uspc/menu/base.ftc'
ATLAS_ROOT = 'ffx_data/gamedata/ps3data/menu_us/base_ftc/d3d11/'
ATLAS_NAMES = ('font_0_0.dds.phyre', 'font_0_1.dds.phyre',
               'shadow_0_0.dds.phyre', 'shadow_0_1.dds.phyre')


def texture_image(data):
    from PIL import Image
    layout = phyre_layout(data)
    pixels = data[layout.pixels:layout.pixels+layout.pixel_size]
    if layout.format == 'ARGB8':
        return Image.frombytes('RGBA', (layout.width, layout.height), pixels, 'raw', 'BGRA')
    if layout.format == 'L8':
        return Image.frombytes('L', (layout.width, layout.height), pixels).convert('RGBA')
    header = bytearray(128)
    header[:4] = b'DDS '
    for offset, value in {4:124, 8:0x81007, 12:layout.height, 16:layout.width,
                          20:layout.pixel_size, 76:32, 80:4, 108:0x1000}.items():
        struct.pack_into('<I', header, offset, value)
    header[84:88] = layout.format.encode('ascii')
    return Image.open(io.BytesIO(header+pixels)).convert('RGBA')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vbf', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    archive_path = args.vbf.resolve(strict=True)
    output = args.output.resolve()
    # The inspection must never mutate the original installation or an existing output.
    install = (archive_path.parent.parent if archive_path.parent.name.lower() == 'data'
               and (archive_path.parent.parent / 'FFX.exe').is_file() else archive_path.parent)
    if output.exists() or output == install or install in output.parents:
        parser.error('Use a new output directory outside the archive/install directory')
    try:
        with VbfArchive(archive_path) as archive:
            metrics = archive.read(METRICS)
            layout = ftc_layout(metrics)
            payloads = {name:archive.read(ATLAS_ROOT+name) for name in ATLAS_NAMES}
            report = {'metrics': {'sha256':digest(metrics), 'size':len(metrics),
                                  'layout':asdict(layout),
                                  'widths':list(metrics[layout.metrics:layout.metrics+layout.count])},
                      'atlases': {name: {'sha256':digest(data), 'size':len(data),
                                         'layout':asdict(phyre_layout(data))}
                                   for name,data in payloads.items()}}
        output.mkdir(parents=True)
        (output/'base.ftc').write_bytes(metrics)
        for name, data in payloads.items():
            (output/name).write_bytes(data)
            texture_image(data).save(output/(name+'.png'))
        (output/'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        print(json.dumps(report, indent=2))
        return 0
    except (AssetError, OSError) as exc:
        parser.exit(1, f'Font inspection failed: {exc}\n')


if __name__ == '__main__':
    raise SystemExit(main())
