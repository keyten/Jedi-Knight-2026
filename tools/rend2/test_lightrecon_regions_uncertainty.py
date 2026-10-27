#!/usr/bin/env python3
"""X01 stock ROI witness. Licensed inputs stay local; no solver or GPU required.

Requires the renderer-exported D09 hoth2_a/hoth3_a datasets and original PK3s.
The MP dataset is explicitly geometry-only with unresolved parsed materials.
"""
import argparse
import csv
import ctypes
import hashlib
import gzip
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time
import zipfile


def run(command, log):
    start = time.perf_counter()
    with log.open('w') as stream:
        process = subprocess.Popen(list(map(str, command)), stdout=stream, stderr=subprocess.STDOUT)
        code = process.wait()
        peak = None
        if os.name == 'nt':
            class Counters(ctypes.Structure):
                _fields_ = [('cb', ctypes.c_uint32), ('faults', ctypes.c_uint32)] + [(n, ctypes.c_size_t) for n in ('peak_working', 'working', 'peak_paged', 'paged', 'peak_nonpaged', 'nonpaged', 'pagefile', 'peak_pagefile')]
            counters = Counters()
            counters.cb = ctypes.sizeof(counters)
            function = ctypes.WinDLL('psapi').GetProcessMemoryInfo
            function.argtypes = [ctypes.c_void_p, ctypes.POINTER(Counters), ctypes.c_uint32]
            if function(int(process._handle), ctypes.byref(counters), counters.cb):
                peak = counters.peak_working
    if code:
        raise RuntimeError(f'{command[0]} failed ({code}): {log.read_text(errors="replace")[-2000:]}')
    return dict(wall_seconds=time.perf_counter()-start, peak_working_set_bytes=peak)


def extract(base, name, output):
    content = None
    for archive in sorted(base.glob('assets?.pk3')):
        with zipfile.ZipFile(archive) as z:
            if f'maps/{name}.bsp' in z.namelist():
                content = z.read(f'maps/{name}.bsp')
    if content is None:
        raise RuntimeError(f'Licensed stock map {name} unavailable')
    output.write_bytes(content)
    return content


def slices(prefix, centre):
    # Small SVG sections: scientific debug geometry, no renderer screenshot.
    def debug_text(suffix):
        path = prefix.with_name(prefix.name+suffix)
        return path.open() if path.exists() else gzip.open(path.with_name(path.name+'.gz'), 'rt')
    with debug_text('-cells.tsv') as stream:
        rows = list(csv.DictReader(stream, delimiter='\t'))
    vertices, triangles, category = [], [], 'unresolved'
    with debug_text('-geometry.obj') as stream:
        lines = stream.readlines()
    for line in lines:
        p = line.split()
        if not p:
            continue
        if p[0] == 'g': category = p[1].split('_surface_')[0]
        if p[0] == 'v': vertices.append(tuple(map(float, p[1:4])))
        if p[0] == 'f': triangles.append(([vertices[int(i)-1] for i in p[1:4]], category))
    colours = {0: '#78b5df', 1: '#292d36', 2: '#fff', 3: '#c4aaf4', 4: '#babec6', 5: '#82ccb0'}
    optics = dict(opaque='#101820', alpha='#bc5311', translucent='#00a5aa', sky='#7551ba', unresolved='#d3267c', moving='#f0a000')
    for a, b, normal, label in ((0, 1, 2, 'xy'), (0, 2, 1, 'xz')):
        keys = 'xyz'
        lo = [min(float(c[f'{k}0']) for c in rows) for k in keys]
        hi = [max(float(c[f'{k}1']) for c in rows) for k in keys]
        scale = 850 / max(hi[a]-lo[a], hi[b]-lo[b])
        xy = lambda p: ((p[a]-lo[a])*scale+40, 910-(p[b]-lo[b])*scale)
        svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="980" height="1030" viewBox="0 0 980 1030"><rect width="980" height="1030" fill="white"/><defs><clipPath id="roi"><rect x="40" y="60" width="850" height="850"/></clipPath></defs>', f'<text x="20" y="22" font-size="16">{prefix.name} {label.upper()} at {keys[normal]}={centre[normal]:.3f}</text>', '<g clip-path="url(#roi)">']
        for c in rows:
            if not float(c[f'{keys[normal]}0']) <= centre[normal] < float(c[f'{keys[normal]}1']):
                continue
            low = [float(c[f'{k}0']) for k in keys]; high = [float(c[f'{k}1']) for k in keys]
            x0, y1 = xy(low); x1, y0 = xy(high)
            issue = int(c['issues']); fill = colours[int(c['class'])]
            stroke = '#f0a000' if issue & 2 else '#697887' if issue & 1 else fill
            svg.append(f'<rect x="{x0:.2f}" y="{y0:.2f}" width="{x1-x0:.2f}" height="{y1-y0:.2f}" fill="{fill}" stroke="{stroke}" stroke-width=".4"/>')
        for triangle, kind in triangles:
            points = []
            for p, q in zip(triangle, triangle[1:]+triangle[:1]):
                u, v = p[normal]-centre[normal], q[normal]-centre[normal]
                if u*v < 0:
                    t = u/(u-v)
                    points.append(tuple(p[k]+t*(q[k]-p[k]) for k in range(3)))
                elif u == 0: points.append(p)
            if len(points) >= 2:
                p, q = xy(points[0]), xy(points[1])
                svg.append(f'<path d="M {p[0]:.2f} {p[1]:.2f} L {q[0]:.2f} {q[1]:.2f}" stroke="{optics[kind]}" stroke-width="1.1"/>')
        svg += ['</g>', '<text x="20" y="950" font-size="13">Blue: certified free; dark: solid; grey: unresolved; purple: sky; green: exterior</text>', '<text x="20" y="972" font-size="13">Borders: geometry boundary grey; moving orange. Geometry: opaque black; alpha brown;</text>', '<text x="20" y="994" font-size="13">translucent cyan; unresolved optics magenta; sky purple. Openings are gaps, not light sources.</text>', '</svg>']
        prefix.with_name(prefix.name+f'-{label}.svg').write_text('\n'.join(svg))


