"""Private stock geometry plates and OBJ diagnostics; original coordinate frame.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import html
import json
from pathlib import Path
from PIL import Image, ImageDraw


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def report(folder, spec):
    geometry = load(folder/'geometry.json')
    folds = load(folder/'folds.json')
    patches = load(folder/'patches.json')
    result = load(folder/'result.json')
    selected = result['selected']
    source = spec['source_position']
    area = spec.get('kind') == 'surface-mesh'
    if area:
        for config in result['configurations']:
            if config['sources']:
                source = config['sources'][0]['position']
                break
    witness = geometry['grid_witness']
    checks = {c['ordinal']: c for c in witness['checks']}
    image = Image.new('RGB', (1500, 1050), '#111927')
    draw = ImageDraw.Draw(image)
    draw.text((20, 12), spec['name']+' | ORIGINAL BSP FRAME | conditional aesthetic hypothesis', fill='white')
    triangles = geometry['mesh']['triangles']
    fixture = spec['fixture']
    for panel, axes in enumerate(((0, 1), (0, 2), (1, 2))):
        left, top, size = 20+panel*495, 55, 465
        lo = [spec['roi'][k] for k in axes]
        hi = [spec['roi'][k+3] for k in axes]
        span = max(hi[k]-lo[k] for k in range(2))
        def xy(p):
            return (left+size/2+size*(p[axes[0]]-(hi[0]+lo[0])/2)/span,
                    top+size/2-size*(p[axes[1]]-(hi[1]+lo[1])/2)/span)
        draw.rectangle((left, top, left+size, top+size), outline='#506078')
        draw.text((left, top-18), 'XYZ'[axes[0]]+' / '+'XYZ'[axes[1]]+' (WU)', fill='white')
        # ExtractBox may return intersecting triangles with vertices outside ROI.
        # Clip drawing through a temporary panel, never reinterpret missing geometry.
        layer = Image.new('RGB', (size+1, size+1), '#111927')
        ld = ImageDraw.Draw(layer)
        def local(p):
            x, y = xy(p)
            return x-left, y-top
        for tri in triangles:
            ld.line([local(p) for p in tri]+[local(tri[0])], fill='#29384c', width=1)
        image.paste(layer, (left, top))
        for role, rows in folds.items():
            for r in rows:
                x, y = xy(r['position'])
                colour = tuple(r['raw'])
                if role == 'heldout':
                    draw.rectangle((x-3, y-3, x+3, y+3), fill=colour, outline='#ffffff')
                else:
                    draw.ellipse((x-2, y-2, x+2, y+2), fill=colour)
        for poly in [p for f in spec.get('source_fixtures', [fixture]) for p in f['footprint']]:
            draw.line([xy(p) for p in poly]+[xy(poly[0])], fill='#ffe16b', width=3)
        for g in geometry['grid']:
            c = checks.get(g['ordinal'])
            if c is None:
                continue
            start = xy(g['position'])
            end = xy([p+40*d for p, d in zip(g['position'], g['consumer256'])])
            colour = '#50d9eb' if c['angle_degrees'] <= 30 and c['ray']['static_state'] == 'clear' else '#65718b'
            draw.line((start, end), fill=colour, width=1)
            draw.ellipse((end[0]-1, end[1]-1, end[0]+1, end[1]+1), fill=colour)
        for patch in patches['patches']:
            x, y = xy(patch['maximum_world'])
            draw.rectangle((x-6, y-6, x+6, y+6), outline='#f49dff', width=2)
        for position in spec.get('source_positions', [source]):
            x, y = xy(position)
            draw.line((x-10, y, x+10, y), fill='#ff765c', width=3)
            draw.line((x, y-10, x, y+10), fill='#ff765c', width=3)
        hit = spec.get('receiver_path', {}).get('result', {}).get('hit_position')
        if hit is not None:
            for part in range(0, 20, 2):
                points = [[source[k]+(hit[k]-source[k])*t/20 for k in range(3)] for t in (part, part+1)]
                draw.line([xy(p) for p in points], fill='#90e78b', width=2)
            x, y = xy(hit)
            draw.polygon([(x, y-6), (x+6, y), (x, y+6), (x-6, y)], outline='#90e78b')
    draw.text((20, 540), 'Yellow: original luminous footprint | coral cross: fixed mounted point | magenta: training patch peaks', fill='white')
    draw.text((20, 560), 'Cyan: original-grid incoming arrows within 30 deg + known-static clear ray | grey: remaining checked arrows', fill='white')
    draw.text((20, 580), 'Coloured dots: original RGB8 samples | white boxes: held-out | wireframe: extracted static geometry', fill='white')
    draw.text((20, 600), 'Unknown/mover geometry remains conditional; arrows are aggregate directions, not individual-light truth.', fill='white')
    if spec.get('receiver_path'):
        draw.text((20, 620), 'Green diamond/dashes: native known opaque receiver hit; equal world scale on each projection.', fill='#90e78b')
    draw.text((20, 635), 'Visible source material: '+fixture['material']+'; original BSP surface '+str(fixture['surfaces']), fill='white')
    draw.text((20, 655), 'Candidate XYZ: '+str([round(x, 4) for x in source])+
              ('; original-face triangle mesh, uniform emission assumed' if area else '; source standoff = 4 WU (fixed before scores)'), fill='white')
    draw.text((20, 675), 'Fixture ID: '+fixture['id'], fill='white')
    draw.text((20, 695), 'Grid static agreeing distinct records: '+str(witness['known_static_agreeing_records'])+
              '; mover-independent: '+str(witness['mover_independent_agreeing_records']), fill='white')
    draw.text((20, 715), 'Fitted selection: '+str(selected)+'; historical certification: false; stock bake transfer: unknown', fill='white')
    for i, path in enumerate(sorted(folder.glob('source-glow-*.png'))):
        texture = Image.open(path).convert('RGB')
        texture.thumbnail((256, 230))
        image.paste(texture, (20+280*i, 775))
        draw.text((20+280*i, 750), 'Original glow source image', fill='white')
    image.save(folder/'geometry.png')
    obj, index = ['# ASR24 PRIVATE licensed stock diagnostic; not a runtime asset'], 1
    def polygon(group, points):
        nonlocal index
        obj.append('g '+group)
        obj.extend('v '+' '.join(format(v, '.9g') for v in p) for p in points)
        obj.append('f '+' '.join(str(index+i) for i in range(len(points))))
        index += len(points)
    for tri in triangles:
        polygon('original_static_geometry', tri)
    for f in spec.get('source_fixtures', [fixture]):
        for poly in f['footprint']:
            polygon('fixture_'+f['id'], poly)
    for i, position in enumerate(spec.get('source_positions', [source])):
        obj.append('g candidate_point_'+str(i+1))
        obj.append('v '+' '.join(map(str, position)))
        obj.append('p '+str(index))
        index += 1
    for g in geometry['grid']:
        if g['ordinal'] not in checks:
            continue
        obj.append('g original_grid_'+str(g['ordinal']))
        for p in (g['position'], [p+40*d for p, d in zip(g['position'], g['consumer256'])]):
            obj.append('v '+' '.join(map(str, p)))
        obj.append('l '+str(index)+' '+str(index+1))
        index += 2
    (folder/'scene.obj').write_text('\n'.join(obj)+'\n')
    text = html.escape(json.dumps(dict(fixture=fixture['id'], surface=fixture['surfaces'],
                  candidates=[dict(native_fixture=i+1, original_fixture=f['id'], surfaces=f['surfaces'],
                                   material=f['material'], position=spec['source_positions'][i])
                              for i, f in enumerate(spec.get('source_fixtures', []))],
                  receiver_selection=spec.get('receiver_path'),
                  fitted_receiver_surface=spec.get('receiver_surface'),
                  same_receiver_chart_patches=[p['id'] for p in patches['patches']
                      if p['chart']['surface'] == spec.get('receiver_surface')],
                  position=source, outward=fixture['outward'], roi=spec['roi'],
                  result=result, grid_witness={k: v for k, v in witness.items() if k != 'checks'}), indent=2))
    (folder/'report.html').write_text('<!doctype html><meta charset="utf-8"><title>ASR24 '+spec['name']+
        '</title><style>body{background:#111927;color:#eee;font:16px sans-serif;margin:24px}img{max-width:100%}pre{white-space:pre-wrap}</style>'+
        '<h1>'+spec['name']+'</h1><p>Conditional aesthetic hypothesis. Source shape: '+
        ('original face triangle mesh with uniform emission assumed. Masked luminous components and dark gaps are shown separately. ' if area else 'fixture-local isotropic point approximation. ')+
        'Relative RGB amplitudes have no SI calibration. Original glow texture, BSP surface and mask identify a visible fixture independently of fitting. '+
        'RGB8 is compared through a declared forward encoding; unknown transfer, albedo, indirect illumination and mover bake state limit confidence. '+
        'Patch extraction uses training blocks only. No exact historical-source claim.</p><img src="geometry.png">'+
        '<p><a href="scene.obj">Geometry OBJ</a> | <a href="fixture-evidence.json">Source evidence</a> | '+
        '<a href="patches.json">Training patches</a> | <a href="associations.json">Conservative fixture associations</a> | '+
        '<a href="geometry.json">Grid arrows and every checked ray</a> | <a href="solar.json">Solar audit</a></p><pre>'+text+'</pre>', encoding='utf-8')
    page = folder/'report.html'
    page.write_text(page.read_text(encoding='utf-8').replace('<pre>',
        '<p><a href="hypothesis.json">Conditional source configuration</a> | <a href="ablation.json">Frozen source-removal audit</a> | <a href="assessment.json">Assessment</a></p><pre>', 1), encoding='utf-8')
