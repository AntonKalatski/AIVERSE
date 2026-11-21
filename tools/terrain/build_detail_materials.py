#!/usr/bin/env python3
"""Package shared 1K/4m Terra PBR atlases and native material overrides.

Run bake_landscape_pbr.py --detail first. Output stays in the exported pack;
the installer applies detail-materials.json through existing material APIs.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image
from terrain_detail_graph import material_graph


def build(root, materials, asset_root, metres=4.0, transparency=.12, sand_layer="sand", secondary_sand=None):
    if sand_layer not in ("sand","river_sand","pebble_sand"):
        raise ValueError("sand layer: sand|river_sand|pebble_sand")
    if secondary_sand not in (None,"sand","river_sand","pebble_sand"):
        raise ValueError("secondary sand: sand|river_sand|pebble_sand")
    if secondary_sand==sand_layer:
        raise ValueError("secondary sand must differ from primary")
    layers=("grass",sand_layer,secondary_sand,"rock","snow") if secondary_sand else ("grass",sand_layer,"rock","snow")
    columns=3 if secondary_sand else 2
    size, gutter = 1024, 32
    cell = size + 2 * gutter
    paths = {}
    for channel in ('basecolor', 'normal', 'orm'):
        atlas = np.zeros((2 * cell, columns * cell, 3), np.uint8)
        for i, name in enumerate(layers):
            image = Image.open(materials / f'{name}_{channel}_repeat.png').convert('RGB')
            if image.size != (size, size):
                raise ValueError(f'{name}/{channel}: expected native 1024px PBR source')
            padded = np.pad(np.asarray(image), ((gutter, gutter), (gutter, gutter), (0, 0)), mode='wrap')
            y, x = i // columns * cell, i % columns * cell
            atlas[y:y+cell, x:x+cell] = padded
        filename = f'detail_{channel}.png'
        Image.fromarray(atlas).save(root / 'textures' / filename)
        paths[channel] = f'{asset_root}/textures/{filename}'
    overrides = {}
    catalog = json.loads((root / 'catalog.json').read_text())
    for tile in catalog['terrain']:
        stem = Path(tile['file']).stem
        if not (root / 'textures' / f'{stem}_weights.png').is_file():
            raise ValueError('missing continuous weights: run bake_landscape_pbr.py --detail')
        mask_size = Image.open(root / 'textures' / f'{stem}_weights.png').size
        if mask_size[0] != mask_size[1]:
            raise ValueError('weight masks must be square')
        graph = material_graph(f'{asset_root}/textures/{stem}_weights.png', paths, metres, transparency, mask_resolution=mask_size[0], secondary_sand=bool(secondary_sand))
        overrides[tile['file']] = {'*': {'color': [1, 1, 1, 1], 'vertexColors': False,
                                       'shaderGraph': graph}}
    manifest = dict(version=1, metresPer1024=metres, noiseTransparency=transparency,
                    layers=list(layers), sandVariation="continuous-mask-alpha" if secondary_sand else "single-family", materials=overrides)
    (root / 'detail-materials.json').write_text(json.dumps(manifest))
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--materials', type=Path, default=Path(__file__).resolve().parents[2] / 'engine/assets/materials/terrain/textures')
    parser.add_argument('--asset-root', required=True, help='project-relative path of installed pack')
    parser.add_argument('--metres', type=float, default=4)
    parser.add_argument('--noise-transparency', type=float, default=.12)
    parser.add_argument('--sand-layer', choices=['sand','river_sand','pebble_sand'], default='sand')
    parser.add_argument('--secondary-sand', choices=['sand','river_sand','pebble_sand'])
    args = parser.parse_args()
    if args.asset_root.startswith('/') or '..' in args.asset_root or '\\' in args.asset_root:
        parser.error('asset-root must be project relative')
    result = build(args.root, args.materials, args.asset_root, args.metres, args.noise_transparency, args.sand_layer, args.secondary_sand)
    print(f"Shared 1K/{args.metres}m PBR: {len(result['materials'])} tiles, {len(result['layers'])} layers")
