#!/usr/bin/env python3
"""Matched receiver-baseline ablation; separate from the full benchmark manifest.

The broad C/distance_ambiguity scene retains an independent wall baseline.
This supplementary control removes it explicitly, rather than claiming that
the name of a scene proves a degeneracy. SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from environment import np
from routines import Objective, Work, solve_objective
from problems import Rows, compiler_rgb


def run():
    records = []
    for baseline in ('single_point', 'narrow_plane', 'broad_plane', 'multi_surface'):
        width = 0. if baseline == 'single_point' else 4. if baseline == 'narrow_plane' else 192.
        positions = [[x, y, 1.] for x in np.linspace(-width, width, 7) for y in np.linspace(-width, width, 5)]
        normals = [[0., 0., 1.]]*len(positions)
        if baseline == 'multi_surface':
            positions += [[-256., y, z] for y in (-96., 0., 96.) for z in (32., 128., 224.)]
            normals += [[1., 0., 0.]]*9
        rows = Rows(np.array(positions), np.array(normals), None, np.ones(len(positions)), ('train',)*len(positions))
        def decode(u): return 96.+u[0]*288., 50.+u[1]*1450.
        def response(u, work):
            height, I = decode(u)
            return compiler_rgb(rows, [[0., 0., height]], [[I]*3], encoded=False, work=work)
        truth = np.array([(192.-96.)/288., (300.-50.)/1450.]); exact = response(truth, Work())
        for noise, seed in ((0., 11), (.05, 11), (.05, 37)):
            observed = np.maximum(0., exact+np.random.default_rng(seed).normal(0., noise, exact.shape))
            for start in (.25, .75):
                for method in ('trf', 'lbfgsb', 'adam', 'powell'):
                    obj = Objective(response, [0., 0.], [1., 1.], [start, start], observed, observed, scale=64., budget=500)
                    result = solve_objective(obj, method, iterations=100)
                    h, I = decode(result['x']); J = []
                    for k in range(2):
                        a, b = truth.copy(), truth.copy(); a[k]+=1e-5; b[k]-=1e-5
                        J.append(((response(a, Work())-response(b, Work()))/2e-5).ravel())
                    singular = np.linalg.svd(np.column_stack(J), compute_uv=False)
                    result.update(receiver_baseline=baseline, noise=noise, seed=seed, start=start,
                        method=method, height_error_world=abs(h-192.), relative_intensity_error=abs(I-300)/300.,
                        scaled_response_singular_values=singular.tolist(),
                        condition=float(singular[0]/singular[-1]) if singular[-1] > 1e-10 else None,
                        unique_receiver_positions=len(set(map(tuple, positions))),
                        information_note='Repeated identical positions in single_point do not add independent evidence')
                    records.append(result)
    return dict(schema='org.openjk.optimization-distance-ablation.v1', records=records,
        source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        units='Compiler relative; same verified kernel, height/intensity only; no new compiler invocation',
        confidence_claim=None)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(); args.output.parent.mkdir(parents=True, exist_ok=True)
    result = run()
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True, allow_nan=False)+'\n', encoding='utf-8')
    print('Distance/baseline control records', len(result['records']))
