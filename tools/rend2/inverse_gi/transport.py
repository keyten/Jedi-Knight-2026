"""Bounded passive diffuse-transport experiments. Not a production inverse solver.
SI examples use metres/cd/lux; compiler fixtures have a separate relative operator.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from dataclasses import dataclass
import math


def dot(a,b): return sum(x*y for x,y in zip(a,b))
def sub(a,b): return tuple(x-y for x,y in zip(a,b))
def length(a): return math.sqrt(dot(a,a))


@dataclass(frozen=True)
class Edge:
    sender: int
    receiver: int
    lower: float
    upper: float


@dataclass(frozen=True)
class Transfer:
    areas: tuple
    edges: tuple
    identity: str

    def __post_init__(self):
        n=len(self.areas)
        if not n or n>4096 or not all(math.isfinite(a) and a>0 for a in self.areas): raise ValueError('Invalid patch areas')
        if len(self.edges)>100000: raise ValueError('Sparse edge budget')
        sums=[0.]*n; keys=set()
        for e in self.edges:
            if not (0<=e.sender<n and 0<=e.receiver<n) or e.sender==e.receiver: raise ValueError('Invalid edge identity')
            if not (math.isfinite(e.lower+e.upper) and 0<=e.lower<=e.upper<=1): raise ValueError('Invalid transfer interval')
            if (e.sender,e.receiver) in keys: raise ValueError('Duplicate edge')
            keys.add((e.sender,e.receiver)); sums[e.sender]+=e.upper
        if max(sums)>1+1e-12: raise ValueError('Outgoing power exceeds available budget; no implicit renormalization')

    def apply(self,incident,reflectance):
        """E^(b+1)_i=sum_j F_ji A_j rho_j E^b_j/A_i; intervals, per RGB.

        Sparse absence in THIS explicit toy network means no edge. In an exported
        unknown scene, absent transfer records mean unknown, not certified zero.
        """
        n=len(self.areas)
        if len(incident)!=n or len(reflectance)!=n: raise ValueError('Patch span mismatch')
        for row in (*incident,*reflectance):
            if len(row)!=3 or not all(math.isfinite(v) and v>=0 for v in row): raise ValueError('Invalid RGB')
        if any(v>1 for row in reflectance for v in row): raise ValueError('Reflectance must be bounded in [0,1]')
        lower=[[0.]*3 for _ in range(n)]; upper=[[0.]*3 for _ in range(n)]
        reflected=[[a*r*x for r,x in zip(rho,E)] for a,rho,E in zip(self.areas,reflectance,incident)]
        for e in self.edges:
            for c in range(3):
                factor=reflected[e.sender][c]/self.areas[e.receiver]
                lower[e.receiver][c]+=factor*e.lower; upper[e.receiver][c]+=factor*e.upper
        return lower,upper

    def bounces(self,direct,rho,count=1):
        if not 0<=count<=8: raise ValueError('Bounce/work limit')
        low=direct; high=direct; layers=[]
        for _ in range(count):
            low=self.apply(low,rho)[0]; high=self.apply(high,rho)[1]
            layers.append((low,high))
        return layers


def solve(matrix,rhs):
    n=len(rhs)
    if not 1<=n<=64 or len(matrix)!=n or any(len(row)!=n for row in matrix): raise ValueError('Linear system span/work budget')
    if not all(math.isfinite(v) for row in (*matrix,rhs) for v in row): raise ValueError('Nonfinite system')
    a=[list(row)+[v] for row,v in zip(matrix,rhs)]
    for k in range(n):
        pivot=max(range(k,n),key=lambda j:abs(a[j][k]))
        if abs(a[pivot][k])<1e-14: raise ValueError('Unidentifiable/singular system')
        a[k],a[pivot]=a[pivot],a[k]; scale=a[k][k]; a[k]=[v/scale for v in a[k]]
        for j in range(n):
            if j!=k:
                scale=a[j][k]; a[j]=[x-scale*y for x,y in zip(a[j],a[k])]
    return [row[-1] for row in a]


def nnls(columns,targets,l1=(),ridge=0.,passes=200):
    """Deterministic tiny nonnegative amplitude fit; no geometry/source discovery."""
    if not columns: return []
    m=len(targets); n=len(columns)
    if n>64 or m>10000 or any(len(c)!=m for c in columns): raise ValueError('Fit budget/span')
    weights=list(l1) if l1 else [0.]*n
    if len(weights)!=n or not 0<=ridge<math.inf or not 1<=passes<=10000:
        raise ValueError('Fit constraints/work budget')
    if not all(math.isfinite(v) and v>=0 for row in (*columns,targets,weights) for v in row):
        raise ValueError('Nonnegative finite fit inputs required')
    values=[0.]*n; predicted=[0.]*m
    for _ in range(passes):
        change=0.
        for k,column in enumerate(columns):
            denom=dot(column,column)+ridge
            if denom<1e-20: continue
            rhs=dot(column,[y-p+values[k]*x for y,p,x in zip(targets,predicted,column)])-weights[k]
            new=max(0.,rhs/denom); delta=new-values[k]; values[k]=new
            change=max(change,abs(delta)); predicted=[p+delta*x for p,x in zip(predicted,column)]
        if change<1e-12: break
    return values


def fit_smooth(primary,target,positions,source_penalty=.02,smoothness=.01,magnitude=.0):
    """Deliberately flexible TRAIN-only residual. L1(source) can suppress real I.

    Residual has one DOF per observation. Smoothness follows given adjacency;
    it is not evidence of physical transport or an energy budget.
    """
    n=len(target)
    if not 1<=n<=256 or len(primary)!=n or len(positions)!=n: raise ValueError('Residual span/work budget')
    if not all(math.isfinite(v) and v>=0 for row in (primary,target,(source_penalty,smoothness,magnitude)) for v in row):
        raise ValueError('Invalid residual parameters')
    if not all(math.isfinite(v) for v in positions): raise ValueError('Invalid residual positions')
    if smoothness==0 and magnitude==0:
        # Exact global minimizer (one possible allocation when penalty==0).
        return 0.,list(target)
    if n>63: raise ValueError('Dense research QP work budget')
    neighbours=[[] for _ in range(n)]
    for i in range(n):
        for j in range(i):
            if abs(positions[i]-positions[j])<.31: neighbours[i].append(j); neighbours[j].append(i)
    # Convex QP: .5*||I D+z-y||² + lambda_I*I +
    # .5*lambda_s*sum_edges(zi-zj)² + .5*lambda_m*||z||², I,z>=0.
    # Active-set solve to KKT tolerance, not a possibly stalled short alternating
    # descent mistaken for a converged source-collapse experiment.
    H=[[0.]*(n+1) for _ in range(n+1)]; b=[dot(primary,target)-source_penalty]+list(target)
    H[0][0]=dot(primary,primary)
    for i in range(n):
        H[0][i+1]=H[i+1][0]=primary[i]
        H[i+1][i+1]=1+magnitude+smoothness*len(neighbours[i])
        for j in neighbours[i]: H[i+1][j+1]=-smoothness
    values=[0.]*(n+1); free=set()
    for _ in range(1024):
        gradient=[rhs-dot(row,values) for row,rhs in zip(H,b)]
        inactive=[i for i in range(n+1) if i not in free]
        if not inactive or max(gradient[i] for i in inactive)<1e-10:
            return values[0],values[1:]
        free.add(max(inactive,key=lambda i:gradient[i]))
        for _ in range(128):
            ordered=sorted(free)
            solution=solve([[H[i][j] for j in ordered] for i in ordered],[b[i] for i in ordered])
            proposed=[0.]*(n+1)
            for i,v in zip(ordered,solution): proposed[i]=v
            negative=[i for i in ordered if proposed[i]<=0]
            if not negative: values=proposed; break
            alpha=min(values[i]/(values[i]-proposed[i]) for i in negative)
            values=[x+alpha*(y-x) for x,y in zip(values,proposed)]
            for i in ordered:
                if values[i]<1e-12: values[i]=0.; free.discard(i)
        else: raise ValueError('Residual QP inner iteration budget')
    raise ValueError('Residual QP did not meet KKT tolerance')


def rmse(a,b):
    if len(a)!=len(b) or not a: raise ValueError('Metric span')
    return math.sqrt(sum((x-y)**2 for x,y in zip(a,b))/len(a))


def direct(position,normal,source,profile='inverse_square'):
    if any(len(v)!=3 for v in (position,normal,source)) or not all(math.isfinite(x) for v in (position,normal,source) for x in v):
        raise ValueError('Invalid direct geometry')
    if abs(dot(normal,normal)-1)>1e-8: raise ValueError('Unit receiver normal required')
    d=sub(source,position); r=length(d)
    if r<1e-8: raise ValueError('Singular source')
    cosine=max(0.,dot(normal,d)/r)
    if profile=='inverse_square': return cosine/r**2
    if profile=='linear_alternative': return max(0.,1-r/6)*cosine/.8
    raise ValueError('UnspecifiedModel')
