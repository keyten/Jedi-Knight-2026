#!/usr/bin/env python3
"""Read-only licensed JA stock/PBR audit through the real C++ classifier probe.

python tools/rend2_material_audit.py --base <base> --probe <test.exe> --output <report.json>
Reads assets0-3.pk3 and assets8_pbr*.pk3 only. Requires Pillow for RMO histograms.
No renderer, GL context or game is started. Does not extract/distribute assets.
The audit uses full-resolution histograms; runtime uses <=4096 jittered samples.
"""
import argparse
import collections
import io
import json
from pathlib import Path
import re
import subprocess
import zipfile

from PIL import Image
from rend2_pbr_inventory import parse_shader_file, parse_shader_body, strip_ext

JA = {
    'solidwood': 'wood', 'hollowwood': 'wood', 'solidmetal': 'metal', 'hollowmetal': 'metal',
    'shortgrass': 'grass', 'longgrass': 'grass', 'dirt': 'soil', 'sand': 'sand',
    'gravel': 'gravel', 'glass': 'glass', 'concrete': 'concrete', 'marble': 'rock',
    'water': 'water', 'snow': 'snow', 'ice': 'ice', 'flesh': 'skin', 'mud': 'mud',
    'bpglass': 'glass', 'dryleaves': 'foliage', 'greenleaves': 'foliage',
    'fabric': 'fabric', 'canvas': 'fabric', 'rock': 'rock', 'rubber': 'rubber',
    'plastic': 'plastic', 'tiles': 'tile', 'carpet': 'carpet', 'plaster': 'plaster',
    'shatterglass': 'glass', 'armor': 'plastic', 'computer': 'electronics',
}


def rmo_stats(archive, member):
    with Image.open(io.BytesIO(archive.read(member))) as image:
        red, green, blue, alpha = image.convert('RGBA').split()
        rh, mh = red.histogram(), green.histogram()
        n = sum(mh)
        return [sum(i * v for i, v in enumerate(mh)) / (255 * n),
                sum(i * v for i, v in enumerate(rh)) / (255 * n),
                sum(mh[204:]) / n, sum(mh[:52]) / n, n]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', required=True, type=Path)
    parser.add_argument('--probe', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    archives, scripts, images, shaders = [], [], {}, {}
    try:
        for path in sorted(args.base.glob('*.pk3')):
            if not re.fullmatch(r'assets[0-3]\.pk3|assets8_pbr\d+\.pk3', path.name, re.I):
                continue
            archive = zipfile.ZipFile(path)
            archives.append(archive)
            for member in archive.namelist():
                lower = member.lower()
                if lower.endswith('.shader'):
                    scripts.append((archive, member))
                if lower.endswith(('.tga', '.png', '.jpg', '.jpeg', '.dds')):
                    images[strip_ext(lower)] = (archive, member)
        for archive, member in scripts:
            text = re.sub(r'/\*.*?\*/', '', archive.read(member).decode('latin1'), flags=re.S)
            for name, body in parse_shader_file(text):
                info = parse_shader_body(body)
                material = re.search(r'(?im)^\s*(?:q3map_material|material)\s+(\w+)', body)
                keyword = 'sky' if re.search(r'(?im)^\s*skyparms\b', body) else ''
                if not keyword and re.search(r'(?im)^\s*surfaceparm\s+water\b', body):
                    keyword = 'water'
                shaders[name] = (info['diffuse'] or name, JA.get(material[1].lower(), '') if material else '', keyword)
        # Implicit stock/PBR textures are registered without .shader definitions.
        for image_name in images:
            if image_name.endswith('_rmo'):
                name = image_name[:-4]
                shaders.setdefault(name, (name, '', ''))
        rows, stats_cache, errors = [], {}, []
        for name, (diffuse, material, keyword) in sorted(shaders.items()):
            key = strip_ext(diffuse) + '_rmo'
            stats = [0, 0, 0, 0, 0]
            if key in images:
                if key not in stats_cache:
                    archive, member = images[key]
                    try:
                        stats_cache[key] = rmo_stats(archive, member)
                    except (OSError, ValueError) as exc:
                        errors.append({'texture': key, 'error': str(exc)})
                        stats_cache[key] = stats
                stats = stats_cache[key]
            rows.append('\t'.join(map(str, [name, diffuse, material, keyword] + stats)))
        result = subprocess.run([str(args.probe.resolve()), '--probe'], input='\n'.join(rows) + '\n',
                                text=True, capture_output=True, check=True)
        records = []
        for line in result.stdout.splitlines():
            name, cls, source, confidence, flags, reason = line.split('\t')
            records.append(dict(shader=name, type=cls, source=source, confidence=int(confidence),
                                flags=int(flags), reason=reason))
        report = dict(shader_files=len(scripts), shaders=len(records),
                      rmo_images_read=len(stats_cache), image_errors=errors,
                      types=dict(collections.Counter(r['type'] for r in records)),
                      sources=dict(collections.Counter(r['source'] for r in records)),
                      mixed_pbr=sum(bool(r['flags'] & 1) for r in records),
                      conflicting_pbr=sum(bool(r['flags'] & 2) for r in records),
                      records=records)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps({k: v for k, v in report.items() if k != 'records'}, indent=2))
    finally:
        for archive in archives:
            archive.close()


if __name__ == '__main__':
    main()
