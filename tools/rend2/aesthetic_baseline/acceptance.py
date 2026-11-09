"""Report-only aesthetic policy; never changes production acceptance.

Missing measurements block readiness. Negative controls cannot establish positive
source recovery. Units/denominators are defined in the ASR01 report.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import math


def assess(metrics, policy):
    gates = {}
    for name, bounds in policy['required_metrics'].items():
        value = metrics.get(name)
        if value is None or not isinstance(value, (int, float)) or not math.isfinite(value):
            gates[name] = dict(state='Blocked', value=None, reason='Required measurement unavailable')
        else:
            passed = all(value >= limit if key == 'min' else value <= limit
                         for key, limit in bounds.items())
            gates[name] = dict(state='Passed' if passed else 'Failed', value=value)
    states = {gate['state'] for gate in gates.values()}
    return dict(scene_aesthetic_readiness='Failed' if 'Failed' in states else
                'Blocked' if 'Blocked' in states else 'Passed', gates=gates,
                historical_source_certification='NotCertified', renderer_readiness='NotEvaluated')
