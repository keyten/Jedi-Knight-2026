"""Read existing lightrecon_dataset_quality JSONL(.gz); write offline diagnostics.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path

from .extract import combine, extract


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024*1024), b''):
            h.update(block)
    return h.hexdigest()


def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False)+'\n', encoding='utf-8')


def read_rows(path, surfaces=(), roi=None):
    """Explicit geometry-only ROI; no target-dependent truncation or top-K crop."""
    path = Path(path)
    opener = gzip.open if path.suffix == '.gz' else open
    with opener(path, 'rt', encoding='utf-8') as f:
        for count, line in enumerate(f):
            if count >= 2000000 or len(line) > 32768:
                raise ValueError('Input scan/line budget')
            r = json.loads(line)
            if surfaces and r['surface'] not in surfaces:
                continue
            if roi and (not r.get('position') or not all(roi[k] <= r['position'][k] <= roi[k+3] for k in range(3))):
                continue
            yield r


class Geometry:
    """Adapter to the existing offline ASR13 capture geometry bridge."""
    def __init__(self, tool, capture, map_sha256):
        from tools.rend2.grid_triangulation.triangulate import Bridge
        self.bridge = Bridge(tool, capture)
        self.metadata = self.bridge.metadata
        if self.metadata['map_sha256'] != map_sha256:
            self.close()
            raise ValueError('Receiver/geometry map identity mismatch')

    def point(self, p):
        # Unknown mover travel extents cannot certify any candidate as empty.
        if self.metadata['dynamic_models']:
            return 'unknown'
        result = self.bridge.point(p)
        return result['space'] if not result['boundary'] else 'unknown'

    def ray(self, a, b):
        result = self.bridge.ray(a, b)
        return 'unknown' if result['dynamic_possible'] else result['static_state']

    def close(self):
        self.bridge.close()


def run(rows, source, output, surfaces=(), roi=None, geometry_tool=None, capture=None):
    metadata = json.loads(Path(source).read_text(encoding='utf-8'))
    report = extract(read_rows(rows, surfaces, roi))
    report['input'] = dict(rows_sha256=sha(rows), source_sha256=sha(source),
                           map_sha256=metadata['map_sha256'], map=metadata['map'],
                           surfaces=list(surfaces), roi_world=roi,
                           sampling='Original mapped luxel export; full selected chart/ROI census',
                           evaluation_split='Descriptive discovery census, no fitting/evaluation score reuse')
    if bool(geometry_tool) != bool(capture):
        raise ValueError('Geometry tool and capture must be supplied together')
    geometry = Geometry(geometry_tool, capture, metadata['map_sha256']) if geometry_tool else None
    try:
        report['candidates'] = combine(report['patches'], geometry)
        if geometry:
            report['geometry'] = dict(tool_sha256=sha(geometry_tool), capture_sha256=sha(capture),
                                      map_sha256=geometry.metadata['map_sha256'])
    finally:
        if geometry:
            geometry.close()
    write(output, report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rows', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--surface', type=int, nargs='*', default=[])
    parser.add_argument('--roi', type=float, nargs=6, metavar=('X0', 'Y0', 'Z0', 'X1', 'Y1', 'Z1'))
    parser.add_argument('--geometry-tool', type=Path)
    parser.add_argument('--capture', type=Path)
    args = parser.parse_args()
    if args.roi and not all(args.roi[k] <= args.roi[k+3] for k in range(3)):
        parser.error('ROI minima must precede maxima')
    report = run(args.rows, args.source, args.out, args.surface, args.roi, args.geometry_tool, args.capture)
    print(json.dumps(dict(patches=len(report['patches']), candidates=len(report['candidates']),
                          output=str(args.out), physical_source_certified=False)))


if __name__ == '__main__':
    main()
