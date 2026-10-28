#!/usr/bin/env python3
"""Trusted controller: quality census + physically separate blind observation views.

Full .jkobs/rows/answer files remain private. The optimizer receives ONLY inputs/.
No solver is implemented. SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from contextlib import ExitStack
from collections import Counter, defaultdict
import gzip
import hashlib
import json
import math
from pathlib import Path
import shutil
import time
from solver_view import SCHEMA, FIELDS, digest


def rows(path):
    opener = gzip.open if str(path).endswith('.gz') else open
    with opener(path, 'rt', encoding='utf-8') as stream:
        for line in stream:
            if len(line) > 32768:
                raise ValueError('Controller row limit')
            yield json.loads(line)


def write(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + '\n', encoding='utf-8')


def quality(r):
    names = ['uncovered', 'footprint_only', 'mapped', 'ambiguous_overlap', 'unsupported_geometry']
    result = [names[r['coverage']]]
    if r['coverage']==0:
        result.append('chart_unmapped_or_padding' if r.get('chart_reference_flags',0) else 'atlas_padding_certified')
        if r.get('chart_reference_flags',0)&4: result.append('potentially_moving_receiver')
        if r.get('chart_reference_flags',0)&2: result.append('excluded_material_chart')
    if r['coverage'] == 2:
        result += ['approximate_footprint' if r['issues'] & 128 else 'affine_footprint']
    for test, name in [
        (r['position'] is not None, 'position_known'), (r['normal'] is not None, 'normal_oriented'),
        (r['area'] > 0, 'positive_area'), (r['transfer'] == 0, 'unknown_transfer'),
        (r['coverage'] == 2 and r['normal'] is None, 'invalid_or_missing_normal'),
        (r['coverage'] in (1,2) and r['area'] <= 0, 'invalid_or_missing_area'),
        (r['primaries'] == 0, 'unknown_primaries'), (r['source'] is not None, 'source_relative_available'),
        (r['source'] is not None and r['primaries'] != 0, 'source_rgb_comparable_conditional'),
        (r['scene'] is not None, 'scene_equivalent_available'),
        (any(f & 1 for f in r['channel_flags']), 'quantized'),
        (any(f & 8 for f in r['channel_flags']), 'censored'),
        (r['classification'] & ((1 << 7) | (1 << 8) | (1 << 24)), 'material_style_inconsistent'),
        (r['classification'] & (1 << 15), 'moving_receiver'),
        (r['classification'] & (1 << 23), 'missing_lightmap'),
        (r['style'] not in (0, 255), 'styled_separate'),
        (not r['local_geometry_complete'] or r.get('local_geometry_intersects',False) or r['local_geometry_issues'], 'local_geometry_uncertain'),
        (not r['positive_region'] and not r['negative_region'], 'region_unassigned'),
        (r['deluxe'] is None, 'deluxe_unavailable'), (r['deluxe'] is not None, 'deluxe_supplementary'),
        (r['deluxe_state'] not in (0, 1), 'deluxe_correspondence_or_encoding_unusable'),
        (r['material_uncertainty'] != 0, 'material_uncertain'),
        (r['fit_candidate'], 'conditional_diffuse_candidate')]:
        if test:
            result.append(name)
    result.append('localization_not_established')
    return result


def eligible(r):
    return (r['coverage'] == 2 and r['position'] is not None and r['normal'] is not None
            and r['area'] > 0 and r['style'] == 0 and r['fit_candidate']
            and not (r['issues'] & ((1 << 2) | (1 << 8) | (1 << 11)))
            and all(v is not None and math.isfinite(v) and v >= 0 for v in r['raw']))


def assignment(r, block, gap, seed, origin=(0., 0., 0.)):
    """Entire conservative footprint + filter margin must remain in one block.

    3 source pixels of margin cover bilinear filtering plus adjacent texels;
    each radius bounds a full pixel (patches use the full control hull). Thus
    seam duplicates cannot bridge held-out/training just across a world boundary.
    """
    radius = 3 * r['footprint_radius'] + gap
    p = r['position']
    if not p or not math.isfinite(radius) or radius < 0:
        return None
    if len(origin) != 3 or not all(math.isfinite(v) for v in origin):
        raise ValueError('Invalid partition origin')
    p = [v-o for v,o in zip(p,origin)]
    cell = tuple(math.floor(x / block) for x in p)
    if any(min(x - c * block, (c + 1) * block - x) <= radius for x, c in zip(p, cell)):
        return None
    key = json.dumps([seed, cell], separators=(',', ':')).encode('ascii')
    value = int.from_bytes(hashlib.sha256(key).digest()[:8], 'little') % 10
    return 'train' if value < 6 else 'validation' if value < 8 else 'heldout'


def grid_views(path, inputs, evaluator, map_hash, block, gap, seed, origin=(0., 0., 0.)):
    """Deduplicated mgrid source records must not bridge colour partitions.

    A record touched by any gap cell or by multiple partitions is withheld.
    Original ambient/directed names do not assert physical indirect/direct light.
    """
    masks, counts = {}, Counter()
    bits = {None: 8, 'train': 1, 'validation': 2, 'heldout': 4}
    if not path.exists():
        path = Path(str(path) + '.gz')
    if path.exists():
        for r in rows(path):
            bit = bits[assignment(r, block, gap, seed, origin)]
            masks[r['record']] = masks.get(r['record'], 0) | bit
    with ExitStack() as stack:
        streams = {role: stack.enter_context((directory / filename).open('w', encoding='utf-8', newline='\n'))
                   for role, directory, filename in [('train',inputs,'grid.jsonl'),
                        ('validation',evaluator,'validation-grid.jsonl'),('heldout',evaluator,'heldout-grid.jsonl')]}
        if path.exists():
            for r in rows(path):
                role = assignment(r, block, gap, seed, origin)
                if role is None or masks[r['record']] != bits[role]:
                    counts['withheld_shared_or_gap'] += 1
                    continue
                r['map_sha256'] = map_hash
                r['id'] = hashlib.sha256(f'{map_hash}/grid/v1/{r["ordinal"]}'.encode('ascii')).hexdigest()
                r['geometry_status'] = 'unclassified; styles/RGB never certify free space'
                r['direction_quality'] = 'quantized_uncalibrated'
                r['component_meaning'] = 'baked ambient/directed; not physical indirect/direct'
                streams[role].write(json.dumps(r, separators=(',', ':'), allow_nan=False) + '\n')
                counts[role] += 1
    return dict(counts)


class Group:
    def __init__(self):
        self.counts = Counter()
        self.surfaces = set()
        self.normals = Counter()
        self.lo = [math.inf] * 3
        self.hi = [-math.inf] * 3
        self.normal_moment = [0.] * 9

    def add(self, r, flags=None):
        self.counts['denominator_candidate_texels'] += 1
        self.counts.update(quality(r) if flags is None else flags)
        self.counts['censored_channels'] += sum(bool(f & 8) for f in r['channel_flags'])
        if r['coverage'] != 2:
            return
        self.surfaces.add(r['surface'])
        for i, x in enumerate(r['position']):
            self.lo[i] = min(self.lo[i], x)
            self.hi[i] = max(self.hi[i], x)
        n = r['normal']
        if n:
            axis = max(range(3), key=lambda i: abs(n[i]))
            self.normals[f'{"xyz"[axis]}{"+" if n[axis] >= 0 else "-"}'] += 1
            for i in range(3):
                for j in range(3):
                    self.normal_moment[i*3+j] += n[i]*n[j]

    def report(self):
        count = sum(self.normals.values())
        m = [x / count if count else 0 for x in self.normal_moment]
        det = m[0]*(m[4]*m[8]-m[5]*m[7])-m[1]*(m[3]*m[8]-m[5]*m[6])+m[2]*(m[3]*m[7]-m[4]*m[6])
        return dict(counts=dict(self.counts), distinct_receiver_surfaces=len(self.surfaces),
                    normal_orientation_counts=dict(self.normals), normal_second_moment=m,
                    orientation_span_proxy=max(0., 27*det),
                    position_bounds=[self.lo, self.hi] if self.surfaces else None,
                    localization_information='No hypothesis/Jacobian: not Fisher information; pixel independence unproven')


def prepare(prefix, output, block=1024., gap=8., seed=20261026, origin=(0., 0., 0.)):
    if block <= 0 or gap < 0 or not math.isfinite(block + gap):
        raise ValueError('Invalid partition geometry')
    if len(origin) != 3 or not all(math.isfinite(v) for v in origin):
        raise ValueError('Invalid partition origin')
    prefix, output = Path(prefix), Path(output)
    source = json.loads(prefix.with_suffix('.source.json').read_text(encoding='utf-8'))
    path = prefix.with_suffix('.rows.jsonl')
    if not path.exists():
        path = Path(str(path) + '.gz')
    output.mkdir(parents=True, exist_ok=False)
    inputs, evaluator = output / 'inputs', output / 'evaluator'
    inputs.mkdir(); evaluator.mkdir()
    geometry = prefix.with_suffix('.public.jkobs')
    shutil.copyfile(geometry, inputs / 'geometry.jkobs')
    # These are separate files, not views into one shared RGB backing buffer.
    streams = {'train': (inputs / 'train.jsonl').open('w', encoding='utf-8', newline='\n'),
               'validation': (evaluator / 'validation.jsonl').open('w', encoding='utf-8', newline='\n'),
               'heldout': (evaluator / 'heldout.jsonl').open('w', encoding='utf-8', newline='\n')}
    counts, exclusions, brightness = Counter(), Counter(), Counter()
    groups = defaultdict(Group)
    observed_pages, page_coordinates = set(), set()
    previous_image = None
    pages = {p['image']: p for p in source['pages']}
    primary_images = {}
    for p in source['pages']:
        if not p['missing']:
            logical=p.get('page',p['image'])
            primary_images[logical]=min(p['image'],primary_images.get(logical,p['image']))
    page_counts = Counter()
    page_start = time.perf_counter()
    duplicate_ids = set()
    sample_path = output / 'sample-positions.tsv'
    with ExitStack() as stack:
        for stream in streams.values():
            stack.enter_context(stream)
        with sample_path.open('w', encoding='utf-8') as points:
            points.write('x\ty\tz\tcoverage\tpartition\n')
            for r in rows(path):
                image = r['image']
                pinfo = pages.get(image)
                if (pinfo is None or r['page'] != pinfo.get('page', r['page']) or
                        not 0 <= r['x'] < pinfo['width'] or not 0 <= r['y'] < pinfo['height']):
                    raise ValueError('Source image/coordinate mismatch')
                if image != previous_image:
                    if image in observed_pages:
                        raise ValueError('Noncontiguous/repeated image tiles')
                    if previous_image is not None:
                        print(f'page={previous_image} seconds={time.perf_counter()-page_start:.3f}', flush=True)
                    page_start = time.perf_counter()
                    page_coordinates.clear(); duplicate_ids.clear(); previous_image = image
                coord = (r['x'], r['y'])
                if coord in page_coordinates or r['id'] in duplicate_ids:
                    raise ValueError('Duplicate source sample/tile')
                page_coordinates.add(coord); duplicate_ids.add(r['id']); observed_pages.add(image)
                page_counts[image] += 1
                flags = quality(r)
                groups['map'].add(r, flags)
                for key in (f'page/{image}', f'surface_type/{r["kind"]}', f'style/{r["style"]}',
                            f'material/{r["material"]}'):
                    groups[key].add(r, flags)
                if r['position']:
                    p = r['position']
                    spatial = ','.join(str(math.floor(v / 256)) for v in p)
                    groups[f'spatial256/{spatial}'].add(r, flags)
                    groups[f'height256/{math.floor(p[2]/256)}'].add(r, flags)
                    if r['raw'] and all(v is not None for v in r['raw']):
                        # Raw-domain bins; not linear photometric brightness.
                        value = sum(r['raw']) / 3
                        code = min(255, max(0, int(value if r['encoding'] != 2 else 16*math.log2(1+value))))
                        brightness[code] += 1
                        groups[f'encoded_brightness/{code}'].add(r, flags)
                    if r['normal']:
                        n = r['normal']; axis = max(range(3), key=lambda i: abs(n[i]))
                        groups[f'normal/{"xyz"[axis]}{"+" if n[axis] >= 0 else "-"}'].add(r, flags)
                if len(groups) > 65536:
                    raise ValueError('Quality grouping budget (65536 groups) exceeded')
                primary=image==primary_images[pinfo.get('page',image)]
                role = assignment(r, block, gap, seed, origin) if eligible(r) and primary else None
                if role:
                    record = {key: r[key] for key in sorted(FIELDS)}
                    streams[role].write(json.dumps(record, separators=(',', ':'), allow_nan=False) + '\n')
                    counts[role] += 1
                else:
                    exclusions['alternate_source_representation' if not primary else
                               'gap_or_footprint' if eligible(r) else 'quality_or_duplicate_reference'] += 1
                if r['position']:
                    points.write('\t'.join(map(str, r['position'])) + f'\t{r["coverage"]}\t{role or "excluded"}\n')
    for stream in streams.values():
        stream.close()
    expected = {p['image'] for p in source['pages'] if not p['missing']}
    expected_pixels = sum(p['width']*p['height'] for p in source['pages'] if not p['missing'])
    complete = (source['derived_complete'] and observed_pages == expected and
                all(page_counts[p['image']] == p['width']*p['height'] for p in source['pages'] if not p['missing']) and
                groups['map'].counts['denominator_candidate_texels'] == expected_pixels)
    grid_counts = grid_views(prefix.with_suffix('.grid.jsonl'), inputs, evaluator,
                             source['map_sha256'], block, gap, seed, origin)
    report = dict(source=source, all_pages_complete=bool(complete), grid_partitions=grid_counts,
                  image_alternative_policy='first sorted available image per logical page; alternatives never added as independent energy',
                  derived_texel_denominator=groups['map'].counts['denominator_candidate_texels'],
                  original_colour_pixel_denominator=expected_pixels, partitions=dict(counts),
                  partition_exclusions=dict(exclusions), groups={k:v.report() for k,v in sorted(groups.items())},
                  encoded_brightness_histogram=dict(sorted(brightness.items())),
                  unmeasured=['Per-ray certification requires a particular source/query',
                              'Indoor/outdoor semantics not inferred from region IDs',
                              'Source-distance/shadow diversity requires a candidate-specific experiment',
                              'Stock source XYZ errors have no ground truth'])
    write(output / 'coverage.json', report)
    manifest = dict(schema=SCHEMA, role='train', map_sha256=source['map_sha256'],
                    counts={k:counts[k] for k in ('train','validation','heldout')},
                    block_world=block, gap_world=gap, seed=seed, footprint_filter_multiplier=3,
                    image_alternative_policy='first sorted available image per logical page',
                    all_pages_complete=bool(complete), absolute_scale='unknown',
                    grid_counts={k:grid_counts.get(k,0) for k in ('train','validation','heldout')},
                    sha256={name:digest(inputs / name) for name in ('train.jsonl','geometry.jkobs','grid.jsonl')})
    # The public manifest reveals counts/hashes, not RGB, raw images or light answers.
    if any(origin):
        manifest['partition_origin_world'] = list(origin)
    write(inputs / 'manifest.json', manifest)
    private = dict(manifest, role='evaluator', source=source,
                   sha256={name:digest(evaluator / name) for name in ('validation.jsonl','heldout.jsonl','validation-grid.jsonl','heldout-grid.jsonl')})
    write(evaluator / 'manifest.json', private)
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('prefix', type=Path); p.add_argument('output', type=Path)
    p.add_argument('--block', type=float, default=1024); p.add_argument('--gap', type=float, default=8)
    p.add_argument('--seed', type=int, default=20261026)
    p.add_argument('--origin', type=float, nargs=3, default=(0.,0.,0.), help='Geometry-only partition origin; source positions are not changed')
    args = p.parse_args()
    report = prepare(args.prefix, args.output, args.block, args.gap, args.seed, args.origin)
    print(json.dumps({k:v for k,v in report.items() if k != 'groups'}, indent=2))


if __name__ == '__main__':
    main()
