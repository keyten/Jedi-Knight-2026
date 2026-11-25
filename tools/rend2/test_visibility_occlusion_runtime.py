"""Private frozen SP renderer A/B checks; writes only in this worktree.

Engine/game binaries and installed assets are read only. Own renderer DLLs are
required. Tests default/compact ordering, query-on/off, prepass-off fallback,
r_nocurves, a camera turn and optional velocity/shadow initialization. Foreign
games block launch; CPU/GPU timing must additionally have an exclusive lease.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
from types import SimpleNamespace
from test_character_cull_runtime import prepare as prepare_character, foreign_games
from test_depth_prepass_runtime import tga_pixels

ROOT = Path(__file__).resolve().parents[2]


def prepare(args):
    staged = ROOT / 'build/visibility-runtime' / args.label / 'binaries'
    staged.mkdir(parents=True, exist_ok=True)
    for name in ('openjk_sp.x86_64.exe', 'jagamex86_64.dll', 'rend2_glslc.exe'):
        source = args.engine_binaries / name
        if source.exists(): shutil.copy2(source, staged / name)
    shutil.copy2(args.binaries / 'rdsp-rend2_x86_64.dll', staged / 'rdsp-rend2_x86_64.dll')
    config = SimpleNamespace(label='visibility-'+args.label, binaries=staged, installation=args.installation,
                            gl43=args.gl43, velocity=args.velocity, shadows=args.shadows, benchmark=False)
    runtime, home, launch = prepare_character(config)
    path = home / 'autoexec_sp.cfg'
    old = path.read_text()
    settings = old[:old.index('wait 90')]
    settings += 'set r_characterCullStats 0\nset r_modelOcclusionDebug 1\nset r_worldSurfaceList 2\n'
    out = ['wait 90', 'devmap '+args.map, 'wait 180', 'exitview', 'cam_disable',
           'lock_disable', 'god', 'notarget', 'noclip', 'cg_thirdPerson 0']
    if args.map == 'mp/ffa3':
        for i in range(12):
            out += [f'setviewpos {431+(i%6)*100} {-769-(i//6)*120} -70 270', 'wait 10',
                    'npc spawn stormtrooper', 'wait 25']
        out += ['setviewpos 671 -369 -70 270', 'wait 30']
    out += ['uimenu cull_freeze', 'fx_freeze 1', 'wait 30']
    def phase(name, commands):
        return commands + ['echo VIS_PHASE_BEGIN_'+name, 'wait 90', 'screenshot_tga vis_'+name,
                           'echo VIS_PHASE_END_'+name]
    out += phase('reference', ['r_worldSurfaceList 0', 'r_modelOcclusion 0'])
    out += phase('compact', ['r_worldSurfaceList 2'])
    out += phase('compact_default', ['r_worldSurfaceList 1'])
    out += phase('query_reference', ['r_modelOcclusion 2'])
    out += phase('occlusion', ['r_modelOcclusion 1'])
    out += phase('repeat', ['r_worldSurfaceList 0', 'r_modelOcclusion 0'])
    out += phase('curves_reference', ['r_nocurves 1'])
    out += phase('curves_compact', ['r_worldSurfaceList 2', 'r_modelOcclusion 1'])
    out += ['r_nocurves 0']
    out += phase('no_prepass_reference', ['r_depthPrepass 0', 'r_modelOcclusion 0'])
    out += phase('no_prepass_enabled', ['r_modelOcclusion 1'])
    out += ['r_depthPrepass 1']
    if args.map == 'mp/ffa3':
        out += ['uimenu cull_resume', 'wait 10', 'setviewpos 671 -669 -70 270', 'wait 30',
                'uimenu cull_freeze', 'wait 20']
        out += phase('near_reference', ['r_modelOcclusion 2'])
        out += phase('near_enabled', ['r_modelOcclusion 1'])
    if args.benchmark:
        out += ['r_worldSurfaceList 1', 'r_modelOcclusionDebug 0', 'com_maxfps 0', 'com_maxfpsUnfocused 0', 'com_maxfpsMinimized 0']
        for i, mode in enumerate((0,1,1,0)):
            out += [f'r_modelOcclusion {mode}', 'wait 60', f'echo VIS_BENCH_BEGIN_{i}_{mode}',
                    'r_sunShadowBenchmark 240', 'wait 270', f'echo VIS_BENCH_END_{i}_{mode}']
    out += ['echo VISIBILITY_RUNTIME_DONE', 'wait 10', 'quit']
    path.write_text(settings+'\n'.join(out)+'\n')
    return runtime, home, launch


def analyze(runtime, home):
    log = '\n'.join(p.read_text(errors='replace') for p in home.glob('*.log'))
    assert 'VISIBILITY_RUNTIME_DONE' in log, 'run incomplete'
    assert not re.search(r'GL_INVALID|GL_OUT_OF_MEMORY|FBO.*incomplete|GLSL.*(?:failed|error)|surface list mismatch', log, re.I)
    names = ['reference','compact','compact_default','query_reference','occlusion','repeat','curves_reference','curves_compact',
             'no_prepass_reference','no_prepass_enabled']
    if (home/'screenshots/vis_near_reference.tga').exists(): names += ['near_reference','near_enabled']
    images = {name:tga_pixels(home/'screenshots'/('vis_'+name+'.tga')) for name in names}
    pairs = [('reference','compact'), ('reference','compact_default'), ('query_reference','occlusion'), ('reference','repeat'),
             ('curves_reference','curves_compact'), ('no_prepass_reference','no_prepass_enabled')]
    if 'near_reference' in images: pairs += [('near_reference','near_enabled')]
    differences = {}
    for a,b in pairs:
        assert len(images[a]) == len(images[b])
        difference = sum(x!=y for x,y in zip(images[a],images[b]))
        differences[a+'/'+b] = difference
        assert difference == 0, (a,b,difference)
    counts = [tuple(map(int,m)) for m in re.findall(r'model occlusion: resolved=(\d+) hidden=(\d+) guardedDraws=(\d+) skippedDraws=(\d+) skippedIndices=(\d+)',log)]
    phases = {name:re.findall(r'model occlusion:[^\r\n]*',body)
              for name,body in re.findall(r'VIS_PHASE_BEGIN_(\w+)(.*?)VIS_PHASE_END_\1',log,re.S)}
    benchmarks=[]
    for index,mode,body in re.findall(r'VIS_BENCH_BEGIN_(\d+)_(\d+)(.*?)VIS_BENCH_END_\1_\2',log,re.S):
        match=re.search(r'sunShadowBenchmark: (\d+) frames, mean ([\d.]+) ms, median ([\d.]+) ms, p95 ([\d.]+) ms',body)
        assert match, ('missing benchmark',index)
        benchmarks.append(dict(index=int(index),enabled=int(mode),frames=int(match[1]),mean_ms=float(match[2]),median_ms=float(match[3]),p95_ms=float(match[4])))
    result = dict(capture_channel_differences=differences, counts=counts, phase_counts=phases, benchmarks=benchmarks)
    (runtime/'results.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--installation',type=Path,required=True)
    p.add_argument('--binaries',type=Path,default=ROOT/'build/msvc/Release')
    p.add_argument('--engine-binaries',type=Path,required=True)
    p.add_argument('--label',default='correctness')
    p.add_argument('--map',default='mp/ffa3')
    p.add_argument('--gl43',type=int,choices=(0,1),default=0)
    p.add_argument('--velocity',action='store_true')
    p.add_argument('--shadows',action='store_true')
    p.add_argument('--benchmark',action='store_true')
    p.add_argument('--run',action='store_true')
    p.add_argument('--analyze-only',action='store_true')
    args=p.parse_args()
    runtime,home,launch=prepare(args)
    if args.analyze_only: analyze(runtime,home)
    elif args.run:
        foreign=foreign_games()
        assert not foreign, ('foreign game processes',foreign)
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
        (runtime/'run-meta.json').write_text(json.dumps(dict(foreign_before=foreign,launch=launch),indent=2))
        with (runtime/'launch.log').open('w') as output:
            process=subprocess.Popen(launch,cwd=runtime,startupinfo=startup,stdout=output,stderr=output)
            try: code=process.wait(timeout=900)
            except subprocess.TimeoutExpired:
                process.terminate();raise
            assert code==0,code
        assert not foreign_games(), 'foreign games appeared during the run'
        analyze(runtime,home)
    else: print('Prepared '+str(runtime))
