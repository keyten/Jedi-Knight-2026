#!/usr/bin/env python3
"""Repeatable bounded stock-map CPU region inspection; no game/GL startup.

Reads installed PK3s only, writes private BSP/OBJ/TSV/plots under ignored build/.
Requires the standalone lightrecon_regions_inspect executable and Pillow.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installation', type=Path, required=True)
    parser.add_argument('--inspector', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--map', default='hoth3')
    parser.add_argument('--centre', type=float, nargs=3, default=(3320, 700, 123))
    parser.add_argument('--radius', type=float, default=512)
    parser.add_argument('--coarse', type=float, default=128)
    parser.add_argument('--minimum', type=float, default=32)
    parser.add_argument('--sky-samples', type=int, default=16)
    args = parser.parse_args()
    if not re.fullmatch(r'[a-zA-Z0-9_-]+', args.map):
        parser.error('simple map name required')
    args.output.mkdir(parents=True, exist_ok=False)
    bsp = args.output / f'{args.map}.bsp'
    for archive in sorted((args.installation / 'base').glob('assets[0-3].pk3'), reverse=True):
        with zipfile.ZipFile(archive) as source:
            name = f'maps/{args.map}.bsp'
            if name in source.namelist():
                if source.getinfo(name).file_size > 128 * 1024 * 1024:
                    raise ValueError('source map size limit')
                bsp.write_bytes(source.read(name))
                break
    else:
        raise FileNotFoundError(f'maps/{args.map}.bsp')
    prefixes = []
    commands = []
    logs = []
    for label in ('first', 'repeat'):
        directory = args.output / label
        directory.mkdir()
        prefix = directory / args.map
        command = [str(args.inspector.resolve()), str(bsp), str(prefix)]
        command += [str(x) for x in (*args.centre, args.radius, args.coarse, args.minimum, args.sky_samples)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=180, check=True)
        (directory / 'inspection.log').write_text(result.stdout + result.stderr)
        prefixes.append(prefix)
        commands.append(command)
        logs.append(result.stdout)
    hashes = {}
    for suffix in ('.obj', '-cells.tsv', '-edges.tsv'):
        first, repeat = (Path(str(p) + suffix) for p in prefixes)
        if first.read_bytes() != repeat.read_bytes():
            raise AssertionError('Nondeterministic stock region export: ' + suffix)
        hashes[suffix] = sha(first)

    with Path(str(prefixes[0]) + '-cells.tsv').open() as source:
        cells = [{k: float(v) for k, v in row.items()} for row in csv.DictReader(source, delimiter='\t')]
    with Path(str(prefixes[0]) + '-edges.tsv').open() as source:
        edges = [{k: float(v) for k, v in row.items()} for row in csv.DictReader(source, delimiter='\t')]
    if not any(row['region'] > 0 for row in cells):
        raise AssertionError('No certified free cells in this inspection domain')
    z = args.centre[2]
    selected = [row for row in cells if row['z0'] <= z < row['z1']]
    region_ids = sorted({int(row['region']) for row in cells if row['region'] > 0})
    from PIL import Image, ImageDraw, ImageFont
    palette = ['#1f77b4', '#aec7e8', '#ff7f0e', '#ffbb78', '#2ca02c', '#98df8a', '#d62728', '#ff9896', '#9467bd', '#c5b0d5', '#8c564b', '#c49c94']
    colours = {value: palette[i % len(palette)] for i, value in enumerate(region_ids)}
    canvas = Image.new('RGB', (1450, 1140), 'white')
    draw = ImageDraw.Draw(canvas)
    def font(size):
        try:
            return ImageFont.truetype('C:/Windows/Fonts/arial.ttf', size)
        except OSError:
            return ImageFont.load_default()
    x0, x1 = min(r['x0'] for r in cells), max(r['x1'] for r in cells)
    y0, y1 = min(r['y0'] for r in cells), max(r['y1'] for r in cells)
    scale = min(1024/(x1-x0), 920/(y1-y0))
    def xy(x, y):
        return 90+(x-x0)*scale, 1040-(y-y0)*scale
    draw.text((90, 18), f'{args.map}: sampled lighting regions at Z={z:g}', fill='black', font=font(28))
    draw.text((90, 57), f'Adaptive {args.minimum:g}..{args.coarse:g} world units. Grey is unresolved, not free space.', fill='#45484e', font=font(20))
    for row in selected:
        fill = colours[int(row['region'])] if row['region'] else {1: '#32363d', 2: 'white', 3: '#b8e1fa', 4: '#d6d8dc'}.get(int(row['class']), '#d6d8dc')
        a, b = xy(row['x0'], row['y1']), xy(row['x1'], row['y0'])
        draw.rectangle((a, b), fill=fill, outline='#ffffff', width=1)
        if int(row['issues']) & 2:
            draw.rectangle((a, b), outline='#b35c00', width=3)
    openings = [row for row in edges if abs(row['z']-z) <= args.minimum]
    for row in openings:
        x, y = xy(row['x'], row['y'])
        draw.line([(x, y-7), (x+7, y), (x, y+7), (x-7, y), (x, y-7)], fill='#9b5000', width=2)
    for step in range(5):
        x, y = xy(x0+(x1-x0)*step/4, y0+(y1-y0)*step/4)
        draw.text((x-22, 1050), f'{x0+(x1-x0)*step/4:g}', fill='black', font=font(17))
        draw.text((10, y-9), f'{y0+(y1-y0)*step/4:g}', fill='black', font=font(17))
    draw.text((490, 1090), 'World X (world units); World Y on vertical axis', fill='black', font=font(19))
    legend = [('#32363d', 'Certified solid'), ('#d6d8dc', 'Unknown / boundary'), ('#b35c00', 'Moving bounds (outline)')]
    legend += [(colours[value], f'Region {value}') for value in region_ids[:12]]
    for i, (colour, label) in enumerate(legend):
        y = 145+i*42
        draw.rectangle((1140, y, 1170, y+24), fill=colour)
        draw.text((1182, y+2), label, fill='black', font=font(17))
    draw.text((1140, 760), 'Diamond: sampled interface\nStatic topology only.\nNo light / irradiance field.', fill='#45484e', font=font(17))
    image = args.output / 'regions-xy.png'
    canvas.save(image)
    stats = {key: float(value) if '.' in value else int(value) for key, value in re.findall(r'([\w-]+)=([0-9]+(?:\.[0-9]+)?)(?=\s|$)', logs[0])}
    record = dict(passed=True, map=args.map, source_sha256=sha(bsp), inspector_sha256=sha(args.inspector),
                  commands=commands, logs=logs, identical_exports=True, export_sha256=hashes,
                  statistics=stats, centre=args.centre, radius=args.radius, coarse=args.coarse,
                  minimum=args.minimum, sky_samples=args.sky_samples,
                  slice_z=z, slice_cells=len(selected), image=str(image), image_sha256=sha(image),
                  limits=['Bounded ROI, not a whole-map room census.',
                          'Shader scripts/alpha masks not interpreted; non-sky materials stay optically unknown.',
                          'Original nonpolygonal flares excluded explicitly by source type.',
                          'Patch analytic chord bound is minimum cell size / 4, with conservative envelopes.',
                          'Static-only geometry; authored moving-model bounds do not prove travel extents.',
                          'No game startup, GPU rendering, inverse-light fitting or irradiance smoothing.'])
    (args.output / 'result.json').write_text(json.dumps(record, indent=2)+'\n')
    print(json.dumps(record, indent=2))


if __name__ == '__main__':
    main()
