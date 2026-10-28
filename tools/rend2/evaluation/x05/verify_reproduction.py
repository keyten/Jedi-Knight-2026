#!/usr/bin/env python3
"""Compare pinned bakes without concealing lossy lightgrid non-reproducibility.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
from pathlib import Path
import struct
from fixtures import bsp,write,sha


def resolved_grid(lumps):
    indices=lumps[17];records=lumps[15];result=bytearray()
    if len(indices)%2 or len(records)%30:raise ValueError('Invalid original RBSP grid')
    for i in range(0,len(indices),2):
        index=struct.unpack_from('<H',indices,i)[0]
        if index*30+30>len(records):raise ValueError('Invalid grid reference')
        result.extend(records[index*30:index*30+30])
    return result


def compare(original,rebuilt):
    result={}
    for path in sorted(Path(original).glob('*.bsp')):
        other=Path(rebuilt)/path.name
        a,b=bsp(path),bsp(other);ga,gb=resolved_grid(a),resolved_grid(b)
        different=[k for k in range(len(a)) if a[k]!=b[k]]
        brush_planes=(len(a[9])==len(b[9]) and all(a[9][i:i+8]==b[9][i:i+8] for i in range(0,len(a[9]),12)))
        visual_equal=all(a[k]==b[k] for k in range(len(a)) if k not in (9,15,17))
        result[path.stem]=dict(original_sha256=sha(path),rebuilt_sha256=sha(other),whole_bsp_equal=sha(path)==sha(other),
            different_lumps=different,original_lightmaps_equal=a[14]==b[14],
            visual_geometry_equal=visual_equal,brush_plane_materials_equal=brush_planes,
            brush_draw_surface_references_equal=a[9]==b[9],
            resolved_grid_equal=ga==gb,resolved_grid_byte_mismatches=sum(x!=y for x,y in zip(ga,gb)) if len(ga)==len(gb) else None,
            resolved_grid_max_byte_delta=max((abs(x-y) for x,y in zip(ga,gb)),default=0) if len(ga)==len(gb) else None)
    if not result:raise ValueError('No compiled BSPs')
    return dict(cases=result,surface_witness_reproduced=all(r['original_lightmaps_equal'] and r['visual_geometry_equal'] and r['brush_plane_materials_equal'] for r in result.values()),
        whole_bsp_deterministic=all(r['whole_bsp_equal'] for r in result.values()),
        interpretation='Report grid record ordering/values separately. Never reuse a BSP content hash across these differences. Freeze original artifacts; repeat analysis from identical frozen bytes.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--original',type=Path,required=True);p.add_argument('--rebuilt',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();r=compare(a.original,a.rebuilt);write(a.output,r)
    print('surface_witness_reproduced=',r['surface_witness_reproduced'],'whole_bsp_deterministic=',r['whole_bsp_deterministic'])
    raise SystemExit(0 if r['surface_witness_reproduced'] else 1)
