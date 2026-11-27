"""Build a labeled before/after animation from unmodified gameplay captures."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageDraw

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('runtime',type=Path)
    p.add_argument('name')
    p.add_argument('--frames',type=int,default=16)
    p.add_argument('--duration',type=int,default=133)
    a=p.parse_args();shots=a.runtime/'home/OpenJK/screenshots';frames=[]
    for i in range(a.frames):
        frame=Image.new('RGB',(1120,340),'#05070d');draw=ImageDraw.Draw(frame)
        for side,label in enumerate(('before','after')):
            im=Image.open(shots/(f'{a.name}-{label}-{i:02}.tga')).convert('RGB')
            assert im.size==(1920,1080)
            im=im.resize((560,315),Image.Resampling.LANCZOS)
            frame.paste(im,(560*side,25));draw.text((560*side+12,7),label.capitalize(),fill='white')
        frames.append(frame)
    palette=frames[0].quantize(colors=256)
    frames=[frame.quantize(palette=palette,dither=Image.Dither.NONE) for frame in frames]
    output=a.runtime/(a.name+'-comparison.gif')
    frames[0].save(output,save_all=True,append_images=frames[1:],duration=a.duration,loop=0,optimize=True)
    print(output,output.stat().st_size,'bytes')

if __name__=='__main__':main()
