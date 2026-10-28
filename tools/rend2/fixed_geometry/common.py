"""X07 finite, encoded-domain metrics only. No light formula. GPL-2.0-or-later."""
import hashlib
import json
import math
from pathlib import Path

PROTOCOL='FixedGeometryBlindIntensityFit'
SCHEMA='org.openjk.fixed-geometry-blind-intensity-fit.v1'
POLICY=dict(initial=64.,upper=1200.,max_calls=1800,max_passes=12,golden_steps=15,
            objective_stop=1e-8,improvement_stop=1e-7,parameter_step_stop=.02,
            feasible_excess=1.,spatial_block=128.,seed=20261028)

def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def load(path):
    p=Path(path)
    if p.stat().st_size>16*1024*1024:raise ValueError('JSON size budget')
    return json.loads(p.read_text(),parse_constant=lambda s:(_ for _ in ()).throw(ValueError(s)))
def write(path,value):Path(path).write_text(json.dumps(value,sort_keys=True,indent=2,allow_nan=False)+'\n')
def canonical(value):return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':'),allow_nan=False).encode()).hexdigest()

def validate_rows(rows):
    if not rows or len(rows)>512:raise ValueError('Observation count budget/empty data')
    seen=set()
    for r in rows:
        if r['id'] in seen:raise ValueError('Duplicate observation')
        seen.add(r['id'])
        if (r['encoding']!=0 or r['transfer']!=1 or r['primaries']!=1 or r['style']!=0 or r['normal'] is None or r['position'] is None
            or len(r['raw'])!=3 or any(type(v)is not int or not 0<=v<=255 for v in r['raw'])
            or not math.isfinite(r['area']) or r['area']<=0):raise ValueError('Unsupported observation domain/geometry')
        if (len(r['position'])!=3 or len(r['normal'])!=3 or
            not all(math.isfinite(v) for v in r['position']+r['normal']) or
            abs(math.fsum(v*v for v in r['normal'])-1)>1e-4):raise ValueError('Invalid receiver position/normal')

def weights(rows,block=128.):
    cells=[tuple(math.floor(x/block) for x in r['position']) for r in rows];areas={}
    for c,r in zip(cells,rows):areas[c]=areas.get(c,0.)+r['area']
    return [r['area']/areas[c]/len(areas) for r,c in zip(rows,cells)],len(areas)

def residual(v,b):
    if not math.isfinite(v) or not 0<=v<=255.0001:raise ValueError('Invalid encoded prediction')
    # Shared-peak encoded endpoint, NOT an independent infinite irradiance bin.
    return min(0.,v-b)+max(0.,v-min(255.,b+1.))

def loss(rows,rgb,w):
    if rgb is None or len(rgb)!=len(rows):raise ValueError('Missing prediction coverage')
    squared=0.;maximum=0.
    for r,p,a in zip(rows,rgb,w):
        if len(p)!=3:raise ValueError('RGB span')
        for v,b in zip(p,r['raw']):
            e=residual(v,b);squared+=a*e*e/3;maximum=max(maximum,abs(e))
    return squared,maximum

def admission(rows):
    validate_rows(rows);w,blocks=weights(rows)
    positive=[r for r in rows if max(r['raw'])>=8]
    positive_blocks=len({tuple(math.floor(v/128.) for v in r['position']) for r in positive})
    zero=math.sqrt(loss(rows,[[0.]*3 for _ in rows],w)[0])
    all_saturated=all(all(v==255 for v in r['raw']) for r in rows)
    status='Admitted' if len(positive)>=4 and blocks>=2 and zero>=4 else 'InsufficientPositiveEvidence'
    if all_saturated:status='CensoredUnidentifiable'
    return dict(status=status,observations=len(rows),spatial_blocks=blocks,positive_receivers=len(positive),
                positive_spatial_blocks=positive_blocks,zero_interval_rmse=zero,saturated_channels=sum(v==255 for r in rows for v in r['raw']))

def metrics(rows,predictions):
    w,blocks=weights(rows);a,m=loss(rows,predictions,w);area=[r['area']/sum(t['area'] for t in rows) for r in rows]
    area_loss,_=loss(rows,predictions,area)
    return dict(count=len(rows),prediction_count=len(predictions),known_area=sum(r['area'] for r in rows),
                prediction_coverage=1.,spatial_blocks=blocks,spatial_interval_rmse=math.sqrt(a),
                area_interval_rmse=math.sqrt(area_loss),maximum_interval_error=m)

def eigenvalues(a):
    """Small deterministic symmetric Jacobi diagonalization; conditioning only."""
    a=[r[:] for r in a];n=len(a)
    for _ in range(80*n*n):
        if n<2:break
        p,q=max(((i,j) for i in range(n) for j in range(i+1,n)),key=lambda ij:abs(a[ij[0]][ij[1]]))
        if abs(a[p][q])<1e-14:break
        angle=.5*math.atan2(2*a[p][q],a[q][q]-a[p][p]);c,s=math.cos(angle),math.sin(angle)
        app,aqq,apq=a[p][p],a[q][q],a[p][q]
        for k in range(n):
            if k in (p,q):continue
            x,y=a[k][p],a[k][q];a[k][p]=a[p][k]=c*x-s*y;a[k][q]=a[q][k]=s*x+c*y
        a[p][p]=c*c*app-2*s*c*apq+s*s*aqq;a[q][q]=s*s*app+2*s*c*apq+c*c*aqq;a[p][q]=a[q][p]=0.
    return sorted((max(0.,a[i][i]) for i in range(n)),reverse=True)

def solve(a,b):
    """Bounded small dense pivoted solve for a LOCAL amplitude proposal."""
    n=len(b);m=[list(row)+[v] for row,v in zip(a,b)]
    for k in range(n):
        pivot=max(range(k,n),key=lambda i:abs(m[i][k]));m[k],m[pivot]=m[pivot],m[k]
        if abs(m[k][k])<1e-18:raise ValueError('Rank-deficient local system')
        q=m[k][k];m[k]=[x/q for x in m[k]]
        for i in range(n):
            if i!=k:
                q=m[i][k];m[i]=[x-q*y for x,y in zip(m[i],m[k])]
    return [row[-1] for row in m]
