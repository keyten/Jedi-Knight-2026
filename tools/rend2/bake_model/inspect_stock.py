#!/usr/bin/env python3
"""Read private licensed PK3s; publish only hashes/counts/compiler provenance.

Never classifies a stock compiler from RBSP layout or retained entity counts.
No map, shader, or entity content is copied to the report.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zipfile


def inspect(data):
    if data[:8]!=b'RBSP\x01\0\0\0' or len(data)<152:
        raise ValueError('Expected JA RBSP v1')
    lumps=[]
    for i in range(18):
        off,size=struct.unpack_from('<II',data,8+i*8)
        if off>len(data) or size>len(data)-off:
            raise ValueError('Invalid BSP lump')
        lumps.append(data[off:off+size])
    entities=[]
    for block in re.findall(rb'\{([^{}]*)\}',lumps[0]):
        entities.append(dict((k.decode('ascii','replace'),v.decode('ascii','replace')) for k,v in
                             re.findall(rb'"([^"\r\n]*)"\s*"([^"\r\n]*)"',block)))
    lights=[e for e in entities if e.get('classname','').lower().startswith('light')]
    world=entities[0]
    compiler={k:v for k,v in world.items() if k.lower() in ('_q3map2_version','_q3map2_cmdline','_keeplights')}
    return dict(bsp_sha256=hashlib.sha256(data).hexdigest(),bsp_bytes=len(data),
                light_entities=len(lights),light_entity_field_names=sorted(set(k for e in lights for k in e)),
                compiler_provenance=compiler,profile='UnknownProfile',verification='unverified',
                lightmap_pages=len(lumps[14])//(128*128*3),grid_records=len(lumps[15])//30,
                logical_grid_indices=len(lumps[17])//2)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pk3',type=Path,action='append',required=True)
    p.add_argument('--map',action='append',required=True,help='archive member, e.g. maps/hoth2.bsp')
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():
        p.error('Use a new report path')
    report=dict(schema='org.openjk.stock-bake-inventory',version=1,maps={})
    # Later supplied archives override earlier ones, matching the explicit order.
    for path in a.pk3:
        with zipfile.ZipFile(path) as z:
            names={n.lower():n for n in z.namelist()}
            for name in a.map:
                if name.lower() in names:
                    report['maps'][name]=inspect(z.read(names[name.lower()]))
    missing=set(a.map)-set(report['maps'])
    if missing:
        raise ValueError('Requested maps absent: '+str(sorted(missing)))
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2,sort_keys=True)+'\n',encoding='utf-8')
    print('Inventoried %d maps; all remain unverified UnknownProfile'%len(report['maps']))


if __name__=='__main__':
    main()
