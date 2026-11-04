#!/usr/bin/env python3
"""V02 consolidated runner around existing native/evaluation/stock tooling.

Every phase preserves failed/unsupported outcomes and exact commands/resources.
No reconstruction, field or metric formula is reimplemented here.
GPL-2.0-or-later.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.rend2.all_stock.run import execute,sha,write

PHASES=('cpu','joint','fields','angular','shadow','contributions','queries','cache',
        'compiled','probes','stock','bridge','indirect','solar','shadow_association','uncertainty','shaders','gpu','disabled_delta',
        'renderer_build','runtime_sp','runtime_mp','runtime_cache','report')
DEFAULT_PHASES=tuple(p for p in PHASES if not p.startswith('runtime_'))


def invoke(out,label,cmd,seconds=900,memory=1024**3,expected=0):
    r=execute(cmd,out,label,seconds,memory)
    return dict(status='Passed' if r['state']=='Completed' and r['exit_code']==expected
                else 'BudgetExceeded' if r['state']!='Completed' else 'Failed',
                resources=r,expected_exit=expected)


def run(a):
    policy_path=Path(__file__).with_name('policy-v1.json');policy=json.loads(policy_path.read_text())
    if a.output.exists() and not a.resume:raise ValueError('Use a new output directory or exact resume')
    a.output.mkdir(parents=True,exist_ok=True)
    protocol=a.output/'protocol.json'
    binaries={p.name:sha(p) for p in a.native.glob('lightrecon*.exe')}
    identity=dict(schema='org.openjk.reusable-lighting-protocol/1',policy=policy,
        policy_sha256=sha(policy_path),base_revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        native=binaries,baseline_sha256=sha(ROOT/policy['baseline']),
        environment=dict(os=platform.platform(),cpu=platform.processor(),
                         logical_processors=os.cpu_count(),python=platform.python_version()),
        build='GCC 16.2 Release C++11; MSVC 2019 SP/MP Release; sequential numerical phases',
        source_hashes={str(p.relative_to(ROOT)).replace('\\','/'):sha(p)
                      for p in sorted((ROOT/'shared/rd-rend2/lightrecon').glob('*.[ch]*'))})
    if a.stock_maps:
        if any(n not in policy['stock_maps'] for n in a.stock_maps):raise ValueError('Map outside frozen stock policy')
        identity['selected_stock_maps']=a.stock_maps
    if protocol.exists():
        old=json.loads(protocol.read_text())
        if old!=identity:raise ValueError('Resume dependency/policy changed; use a new versioned run')
    else:write(protocol,identity)
    record=a.output/'phases.json';results=json.loads(record.read_text()) if record.exists() else {}
    py=sys.executable
    def script(path,*args):return [py,'-X','utf8',ROOT/'tools/rend2'/path,*args]
    for phase in a.phases:
        if phase in results:continue
        folder=a.output/phase;folder.mkdir(exist_ok=True)
        try:
            if phase=='cpu':
                result=invoke(folder,'ctest',['ctest','--test-dir',a.native,'--output-on-failure','-j','2'])
            elif phase=='joint':
                result=invoke(folder,'joint',script('joint/benchmark.py','--tool',a.native/'lightrecon_joint_test.exe','--out',folder/'run'))
            elif phase=='fields':
                result=invoke(folder,'fields',script('field_builder/run.py','--out',folder/'run','--test',a.native/'lightrecon_field_builder_test.exe'))
            elif phase=='angular':
                result=invoke(folder,'angular',script('field_representation/benchmark.py','--tool',a.native/'lightrecon_field_basis_tool.exe','--test-tool',a.native/'lightrecon_field_basis_test.exe','--output',folder/'run'),seconds=1800)
            elif phase=='shadow':
                result=invoke(folder,'shadow',script('shadow_cache/run.py','--out',folder/'run','--test',a.native/'lightrecon_shadow_cache_test.exe'))
            elif phase=='contributions':
                result=invoke(folder,'contributions',script('contributions/benchmark.py','--output',folder/'run','--tool',a.native/'lightrecon_contributions_test.exe'))
            elif phase=='queries':
                result=invoke(folder,'queries',[a.native/'lightrecon_validation_tool.exe',folder/'metrics.json'])
            elif phase=='cache':
                checks={}
                cache=folder/'owned-cache'
                checks['correctness']=invoke(folder,'correctness',[a.native/'lightrecon_persistent_test.exe',cache])
                checks['selective']=invoke(folder,'selective',[a.native/'lightrecon_persistent_test.exe',cache/'prepared','benchmark'])
                checks['interrupted']=invoke(folder,'interrupted',[a.native/'lightrecon_persistent_test.exe',folder/'crash-cache','crash'],expected=23)
                checks['verify_interrupted']=invoke(folder,'verify-interrupted',[a.native/'lightrecon_persistent_test.exe',folder/'crash-cache','verify-crash'])
                result=dict(status='Passed' if all(x['status']=='Passed' for x in checks.values()) else 'Failed',checks=checks)
            elif phase=='compiled':
                result=invoke(folder,'compiled',script('producer/benchmark.py','--tool',a.native/'lightrecon_joint_tool.exe',
                              '--views',ROOT/'build/x07-verified','--bakes',ROOT/'build/q3map2-bake-verified','--out',folder/'run'))
            elif phase=='probes':
                result=invoke(folder,'probes',script('probes/run.py','--private',a.private,'--out',folder/'run',
                              '--test',a.native/'lightrecon_probes_test.exe','--tool',a.native/'lightrecon_probes_tool.exe'))
            elif phase=='stock':
                captures=json.loads((a.private/'captures.json').read_text())
                inventory=json.loads((a.private/'inventory.json').read_text())['maps'];maps={}
                for name in (a.stock_maps or policy['stock_maps']):
                    target=folder/name.replace('/','_');target.mkdir()
                    source=a.private/'runs'/name.replace('/','_')/'all-pages.jkobs'
                    if not source.exists():source=Path(captures[name]['dataset'])
                    expected=next(x['sha256'] for x in inventory if x['map']==name)
                    # Dataset identity/complete-page preflight is implemented by native code.
                    maps[name]=invoke(target,'native',[a.native/'lightrecon_map_solver.exe',source,target,expected,'scene-receiver-preflight'])
                    maps[name].update(bsp_sha256=expected,dataset_sha256=sha(source))
                    write(folder/'maps.json',maps)
                result=dict(status='Partial',maps=maps,scope='Process completion is not successful joint reconstruction')
            elif phase=='bridge':
                from tools.rend2.validation.bridge import run as bridge
                # The bridge module creates output/bridge itself.
                folder.rmdir()
                bridge(a,policy['blind_bridge'])
                result=dict(status='Partial',scope='Compiler-backed discovery; scientific gates evaluated separately')
            elif phase=='shaders':
                # Historical source/binding baseline drift is retained as a result.
                result=invoke(folder,'disabled',script('lightrecon_regression.py','--cpp',a.cpp,'--no-gpu','--output',folder/'historical'))
            elif phase=='indirect':
                result=invoke(folder,'indirect',[a.native/'lightrecon_indirect_test.exe',folder/'native'])
            elif phase=='solar':
                result=invoke(folder,'solar',[a.native/'lightrecon_sun_test.exe',folder/'native.json'])
            elif phase=='shadow_association':
                result=invoke(folder,'shadow',[a.native/'lightrecon_shadow_test.exe',folder/'native'])
            elif phase=='uncertainty':
                result=invoke(folder,'uncertainty',script('uncertainty/benchmark.py',
                    '--tests',a.native/'lightrecon_uncertainty_test.exe',
                    '--population',a.native/'lightrecon_uncertainty_population.exe','--out',folder/'run'),seconds=900)
            elif phase=='gpu':
                result=invoke(folder,'gpu',script('lightrecon_regression.py','--cpp',a.cpp,'--output',folder/'run'))
            elif phase=='disabled_delta':
                result=invoke(folder,'delta',script('producer/compare_revision.py','--revision',identity['base_revision'],
                    '--gpu-report',a.output/'gpu/run/report.json','--out',folder/'run'))
            elif phase=='renderer_build':
                result=invoke(folder,'release',['cmake','--build',a.renderer_build,'--config','Release',
                    '--target','rdsp-rend2_x86_64','rd-rend2_x86_64','--parallel','2'],seconds=1800,memory=2*1024**3)
            elif phase in ('runtime_sp','runtime_mp','runtime_cache'):
                if phase=='runtime_mp' and not a.allow_mp_launch:
                    result=dict(status='Unperformed',reason='MP launch requires explicit --allow-mp-launch; may prompt Windows firewall')
                elif not all((a.installation,a.binaries,a.renderer)):
                    result=dict(status='Unperformed',reason='Explicit installation, binaries and renderer required')
                else:
                    runtime_args=['--installation',a.installation,'--binaries',a.binaries,'--renderer',a.renderer]
                    if phase=='runtime_mp':
                        if not all((a.mp_binaries,a.mp_renderer)):raise FileNotFoundError('Explicit MP binaries/renderer required')
                        command=script('validation/runtime_mp.py',*runtime_args,
                            '--mp-binaries',a.mp_binaries,'--mp-renderer',a.mp_renderer,
                            '--allow-mp-launch','--output',folder/'run')
                    elif phase=='runtime_sp':
                        command=script('validation/runtime.py',*runtime_args,'--output',folder/'run')
                    else:
                        command=script('test_lightrecon_cache_runtime.py',*runtime_args,
                            '--native-test',a.native/'lightrecon_persistent_test.exe','--output',folder/'run')
                    result=invoke(folder,phase,command,seconds=1200,memory=1536*1024**2)
            elif phase=='report':
                result=invoke(folder,'report',script('validation/report.py','--input',a.output,
                    '--output',folder/'run','--bridge',a.output,'--runtime',a.output/'runtime_sp/run',
                    '--mp',a.output/'runtime_mp/run','--runtime-cache',a.output/'runtime_cache/run',
                    '--gpu',a.output/'gpu/run','--delta',a.output/'disabled_delta/run'))
            else:raise ValueError(phase)
        except FileNotFoundError as e:
            result=dict(status='Unperformed',reason=str(e))
        except Exception as e:
            result=dict(status='Failed',reason=str(e))
        results[phase]=result;write(record,results)
        print(phase,result['status'],flush=True)
    return results


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--private',type=Path,required=True)
    p.add_argument('--native',type=Path,default=ROOT/'build/s06-gcc')
    p.add_argument('--q3map',type=Path,default=ROOT/'build/toolchain/netradiant-20220628/portable/netradiant_1.5.0-20220628-windows-amd64/q3map2.exe')
    p.add_argument('--cpp',type=Path,default=Path('C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin/gcc.exe'))
    p.add_argument('--phases',nargs='+',choices=PHASES,default=DEFAULT_PHASES)
    p.add_argument('--resume',action='store_true')
    p.add_argument('--allow-mp-launch',action='store_true',help='Only with explicitly selected runtime_mp phase')
    p.add_argument('--stock-maps',nargs='+',help='Explicit repeat subset of the already frozen selection')
    for name in ('installation','binaries','renderer','mp-binaries','mp-renderer'):
        p.add_argument('--'+name,type=Path)
    p.add_argument('--renderer-build',type=Path,default=ROOT/'build/s13-renderers')
    a=p.parse_args();a.output=a.output.resolve();a.native=a.native.resolve();a.private=a.private.resolve()
    results=run(a)
    raise SystemExit(1 if any(r['status'] in ('Failed','BudgetExceeded') for r in results.values()) else 0)
