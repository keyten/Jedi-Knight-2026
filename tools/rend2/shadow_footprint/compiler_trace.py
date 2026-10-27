"""Restricted pinned light_trace.c tree replay for the opaque/sky room fixture.

Not production tracing: no texture filters, entities, patches, portals or RNG.
Source locations/normal nudges are supplied. Constants are source-locked, not fit.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import math
import struct
from geometry import Triangle, dot, sub, cross, unit, add, mul, ray_triangle


class Node:
    def __init__(self,plane=None,solid=False):
        self.plane=plane; self.solid=solid; self.children=[]; self.windings=[]; self.triangles=[]; self.count=0


def split(p,plane):
    distances=[dot(x,plane[:3])-plane[3] for x in p]
    sides=[-1 if d<-.25 else 1 if d>.25 else 0 for d in distances]
    if -1 not in sides: return p,()
    if 1 not in sides: return (),p
    front=[]; back=[]
    for i,(a,b) in enumerate(zip(p,p[1:]+p[:1])):
        j=(i+1)%len(p)
        if sides[i]>=0: front.append(a)
        if sides[i]<=0: back.append(a)
        if sides[j]==0 or sides[i]==sides[j] or sides[i]==0: continue
        t=distances[i]/(distances[i]-distances[j])
        point=tuple(plane[3] if plane[k]==1 else -plane[3] if plane[k]==-1 else a[k]+t*(b[k]-a[k]) for k in range(3))
        front.append(point); back.append(point)
    return tuple(front),tuple(back)


class Replay:
    def __init__(self,lumps,triangles):
        planes=[struct.unpack_from('<4f',lumps[2],o) for o in range(0,len(lumps[2]),16)]
        nodes=[struct.unpack_from('<9i',lumps[3],o) for o in range(0,len(lumps[3]),36)]
        clusters=[struct.unpack_from('<i',lumps[4],o)[0] for o in range(0,len(lumps[4]),48)]
        def build(index):
            if index<0: return Node(solid=clusters[-index-1]==-1)
            raw=nodes[index]; n=Node(planes[raw[0]]); n.children=[build(raw[1]),build(raw[2])]; return n
        self.root=build(0)
        def insert(n,t,p):
            if n.plane is None: n.windings.append((t,p)); return
            normal=unit(cross(sub(t.world[2],t.world[0]),sub(t.world[1],t.world[0])))
            d=dot(normal,t.world[0]); cosine=dot(n.plane[:3],normal)
            if cosine>.999 and abs(n.plane[3]-d)<.001: insert(n.children[0],t,p); return
            if cosine<-.999 and abs(n.plane[3]+d)<.001: insert(n.children[1],t,p); return
            for child,poly in zip(n.children,split(p,n.plane)):
                if len(poly)>=3: insert(child,t,poly)
        for t in triangles: insert(self.root,t,t.world)
        def subdivide(n,depth=0):
            if depth>=32: return
            if n.plane is not None:
                for c in n.children: subdivide(c,depth+1)
                return
            verts=[p for _,poly in n.windings for p in poly]
            if len(verts)-2*len(n.windings)<5: return
            lo=[min(p[k] for p in verts) for k in range(3)]; hi=[max(p[k] for p in verts) for k in range(3)]
            axis=max(range(3),key=lambda k:(hi[k]-lo[k],-k))
            if hi[axis]-lo[axis]<=32: return
            dist=math.floor(sum(p[axis] for p in verts)/len(verts))
            if dist<=lo[axis] or dist>=hi[axis]: dist=math.floor((hi[axis]+lo[axis])/2)
            normal=tuple(float(k==axis) for k in range(3)); n.plane=(*normal,dist); n.children=[Node(),Node()]
            for t,p in n.windings:
                for c,poly in zip(n.children,split(p,n.plane)):
                    if len(poly)>=3: c.windings.append((t,poly))
            n.windings=[]
            for c in n.children: subdivide(c,depth+1)
        subdivide(self.root)
        self.node_count=0; self.triangle_count=0
        def triangulate(n):
            self.node_count+=1
            if n.plane is not None:
                n.count=sum(triangulate(c) for c in n.children); return n.count
            for t,p in n.windings:
                for i in range(1,len(p)-1):
                    if math.sqrt(dot(cross(sub(p[i],p[0]),sub(p[i+1],p[0])),cross(sub(p[i],p[0]),sub(p[i+1],p[0]))))<1e-10: continue
                    n.triangles.append(Triangle(t.id+f'/clip{len(n.triangles)}',t.surface,(p[0],p[i],p[i+1]),((0.,0.),(1.,0.),(0.,1.)),t.normals,t.optical))
            n.count=len(n.triangles); self.triangle_count+=n.count; return n.count
        triangulate(self.root)

    def trace(self,origin,direction,epsilon=.01):
        end=add(origin,mul(direction,65536)); leaves=[]
        def walk(n,a,b):
            if n.plane is None:
                if n.solid: return True
                if n.triangles: leaves.append(n)
                return False
            if n.count==0: return False # testAll optimization in pinned compiler
            f=dot(a,n.plane[:3])-n.plane[3]; g=dot(b,n.plane[:3])-n.plane[3]
            if f>=-.1 and g>=-.1: return walk(n.children[0],a,b)
            if f<.1 and g<.1: return walk(n.children[1],a,b)
            side=int(f<0); mid=add(a,mul(sub(b,a),f/(f-g)))
            return walk(n.children[side],a,mid) or walk(n.children[1-side],mid,b)
        walk(self.root,origin,end); hits=[]
        for n in leaves:
            for t in n.triangles:
                hit=ray_triangle(origin,direction,t,epsilon,minimum=.5,maximum=65536,det_epsilon=.25)
                if hit:
                    hits.append(dict(triangle=t.id,optical=t.optical,triangle_vertices=t.world,world=[origin[k]+direction[k]*hit[0] for k in range(3)],barycentric=[1-hit[1]-hit[2],hit[1],hit[2]],distance=hit[0]))
                    if t.optical=='opaque': return False,hits
        return any(h['optical']=='sky' for h in hits),hits
