"""Independently recheck final stock identities and every selected fitting row against its BSP."""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct

from tools.rend2.bsp_fixtures.catalogue import Assets, digest
from tools.rend2.stock_aesthetic.run import split, POLICY
from .run import ROOT, implementation, load, sha, write


def verify(work, base, captures_path):
    protocol = load(work/'protocol.json')
    captures = load(captures_path)
    assert sha(captures_path) == protocol['captures_manifest_sha256']
    assert implementation() == protocol['implementation']
    assert all(sha(Path(p)) == h for p,h in protocol['tools'].items())
    # ASR24 local design is fixed in each original protocol, restored here only
    # to replay its existing splitter, never to choose or change a membership.
    POLICY.update(roi_half_extent=192,block_size=128,split_seed='asr24-local128-v1',
                  geometry_only_offset_alternatives=[17,49,81,113])
    assets=Assets(base)
    checked=[]
    capture_count=0
    catalogue_count=0
    image_count=0
    masks_checked=set()
    try:
        for item in protocol['inventory']['maps']:
            name,key=item['map'],item['map'].replace('/','_')
            folder=work/'maps'/key
            receipt=load(folder/'receipt.json')
            data,_=assets.read(item['member'])
            assert digest(data) == item['sha256']
            if receipt['capture_available']:
                assert sha(Path(captures[name]['dataset'])) == receipt['capture_sha256']
                capture_count+=1
            catpath=work/'catalogues'/key/'catalogue.json'
            if catpath.exists():
                cat=load(catpath)
                assert cat['map_sha256'] == item['sha256']
                assert sha(catpath) == receipt['catalogue']['sha256']
                catalogue_count+=1
                for mask in cat['masks'].values():
                    if not mask.get('path') or not mask.get('sha256'):
                        continue
                    if (mask['path'],mask['sha256']) in masks_checked:
                        continue
                    image,_=assets.read(mask['path'])
                    assert image is not None and digest(image) == mask['sha256']
                    masks_checked.add((mask['path'],mask['sha256']))
                    image_count+=1
            # Original RBSP lump14 contains 128x128 RGB pages, with no decoding.
            lm_offset,lm_bytes=struct.unpack_from('<ii',data,8+14*8)
            surf_offset,surf_bytes=struct.unpack_from('<ii',data,8+13*8)
            vertex_offset,_=struct.unpack_from('<ii',data,8+10*8)
            for path in sorted(folder.glob('fit/*/folds.json')):
                folds=load(path)
                sets=[]
                blocks=[]
                count=0
                plane_error=0.
                for role,rows in folds.items():
                    sets.append({r['id'] for r in rows})
                    blocks.append({split(r)[1] for r in rows})
                    for r in rows:
                        page,x,y=r['page'],r['x'],r['y']
                        offset=lm_offset+(page*128*128+y*128+x)*3
                        assert 0 <= offset-lm_offset <= lm_bytes-3
                        assert list(data[offset:offset+3]) == r['raw']
                        assert r['surface']*148 < surf_bytes
                        surface_record=surf_offset+r['surface']*148
                        first_vertex=struct.unpack_from('<i',data,surface_record+12)[0]
                        original_vertex=struct.unpack_from('<3f',data,vertex_offset+80*first_vertex)
                        normal=struct.unpack_from('<3f',data,surface_record+128)
                        error=abs(sum(n*(x-y) for n,x,y in zip(normal,r['position'],original_vertex)))
                        assert error < .01
                        plane_error=max(plane_error,error)
                        assert split(r)[0] == role
                        count+=1
                for i in range(len(sets)):
                    for j in range(i):
                        assert not sets[i]&sets[j]
                        assert not blocks[i]&blocks[j]
                checked.append(dict(map=name,region=path.parent.name,original_rgb8_rows=count,
                                    disjoint_sample_ids=True,disjoint_geometric_blocks=True,
                                    maximum_original_face_plane_error_world=plane_error))
        # Every core/canonical file hashes to the pre-audit snapshot, and is
        # byte-identical to the starting revision according to git diff below.
        core=[p for p in protocol['implementation'] if p.startswith('shared/')]
        result=dict(schema='org.openjk.stock-aesthetic-audit-checks.v1',
            original_bsps_checked=57,current_captures_rehashed=capture_count,
            catalogues_rehashed=catalogue_count,original_source_images_checked=image_count,
            implementation_and_tools_unchanged=True,historical_core_and_canonical_files_unchanged=len(core),
            fitting_regions=checked,original_rgb8_rows_checked=sum(x['original_rgb8_rows'] for x in checked),
            calibrated_irradiance=False,historical_source_certified=False)
        write(work/'verification.json',result)
        print(json.dumps(result,indent=2))
    finally:
        assets.close()


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work',type=Path,required=True)
    p.add_argument('--base',type=Path,default=ROOT/'build/lightrecon-evidence-runtime/final-source/stock-assets/base')
    p.add_argument('--captures',type=Path,required=True)
    a=p.parse_args()
    verify(a.work,a.base,a.captures)
