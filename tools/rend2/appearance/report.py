#!/usr/bin/env python3
"""Summarize frozen native X18 receipts. Missing predictions are never zero."""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import sha, write


def eligible(r):
    """Predeclared empirical surface-proxy gate, not a physical certificate."""
    if r['field_code']:
        return False
    m = r['metrics']['geometry_L0']
    c = r['common_domain']
    if m['available'] < 6 or m['available'] < .25 * m['requested'] or not c:
        return False
    error = c['geometry_L0']['rmse_rgb8']
    return (error is not None and r['support_check_blocked'] == 0 and
            all(c[x]['rmse_rgb8'] is not None and c[x]['rmse_rgb8'] > 0 and error <= .95 * c[x]['rmse_rgb8']
                for x in ('constant', 'legacy_grid_entity_proxy')))


def number(x):
    return 'Unavailable' if x is None else f'{x:.2f}'


def seal(out):
    # Git's text attributes and cross-platform replay must hash the same bytes.
    for p in out.iterdir():
        if p.is_file() and p.suffix in ('.json', '.md'):
            p.write_bytes(p.read_bytes().replace(b'\r\n', b'\n'))
    write(out / 'manifest.json', dict(artifacts={p.name: sha(p) for p in out.iterdir()
                                               if p.is_file() and p.name != 'manifest.json'},
                                     original_stock_geometry_committed=False))
    path = out / 'manifest.json'
    path.write_bytes(path.read_bytes().replace(b'\r\n', b'\n'))


def main(a):
    a.out.mkdir(parents=True, exist_ok=False)
    raw = json.loads((a.run / 'stock.json').read_text())
    summary = {}
    text = ['| Map / ROI | Field | Held-out support | Selected area support | Constant | Grid proxy | Nearest surface | Geometry L0 | Empirical gate |',
            '|---|---|---:|---:|---:|---:|---:|---:|---|']
    baselines = ['\nFull frozen held-out denominators (each baseline uses its own reported support; these errors are not a common-domain ranking):\n',
                 '| Map / ROI | Constant RGB8 RMSE | Grid used/requested | Grid RGB8 RMSE | Nearest RGB8 RMSE |',
                 '|---|---:|---:|---:|---:|']
    for name, entry in raw.items():
        item = dict(resources=entry['resources'], input=entry['input'], rois=[])
        if 'result' not in entry:
            item['process_failure'] = True
            summary[name] = item
            continue
        data = entry['result']
        item['eligible_mapped_chart_area_fraction'] = data['eligible_chart_area'] / data['mapped_chart_area']
        item['exclusions'] = data['exclusions']
        for r in data['rois']:
            m = r['metrics']['geometry_L0']
            row = dict(ordinal=r['ordinal'], admitted=r['field_code'] == 0,
                       reason=r['field_reason'], useful_on_scoped_surface_proxy=eligible(r),
                       heldout_available=m['available'], heldout_requested=m['requested'],
                       selected_chart_area_fraction=(m['supported_selected_area'] / m['selected_receiver_area']
                                                    if m['selected_receiver_area'] else None),
                       geometry_volume_fraction=r['supported_static_volume'] / r['analysis_volume'],
                       lighting_volume_fraction=r['lighting_supported_volume'] / r['analysis_volume'],
                       common_domain=r['common_domain'])
            item['rois'].append(row)
            c = r['common_domain']
            b = r['metrics']
            grid = b['legacy_grid_entity_proxy']
            baselines.append(f"| {name} / {r['ordinal']} | {number(b['constant']['rmse_rgb8'])} | "
                             f"{grid['available']}/{grid['requested']} | {number(grid['rmse_rgb8'])} | "
                             f"{number(b['nearest_surface']['rmse_rgb8'])} |")
            errors = [c.get(x, {}).get('rmse_rgb8') for x in
                      ('constant', 'legacy_grid_entity_proxy', 'nearest_surface', 'geometry_L0')]
            area = row['selected_chart_area_fraction']
            text.append(f"| {name} / {r['ordinal']} | {'Partial proxy' if row['admitted'] else r['field_reason']} | "
                        f"{m['available']}/{m['requested']} | {100*area:.1f}% | " +
                        ' | '.join(number(x) for x in errors) +
                        f" | {'Pass' if row['useful_on_scoped_surface_proxy'] else 'No'} |")
        summary[name] = item
    write(a.out / 'summary.json', dict(schema='org.openjk.appearance-summary/1', maps=summary,
                                      physical_source_models=0, physical_irradiance_fields=0,
                                      incoming_radiance_fields=0, consumers_enabled=0,
                                      scope='Three geometry-defined ROIs per map, overlapping ROIs are not independent map coverage'))
    (a.out / 'tables.md').write_text('\n'.join(text + baselines) + '\n', encoding='utf-8')
    for name in ('protocol.json', 'stock.json', 'repeat.json'):
        # Preserve exact UTF-8/LF bytes and their native input/source witnesses.
        (a.out / name).write_bytes((a.run / name).read_bytes())
    if a.preliminary:
        write(a.out / 'preliminary.json', dict(
            scope='Historical first run; area-derived fold guard was insufficient for elongated luxels. Not final quality evidence.',
            protocol_sha256=sha(a.preliminary / 'protocol.json'),
            results=json.loads((a.preliminary / 'stock.json').read_text())))
    seal(a.out)


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--run', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--preliminary', type=Path)
    main(p.parse_args())
