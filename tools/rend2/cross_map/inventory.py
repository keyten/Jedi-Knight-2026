#!/usr/bin/env python3
"""Enumerate real licensed PK3 members with engine archive precedence.
No payload is copied into the repository. GPL-2.0-or-later.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

HERE=Path(__file__).resolve().parent


def inventory(archives,private,policy):
    wanted=policy['selection']['sp_priority']+policy['selection']['mp_priority']+policy['selection']['regression']
    found={};catalogue={}
    for path in sorted(archives.glob('assets?.pk3')):
        entries={}
        with zipfile.ZipFile(path) as z:
            names={n.lower():n for n in z.namelist()}
            for name in wanted:
                key='maps/'+name+'.bsp'
                if key not in names:continue
                info=z.getinfo(names[key]);entries[name]=dict(member=names[key],uncompressed_bytes=info.file_size,zip_crc32=f'{info.CRC:08x}')
                found[name]=dict(archive=str(path.resolve()),**entries[name])
        catalogue[path.name]=dict(bytes=path.stat().st_size,maps=entries,total_bsp_members=sum(n.endswith('.bsp') for n in names))
    sp=[n for n in policy['selection']['sp_priority'] if n in found][:policy['selection']['sp_take']]
    mp=[n for n in policy['selection']['mp_priority'] if n in found][:policy['selection']['mp_take']]
    return dict(schema='org.openjk.x06-availability.v1',private_root=str(private.resolve()),archives=catalogue,selected_new=sp+mp,
        regression=[n for n in policy['selection']['regression'] if n in found],missing=[n for n in wanted if n not in found],available=found)


def archive_hashes(archives):
    result={}
    for path in sorted(archives.glob('assets?.pk3')):
        h=hashlib.sha256()
        with path.open('rb') as f:
            for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
        result[path.name]=dict(bytes=path.stat().st_size,sha256=h.hexdigest())
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--archives',type=Path,required=True);p.add_argument('--private',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--hashes',type=Path)
    a=p.parse_args();r=inventory(a.archives,a.private,json.loads((HERE/'policy.json').read_text()))
    a.output.write_text(json.dumps(r,indent=2)+'\n',encoding='utf-8')
    if a.hashes:a.hashes.write_text(json.dumps(archive_hashes(a.archives),indent=2)+'\n',encoding='utf-8')
    print('selected:',r['selected_new'],'missing:',r['missing'])
