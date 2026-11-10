"""Rank original visible fixtures by independent grid/geometry evidence, without luxel RGB.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import math
from pathlib import Path

from .run import load, write, sha, sub, dot, length
from tools.rend2.grid_triangulation.triangulate import Bridge


def discover(catalogue, capture, tool, output):
    cat = load(catalogue)
    b = Bridge(tool, capture)
    try:
        if b.metadata['map_sha256'] != cat['map_sha256'] or b.metadata['dynamic_models']:
            raise ValueError('This bounded follow-up requires matching original static-map geometry')
        origin, spacing, shape = (b.metadata[k] for k in ('origin', 'spacing', 'shape'))
        rows = []
        # Native bridge cap is 4096 cells per request, without truncation.
        width = max(1, 4096//(shape[1]*shape[2]))
        for first in range(0, shape[0], width):
            lo = [origin[0]+first*spacing[0], origin[1], origin[2]]
            hi = [origin[0]+min(shape[0]-1, first+width-1)*spacing[0],
                  origin[1]+(shape[1]-1)*spacing[1], origin[2]+(shape[2]-1)*spacing[2]]
            rows += b.query('cells', *[x-.01 for x in lo], *[x+.01 for x in hi])['rows']
        rows = [r for r in rows if r['space'] == 'free' and not r['boundary'] and r['present'] and
                not r['issues'] & 64 and len(r['styles']) == 1 and not r['styles'][0]['weak']]
        fixtures = [f for f in cat['fixtures'] if f['static_eligible'] and f['model'] == 0 and
                    f['emitting_footprint_known'] and f['emitting_area'] >= 500 and
                    set(f['issues']) <= {'colour_transfer_assumed', 'historical_bake_unknown'} and
                    len(f['surfaces']) == 1 and len(f['layers']) == 1]
        if len(fixtures) > 256:
            raise ValueError('Declared 256-fixture follow-up budget exceeded')
        candidates = []
        for f in fixtures:
            p = [x+4*n for x, n in zip(f['centroid'], f['outward'])]
            state = b.point(p)
            supports = []
            if state['space'] == 'free' and not state['boundary']:
                for r in rows:
                    delta = sub(p, r['position'])
                    if length(delta) > 384 or length(delta) < 1:
                        continue
                    angle = math.degrees(math.acos(max(-1, min(1, dot(delta, r['consumer256'])/length(delta)))))
                    if angle > 30:
                        continue
                    ray = b.query('segment', *r['position'], *p)
                    colour = r['styles'][0]['directed']
                    prior = f['layers'][0]['colour_prior']
                    affinity = dot(colour, prior)/max(1e-30, length(colour)*length(prior))
                    supports.append(dict(ordinal=r['ordinal'], record=r['correlation_key'], angle=angle,
                                         colour_cosine=affinity, ray=ray))
            good = [s for s in supports if s['ray']['static_state'] == 'clear' and
                    not s['ray']['dynamic_possible'] and s['colour_cosine'] >= .85]
            count = len({s['record'] for s in good})
            candidates.append(dict(fixture_id=f['id'], surfaces=f['surfaces'], material=f['material'],
                                   position=p, point=state, agreeing_distinct_records=count,
                                   mean_angle=sum(s['angle'] for s in good)/len(good) if good else None,
                                   supports=supports))
        candidates.sort(key=lambda c: (-c['agreeing_distinct_records'],
                                      c['mean_angle'] if c['mean_angle'] is not None else 999, c['fixture_id']))
        write(output, dict(schema='org.openjk.stock-grid-fixture-discovery.v1', map=cat['map_path'], map_sha256=cat['map_sha256'],
              catalogue_sha256=sha(catalogue), capture_sha256=sha(capture), tool_sha256=sha(tool),
              original_luxel_rgb_read=False, original_grid_records_are_aggregate=True,
              policy=dict(minimum_area=500, radius=384, standoff=4, maximum_angle=30, minimum_colour_cosine=.85,
                          selection='Top three ranked independent original-grid supports; no lightmap fit scores'),
              candidates=candidates, historical_source_certified=False))
        for c in candidates[:10]:
            print(c['fixture_id'][:10], c['surfaces'], c['material'], c['agreeing_distinct_records'], c['mean_angle'], flush=True)
    finally:
        b.close()


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--catalogue', type=Path, required=True)
    p.add_argument('--capture', type=Path, required=True)
    p.add_argument('--tool', type=Path, default=Path('build/s06-gcc/lightrecon_grid_geometry_tool.exe'))
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    discover(a.catalogue, a.capture, a.tool, a.out)
