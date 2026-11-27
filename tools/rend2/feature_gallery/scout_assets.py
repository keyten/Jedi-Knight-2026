"""Summarize stock-map materials and effects without extracting licensed assets."""
import json
import math
import re
import struct
import sys
import zipfile
from collections import Counter
from pathlib import Path

def main():
    base=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
    maps=sys.argv[3:]
    packs=[zipfile.ZipFile(base/f'assets{i}.pk3') for i in range(4)]
    def read(name):
        for pack in reversed(packs):
            try:return pack.read(name)
            except KeyError:pass
        raise KeyError(name)
    reports=[]
    for name in maps:
        data=read('maps/'+name+'.bsp')
        def lump(i):
            o,n=struct.unpack_from('<ii',data,8+8*i);return data[o:o+n]
        entities=[dict(re.findall(r'"([^"]+)"\s*"([^"]*)"',b)) for b in re.findall(r'\{([^{}]*)\}',lump(0).decode('latin1'))]
        starts=[e for e in entities if e.get('classname')=='info_player_start']
        shaders=[(s.rstrip(b'\0').decode('latin1'),flags,contents) for s,flags,contents in struct.iter_unpack('<64sii',lump(1))]
        vertices=list(struct.iter_unpack('<16f16B',lump(10)))
        surfaces=lump(13);candidates=[];counts=Counter()
        for offset in range(0,len(surfaces),148):
            shader,fog,kind,first,count=struct.unpack_from('<5i',surfaces,offset)
            if not count:continue
            material=shaders[shader][0]
            keys=[k for k,p in [('metal',r'metal|floor|plate|grate|panel|pipe'),('glow',r'light|glow|lamp|screen|lava'),('plants',r'grass|fern|vine|leaf|plant'),('water',r'water|wfall'),('glass',r'glass|window')] if re.search(p,material,re.I)]
            if not keys:continue
            pts=vertices[first:first+count]
            center=[sum(p[i] for p in pts)/count for i in range(3)]
            distance=min((math.dist(center,list(map(float,e['origin'].split()))) for e in starts),default=0)
            counts.update(keys)
            candidates.append(dict(surface=offset//148,shader=material,tags=keys,center=[round(v,1) for v in center],distance_from_start=round(distance)))
        candidates.sort(key=lambda x:x['distance_from_start'])
        effects=[{k:e[k] for k in ('classname','origin','fxFile','model') if k in e} for e in entities if e.get('classname') in ('fx_runner','fx_rain','fx_puff','fx_cloudlayer')]
        report=dict(map=name,starts=starts,materials=dict(counts),near_start=candidates[:50],effects=effects)
        reports.append(report)
        print(name,'start',[(e.get('origin'),e.get('angle')) for e in starts],'materials',dict(counts),'effects',len(effects))
    (out/'asset-scout.json').write_text(json.dumps(reports,indent=2))

if __name__=='__main__':main()
