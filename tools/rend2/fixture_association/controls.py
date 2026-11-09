"""Owned analytical evidence controls, independent of the compatibility scorer.

Positions, tangent-gradient directions and incoming directions are constructed
from declared source arrangements, not by querying the ASR15 score. These are
not fresh compiler bakes or stock-map source truth.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import math


def fixture(key, x=0, z=128, colour=(1., .6, .25), y=0):
    return dict(id=key, position=[x, y, z], outward=[0, 0, -1], colour=list(colour),
                radius_world=4., static_eligible=True, uncertainty=[])


def region(key, sources, colour=(1., .6, .25), grid=True):
    samples = []
    for index, (source, centre) in enumerate(sources):
        # Distinct floor and volume positions; independent evidence domains.
        for j, offset in enumerate((-24, -8, 8, 24)):
            p = [centre+offset, 20, 0]
            dx, dy = source[0]-p[0], source[1]-p[1]
            # Direction of the analytical cosine/inverse-square floor gradient.
            length = math.hypot(dx, dy)
            samples.append(dict(id=key+'/surface/'+str(index)+'/'+str(j),
                position=p, normal=[0, 0, 1], gradient=[dx/length, dy/length, 0],
                incoming=None, colour=list(colour), rgb8=[160, 96, 40],
                material='owned/uniform', style=0, correlation_key=key+'/surface/'+str(index)))
            if grid:
                q = [centre+offset, -16, 40]
                d = [a-b for a, b in zip(source, q)]
                size = math.sqrt(sum(v*v for v in d))
                samples.append(dict(id=key+'/grid/'+str(index)+'/'+str(j),
                    position=q, normal=None, gradient=None, incoming=[v/size for v in d],
                    colour=list(colour), rgb8=None, correlation_key=key+'/grid/'+str(index)))
    return dict(id=key, samples=samples, projection=None, dark_samples=[], uncertainty=[])


class Geometry:
    def __init__(self, blocked=(), unknown=False):
        self.blocked, self.unknown = blocked, unknown

    def ray(self, a, b):
        if self.unknown:
            return 'unknown'
        # Owned opaque screens isolate these fixture endpoints in the control.
        return 'blocked' if any(sum((x-y)**2 for x, y in zip(b, p)) < 1e-10 for p in self.blocked) else 'clear'


def cases():
    true = fixture('lamp', x=48)
    near = fixture('near-colour-decoy', x=-16, z=32)
    a, b = fixture('left', x=-220), fixture('right', x=220)
    close_a, close_b = fixture('left', x=-48), fixture('right', x=48)
    white = fixture('white-primary', x=48, colour=(1., 1., 1.))
    red = fixture('red-bounce-decoy', x=-32, z=32, colour=(1., .1, .05))
    return [
        dict(name='one_to_one', fixtures=[true], observations=[region('floor', [(true['position'], 0)])],
             geometry=Geometry(), truth={('lamp', 'floor')}),
        dict(name='one_to_many', fixtures=[true], observations=[region('floor-a', [(true['position'], -32)]),
             region('floor-b', [(true['position'], 64)])], geometry=Geometry(), truth={('lamp', 'floor-a'), ('lamp', 'floor-b')}),
        dict(name='many_to_one', fixtures=[a, b], observations=[region('joined-illumination', [(a['position'], -220), (b['position'], 220)])],
             geometry=Geometry(), truth={('left', 'joined-illumination'), ('right', 'joined-illumination')}),
        dict(name='wrong_colour', fixtures=[true, fixture('blue-decoy', x=48, colour=(.02, .1, 1.))],
             observations=[region('warm', [(true['position'], 0)])], geometry=Geometry(), truth={('lamp', 'warm')}),
        dict(name='nearby_separate_fixtures', fixtures=[close_a, close_b, fixture('near-left-decoy', x=-48, z=32)],
             observations=[region('left-region', [(close_a['position'], -48)]), region('right-region', [(close_b['position'], 48)])],
             geometry=Geometry(), truth={('left', 'left-region'), ('right', 'right-region')}),
        dict(name='occluded_fixture', fixtures=[true, near], observations=[region('visible-region', [(true['position'], 0)])],
             geometry=Geometry(blocked=[near['position']]), truth={('lamp', 'visible-region')}),
        dict(name='coloured_bounce', fixtures=[white, red], observations=[region('red-receiver', [(white['position'], 0)], colour=(1., .1, .05))],
             geometry=Geometry(), truth={('white-primary', 'red-receiver')}),
        dict(name='unexplained_colour_only', fixtures=[red], observations=[dict(id='unexplained', projection=None,
             dark_samples=[], uncertainty=['Colour patch may be indirect'], samples=[dict(id='colour', position=[0, 0, 0],
             normal=[0, 0, 1], gradient=None, incoming=None, colour=[1, .1, .05], rgb8=[180, 18, 9])])],
             geometry=Geometry(), truth=set()),
    ]
