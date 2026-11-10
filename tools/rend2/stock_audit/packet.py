"""Private geometric inspection plates from original stock assets and native mesh queries.

These are geometric diagnostics, never screenshots of remastered rendering.
"""
import argparse
import html
import json
from pathlib import Path

from PIL import Image, ImageDraw
from tools.rend2.bsp_fixtures.catalogue import Assets
from tools.rend2.grid_triangulation.triangulate import Bridge
from tools.rend2.model_instances.instances import bsp_entities
from .run import ROOT, load, sha, write

# Declared diagnostic scene diversity. Inspection never changes the frozen
# numerical cohort. Labels describe intent; actual sky reach stays in receipts.
SCENES = [('hoth2','exterior/model-rich'),('hoth3','interior/blue-lit/model-rich'),
          ('mp/duel10','industrial fixtures'),('mp/duel6','colour/material diversity'),
          ('mp/duel7','yellow-lit fixture'),
          ('mp/ffa3','mixed arena'),('t1_danger','large exterior/model-rich')]


def build(work, base, captures, tool):
    output = work/'inspection'
    output.mkdir(exist_ok=False)
    assets = Assets(base)
    index = []
    try:
        for name, intent in SCENES:
            key = name.replace('/','_')
            folder = work/'maps'/key
            receipt = load(folder/'receipt.json')
            design = load(folder/'roi-design.json')['rois']
            coverage = load(folder/'roi-coverage.json')['rois'] if (folder/'roi-coverage.json').exists() else []
            data,_ = assets.read('maps/'+name+'.bsp')
            external = []
            for entity in bsp_entities(data):
                fields = entity['fields']
                if fields.get('model','').startswith('*') or not fields.get('model'):
                    continue
                try:
                    p = [float(v) for v in fields['origin'].split()]
                    if len(p) == 3:
                        external.append(dict(position=p,entity=entity['ordinal'],model=fields['model']))
                except (KeyError,ValueError):
                    pass
            def model_count(i):
                return sum(all(abs(m['position'][k]-design[i]['centre'][k]) <= 256 for k in range(3)) for m in external)
            # Representative geometry selection is diagnostic only and never feeds
            # discovery or fitting. Prefer the declared exterior's observed sky.
            which = max(range(len(design)),key=lambda i:
                        (coverage[i]['upward_sky_reached'],model_count(i)) if coverage and 'exterior' in intent
                        else (model_count(i),coverage[i]['mapped'] if coverage else 0))
            centre, radius = design[which]['centre'], 256
            roi = [x-radius for x in centre]+[x+radius for x in centre]
            bridge = Bridge(tool, captures[name]['dataset'])
            try:
                mesh = bridge.query('mesh',*roi)
                grid = bridge.query('cells',*roi)['rows']
                point = bridge.point(centre)
                metadata = bridge.metadata
            finally:
                bridge.close()
            markers = [m for m in external if all(roi[k] <= m['position'][k] <= roi[k+3] for k in range(3))]
            cat_path = work/'catalogues'/key/'catalogue.json'
            cat = load(cat_path) if cat_path.exists() else {'fixtures':[]}
            fixtures = [f for f in cat['fixtures'] if all(roi[k] <= f['centroid'][k] <= roi[k+3] for k in range(3))]
            image = Image.new('RGB',(1500,770),'#111927')
            draw = ImageDraw.Draw(image)
            draw.text((20,16),'ASR25 | '+name+' | '+intent+' | original geometric diagnostic',fill='white')
            for panel,axes in enumerate(((0,1),(0,2),(1,2))):
                left,top,size=20+495*panel,65,465
                layer = Image.new('RGB',(size+1,size+1),'#111927')
                ld = ImageDraw.Draw(layer)
                def xy(p): return (size/2+(p[axes[0]]-centre[axes[0]])*size/(2*radius),
                                   size/2-(p[axes[1]]-centre[axes[1]])*size/(2*radius))
                for tri in mesh['triangles']:
                    ld.line([xy(p) for p in tri]+[xy(tri[0])],fill='#536a83',width=1)
                for f in fixtures:
                    for poly in f['footprint']:
                        ld.line([xy(p) for p in poly]+[xy(poly[0])],fill='#ffe16b',width=2)
                for g in grid:
                    if g['space'] != 'free' or not g['present']:
                        continue
                    p=xy(g['position']); q=xy([x+32*n for x,n in zip(g['position'],g['consumer256'])])
                    ld.line((p,q),fill='#59bdd2',width=1)
                for marker in markers:
                    x,y=xy(marker['position'])
                    ld.line((x-5,y-5,x+5,y+5),fill='#ff8070',width=2)
                    ld.line((x-5,y+5,x+5,y-5),fill='#ff8070',width=2)
                image.paste(layer,(left,top))
                draw.rectangle((left,top,left+size,top+size),outline='#91a2b5')
                draw.text((left,top-20),'XYZ'[axes[0]]+' / '+'XYZ'[axes[1]]+' | equal world scale',fill='white')
            facts = dict(map=name,intent=intent,independent_roi=which,roi_world=roi,
                native_mesh_complete=mesh['coverage_complete'], native_point=point,
                dynamic_models=metadata['dynamic_models'],external_model_markers=markers,
                model_presence=receipt['model_presence'],fixture_components_in_roi=len(fixtures),
                coverage=coverage[which] if coverage else None,status=receipt['status'],
                accepted_lights=receipt['accepted_lights'], block_reasons=receipt['blockers'])
            draw.text((20,550),'Wireframe: captured static geometry | yellow: catalogue luminous footprints | cyan: original aggregate grid directions',fill='white')
            draw.text((20,575),'Coral crosses: external model entity origins only. Missing model triangles/emission remain UNKNOWN; never empty-space certificates.',fill='#ffb3a8')
            draw.text((20,600),'ROI '+str(which)+' | mesh complete='+str(mesh['coverage_complete'])+' | dynamic models='+str(metadata['dynamic_models'])+
                      ' | model references='+str(receipt['model_presence']['external_references']),fill='white')
            draw.text((20,625),'Pipeline outcome='+receipt['status']+' | accepted configurations='+str(receipt['accepted_lights'])+
                      ' | source-count truth/absolute SI/exact historical XYZ: UNKNOWN',fill='white')
            if coverage:
                c=coverage[which]
                draw.text((20,650),'Mapped observations='+str(c['mapped'])+' | conditional fit observations='+str(c['conditional_fit'])+
                    ' | upward sky reached/blocked/unknown='+str([c['upward_sky_reached'],c['upward_sky_blocked'],c['upward_sky_unknown']]),fill='white')
            draw.text((20,690),'This plate audits original geometry and evidence. It does not show fitted or remastered illumination.',fill='#d7dba3')
            image.save(output/(key+'.png'))
            write(output/(key+'.json'),facts)
            index.append(dict(map=name,intent=intent,plate=key+'.png',facts=key+'.json',sha256=sha(output/(key+'.png'))))
        # Link existing native fit inspection packets, including unsuccessful ROIs.
        fit_links = [p.relative_to(output.parent).as_posix() for p in sorted((work/'maps').glob('*/fit/*/report.html'))]
        text = '<!doctype html><meta charset="utf-8"><title>ASR25 original-stock inspection</title><style>body{background:#111927;color:#eee;font:16px sans-serif;margin:24px}img{max-width:100%}a{color:#80cef1}</style><h1>ASR25 original-stock geometric inspection</h1><p>Private licensed geometry. Original data, conditional hypotheses; no historical lamp/SI calibration or runtime shading claim. External model markers do not supply missing triangles. Independent ROI selection and source acceptance are separate.</p>'
        for r in index:
            text += '<h2>'+html.escape(r['map']+' - '+r['intent'])+'</h2><img src="'+r['plate']+'"><p><a href="'+r['facts']+'">Native geometry and coverage receipt</a></p>'
        text += '<h2>Frozen fitting-region packets (all outcomes)</h2>'
        for path in fit_links:
            text += '<p><a href="../'+path+'">'+html.escape(path)+'</a></p>'
        (output/'index.html').write_text(text,encoding='utf-8')
        write(output/'manifest.json',dict(scenes=index,fit_packets=fit_links,script_sha256=sha(Path(__file__))))
    finally:
        assets.close()


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work',type=Path,required=True)
    p.add_argument('--base',type=Path,default=ROOT/'build/lightrecon-evidence-runtime/final-source/stock-assets/base')
    p.add_argument('--captures',type=Path,required=True)
    p.add_argument('--tool',type=Path,default=ROOT/'build/s06-gcc/lightrecon_grid_geometry_tool.exe')
    a=p.parse_args()
    build(a.work,a.base,load(a.captures),a.tool)
