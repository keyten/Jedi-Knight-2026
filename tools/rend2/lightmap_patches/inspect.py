"""Private receiver-plane diagnostic PNG (Pillow); no renderer/shading changes.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
from pathlib import Path

from .extract import chart_key, dot, sub
from .run import read_rows


def render(rows_path, report_path, output, index=0):
    from PIL import Image, ImageDraw
    report = json.loads(Path(report_path).read_text(encoding='utf-8'))
    patches = report['patches']
    chosen = patches[index]
    key = tuple(chosen['chart'][k] for k in ('image', 'page', 'surface', 'material', 'slot', 'style'))
    frame = chosen['frame']
    rows = [r for r in read_rows(rows_path, [chosen['chart']['surface']])
            if chart_key(r) == key and r['coverage'] == 2 and r.get('position')]
    def local(p):
        delta = sub(p, frame['origin'])
        return dot(delta, frame['u']), dot(delta, frame['v'])
    uv = [local(r['position']) for r in rows]
    lo = [min(p[k] for p in uv) for k in (0, 1)]
    hi = [max(p[k] for p in uv) for k in (0, 1)]
    scale = min(1000/max(1, hi[0]-lo[0]), 700/max(1, hi[1]-lo[1]))
    def xy(p):
        a, b = local(p)
        return 80+(a-lo[0])*scale, 850-(b-lo[1])*scale
    image = Image.new('RGB', (1200, 980), '#12202a')
    draw = ImageDraw.Draw(image)
    draw.text((25, 15), 'ASR14: original encoded receiver colours / measured features', fill='white')
    draw.text((25, 40), f"Surface {key[2]} | {key[3]} | style {key[5]} | patch {index+1}/{len(patches)}", fill='white')
    radius = max(1, chosen['receiver_pitch_world']*scale*.4)
    for r in rows:
        x, y = xy(r['position'])
        draw.rectangle((x-radius, y-radius, x+radius, y+radius), fill=tuple(r['raw']))
    for contour in chosen['isophotes']:
        colour = '#80ffb0' if contour['radial_support'] else '#d0d0d0'
        for p in contour['points_world']:
            x, y = xy(p)
            draw.ellipse((x-2, y-2, x+2, y+2), fill=colour)
    for g in chosen['gradients']['samples']:
        p = g['position_world']
        a, b = g['tangent_codes_per_world']
        length = max(1e-9, (a*a+b*b)**.5)
        x, y = xy(p)
        draw.line((x, y, x+8*a/length, y-8*b/length), fill='#55cfff')
    x, y = xy(chosen['maximum_world'])
    draw.ellipse((x-8, y-8, x+8, y+8), outline='#ff7777', width=3)
    foot = chosen['constraint']['foot_world']
    if foot:
        x, y = xy(foot)
        draw.line((x-10, y, x+10, y), fill='yellow', width=2)
        draw.line((x, y-10, x, y+10), fill='yellow', width=2)
    draw.text((25, 900), 'Red ring: robust maximum | cyan: tangent code gradients | yellow: conditional radial projection', fill='white')
    draw.text((25, 925), 'Green/grey: supported/other contour samples. Contours may be partial; no calibrated irradiance.', fill='white')
    draw.text((25, 950), 'Receiver footprint orientation does not identify emitter orientation. Unknown geometry stays unknown.', fill='white')
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rows', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--index', type=int, default=0)
    args = parser.parse_args()
    print(render(args.rows, args.report, args.out, args.index))
