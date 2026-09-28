#!/usr/bin/env python3
"""Build the SP rendering menu from stock assets and the rendering reference."""
import argparse
import json
import re
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def quote(s):
    return '"' + s.replace('"', "'").replace('\n', ' ') + '"'


def item(name, text, x, y, w, extra='', kind=1):
    return f'''itemDef {{ name {name} rect {x} {y} {w} 19
      type {kind} style 0 visible 1 font 4 textscale 0.8
      textalign 0 textalignx 0 textaligny 0
      forecolor 0.72 0.82 1 1 text {quote(text)} {extra} }}\n'''


def build(assets, reference):
    source = (ROOT / 'code/rd-rend2/tr_init.cpp').read_text(encoding='utf-8')
    regs = {}
    for m in re.finditer(r'(\w+)\s*=\s*(?:ri_Cvar_Get_NoComm|ri\.Cvar_Get)\s*\(\s*"(r_\w+)"\s*,\s*"([^"]*)"\s*,\s*([^;]+)', source):
        regs[m[2]] = {'var': m[1], 'default': m[3], 'restart': 'CVAR_LATCH' in m[4], 'description': (re.findall(r'"([^"]*)"', m[4]) or [''])[0]}
    ranges = {}
    for m in re.finditer(r'Cvar_CheckRange\(\s*(\w+)\s*,\s*([\d.\-]+)f?\s*,\s*([\d.\-]+)f?\s*,\s*(qtrue|qfalse)', source):
        ranges[m[1]] = (float(m[2]), float(m[3]), m[4] == 'qtrue')
    controls = []
    category = section = mode = ''
    seen = set()
    for line in reference.read_text(encoding='utf-8-sig').splitlines():
        if line.startswith('# '): category = line[2:]
        if line.startswith('## '): section = line[3:]; mode = ''
        if line.startswith('**Main controls**'): mode = 'main'
        elif line.startswith('**Other controls**'): mode = 'other'
        elif line.startswith('**Debug'): mode = 'debug'
        if mode not in ('main', 'other'): continue
        m = re.match(r'- `(r_\w+)\s+([^`]*)`\s*[—-]\s*(.*)', line)
        if not m: continue
        name, spec, desc = m.groups()
        if name == 'r_autoPomSilhouette': name = 'r_autoPomSilhouetteMode'
        if name in seen or name not in regs: continue
        # Console commands and gameplay hooks are not persistent user settings.
        if name in ('r_motionBlurReset', 'r_motionBlurShutterScale'): continue
        seen.add(name)
        reg = regs[name]
        reg = dict(reg, name=name, category=category, section=section, spec=spec,
                   description=desc, restart=reg['restart'] or 'vid_restart' in desc)
        reg['range'] = ranges.get(reg['var'])
        controls.append(reg)
    basics = [('r_hdr', 'HDR'), ('r_toneMap', 'Tone mapping'), ('r_autoExposure', 'Auto exposure'),
              ('r_depthPrepass', 'Depth prepass'), ('r_normalMapping', 'Normal mapping'),
              ('r_specularMapping', 'Specular mapping'), ('r_parallaxMapping', 'Parallax mapping'),
              ('r_sunlightMode', 'Realtime sunlight'), ('r_dlightMode', 'Dynamic lighting'),
              ('r_cubeMapping', 'Cubemap reflections')]
    prerequisites = []
    for name, title in basics:
        if name in regs:
            reg = regs[name]
            prerequisites.append(dict(reg, name=name, category='Pipeline prerequisites', section=title,
                                      spec='', range=ranges.get(reg['var'])))
    pages = [('Pipeline prerequisites', prerequisites)]
    for category in dict.fromkeys(c['category'] for c in controls):
        rows = [c for c in controls if c['category'] == category]
        pages += [(category, rows[i:i+13]) for i in range(0, len(rows), 13)]
    with zipfile.ZipFile(assets) as z:
        stock = z.read('ui/ingame.menu').decode('cp1252').replace('\r', '')
        names = {n.lower(): n for n in z.namelist()}
        stock_count = sum(len(re.findall(r'\bmenuDef\b', z.read(names[n.lower()]).decode('cp1252')))
                          for n in re.findall(r'"(ui/[^\"]+\.menu)"', z.read('ui/ingame.txt').decode('cp1252')))
    assert stock_count + len(pages) <= 64, 'UI menu limit exceeded'
    # Insert the entry into the stock menu; other stock screens load unchanged.
    insertion = stock.rfind('}')
    main_end = stock.rfind('}', 0, insertion)
    stock = stock[:main_end] + item('render2026', 'RENDER 2026', 40, 395, 180,
             'action { close all ; open render2026_0 ; }') + stock[main_end:]
    generated = ''
    for p, (title, rows) in enumerate(pages):
        menu = f'render2026_{p}'
        generated += f'''menuDef {{ name {menu} fullScreen 1 visible 0 rect 0 0 640 480
          style 1 backcolor 0.025 0.04 0.075 1 focusColor 1 0.72 0.25 1
          descX 320 descY 407 descScale 0.65 descColor 0.85 0.85 0.85 1 descAlignment 1
          onESC {{ close all ; open ingameMainMenu ; }}
          onOpen {{ setfocus setting_0 ; }}\n'''
        generated += item('title', f'RENDER 2026 | {title}', 28, 23, 590, 'decoration', 0)
        generated += item('hint', f'Page {p+1}/{len(pages)}  |  * = renderer restart / reload required', 28, 48, 590, 'decoration', 0)
        for i, c in enumerate(rows):
            rng = c['range']
            enum = None
            spec = c['spec']
            if re.fullmatch(r'-?\d+(?:\s*\|\s*-?\d+)+', spec): enum = [int(n) for n in re.findall(r'-?\d+', spec)]
            elif re.fullmatch(r'-?\d+\.\.-?\d+', spec):
                lo, hi = map(int, spec.split('..'))
                if hi-lo <= 5: enum = list(range(lo, hi+1))
            elif rng and rng[2] and rng[1]-rng[0] <= 4: enum = list(range(int(rng[0]), int(rng[1])+1))
            label = re.sub(r'(?<=[a-z0-9])(?=[A-Z])', ' ', c['name'][2:])
            label += ' *' if c['restart'] else ''
            desc = (c['section'] + ': ' + c['description']).encode('ascii', errors='replace').decode()[:115]
            extra = f'cvar {quote(c["name"])} descText {quote(desc)} '
            if enum:
                labels = {0:'Off',1:'On'} if enum == [0,1] else {}
                if c['name'] == 'r_toneMapMode': labels = {0:'Legacy',1:'ACES',2:'AgX-like'}
                extra += 'cvarFloatList { ' + ' '.join(f'{quote(labels.get(v,str(v)))} {v}' for v in enum) + ' }'
                kind = 12
            else:
                kind = 4 if c['name'] in ('r_colorGradingLut','r_puddleSlope') else 9
                extra += 'maxChars 128 maxPaintChars 20'
            # Right-aligned labels give all value fields the same starting X.
            row = item(f'setting_{i}', label, 26, 82+i*23, 586, extra, kind)
            row = row.replace('textalign 0 textalignx 0', 'textalign 2 textalignx 330')
            generated += row
        generated += item('previous', '< PREV', 28, 444, 95, f'action {{ close {menu} ; open render2026_{(p-1)%len(pages)} ; }}')
        generated += item('next', 'NEXT >', 145, 444, 95, f'action {{ close {menu} ; open render2026_{(p+1)%len(pages)} ; }}')
        generated += item('apply', 'APPLY / RESTART', 270, 444, 175, 'action { uiScript closeingame ; exec "vid_restart" ; }')
        generated += item('back', 'BACK', 524, 444, 88, 'action { close all ; open ingameMainMenu ; }')
        generated += '}\n'
    final = stock[:stock.rfind('}')] + generated + '\n}\n'
    output = HERE / 'zzzz_render2026_menu.pk3'
    payload = HERE / 'pk3/ui'
    payload.mkdir(parents=True, exist_ok=True)
    (payload / 'ingame.menu').write_text(final, encoding='cp1252')
    readme = '''Jedi Academy 2026 rendering menu (single player)

Copy zzzz_render2026_menu.pk3 into GameData/base (or your active mod).
Restart the game. During gameplay: Esc -> RENDER 2026.
Settings update cvars immediately. APPLY / RESTART runs vid_restart,
which creates buffers and applies latched settings. BACK does not undo edits.
Numeric values: click the value, edit, then press Enter. Right-click multi
choices to cycle backwards. Hover a row for its description.

First page contains existing pipeline prerequisites. Enable HDR and tone
mapping for linear lighting / HDR bloom / motion blur / rain lens. Enable
depth prepass for GTAO and contact shadows; sunlight for sun shadows.
POM needs normal mapping and material height data. LTC saber lights need
Forward+ and LTC. Wetness needs active map rain; puddles/runoff need wetness.
Foliage flutter/plant wind need auto foliage; persistent field needs interaction.
Some effects need updated engine/game modules, authored materials or map data.
Automatic LTC changes may need map reload (r_reloadAreaLights).

Use the matching Jedi Academy 2026 Rend2 binaries. This PK3 adds menus only.
Labels use English to work with stock fonts. Debug views and authoring commands
are intentionally omitted. Existing save/load/setup/datapad screens are retained.
This overrides ui/ingame.menu; mods replacing that menu may conflict.
Remove this PK3 to uninstall. Rendering cvars remain in your configuration.
'''
    (HERE / 'README.txt').write_text(readme, encoding='utf-8')
    (HERE / 'controls.json').write_text(json.dumps(prerequisites+controls, indent=2), encoding='utf-8')
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('ui/ingame.menu', final.encode('cp1252'))
        z.writestr('render2026-menu-readme.txt', readme)
    with zipfile.ZipFile(output) as z:
        assert z.testzip() is None
        assert z.read('ui/ingame.menu').decode('cp1252') == final
    assert final.count('{') == final.count('}')
    assert len(re.findall(r'\bmenuDef\b', final)) == len(pages)+1
    print(f'{output}\n{len(controls)} new controls + {len(prerequisites)} prerequisites; {len(pages)} pages; {stock_count+len(pages)}/64 menus')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--reference', type=Path, required=True)
    args = parser.parse_args()
    build(args.assets, args.reference)
