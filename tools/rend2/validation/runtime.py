#!/usr/bin/env python3
"""V02 reuse of actual SP disabled-consumer A/B/A tests at minimal resolution."""
import argparse
import json
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/rend2'))
sys.path.insert(0,str(ROOT))
import test_lightrecon_legacy_runtime as legacy
from tools.rend2.all_stock.run import write

def run(a):
    a.output.mkdir(parents=True,exist_ok=False)
    original=legacy.settings
    def minimum(profile,policy):
        value=original(profile,policy)
        value.update(r_customwidth=640,r_customheight=360,r_picmip=3,
            com_maxfps=30,com_maxfpsUnfocused=30,com_maxfpsMinimized=30,
            r_subdivisions=16,r_lodbias=2,r_shadowMapSize=256,
            r_textureMode='GL_LINEAR_MIPMAP_NEAREST')
        return value
    legacy.settings=minimum
    # Existing frozen camera and image tolerance, dynamic-light/FX positive control.
    a.build_policy=0;a.label='v02';a.nvidia=False
    results=[]
    for name in ('hoth3','hoth2'):
        for profile in ('legacy','modern'):
            if legacy.game_processes():
                results.append(dict(map=name,profile=profile,status='Unperformed',
                    reason='Another independently running OpenJK process is preserved; no overlapping game validation'))
                write(a.output/'results.json',results)
                continue
            try:
                value=legacy.run_case(a,name,profile)
                results.append(dict(map=name,profile=profile,status='Passed' if value['passed'] else 'Failed',result=value))
            except Exception as e:
                results.append(dict(map=name,profile=profile,status='Failed',reason=str(e)))
            write(a.output/'results.json',results)
    return results

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--installation',type=Path,required=True)
    p.add_argument('--binaries',type=Path,required=True)
    p.add_argument('--renderer',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--timeout',type=int,default=240)
    raise SystemExit(1 if any(r['status']=='Failed' for r in run(p.parse_args())) else 0)
