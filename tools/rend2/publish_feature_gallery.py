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
            source=ROOT/item['runtime']/'home/OpenJK/screenshots'/(item.get('name',slug)+'-'+label+'.tga')
            image=Image.open(source).convert('RGB')
            assert image.size==(1920,1080),'Use native full HD captures'
            name=slug+'-'+label+'.jpg'
            output=directory/name
            image.save(output,quality=94,subsampling=0,optimize=True)
            shutil.copy2(output,site_dir/name)
            hashes[label]=hashlib.sha256(source.read_bytes()).hexdigest()
            entries.append(dict(type='image',src='assets/media/gallery/'+page+'/'+name,
                caption=label.capitalize()+': '+item[label]+' ('+item['map']+')'))
        media.append('    '+repr(fid)+': '+repr(entries)+',')
        doc=ROOT/'docs/rendering-features'/(page+'.md')
        text=doc.read_text(encoding='utf-8')
        title=next(x['title'] for x in coverage if x['id']==fid)
        heading='## '+title+'\n'
        assert heading in text
        example=('\n<!-- gallery:'+fid+' -->\n'
            '**Before / after:** '+item['map']+', '+item['description']+' '+item.get('note','').strip()+'\n\n'
            '!['+item['before']+'](../rendering-gallery/'+page+'/'+slug+'-before.jpg)\n\n'
            '!['+item['after']+'](../rendering-gallery/'+page+'/'+slug+'-after.jpg)\n'
            '<!-- /gallery:'+fid+' -->\n')
        assert '<!-- gallery:'+fid+' -->' not in text,'Already published'
        # Put examples after the section prose, preserving its existing structure.
        start=text.index(heading)+len(heading)
        end=text.find('\n## ',start)
        if end<0:end=len(text)
        text=text[:end].rstrip()+'\n'+example+'\n'+text[end:].lstrip('\n')
        doc.write_text(text,encoding='utf-8')
        for row in coverage:
            if row['id']==fid:
                row.update(status='published-prepared',media=entries,location=dict(map=item['map'],
                    pose=item['pose'],description=item['description']),batch=a.batch)
        receipts.append(dict(**item,original_sha256=hashes))
    value=cards.read_text(encoding='utf-8')
    assert 'MEDIA = {\n' in value
    cards.write_text(value.replace('MEDIA = {\n','MEDIA = {\n'+'\n'.join(media)+'\n',1),encoding='utf-8')
    coverage_path.write_text(json.dumps(coverage,indent=2),encoding='utf-8')
    (ROOT/'tools/rend2/feature_gallery'/('batch-'+a.batch+'.json')).write_text(json.dumps(receipts,indent=2))
    print('Prepared ten reviewed comparisons; build and inspect before commit/push.')

if __name__=='__main__':main()
