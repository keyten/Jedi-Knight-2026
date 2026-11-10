"""Private held-out RGB8 support plates; replay the frozen native encoding without refitting."""
import argparse
import json
from pathlib import Path
import subprocess

from PIL import Image, ImageDraw
from .run import ROOT, load, sha, write


def build(work, encoder):
    plates=[]
    for path in sorted((work/'maps').glob('*/fit/*/assessment.json')):
        assessment=load(path)
        if assessment['status'] != 'AestheticCandidate':
            continue
        folder=path.parent
        rows=load(folder/'folds.json')['heldout']
        encoding=folder/'ablation-encode.txt'
        original_hash=sha(encoding)
        output=subprocess.check_output([str(encoder), '--encode', str(encoding)],text=True,timeout=30)
        encoded=json.loads(output)['rows']
        n=len(rows)
        assert len(encoded) == 2*n
        assert original_hash == sha(encoding)
        values=[[r['raw'] for r in rows],encoded[:n],encoded[n:]]
        lo=[min(r[k] for r in rows) for k in ('x','y')]
        hi=[max(r[k] for r in rows) for k in ('x','y')]
        scale=min(12,430/max(hi[k]-lo[k]+1 for k in range(2)))
        image=Image.new('RGB',(1500,710),'#111927')
        draw=ImageDraw.Draw(image)
        draw.text((20,15),'ASR25 | '+assessment['map']+' / '+assessment['name']+' | frozen held-out appearance response',fill='white')
        labels=['Original encoded RGB8 samples','Native fitted source + frozen background','Same frozen background; source removed']
        for panel,colors in enumerate(values):
            left,top=20+panel*495,80
            draw.text((left,top-28),labels[panel],fill='white')
            draw.rectangle((left,top,left+465,top+465),fill='#353d48',outline='#8d9bab')
            for r,color in zip(rows,colors):
                x,y=left+(r['x']-lo[0])*scale,top+(r['y']-lo[1])*scale
                rgb=tuple(max(0,min(255,round(v))) for v in color)
                draw.rectangle((x,y,x+max(1,scale-1),y+max(1,scale-1)),fill=rgb)
        a=load(folder/'ablation.json')
        draw.text((20,565),'Only '+str(n)+' original held-out sample locations are colored. Grey is unmeasured support, never an empty/black lighting prediction.',fill='white')
        draw.text((20,590),'Chart pixels shown at identical scale; fitted display colors are rounded encoded codes. Metrics use unrounded native encoding/interval loss.',fill='white')
        draw.text((20,615),'Frozen on/off held-out interval MSE: '+str(round(a['on']['mse'],4))+' / '+str(round(a['source_removed']['mse'],4))+
                  ' | rows changing >=1 code: '+str(a['rows_with_at_least_one_code_response'])+'/'+str(n),fill='white')
        draw.text((20,650),'Conditional relative appearance only. No refit, SI calibration, exact historical source or whole-map relighting claim.',fill='#d7dba3')
        image.save(folder/'receiver-response.png')
        write(folder/'receiver-response.json',dict(rows=n,on=encoded[:n],source_removed=encoded[n:],
              original_ids=[r['id'] for r in rows],encoder_sha256=sha(encoder),input_sha256=original_hash,
              parameters_refitted=False,source_rgb8_calibrated=False))
        report=folder/'report.html'
        text=report.read_text(encoding='utf-8')
        text=text.replace('<img src="geometry.png">','<img src="geometry.png"><h2>Frozen held-out appearance response</h2><img src="receiver-response.png">')
        report.write_text(text,encoding='utf-8')
        plates.append(dict(region=assessment['name'],plate=str((folder/'receiver-response.png').relative_to(work)),
                           sha256=sha(folder/'receiver-response.png')))
    write(work/'inspection/response-manifest.json',dict(plates=plates,script_sha256=sha(Path(__file__))))


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work',type=Path,required=True)
    p.add_argument('--encoder',type=Path,default=ROOT/'build/s06-gcc/lightrecon_bake_forward_tool.exe')
    a=p.parse_args()
    build(a.work,a.encoder)
