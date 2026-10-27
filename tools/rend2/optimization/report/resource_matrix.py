#!/usr/bin/env python3
"""Representative per-method process peaks, separate from whole-suite RSS.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--case'); parser.add_argument('--method')
    args = parser.parse_args(); module = Path(__file__).resolve().parents[1]
    if args.method:
        sys.path.insert(0, str(module))
        from environment import np
        from routines import Work, linear_fit, solve_objective
        from problems import geometry_problem, smooth_rows, contribution_matrix
        if args.case == 'startup':
            result = dict(status='ImportedOnly', numerical_work=False)
        elif args.case == 'linear':
            rows = smooth_rows(); origins = np.array([[-120.+i*30., -40.+(i%3)*32., 160.+(i%2)*32.] for i in range(8)])
            A = contribution_matrix(rows, origins); norms = np.linalg.norm(A, axis=0)
            target = A@np.array([[100.+i*20., 50., 70.] for i in range(8)])
            result = linear_fit(A/norms, target, args.method, passes=500)
            result['matrix_shape'] = list(A.shape)
        else:
            obj, metadata = geometry_problem('xyz_intensity', [.7]*4, noise=.5, seed=11, budget=700)
            result = solve_objective(obj, args.method)
        print(json.dumps(result, sort_keys=True, allow_nan=False))
        return
    if args.output is None: parser.error('--output is required for resource matrix')
    sys.path.insert(0, str(module.parent/'datasets'))
    from measure import run
    args.output.mkdir(parents=True, exist_ok=False)
    cases = [('startup', 'imports')]+[('linear', m) for m in ('nnls', 'bvls', 'trf_linear', 'coordinate', 'group_prox')]+[
        ('nonlinear', m) for m in ('trf', 'projected_lm', 'lbfgsb', 'adam', 'powell')]
    records = []
    for case, method in cases:
        path = args.output/(case+'-'+method+'.log')
        measurement = run([sys.executable, '-X', 'utf8', str(Path(__file__).resolve()), '--case', case, '--method', method], path)
        values = json.loads(path.read_text(encoding='utf-8'))
        records.append(dict(case=case, method=method, measurement=measurement, result=values))
        print(case, method, measurement['peak_working_set_bytes'], flush=True)
    result = dict(schema='org.openjk.optimization-resource-matrix.v1', records=records,
        source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        interpretation='Actual independent process peak including Python/NumPy/SciPy imports; startup reference is separate, do not subtract peaks as exact allocator cost',
        scale='Representative small problem only, not a large sparse/full-map memory benchmark')
    (args.output/'result.json').write_text(json.dumps(result, indent=2, sort_keys=True)+'\n', encoding='utf-8')


if __name__ == '__main__': main()
