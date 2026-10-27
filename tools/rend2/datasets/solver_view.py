"""Optimizer-side input API. No Dataset/archive/evaluator handle is accepted.

Mount ONLY the published input directory for an untrusted solver process. Python
object privacy is not an OS sandbox. This module enforces the data/API boundary,
including checksums, schema, map identity, role and bounded streaming reads.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import hashlib
import json
from pathlib import Path

SCHEMA = 'org.openjk.blind-observations.v1'
FIELDS = {'id', 'page', 'x', 'y', 'surface', 'slot', 'style', 'position', 'normal',
          'area', 'raw', 'encoding', 'channel_flags', 'transfer', 'primaries',
          'source', 'scene', 'deluxe', 'classification', 'material_uncertainty'}
GRID_FIELDS = {'id', 'map_sha256', 'ordinal', 'record', 'position', 'footprint_radius',
               'styles', 'ambient', 'directed', 'direction_bytes', 'to_source',
               'angular_bound', 'hdr', 'ambient_source', 'directed_source',
               'geometry_status', 'direction_quality', 'component_meaning'}


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


class TrainingView:
    """Repeatable immutable training stream. Contains no held-out RGB backing store.

    The geometry archive contains neither images/grid RGB nor vertex colours/HDR.
    Validation and held-out files are evaluator-owned, outside this directory.
    """
    def __init__(self, directory, map_sha256, max_bytes=1024**3, max_records=4_000_000):
        self._root = Path(directory).resolve()
        manifest_path = self._root / 'manifest.json'
        if manifest_path.stat().st_size > 1024 * 1024:
            raise ValueError('Manifest limit')
        self._manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
        m = self._manifest
        if (m.get('schema') != SCHEMA or m.get('role') != 'train' or
                m.get('map_sha256') != map_sha256):
            raise ValueError('Schema/role/map mismatch')
        self._limit = max_records
        for name in ('train.jsonl', 'geometry.jkobs', 'grid.jsonl'):
            path = self._root / name
            if path.is_symlink() or not path.is_file() or path.stat().st_size > max_bytes:
                raise ValueError('Invalid/oversized training member')
            if digest(path) != m['sha256'][name]:
                raise ValueError('Training member integrity failure')

    @property
    def geometry_path(self):
        return self._root / 'geometry.jkobs'

    def observations(self):
        # Recheck: the controller must not mutate files after publication.
        path = self._root / 'train.jsonl'
        if digest(path) != self._manifest['sha256']['train.jsonl']:
            raise ValueError('Training data changed')
        with path.open(encoding='utf-8') as stream:
            for count, line in enumerate(stream, 1):
                if count > self._limit or len(line) > 32768:
                    raise ValueError('Training stream limit')
                row = json.loads(line, parse_constant=lambda x: (_ for _ in ()).throw(ValueError(x)))
                if set(row) != FIELDS:
                    raise ValueError('Unexpected training fields')
                yield row
        if (count if 'count' in locals() else 0) != self._manifest['counts']['train']:
            raise ValueError('Truncated training stream')

    def grid_observations(self):
        path = self._root / 'grid.jsonl'
        if digest(path) != self._manifest['sha256']['grid.jsonl']:
            raise ValueError('Grid data changed')
        with path.open(encoding='utf-8') as stream:
            for count, line in enumerate(stream, 1):
                if count > self._limit or len(line) > 32768:
                    raise ValueError('Grid stream limit')
                row = json.loads(line, parse_constant=lambda x: (_ for _ in ()).throw(ValueError(x)))
                if set(row) != GRID_FIELDS or row.get('map_sha256') != self._manifest['map_sha256']:
                    raise ValueError('Grid map mismatch')
                yield row
        if (count if 'count' in locals() else 0) != self._manifest['grid_counts']['train']:
            raise ValueError('Truncated grid stream')
