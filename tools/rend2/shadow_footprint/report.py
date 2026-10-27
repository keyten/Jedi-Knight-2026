#!/usr/bin/env python3
"""Version scalar tables and self-authored boundary observations, never stock assets.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path


def write(path,value): path.write_text(json.dumps(value,sort_keys=True,indent=2,allow_nan=False)+'\n',encoding='utf-8')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('benchmark','boundary','compiler','variants','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=False)
    result=json.loads(args.benchmark.read_text()); boundary=json.loads(args.boundary.read_text())
    compiler=json.loads(args.compiler.read_text()); variants=json.loads(args.variants.read_text())
    write(args.output/'benchmark.json',result); write(args.output/'boundary.json',boundary)
    summary=dict(schema='org.openjk.x04-witness.v1',compiler_sha256=compiler['compiler_sha256'],
        fresh_compiler_passed=compiler['passed'],
        compiler_cases={k:dict(passed=v['passed'],bsp_sha256=v['bsp_sha256']) for k,v in compiler['cases'].items()},
        input_hashes={name:hashlib.sha256(getattr(args,name).read_bytes()).hexdigest()
                      for name in ('benchmark','boundary','compiler','variants')},
        variants={k:{key:v[key] for key in ('compiler_sha256','shader_sha256','bsp_sha256','source_override','full_interior_max_error_to_OLD_control_formula')}
                  for k,v in variants.items()},
        claims=dict(historical_raven='UnknownProfile',solver=False,runtime=False,physical_units_for_bsp=False))
    write(args.output/'witness.json',summary)
    for name,rows in (('methods',result['values']),('derivatives',result['derivatives']),
        ('finite_derivatives',result['finite']['derivatives']),('localization',result['localization']['results'])):
        with (args.output/(name+'.csv')).open('w',newline='',encoding='utf-8') as stream:
            writer=csv.DictWriter(stream,fieldnames=list(rows[0])); writer.writeheader()
            writer.writerows({k:json.dumps(v) if isinstance(v,(list,dict)) else v for k,v in row.items()} for row in rows)
    rows=[]
    for r in boundary['records']:
        rows.append({k:r[k] for k in ('id','page','x','y','surface','world','footprint_area','geometric_normal',
            'shading_normal','compiler_sample_origin','compiler_rgb8','old_analytic_rgb8','residual_compiler_minus_old','epsilon_replay_rgb8')})
    with (args.output/'boundary.csv').open('w',newline='',encoding='utf-8') as stream:
        writer=csv.DictWriter(stream,fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    # Scientific/debug SVG without a plotting dependency: exact 35 row positions,
    # compiler red contribution vs old centre prediction and clipped-tree replay.
    svg=['<svg xmlns="http://www.w3.org/2000/svg" width="800" height="780" viewBox="0 0 800 780">',
         '<rect width="800" height="780" fill="white"/>',
         '<g font-family="sans-serif" font-size="14" fill="#18202b">',
         '<text x="24" y="26">Pinned multi_sun: x=-16 WU, floor z=0, sample z=1</text>',
         '<text x="24" y="50">World Y</text><text x="160" y="50">Compiler R</text>',
         '<text x="330" y="50">Old analytic R</text><text x="525" y="50">Tree replay R</text>']
    for i,r in enumerate(sorted(boundary['records'],key=lambda r:r['world'][1])):
        y=75+i*19
        svg.append(f'<text x="35" y="{y}">{r["world"][1]:.0f}</text>')
        for x,key in ((160,'compiler_rgb8'),(330,'old_analytic_rgb8'),(525,'epsilon_replay_rgb8')):
            value=r[key][0]; colour='#e59540' if value>120 else '#6e849d'
            svg.append(f'<rect x="{x}" y="{y-13}" width="110" height="16" fill="{colour}"/><text x="{x+118}" y="{y}">{value}</text>')
    svg.extend(['<text x="24" y="765">Orange: oblique sun admitted. Grey: only vertical sun. All 35 texels retained.</text>','</g></svg>'])
    (args.output/'boundary.svg').write_text('\n'.join(svg)+'\n',encoding='utf-8')
    print('Wrote public own-content witnesses, CSV and boundary SVG')


if __name__=='__main__': main()
