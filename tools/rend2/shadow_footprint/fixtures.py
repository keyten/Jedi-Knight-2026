"""Self-authored SI geometry; no stock data or inferred source answers.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import math
from dataclasses import replace
from geometry import Triangle, rectangle
from integrate import Scene, Source


def quad(points,identifier='wall',optical='opaque'):
    return tuple(Triangle(identifier+str(i),100+i,tuple(points[j] for j in ids),
                          ((0.,0.),(1.,0.),(0.,1.)),((0.,0.,1.),)*3,optical)
                 for i,ids in enumerate(((0,1,2),(0,2,3))))


def wall(x=1.,height=1.,y0=-10.,y1=10.,optical='opaque'):
    return quad(((x,y0,0.),(x,y1,0.),(x,y1,height),(x,y0,height)),'wall',optical)


def doorway(width=.08,height=2.):
    return (wall(height=height,y0=-10.,y1=-width/2)+
            wall(height=height,y0=width/2,y1=10.))


def tilted(source,degrees,axis='y'):
    # Source frame rotates as a whole, not just the spot direction.
    t=math.radians(degrees); c,s=math.cos(t),math.sin(t)
    def rotate(v):
        x,y,z=v
        return (c*x+s*z,y,-s*x+c*z) if axis=='y' else (x,c*y-s*z,s*y+c*z)
    return replace(source,axis=rotate(source.axis),right=rotate(source.right),up=rotate(source.up))


def default():
    return rectangle(1.3,1.4,-.05,.05),Source(),Scene(wall())