def pack_debug(prefix):
    # Bounded streaming compression; retain all debug data without duplicate large text.
    import shutil
    for suffix in ('.obj', '-geometry.obj', '-cells.tsv'):
        source = prefix.with_name(prefix.name+suffix)
        with source.open('rb') as raw, source.with_name(source.name+'.gz').open('wb') as out:
            with gzip.GzipFile(fileobj=out, mode='wb', mtime=0, compresslevel=3) as compressed:
                shutil.copyfileobj(raw, compressed, 1024*1024)
        source.unlink()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tool', type=Path, required=True)
    p.add_argument('--inspector', type=Path, required=True)
    p.add_argument('--datasets', type=Path, required=True)
    p.add_argument('--archives', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    entries = {}
    for name in ('hoth3', 'hoth2', 'mp/ffa3'):
        stem = name.replace('/', '_')
        bsp = args.output / (stem+'.bsp')
        raw = extract(args.archives, name, bsp)
        dataset = args.datasets / (stem+'_a.jkobs')
        rois = [('default', ['auto']*3, 256, 16)]
        if name == 'hoth3':
            rois += [('historical', [3320,700,123],512,32), ('export', [3320,700,123],256,16), ('west', [2300,700,123],256,16), ('upper', [3320,700,635],256,16)]
        elif name == 'hoth2':
            # Fixed outdoor/indoor positions used by the existing runtime camera fixture.
            rois += [('export', ['auto']*3,0,16), ('east', [3000,1000,800],256,16), ('courtyard', [-3000,3000,800],256,16)]
        else:
            entities_offset, entities_length = struct.unpack_from('<II', raw, 8)
            entities = raw[entities_offset:entities_offset+entities_length].decode('latin1')
            spawn = next((block for block in re.findall(r'\{[^{}]*\}', entities) if 'info_player_deathmatch' in block), None)
            if not spawn: raise RuntimeError('No MP source spawn')
            origin = list(map(float,re.search(r'"origin"\s*"([^"]+)"',spawn)[1].split()))
            origin[2] += 32
            dataset = args.output / 'mp_ffa3_geometry.jkobs'
            run([args.inspector.resolve(), bsp, args.output/'mp-source', *origin,256,128,16,4,dataset], args.output/'mp-source.log')
            rois = [('spawn',origin,256,16)]
        for label, centre, radius, minimum in rois:
            key = stem+'-'+label; prefix=args.output/key
            command = [args.tool.resolve(), dataset.resolve(), bsp, prefix, *centre,radius,128,minimum,4,8,'flares']
            timings = run(command, args.output/(key+'.log'))
            report = json.loads(prefix.with_suffix('.json').read_text())
            report.update(timings)
            report['bsp_sha256'] = hashlib.sha256(raw).hexdigest()
            report['input_dataset_sha256'] = hashlib.sha256(dataset.read_bytes()).hexdigest()
            report['command'] = list(map(str,command))
            report['parsed_materials_available'] = name != 'mp/ffa3'
            entries[key]=report
            section_centre = [(report['archive_bounds_min'][k]+report['archive_bounds_max'][k])/2 if radius==0 else report['centre'][k] for k in range(3)]
            slices(prefix, section_centre)
            pack_debug(prefix)
            print(key, report['free'],report['solid'],report['unknown'],report['regions'],flush=True)
    (args.output/'matrix.json').write_text(json.dumps(entries,indent=2)+'\n')


if __name__ == '__main__': main()
