#!/usr/bin/env python3
"""Standalone SVG count heatmaps using only the Python standard library.

World panels project MAPPED receiver cells; missing mappings have no fabricated
world position. The page panel separately accounts for unmapped texels.
"""
import argparse
from collections import defaultdict
import html
import json
import math
from pathlib import Path


def plot(report_path, output):
    d=json.loads(Path(report_path).read_text(encoding='utf-8'))
    groups=d['groups']; mapname=d['source']['map']
    spatial=[]
    for k,v in groups.items():
        if k.startswith('spatial256/'):
            spatial.append((tuple(map(int,k.split('/')[1].split(','))),v['counts']))
    svg=['<svg xmlns="http://www.w3.org/2000/svg" width="1360" height="1110" viewBox="0 0 1360 1110">',
         '<rect width="1360" height="1110" fill="#fff"/>',
         f'<text x="25" y="30" font-family="sans-serif" font-size="20">{html.escape(mapname)} — all-page observation coverage</text>']
    for panel,(a,b,label) in enumerate([(0,1,'XY'),(0,2,'XZ')]):
        totals=defaultdict(int); uncertain=defaultdict(int)
        for p,c in spatial:
            key=(p[a],p[b]); totals[key]+=c.get('mapped',0); uncertain[key]+=c.get('material_uncertain',0)
        if not totals: continue
        xs=[p[0] for p in totals]; ys=[p[1] for p in totals]
        lo=[min(xs),min(ys)]; hi=[max(xs)+1,max(ys)+1]
        scale=min(590/(hi[0]-lo[0]),460/(hi[1]-lo[1])); ox=25+panel*670; oy=85
        svg.append(f'<text x="{ox}" y="65" font-size="16">{label}: log count of mapped receiver luxels (256 WU bins)</text>')
        maximum=max(totals.values())
        for (x,y),n in sorted(totals.items()):
            t=math.log1p(n)/math.log1p(maximum) if maximum else 0
            colour=f'rgb({int(235-210*t)},{int(245-125*t)},{int(250-70*t)})'
            px=ox+(x-lo[0])*scale; py=oy+(hi[1]-y-1)*scale
            svg.append(f'<rect x="{px:.2f}" y="{py:.2f}" width="{scale:.2f}" height="{scale:.2f}" fill="{colour}"><title>cell {x},{y}; mapped={n}; material uncertain={uncertain[(x,y)]}</title></rect>')
        svg.append(f'<text x="{ox}" y="575" font-size="12">world extent: {lo[0]*256}..{hi[0]*256}, {lo[1]*256}..{hi[1]*256}; empty ≠ free space</text>')
    colours={'mapped':'#247f9c','footprint_only':'#86c4cf','ambiguous_overlap':'#d59235','unsupported_geometry':'#a458b4',
             'atlas_padding_certified':'#dedede','chart_unmapped_or_padding':'#e5bcba'}
    y=620
    svg.append('<text x="25" y="610" font-size="16">Per original colour page: exclusive coverage categories; bar denominator = page texels</text>')
    pages=sorted((int(k.split('/')[1]),v) for k,v in groups.items() if k.startswith('page/'))
    for page,v in pages:
        c=v['counts']; denominator=c['denominator_candidate_texels']; x=100
        svg.append(f'<text x="25" y="{y+10}" font-size="10">image {page}</text>')
        for key,colour in colours.items():
            count=c.get(key,0); width=1120*count/max(1,denominator)
            svg.append(f'<rect x="{x:.2f}" y="{y}" width="{width:.2f}" height="8" fill="{colour}"><title>{key}: {count}/{denominator}</title></rect>'); x+=width
        y+=10
    y=max(1045,y+15); x=25
    for key,colour in colours.items():
        svg.append(f'<rect x="{x}" y="{y}" width="10" height="10" fill="{colour}"/><text x="{x+14}" y="{y+9}" font-size="10">{key}</text>'); x+=215
    svg.append('<text x="25" y="1090" font-size="12">Counts are not independent localization constraints. Unknown transfer/primaries and missing deluxe are reported separately.</text></svg>')
    Path(output).write_text('\n'.join(svg)+'\n',encoding='utf-8')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('report'); p.add_argument('output')
    a=p.parse_args(); plot(a.report,a.output)
