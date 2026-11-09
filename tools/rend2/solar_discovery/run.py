#!/usr/bin/env python3
"""ASR07: compiled discovery controls and conservative stock solar audit.

Reuses Core::Solar and the S02 BSP/UV/fold/forward adapters. Standard library.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.sun_fit import run as solar
from tools.rend2.solar_discovery.policy import decide, semantic, baseline, controlled_authored_sun

BASE = ('sun_oriented', 'sun_shadow', 'sun_with_lamp', 'sky_aperture_positive_v2', 'no_sun')
EXTRA = ('outdoor_local', 'indoor_local', 'distant_local')
REFERENCES = ('shared/rd-rend2/lightrecon/sun_fit.h', 'shared/rd-rend2/lightrecon/sun_fit.cpp',
              'shared/rd-rend2/lightrecon/lightgrid.cpp', 'tools/lightrecon_test/solar_tool.h',
              'tools/lightrecon_test/lightrecon_solar_audit.cpp',
              'tools/rend2/sun_fit/run.py', 'tools/rend2/bake_model/validate_cpp.py',
              'tools/rend2/shadow_footprint/compiler_boundary.py',
              'tools/rend2/solar_discovery/run.py', 'tools/rend2/solar_discovery/policy.py',
              'tools/rend2/solar_discovery/report.py', 'tools/rend2/solar_discovery/test_policy.py',
              'docs/rend2-bake-model.md')


def write(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False)+'\n', encoding='utf-8')


def invoke(args):
    p = subprocess.run(list(map(str, args)), capture_output=True, text=True, timeout=180)
    if p.returncode:
        raise RuntimeError(p.stderr or p.stdout)
    return p.stdout


def compile_scene(compiler, root, name):
    folder = root/name
    base = folder/'authoring/base'
    solar.make_assets(base)
    distant = name == 'distant_local'
    sun = name in ('sun_oriented','sun_shadow','sun_with_lamp')
    sky = name == 'sky_aperture_positive_v2'
    local = name in EXTRA or name == 'sun_with_lamp'
    top = 30272 if distant else 400
    shader = base/'shaders/bake_forward.shader'
    declarations = ('q3map_sun 1 1 1 120 30 55\n' if sun else '')
    declarations += 'q3map_lightRGB 1 1 1\n q3map_skyLight 240 3\n' if sky else ''
    shader.write_text(shader.read_text()+'\ntextures/bake_forward/asr07sky\n{\n surfaceparm sky\n surfaceparm nolightmap\n '+declarations+' skyparms - 512 -\n}\n', encoding='ascii')
    # Matching opaque/sky room pair; the distant fixture extends the enclosing
    # sky above a genuine finite point inside the BSP. No missing geometry.
    geometry = [solar.brush((-400,-400,-16),(400,400,0)),
                solar.brush((-400,-400,0),(-384,400,432)),
                solar.brush((384,-400,0),(400,400,432)),
                solar.brush((-384,-400,0),(384,-384,432)),
                solar.brush((-384,384,0),(384,400,432))]
    if distant:
        geometry += [solar.brush((-400,-400,432),(-384,400,top),'asr07sky'),
                     solar.brush((384,-400,432),(400,400,top),'asr07sky'),
                     solar.brush((-384,-400,432),(384,-384,top),'asr07sky'),
                     solar.brush((-384,384,432),(384,400,top),'asr07sky')]
    if sky:
        geometry += [solar.brush((-384,-384,400),(-272,384,416)),
                     solar.brush((272,-384,400),(384,384,416)),
                     solar.brush((-272,-384,400),(272,-272,416)),
                     solar.brush((-272,272,400),(272,384,416)),
                     solar.brush((-400,-400,432),(400,400,448),'asr07sky')]
    else:
        geometry += [solar.brush((-400,-400,top),(400,400,top+16),
                                 'white' if name in ('indoor_local','no_sun') else 'asr07sky')]
    if name in ('sun_shadow','sun_with_lamp'):
        geometry += [solar.brush((-4,-384,0),(4,120,240))]
    position = (0,0,30000) if distant else (-160,-160,180)
    intensity = 14400000 if distant else 300
    colour = '1 1 1' if distant else '1 .2 .1'
    text = solar.entity({'classname':'worldspawn', 'gridsize':'128 128 512' if distant else '64 64 64'})[:-2]
    text += ''.join(geometry)+'}\n'+solar.entity({'classname':'info_player_start','origin':'-160 -160 64'})
    if local:
        text += solar.entity({'classname':'light','origin':' '.join(map(str,position)), 'light':str(intensity),'_color':colour})
    mp = base/'maps'/(name+'.map'); mp.write_text(text, encoding='ascii')
    commands = []
    for i, stage in enumerate((['-meta','-leaktest','-noflares','-samplesize','16'], ['-vis'],
                              ['-light','-deluxe','-deluxemode','0','-gamma','1','-compensate','1','-exposure','0','-bounce','0','-super','1','-nocollapse'])):
        cmd = [str(compiler),'-game','ja','-fs_basepath',str(base.parent.resolve()),'-fs_game','base','-threads','1',*stage,str(mp.resolve())]
        log = invoke(cmd); (folder/f'stage{i}.log').write_text(log, encoding='utf-8')
        if 'Entity leaked' in log or 'ERROR' in log:
            raise RuntimeError('Compiler rejected '+name)
        commands.append(cmd)
    az, el = map(math.radians,(30,55))
    direction = [math.cos(az)*math.cos(el),math.sin(az)*math.cos(el),math.sin(el)]
    truth = dict(name=name, bsp_sha256=solar.sha(mp.with_suffix('.bsp')), map_sha256=solar.sha(mp),
                 shader_sha256=solar.sha(shader), compiler_sha256=solar.sha(compiler), argv=commands,
                 sun_to_source=direction if sun else None, sun_compiler_rgb=[120]*3 if sun else None,
                 sky_compiler_rgb=[240]*3 if sky else None,
                 local_position=position, local_intensity=intensity, local_colour=colour,
                 source_count=int(sun)+int(local), local_lamp_known=False)
    write(folder/'private-recipe.json', truth)
    return mp.with_suffix('.bsp'), truth


def fit_case(tool, path, folder, name, known_local=False):
    folder.mkdir()
    lumps, casters, folds, rows = solar.observations(path)
    other = {r['id']:[0,0,0] for r in rows}
    excluded = []
    if known_local:
        fixed = folder/'fixed-local.request'
        fixed.write_text(solar.request(path, lumps, casters, rows, {},
                         [solar.source(position=(-160,-160,180),rgb=(300,60,30))],domain=3), encoding='ascii')
        predicted = json.loads(invoke([tool, fixed]))
        for row, value in zip(rows, predicted['rows']):
            if value['available']:
                other[row['id']] = value['rgb']
            else:
                excluded.append(dict(sample=row['id'], partition=row['partition'], status=value['status']))
        rejected = {r['sample'] for r in excluded}
        old_other = other; other = {}; rows = []
        for role in folds:
            folds[role] = [r for r in folds[role] if r['id'] not in rejected]
            for row in folds[role]:
                old = row['id']; row['id'] = len(rows)+1
                rows.append(row); other[row['id']] = old_other[old]
    geometry = folder/'geometry.request'
    geometry.write_text(solar.request(path, lumps, casters, rows, {}, []), encoding='ascii')
    for role, values in folds.items():
        lines = ['JKSOLAR1',str(len(values))]
        lines += [' '.join(map(str,[r['id'],*r['rgb'],r['area'],r['footprint_radius'],*other[r['id']]])) for r in values]
        (folder/(role+'.txt')).write_text('\n'.join(lines)+'\n', encoding='ascii')
    result = json.loads(invoke([tool,'--solar',geometry,*[folder/(role+'.txt') for role in ('train','validation','heldout')]]))
    write(folder/'native.json',result)
    # Sun declaration availability is captured source evidence; its target vector
    # and intensity are never passed as optimizer inputs in these blind searches.
    shader = path.parents[1]/'shaders/bake_forward.shader'
    authored = controlled_authored_sun(shader.read_text())
    decision = decide(result, folds['validation'], authored_sun=authored, encoding_known=True)
    return dict(decision=decision, native_state=result['state'], native_code=result['code'],
                native=semantic(result), known_fixed_local=known_local,
                excluded_original_receivers=excluded,
                fold_sha256={role:solar.sha(folder/(role+'.txt')) for role in folds},
                geometry_request_sha256=solar.sha(geometry),
                authored_shader_sha256=solar.sha(shader), authored_sun_evidence=authored,
                receiver_baseline_world=baseline([r['world'] for r in rows]))


def stock_audit(tool, inputs, archive, output):
    manifest = json.loads((inputs/'manifest.json').read_text())
    if solar.sha(inputs/'train.jsonl') != manifest['sha256']['train.jsonl']:
        raise ValueError('Stock training checksum')
    geometry_bins = {}
    available = []
    with (inputs/'train.jsonl').open(encoding='utf-8') as stream:
        for line in stream:
            r = json.loads(line)
            if r['position'] and r['normal'] and r['style']==0 and r['area']>0:
                available.append(r)
                key = tuple(math.floor(v/512) for v in r['position'])
                if key not in geometry_bins or r['id']<geometry_bins[key]['id']:
                    geometry_bins[key] = r
    points = output/'stock-selection-geometry.txt'
    points.write_text('\n'.join(['JKSOLAROUTDOOR1',str(len(geometry_bins))]+
                      [' '.join(map(str,r['position'])) for _,r in sorted(geometry_bins.items())])+'\n',encoding='ascii')
    seed_file = output/'stock-outdoor-seed.json'
    invoke([tool,archive,'--find-outdoor',points,seed_file])
    seed = json.loads(seed_file.read_text())
    if seed['map_sha256'] != manifest['map_sha256']:
        raise ValueError('Stock outdoor seed map identity')
    centre, radius = seed['centre'], 512
    groups = defaultdict(list)
    for r in available:
        if all(abs(x-c)<=radius for x,c in zip(r['position'],centre)):
            groups[r['surface']].append(r)
    ordered = [sorted(v,key=lambda r:r['id']) for _,v in sorted(groups.items())]
    rows = []
    for i in range(max(map(len,ordered),default=0)):
        for group in ordered:
            if i<len(group) and len(rows)<64:
                rows.append(group[i])
    if not rows:
        raise ValueError('No stock ROI receivers')
    request = output/'stock-receivers.txt'
    request.write_text('\n'.join(['JKSOLARAUDIT1',manifest['map_sha256'],
                       ' '.join(map(str,[*centre,radius,len(rows)]))]+
                       [' '.join(map(str,[r['surface']+1,*r['position'],*r['normal']])) for r in rows])+'\n', encoding='ascii')
    target = output/'stock-native.json'
    invoke([tool,archive,request,target])
    result = json.loads(target.read_text())
    return dict(map_sha256=manifest['map_sha256'], roi_centre=centre, roi_radius_world=radius,
                selection=seed['selection'], outdoor_selection_tested=seed['tested'],
                selection_geometry_sha256=solar.sha(points),
                training_manifest_sha256=solar.sha(inputs/'manifest.json'),
                original_archive_sha256=solar.sha(archive), request_sha256=solar.sha(request),
                selected_surfaces=len(groups), selected_receiver_count=len(rows),
                normal_orientations=len({tuple(r['normal']) for r in rows}),
                raw_rgb8_is_calibrated=False, numerical_profile='UnknownProfile',
                native=result, decision=decide({},[],encoding_known=False))


def validate(report):
    cases = report['cases']
    checks = {}
    for name in ('sun_oriented','sun_shadow','sun_with_lamp_known_local'):
        c = cases[name]
        h = c['native']['heldout']
        checks[name] = (c['decision']['state']=='SunPresent' and c['angular_error_degrees']<2
                        and c['sun_rgb_relative_error']<.02 and bool(h)
                        and all(r['code']==0 and r['rmse']<1 and r['maximum_error']<3
                                and r['coverage']['positive_blocks']>=2 for r in h))
    checks['diffuse_is_not_sun'] = cases['sky_aperture_positive_v2']['decision']['state']=='SunAbsent'
    checks['local_lamps_never_identified_as_sun'] = all(cases[n]['decision']['state']=='SunUncertain' for n in EXTRA)
    checks['dark_indoor_is_unknown_not_zero_sun'] = cases['no_sun']['decision']['state']=='SunUncertain'
    checks['stock_unknown_profile_has_no_numerical_sun'] = (
        report['stock']['decision']['state']=='SunUncertain' and
        ('native' not in report['stock'] or report['stock']['native']['frozen_proposals']==0))
    if 'native' in report['stock']:
        checks['accessible_outdoor_stock_roi_audited'] = (report['stock']['outdoor_selection_tested']>0
                                                        and report['stock']['native']['receiver_count']>0)
    return checks


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--reuse',type=Path,help='Verify and reuse this controller\'s frozen compiled BSPs')
    p.add_argument('--tool',type=Path,default=ROOT/'build/lightrecon-disk-standalone/lightrecon_bake_forward_tool.exe')
    p.add_argument('--audit-tool',type=Path,default=ROOT/'build/lightrecon-disk-standalone/lightrecon_solar_audit.exe')
    p.add_argument('--compiler',type=Path,default=ROOT/'build/toolchain/netradiant-20220628/portable/netradiant_1.5.0-20220628-windows-amd64/q3map2.exe')
    p.add_argument('--stock-inputs',type=Path,default=ROOT/'build/x02/hoth2-views/inputs')
    p.add_argument('--stock-archive',type=Path,default=ROOT/'build/x02/runtime/run/home/rend2/lightrecon/v1/observations/hoth2_a.jkobs')
    a = p.parse_args(); a.output = a.output.resolve(); a.output.mkdir(parents=True,exist_ok=False)
    compiler, tool = a.compiler.resolve(), a.tool.resolve()
    if solar.sha(compiler) != solar.COMPILER_SHA256:
        raise ValueError('Pinned compiler required')
    report = dict(schema='org.openjk.asr07-solar-discovery/1', cases={},
                  compiler_sha256=solar.sha(compiler),tool_sha256=solar.sha(tool),
                  audit_tool_sha256=solar.sha(a.audit_tool),
                  sources={name:solar.sha(ROOT/name) for name in REFERENCES},
                  policy=dict(min_sun_visible_blocks=3,min_sun_visible_baseline_world=256,
                              authored_evidence_required_for_identification=True,
                              local_light_joint_discovery=False,canonical_schema_changed=False,
                              unknown_is_zero=False,runtime_publication=False))
    for name in BASE+EXTRA:
        if a.reuse:
            baked = a.reuse.resolve()/name
            truth = json.loads((baked/'private-recipe.json').read_text())
            path = baked/'authoring/base/maps'/(name+'.bsp')
            for asset, key in ((path,'bsp_sha256'),(path.with_suffix('.map'),'map_sha256'),
                               (baked/'authoring/base/shaders/bake_forward.shader','shader_sha256')):
                if solar.sha(asset) != truth[key]:
                    raise ValueError('Frozen compiled input changed: '+name)
            if truth['compiler_sha256'] != report['compiler_sha256']:
                raise ValueError('Recipe compiler identity changed')
        else:
            path, truth = compile_scene(compiler,a.output,name)
        recipe = (a.reuse.resolve() if a.reuse else a.output)/name/'private-recipe.json'
        variants = (False, True) if name == 'sun_with_lamp' else (False,)
        for known in variants:
            label = name+('_known_local' if known else '')
            result = fit_case(tool,path,a.output/(label+'-fit'),name,known)
            h = next((h for h in result['native']['hypotheses'] if h['model']==2),None)
            best = result['native']['hypotheses'][0] if result['native']['hypotheses'] else None
            expected = truth['sun_to_source']
            result.update(bsp_sha256=truth['bsp_sha256'], recipe_sha256=solar.sha(recipe),
                          truth_sun_present=expected is not None, truth_local_present=name in EXTRA or name=='sun_with_lamp',
                          angular_error_degrees=math.degrees(math.acos(max(-1,min(1,solar.dot(solar.unit(h['to_source']),expected))))) if h and expected else None,
                          sun_rgb_relative_error=math.dist(h['sun_compiler_rgb'],truth['sun_compiler_rgb'])/math.sqrt(solar.dot(truth['sun_compiler_rgb'],truth['sun_compiler_rgb'])) if h and expected else None)
            result['sky_rgb_relative_error'] = (math.dist(best['sky_compiler_rgb'],truth['sky_compiler_rgb'])/
                math.sqrt(solar.dot(truth['sky_compiler_rgb'],truth['sky_compiler_rgb'])) if best and truth['sky_compiler_rgb'] else None)
            report['cases'][label] = result
            print(label, result['native_state'], '->', result['decision']['state'], flush=True)
    if a.stock_archive.is_file() and a.stock_inputs.is_dir():
        report['stock'] = stock_audit(a.audit_tool,a.stock_inputs,a.stock_archive,a.output)
    else:
        report['stock'] = dict(state='Unavailable', reason='Licensed original archive/training ROI not mounted',
                               decision=decide({},[],encoding_known=False))
    report['checks'] = validate(report)
    write(a.output/'results.json',report)
    if not all(report['checks'].values()):
        raise AssertionError('ASR07 checks failed: '+str(report['checks']))


if __name__ == '__main__':
    main()
