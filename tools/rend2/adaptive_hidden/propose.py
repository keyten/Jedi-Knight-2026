"""Select adaptive local discovery after ASR15 fixture-association diagnostics.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
from pathlib import Path
import subprocess

from .run import adapt
from tools.rend2.lightmap_patches.run import sha, write


def explained_areas(associations, area_observations, map_sha256):
    if associations.get('schema') != 'org.openjk.fixture-associations.v1' or associations['input']['map_sha256'] != map_sha256:
        raise ValueError('ASR15 association/map identity mismatch')
    if len(area_observations) > 32 or len(associations['associations']) > 16384:
        raise ValueError('ASR16 association adapter limit')
    covered = set()
    for link in associations['associations']:
        supports = link['supports']
        if (link['selected'] and link['status'] == 'selected_conditional' and link['score'] is not None and
                link['score'] >= .8 and link['known_weight'] >= .3 and supports and
                sum(s['eligible'] for s in supports)/len(supports) >= .75):
            covered.add(link['observation_id'])
    explained = []
    for key, observations in sorted(area_observations.items()):
        if (not str(key).isdigit() or int(key) <= 0 or not isinstance(observations, list) or
                len(observations) > 256 or any(not isinstance(o, str) or not o for o in observations)):
            raise ValueError('Area-to-observation mapping invalid')
        # One anchored patch must not suppress another unexplained patch in the
        # same source volume. Empty/ambiguous/unavailable evidence stays eligible.
        if observations and all(o in covered for o in observations):
            explained.append(int(key))
    return sorted(explained)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('tool', 'geometry', 'request', 'out'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--associations', type=Path)
    parser.add_argument('--area-observations', type=Path)
    args = parser.parse_args()
    if bool(args.associations) != bool(args.area_observations):
        parser.error('Association report and explicit area/observation mapping are required together')
    text = args.request.read_text(encoding='ascii')
    if len(text) > 2*1024*1024 or len(text.splitlines()[0].split()) != 2:
        raise ValueError('Bounded native request required')
    map_hash = text.splitlines()[0].split()[1]
    explained = []
    if args.associations:
        if args.associations.stat().st_size > 64*1024*1024 or args.area_observations.stat().st_size > 1024*1024:
            raise ValueError('Association input byte limit')
        explained = explained_areas(json.loads(args.associations.read_text()), json.loads(args.area_observations.read_text()), map_hash)
    path = args.out.with_suffix('.adaptive.request')
    if path.resolve() == args.request.resolve() or args.out.exists() or path.exists():
        raise ValueError('Use fresh proposal/request output names')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(adapt(text, 'adaptive', explained), encoding='ascii')
    result = subprocess.run([str(args.tool.resolve()), str(args.geometry.resolve()), str(path.resolve())],
                            capture_output=True, text=True, timeout=180)
    if result.returncode:
        raise RuntimeError(result.stderr)
    report = json.loads(result.stdout)
    report['input'] = dict(original_request_sha256=sha(args.request), adaptive_request_sha256=sha(path),
        geometry_sha256=sha(args.geometry), tool_sha256=sha(args.tool),
        association_sha256=sha(args.associations) if args.associations else None,
        area_mapping_sha256=sha(args.area_observations) if args.area_observations else None,
        explained_areas=explained, anchor_policy='ASR15 conditional score>=.8, known mass>=.3, >=75% eligible supports for every mapped observation')
    write(args.out, report)
    print(json.dumps(dict(candidates=len(report['candidates']), explained_areas=explained, code=report['code'])))


if __name__ == '__main__':
    main()
