"""Adaptive, proposal-only intensity-distance/limiter experiment.

Compiled RGB8 input is a CODE-PROXY heuristic, not a physical intensity bound.
No truth, held-out data, fitting, renderer or canonical output dependency.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from collections import Counter
import itertools
import json
import math
import subprocess


def response(row, p):
    d = [a-b for a, b in zip(p, row['position'])]
    r2 = sum(v*v for v in d)
    return max(0., sum(a*b for a, b in zip(row['normal'], d)))/r2**1.5 if r2 > 1e-12 else 0.


def limiter(rows, p, states, mode, noise=2):
    values = []
    for i, row in enumerate(rows):
        s = response(row, p)
        if s <= 1e-12 or states[i] == 2 or (mode != 'raw' and states[i] != 0):
            continue  # blocked/unknown have no known positive coefficient
        raw = row['raw']
        if mode == 'censored':
            if max(raw) == 255:
                continue  # shared-peak encoding: entire RGB vector has no finite upper cap
            lo = sum(max(0, x-noise) for x in raw)/3/s
            hi = sum(x+1+noise for x in raw)/3/s
        else:
            lo = hi = sum(raw)/3/s
        values.append((lo, hi, i))
    if not values:
        return dict(bound=None, labels=[], finite_caps=0)
    upper = min(v[1] for v in values)
    labels = ([min(values, key=lambda v: (v[1], rows[v[2]]['id']))[2]] if mode != 'censored' else
              [v[2] for v in values if v[0] <= upper])
    return dict(bound=upper, labels=labels, finite_caps=len(values))


def vote(fields):
    weights = {}
    for field in fields:
        labels = field['labels']
        for label in labels:
            weights[label] = max(weights.get(label, 0.), 1/len(labels))
    return sum(weights.values())


class Oracle:
    def __init__(self, tool, geometry, receivers):
        self.child = subprocess.Popen([str(tool), str(geometry), str(receivers)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
        line = self.child.stdout.readline()
        if not line:
            raise RuntimeError(self.child.stderr.read())
        self.header = json.loads(line)
        if not self.header.get('ready'):
            raise RuntimeError('Geometry oracle not ready')
        self.calls = self.rays = self.work = 0

    def query(self, p):
        self.child.stdin.write(' '.join(map(str, p))+'\n'); self.child.stdin.flush()
        line = self.child.stdout.readline()
        if not line:
            raise RuntimeError(self.child.stderr.read())
        d = json.loads(line); self.calls += 1; self.rays += d['rays']; self.work += d['work']
        return d

    def close(self):
        self.child.stdin.close()
        self.child.wait(timeout=30)
        if self.child.returncode:
            raise RuntimeError(self.child.stderr.read())


def search(rows, bounds, mode, policy, oracle):
    cells = []; samples = []; depth_counts = Counter(); rejected = Counter(); ray_states = Counter()
    divisions = policy['coarse_divisions']; cap = policy['spatial_query_cap']
    def cell(lo, hi, depth):
        centre = [(a+b)/2 for a, b in zip(lo, hi)]
        points = [centre]
        for axis in range(3):
            for sign in (-1, 1):
                p = centre.copy(); p[axis] += sign*(hi[axis]-lo[axis])/4; points.append(p)
        fields = []; receipts = []
        for p in points:
            g = oracle.query(p); receipts.append(g)
            rejected[str(g['domain'])] += not g['free']
            ray_states.update(map(str, g['states']))
            field = limiter(rows, p, g['states'], mode, policy['noise_codes'])
            if not g['free']:
                field = dict(bound=None, labels=[], finite_caps=0)
            fields.append(field)
            samples.append(dict(position=p, geometry=g, field=field))
        c = dict(position=centre, bounds=lo+hi, depth=depth, vote=vote(fields),
                 free=receipts[0]['free'], bound=fields[0]['bound'],
                 finite_caps=fields[0]['finite_caps'], refined=False)
        depth_counts[str(depth)] += 1; cells.append(c)
        return c
    widths = [(bounds[i+3]-bounds[i])/divisions for i in range(3)]
    for index in itertools.product(range(divisions), repeat=3):
        lo = [bounds[i]+index[i]*widths[i] for i in range(3)]
        cell(lo, [lo[i]+widths[i] for i in range(3)], 0)
    while oracle.calls+56 <= cap:
        eligible = [c for c in cells if not c['refined'] and c['free']]
        if not eligible:
            break
        parent = min(eligible, key=lambda c: (-c['vote'], c['position']))
        parent['refined'] = True; b = parent['bounds']; mid = parent['position']
        for bits in itertools.product((0, 1), repeat=3):
            lo = [mid[i] if bits[i] else b[i] for i in range(3)]
            hi = [b[i+3] if bits[i] else mid[i] for i in range(3)]
            cell(lo, hi, parent['depth']+1)
    chosen = []
    for c in sorted(cells, key=lambda c: (-c['vote'], -c['depth'], c['position'])):
        if not c['free'] or c['vote'] < policy['minimum_vote']:
            continue
        if any(math.dist(c['position'], old['position']) < policy['proposal_separation_world'] for old in chosen):
            continue
        chosen.append(c)
        if len(chosen) == policy['proposal_cap']:
            break
    return dict(candidates=chosen, cells=cells, samples=samples, spatial_queries=oracle.calls,
                rays=oracle.rays, work=oracle.work, response_pairs=oracle.calls*len(rows),
                rejected_domains=dict(rejected), ray_states=dict(ray_states), depths=dict(depth_counts),
                budget_cap=cap, unused_budget=cap-oracle.calls, complete=False,
                domain='EncodedCodeProxy_NotCalibratedIntensity', accepted_sources=0)
