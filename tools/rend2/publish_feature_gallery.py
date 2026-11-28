"""Prepare an explicitly reviewed group of gallery images and synchronized site inputs."""
import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('selection',type=Path)
    p.add_argument('--batch',required=True)
    p.add_argument('--replace',action='store_true',help='Replace a reviewed gallery group, retaining the same URLs')
    a=p.parse_args()
    selection=json.loads(a.selection.read_text())
    assert len(selection)==10,'Publication batches contain ten reviewed features'
    site=ROOT/'build/feature-gallery/site'
    if not site.exists():
        original=ROOT.parent/'OpenJK-rend2-site'
        site.mkdir(parents=True)
        for name in ('index.html','README.md'):shutil.copy2(original/name,site/name)
        for name in ('assets','tools'):shutil.copytree(original/name,site/name)
    cards=site/'tools/cards.py'
    builder=site/'tools/build.py'
    builder_text=builder.read_text(encoding='utf-8')
    needle='        out, lines, i = [], md.strip("\\n").split("\\n"), 0'
    if 'Strip gallery examples' not in builder_text:
        assert needle in builder_text
        builder_text=builder_text.replace(needle,
            '        # Strip gallery examples here: the modal already shows the same images in MEDIA.\n'
            '        md = re.sub(r"<!-- gallery:.*?<!-- /gallery:[^>]* -->", "", md, flags=re.S)\n'+needle)
        builder.write_text(builder_text,encoding='utf-8')
    media=[];receipts=[]
    coverage_path=ROOT/'tools/rend2/feature_gallery/coverage.json'
    coverage=json.loads(coverage_path.read_text())
    for item in selection:
        assert item['reviewed'] is True
        fid=item['id'];page,slug=fid.split('/')
        directory=ROOT/'docs/rendering-gallery'/page
        directory.mkdir(parents=True,exist_ok=True)
        site_dir=site/'assets/media/gallery'/page;site_dir.mkdir(parents=True,exist_ok=True)
        entries=[];hashes={}
        for label in ('before','after'):
            still=item.get('stills',{}).get(label,item.get('name',slug)+'-'+label+'.tga')
            source=ROOT/item['runtime']/'home/OpenJK/screenshots'/still
            image=Image.open(source).convert('RGB')
            assert image.size==(1920,1080),'Use native full HD captures'
            name=slug+'-'+label+'.jpg'
            output=directory/name
            image.save(output,quality=94,subsampling=0,optimize=True)
            shutil.copy2(output,site_dir/name)
            hashes[label]=hashlib.sha256(source.read_bytes()).hexdigest()
            entries.append(dict(type='image',src='assets/media/gallery/'+page+'/'+name,
                caption=label.capitalize()+': '+item[label]+' ('+item['map']+')'))
        animation_md=''
        if item.get('animation'):
            source=ROOT/item['animation'];name=slug+'-comparison.gif'
            assert Image.open(source).is_animated
            shutil.copy2(source,directory/name);shutil.copy2(source,site_dir/name)
            entries.append(dict(type='image',src='assets/media/gallery/'+page+'/'+name,
                caption='Animated comparison: before left, after right ('+item['map']+')'))
            animation_md='\n![Animated before / after](../rendering-gallery/'+page+'/'+name+')\n'
        media.append('    '+repr(fid)+': '+repr(entries)+',')
        doc=ROOT/'docs/rendering-features'/(page+'.md')
        text=doc.read_text(encoding='utf-8')
        title=next(x['title'] for x in coverage if x['id']==fid)
        heading='## '+title+'\n'
        assert heading in text
        example=('\n<!-- gallery:'+fid+' -->\n'+
            ('**Before / after:** '+item['map']+', '+item['description']+' '+item.get('note','')).rstrip()+'\n\n'
            '!['+item['before']+'](../rendering-gallery/'+page+'/'+slug+'-before.jpg)\n\n'
            '!['+item['after']+'](../rendering-gallery/'+page+'/'+slug+'-after.jpg)\n'
            +animation_md+'<!-- /gallery:'+fid+' -->\n')
        marker='<!-- gallery:'+fid+' -->'
        if marker in text:
            assert a.replace,'Already published; use --replace for a reviewed retake'
            pattern=r'\n?<!-- gallery:'+re.escape(fid)+r' -->.*?<!-- /gallery:'+re.escape(fid)+r' -->\n?'
            text,n=re.subn(pattern,'\n',text,flags=re.S)
            assert n==1,'Duplicate gallery markers'
        # Put examples after the section prose, preserving its existing structure.
        start=text.index(heading)+len(heading)
        end=text.find('\n## ',start)
        if end<0:end=len(text)
        text=text[:end].rstrip()+'\n'+example+'\n'+text[end:].lstrip('\n')
        doc.write_text(text.rstrip()+'\n',encoding='utf-8')
        for row in coverage:
            if row['id']==fid:
                row.update(status='published-prepared',media=entries,location=dict(map=item['map'],
                    pose=item['pose'],description=item['description']),batch=a.batch,
                    runtime=item['runtime'],retake_required=False,
                    grounded=True,route_steps=item.get('route_steps',[]))
        receipts.append(dict(**item,original_sha256=hashes))
    value=cards.read_text(encoding='utf-8')
    assert 'MEDIA = {\n' in value
    if a.replace:
        for item in selection:
            pattern=r'^    '+re.escape(repr(item['id']))+r': .*\n'
            value=re.sub(pattern,'',value,flags=re.M)
    cards.write_text(value.replace('\nMEDIA = {\n','\nMEDIA = {\n'+'\n'.join(media)+'\n',1),encoding='utf-8')
    coverage_path.write_text(json.dumps(coverage,indent=2),encoding='utf-8')
    (ROOT/'tools/rend2/feature_gallery'/('batch-'+a.batch+'.json')).write_text(json.dumps(receipts,indent=2))
    print('Prepared ten reviewed comparisons; build and inspect before commit/push.')

if __name__=='__main__':main()
