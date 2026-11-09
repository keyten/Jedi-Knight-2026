"""ASR07 discovery decisions around the existing, conditional Core::Solar fit.

No RGB predictor, canonical publication, or historical-source certification.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import math
import re


def controlled_authored_sun(shader_text):
    """Only the actually used ASR07 sky material in our generated shader text.

    This deliberately restricted reader is not a general stock shader parser;
    stock evidence uses the existing native captured-evidence API instead.
    """
    parts = shader_text.split('textures/bake_forward/asr07sky', 1)
    if len(parts) != 2:
        return False
    body = parts[1].split('}', 1)[0]
    return bool(re.search(r'^\s*(q3map_sun|q3map_sunExt|sun)\s', body, re.MULTILINE))


def baseline(points):
    return max((math.dist(a, b) for i, a in enumerate(points) for b in points[i+1:]), default=0.)


def decide(result, validation_rows, authored_sun=False, encoding_known=False):
    """Validation decides; untouched final-test RGB never enters this function.

    SunAbsent means a supported no-direct-sun explanation, never a physical
    upper bound of zero. Missing geometry/encoding and failed fits are uncertain.
    A good directional fit without independent sun provenance remains compatible
    with a distant finite lamp, even over many blocks.
    """
    out = dict(state='SunUncertain', conditional=True, exact_historical_source=False,
               sunlight_upper_bound=None, relative_strength=None,
               finite_local_alternative='NotExcluded',
               alternatives=['SunPresent', 'SunAbsent', 'SunUncertain'])
    hypotheses = result.get('hypotheses', [])
    if not encoding_known:
        out['reason'] = 'Unknown encoding/profile; numerical sunlight is unavailable, not zero'
        return out
    if result.get('code') or not hypotheses:
        out['reason'] = 'No independently validated complete sun/sky explanation; local light or missing evidence remains possible'
        return out
    if result.get('state') == 'SkyOnly' and all(h['model'] == 0 for h in hypotheses):
        out.update(state='SunAbsent', reason='A positive diffuse-sky explanation needs no direct sun; weaker or occluded sun remains unbounded')
        return out
    sun = [h for h in hypotheses if h['model'] == 2]
    if not sun or result.get('state') == 'AmbiguousSun':
        out['reason'] = 'Competing directional and diffuse explanations remain'
        return out
    h = sun[0]
    rows = {r['id']: r for r in validation_rows}
    support = [rows[r['sample']]['world'] for r in h['receivers']
               if r['exclusion'] == 0 and r['sun_response'] > .05 and r['sample'] in rows]
    blocks = {tuple(math.floor((v-64)/128) for v in p) for p in support}
    out.update(to_source=h['to_source'], candidate_sun_compiler_rgb=h['sun_compiler_rgb'],
               candidate_sky_compiler_rgb=h['sky_compiler_rgb'],
               sun_visible_receivers=len(support), sun_visible_blocks=len(blocks),
               sun_visible_baseline_world=baseline(support),
               angular_ambiguity_radians=h['sampled_angular_ambiguity_radians'])
    if len(blocks) < 3 or baseline(support) < 256:
        out['reason'] = 'Directional fit lacks long-baseline actual sky-visible receiver support'
        return out
    if not authored_sun:
        out['reason'] = 'Directional field is also compatible with a distant local lamp; no independent authored sun evidence'
        return out
    rgb = h['sun_compiler_rgb']
    y = sum(a*b for a, b in zip(rgb, (.2126, .7152, .0722)))
    out.update(state='SunPresent', relative_strength=y,
               chromaticity_y1=[v/y for v in rgb],
               reason='Conditional directional improvement over sky/fill, long-baseline sky receivers and independent authored sun evidence; local/GI model assumptions still apply')
    return out


def semantic(value):
    if isinstance(value, dict):
        return {k: semantic(v) for k, v in value.items() if k not in ('seconds', 'resources')}
    if isinstance(value, list):
        return [semantic(v) for v in value]
    return value
