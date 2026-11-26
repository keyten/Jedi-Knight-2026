"""Measure A/B/A captures and produce a private review contact sheet."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw, ImageStat

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runtime',type=Path)
    parser.add_argument('--allow-no-repeat',action='append',default=[],help='Explicit latched feature with only the reviewed A/B restart')
    args=parser.parse_args()
    shots=args.runtime/'home/OpenJK/screenshots'
    before=sorted(shots.glob('*-before.tga'))
    assert before,'No completed comparisons'
    sheet=Image.new('RGB',(960,300*len(before)))
    draw=ImageDraw.Draw(sheet)
    results=[]
    def maximum(im):
        a,b,c=im.split()
        return ImageChops.lighter(ImageChops.lighter(a,b),c)
    for i,path in enumerate(before):
        name=path.stem[:-7]
        paths=[shots/(name+suffix+'.tga') for suffix in ('-before','-after','-repeat')]
        has_repeat=paths[2].exists()
        assert has_repeat or name in args.allow_no_repeat,'Missing repeat: '+name
        images=[Image.open(p).convert('RGB') for p in paths[:3 if has_repeat else 2]]
        assert len(set(im.size for im in images))==1,'Capture dimensions changed'
        delta=ImageChops.difference(images[0],images[1])
        repeat=ImageChops.difference(images[0],images[2]) if has_repeat else None
        h=maximum(delta).histogram();rh=maximum(repeat).histogram() if has_repeat else None
        results.append(dict(name=name,size=images[0].size,changed_pixels_over_3=sum(h[4:]),
            mean_absolute_channel_difference=sum(ImageStat.Stat(delta).mean)/3,
            repeat_max=max(b for a,b in repeat.getextrema()) if has_repeat else None,
            repeat_pixels_over_1=sum(rh[2:]) if has_repeat else None,
            review='pending'))
        for j,im in enumerate(images[:2]):
            thumb=im.copy();thumb.thumbnail((480,270));sheet.paste(thumb,(j*480,i*300+25))
        draw.text((5,i*300+5),name,fill='white')
    sheet.save(args.runtime/'review.jpg',quality=90)
    (args.runtime/'differences.json').write_text(json.dumps(results,indent=2))
    print(json.dumps(results,indent=2))

if __name__=='__main__':main()
