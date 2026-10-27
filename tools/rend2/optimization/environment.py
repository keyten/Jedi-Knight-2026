"""Research-only dependency/bootstrap; never imported by renderer builds.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import os
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
for name in ('OPENBLAS_NUM_THREADS', 'OMP_NUM_THREADS', 'MKL_NUM_THREADS', 'NUMEXPR_NUM_THREADS'):
    os.environ[name] = '1'
sys.path.insert(0, str(ROOT/'build/optimization-deps'))
sys.path.insert(0, str(HERE.parent/'bake_model'))
sys.path.insert(0, str(HERE.parent/'shadow_footprint'))
sys.path.insert(0, str(HERE.parent/'evaluation'))
sys.path.insert(0, str(HERE.parent/'inverse_gi'))
try:
    import numpy as np
    import scipy
    from scipy import optimize
except ImportError as e:
    raise RuntimeError('Offline benchmark needs NumPy 2.3.5 / SciPy 1.16.3; see optimization/README.md') from e


def versions():
    return dict(numpy=np.__version__, scipy=scipy.__version__, python=sys.version,
        reference_versions=np.__version__ == '2.3.5' and scipy.__version__ == '1.16.3',
        blas_threads_requested=1, runtime_dependency=False)
