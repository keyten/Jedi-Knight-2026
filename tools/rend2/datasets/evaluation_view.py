"""Controller-only evaluation input. Do not distribute this directory to solvers.

Evaluation targets are never installed into TrainingView. Reading validation at
checkpoints or held-out after a frozen run is the external harness's decision.
No automatic fitting or evaluation against unknown stock source XYZ occurs.
"""
import json
from pathlib import Path
from solver_view import SCHEMA, digest


class EvaluationView:
    def __init__(self, directory, map_sha256, permitted=('validation',), max_records=4_000_000):
        self._root = Path(directory)
        self._manifest = json.loads((self._root/'manifest.json').read_text(encoding='utf-8'))
        if (self._manifest.get('schema') != SCHEMA or self._manifest.get('role') != 'evaluator'
                or self._manifest.get('map_sha256') != map_sha256):
            raise ValueError('Evaluator identity/role mismatch')
        self._permitted = frozenset(permitted)
        if not self._permitted <= {'validation','heldout'}:
            raise ValueError('Unknown evaluator stage')
        self._limit = max_records

    def observations(self, partition, grid=False):
        if partition not in self._permitted:
            raise PermissionError('Partition not available at this evaluation stage')
        name = partition + ('-grid' if grid else '') + '.jsonl'
        path = self._root/name
        if path.is_symlink() or digest(path) != self._manifest['sha256'][name]:
            raise ValueError('Evaluation target integrity failure')
        with path.open(encoding='utf-8') as stream:
            for i,line in enumerate(stream,1):
                if i > self._limit or len(line)>32768:
                    raise ValueError('Evaluation stream limit')
                yield json.loads(line)
