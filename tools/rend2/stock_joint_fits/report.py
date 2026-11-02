#!/usr/bin/env python3
"""Deterministic count/stage diagnostics, with no stock geometry or RGB pixels.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import csv
import html
import json
from pathlib import Path

HERE = Path(__file__).parent


def report():
    root = HERE / 'results/v1'
    value = json.loads((root / 'stock.json').read_text())
    rows = value['maps']
    columns = ['Map', 'Local units / profile', 'Proposing units', 'Candidates isq / linear',
               'Joint calls isq / linear', 'Nonempty completed', 'Final heldout']
    table = []
    for m in rows:
        a, b = (m['profiles'][p] for p in ('isq', 'linear'))
        table.append([m['map'], a['local_units'], a['units_proposing'],
                      f"{a['generated_candidate_records']} / {b['generated_candidate_records']}",
                      f"{a['work']['joint_calls']} / {b['work']['joint_calls']}",
                      a['completed_nonempty_attempts'] + b['completed_nonempty_attempts'],
                      a['controller']['final_calls'] + b['controller']['final_calls']])
    with (root / 'stages.csv').open('w', newline='', encoding='utf-8') as stream:
        writer = csv.writer(stream)
        writer.writerow(columns)
        writer.writerows(table)
    (root / 'tables.md').write_text('| ' + ' | '.join(columns) + ' |\n|' + '|'.join(['---'] * len(columns)) + '|\n' +
                                  '\n'.join('| ' + ' | '.join(map(str, row)) + ' |' for row in table) + '\n')
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="1120" height="365" viewBox="0 0 1120 365">',
           '<rect width="1120" height="365" fill="#101824"/>',
           '<style>text{fill:#e7eef8;font:14px sans-serif}.small{font-size:12px}.title{font-size:21px;font-weight:bold}</style>',
           '<text x="24" y="33" class="title">X12 native stock attempts: execution is not physical recovery</text>',
           '<text x="24" y="58" class="small">Frozen six-map policy; two conditional profiles; no stock XYZ/count truth. Counts, not confidence scores.</text>']
    xs = [24, 200, 350, 480, 660, 825, 995]
    for x, label in zip(xs, ['Map', 'Units/profile', 'Proposing', 'Candidates isq/lin', 'Joint isq/lin', 'Nonempty fits', 'Final heldout']):
        svg.append(f'<text x="{x}" y="94" class="small">{label}</text>')
    for i, row in enumerate(table):
        y = 108 + i * 31
        svg.append(f'<rect x="16" y="{y}" width="1088" height="28" fill="{"#192839" if i%2==0 else "#162332"}"/>')
        for j, (x, text) in enumerate(zip(xs, row)):
            colour = '#ffb272' if j >= 5 else '#e7eef8'
            svg.append(f'<text x="{x}" y="{y+20}" style="fill:{colour}">{html.escape(str(text))}</text>')
    svg += ['<text x="24" y="326" class="small">Positive observations exist. Rejections: motion extent / clearance / unavailable rows / coarse block budget.</text>',
            '<text x="24" y="347" class="small">academy1 v2 fixes the block budget, then exposes unsupported shading-normal replay. No wall/uncertainty gate was relaxed.</text>', '</svg>']
    (root / 'stages.svg').write_text('\n'.join(svg) + '\n', encoding='utf-8')


if __name__ == '__main__':
    report()
