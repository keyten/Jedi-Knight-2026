"""Repeat stock model emission extraction; retain metadata, no redistributed assets."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re

from emission import extract,write
from model_instances import instances as models
from bsp_fixtures.catalogue import digest

PROOFS={
    'models/map_objects/hoth/light_wall.md3':{2},
    'models/map_objects/hoth/bed_lamp.md3':{1},
    'models/map_objects/factory/ceiling_light.md3':{0},
}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base',type=Path,required=True)
    parser.add_argument('--maps',nargs='+',default=['hoth3','t2_wedge'])
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--mask-axis',type=int,default=32)
    args=parser.parse_args()
    if any(not re.fullmatch('[a-z0-9_-]+',name) for name in args.maps):parser.error('Expected lowercase bare map names')
    assets=models.Assets(args.base)
    args.output.mkdir(parents=True,exist_ok=True)
    report=dict(schema='org.openjk.research.model-emission-inventory/1',maps={},
                scope='Conditional emissive RGB support from actual stock static placements; no historical or radiometric certification',
                mask_axis=args.mask_axis,threshold=.1)
    try:
        for name in args.maps:
            data,source=assets.read('maps/'+name+'.bsp')
            if data is None:raise ValueError('Missing original BSP '+name)
            catalogue=models.build(data,assets)
            result=extract(catalogue,assets,mask_axis=args.mask_axis)
            examples=[]
            excluded_factory=[i for i in catalogue['instances'] if i['model']=='models/map_objects/factory/ceiling_light.md3' and not i['static_eligible']]
            excluded_ids={i['id'] for i in excluded_factory}
            if any(r['instance_id'] in excluded_ids for r in result['regions']):
                raise AssertionError('Breakable factory lights became static emission geometry')
            for model,allowed_surfaces in PROOFS.items():
                regions=[r for r in result['regions'] if r['model']==model]
                if not regions:continue
                if any(not set(r['md3_surfaces']).issubset(allowed_surfaces) for r in regions):
                    raise AssertionError('Stock emitter escaped the independently inspected emissive submesh: '+model)
                instance_id=regions[0]['instance_id']
                diagnostic=next(d for d in result['diagnostics'] if d['instance_id']==instance_id)
                if not 0<diagnostic['resolved_RGB_fraction']<1:
                    raise AssertionError('Expected emission only on part of the stock object: '+model)
                region=regions[0]
                examples.append(dict(model=model,instance_id=instance_id,model_asset=region['model_asset'],
                    model_sha256=result['model_manifest'][region['model_asset']]['sha256'],
                    resolved_instance_RGB_fraction=diagnostic['resolved_RGB_fraction'],
                    md3_surface_ordinals=sorted({s for r in regions for s in r['md3_surfaces']}),
                    instance_count=len({r['instance_id'] for r in regions}),region_count=len(regions),
                    representative_region={k:region[k] for k in ('id','source_id','centroid','outward','emitting_area','colour_prior','issues')},
                    material=region['material'],shader_provenance={k:result['shader_manifest'][region['material']][k]
                        for k in ('source','file_sha256','declaration_sha256')},
                    mask_provenance={l['texture']:result['mask_manifest'][l['texture']] for l in region['layers']}))
            result.update(map_path='maps/'+name+'.bsp',map_source=source)
            output=args.output/name
            write(result,output)
            before={p.name:digest(p.read_bytes()) for p in output.iterdir() if p.suffix in ('.json','.tsv','.obj')}
            repeat=extract(catalogue,assets,mask_axis=args.mask_axis)
            repeat.update(map_path='maps/'+name+'.bsp',map_source=source)
            write(repeat,output)
            after={p.name:digest(p.read_bytes()) for p in output.iterdir() if p.name in before}
            if before!=after:raise AssertionError('Repeated stock export differs: '+name)
            report['maps'][name]=dict(map_sha256=digest(data),source=source,counts=result['counts'],examples=examples,
                excluded_factory_instances=[{k:i[k] for k in ('id','entity_ordinal','classname','role','model','asset_status','issues')} for i in excluded_factory],
                output_sha256=before,repeated_export_byte_equal=True,
                issues=dict(sorted(Counter(issue for r in result['regions'] for issue in r['issues']).items())),
                total_resolved_RGB_area=sum(f['emitting_RGB_area'] for f in result['fixtures']))
            print(name,json.dumps(result['counts'],sort_keys=True),'stock model proofs=',len(examples))
        (args.output/'inventory.json').write_text(json.dumps(report,sort_keys=True,indent=2)+'\n',encoding='utf-8')
    finally:assets.close()


if __name__=='__main__':main()
