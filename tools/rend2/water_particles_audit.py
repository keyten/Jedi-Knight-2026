"""Stock audit of the underwater particles (r_waterParticles, tr_waterparticles.cpp): every BSP, read only.

  python tools/rend2/water_particles_audit.py [base directory] [--map name] [--radius 160]

Opens the installed pk3 files as ZIP archives (never writes them) and mirrors the renderer:
- the liquid brushes kept by R_LoadLiquidBrushes (liquid_audit.py), grouped into water bodies as
  R_WaterParticlesWorldLoaded does: brushes of one class whose bounds overlap or touch within one unit
- the profile of each body: the first env.json "WaterParticles" rule matching the shader or the upward side
  shader of one of its brushes, else clean (never guessed from names)
- for a camera at the middle of the largest brush of each water / slime body (below its top), the spawn test of
  WP_Simulate by Monte Carlo: uniform candidates in the sphere, kept inside a brush of the body (one unit
  margin) and in sight of the camera (segment against the solid world brushes, every 4 units, standing in
  for the collision trace); the expected live particles per profile and the frames needed to fill them with
  the per frame spawn budget (48 + target / 8 candidates, 24 + target / 16 sight traces)
"""
import json
import math
import os
import random
import sys

import liquid_audit as LA
import water_audit as W

CLASS_NAMES = ['water', 'slime', 'lava']
PROFILES = ['none', 'clean', 'murky', 'dirty']
# s_wpProfiles density (particles per million cubic units at r_waterParticlesDensity 1)
DENSITY = {'clean': 3.0, 'murky': 12.0, 'dirty': 32.0}


def load_particle_rules(files, base_name):
    """env.json "WaterParticles" of the map (pk3 or loose file)"""
    key = 'cubemaps/%s/env.json' % base_name
    text = None
    loose = os.path.join(files.base, key)
    if os.path.isfile(loose):
        text = open(loose, encoding='latin-1').read()
    elif key in files.files:
        text = files.read(key).decode('latin-1')
    if not text:
        return []
    try:
        doc = json.loads(text)
    except ValueError:
        return []
    rules = []
    for entry in doc.get('WaterParticles', []) if isinstance(doc, dict) else []:
        shader, profile = str(entry.get('Shader', '')).lower(), str(entry.get('Profile', '')).lower()
        if shader and profile in PROFILES:
            rules.append((shader, PROFILES.index(profile)))
    return rules


def bodies_of(kept):
    parent = list(range(len(kept)))

    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for i in range(len(kept)):
        for j in range(i + 1, len(kept)):
            a, b = kept[i], kept[j]
            if a.cls != b.cls:
                continue
            if all(a.bounds[0][k] <= b.bounds[1][k] + 1.0 and b.bounds[0][k] <= a.bounds[1][k] + 1.0 for k in range(3)):
                parent[root(i)] = root(j)
    groups = {}
    for i in range(len(kept)):
        groups.setdefault(root(i), []).append(i)
    return list(groups.values())


def body_profile(brushes, rules):
    best = -1
    for b in brushes:
        for name in (b.shader, b.top):
            if not name:
                continue
            lower = name.lower()
            for index, (pattern, _) in enumerate(rules):
                if (lower.startswith(pattern[:-1]) if pattern.endswith('*') else lower == pattern):
                    if best < 0 or index < best:
                        best = index
                    break
    return PROFILES[rules[best][1]] if best >= 0 else None


def inside_body(brushes, p, margin):
    for b in brushes:
        if all(b.bounds[0][k] <= p[k] <= b.bounds[1][k] for k in range(3)) and b.inside(p, -margin):
            return True
    return False


def in_sight(solid, a, b):
    length = math.dist(a, b)
    steps = max(1, int(length / 4.0))
    for s in range(1, steps + 1):
        t = s / steps
        if solid.inside([a[k] + (b[k] - a[k]) * t for k in range(3)]):
            return False
    return True


def spawn_test(brushes, solid, camera, radius, samples, rng):
    inside = sight = 0
    for _ in range(samples):
        while True:
            o = [rng.uniform(-1, 1) for _ in range(3)]
            if o[0] * o[0] + o[1] * o[1] + o[2] * o[2] <= 1.0:
                break
        p = [camera[k] + radius * o[k] for k in range(3)]
        if not inside_body(brushes, p, 1.0):
            continue
        inside += 1
        if in_sight(solid, camera, p):
            sight += 1
    return inside / samples, sight / samples, (sight / inside if inside else 0.0)


def fill_frames(target, accept, sight_given_inside, inside):
    """frames until the expected live count is spawned with the per frame budget of WP_Simulate"""
    if target <= 0 or accept <= 0.0:
        return 0
    attempts, traces = 48 + target // 8, 24 + target // 16
    per_frame = min(attempts * accept, traces * sight_given_inside) if inside > 0 else 0.0
    return int(math.ceil(target * accept / max(per_frame, 1e-6)))


def main(argv):
    base = W.DEFAULT_BASE
    only, radius = None, 160.0
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == '--map':
            only = args.pop(0).lower()
        elif a == '--radius':
            radius = float(args.pop(0))
        else:
            base = a
    files = W.Pk3Files(base)
    bsps = sorted(k for k in files.files if k.startswith('maps/') and k.endswith('.bsp'))
    volume = 4.0 / 3.0 * math.pi * radius ** 3
    print('%d BSPs in %s, camera volume radius %.0f (%.0f cubic units)' % (len(bsps), base, radius, volume))
    maps = total_bodies = 0
    for key in bsps:
        name = key[len('maps/'):-len('.bsp')]
        if only and name.lower() != only:
            continue
        data = files.read(key)
        base_name = name.split('/')[-1]
        kept, skipped, _ = LA.load_liquids(data, LA.load_rules(files, base_name))
        if not kept:
            continue
        maps += 1
        rules = load_particle_rules(files, base_name)
        groups = bodies_of(kept)
        total_bodies += len(groups)
        solid = LA.SolidWorld(data)
        rng = random.Random(7)
        print('%s: %d kept liquid brushes, %d bodies, %d WaterParticles rules, %d skipped (no particles there)' % (
            name, len(kept), len(groups), len(rules), len(skipped)))
        for g in sorted(groups, key=lambda g: -sum(math.prod(kept[i].bounds[1][k] - kept[i].bounds[0][k] for k in range(3)) for i in g)):
            brushes = [kept[i] for i in g]
            cls = CLASS_NAMES[brushes[0].cls]
            lo = [min(b.bounds[0][k] for b in brushes) for k in range(3)]
            hi = [max(b.bounds[1][k] for b in brushes) for k in range(3)]
            profile = body_profile(brushes, rules)
            line = '   %-5s %3d brush%s  %6.0f x %6.0f x %5.0f  profile %-14s' % (
                cls, len(brushes), 'es' if len(brushes) != 1 else '  ',
                hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], (profile or 'clean') + (' (env.json)' if profile else ''))
            if cls == 'lava':
                print(line + ' lava: no particles')
                continue
            # a camera under the surface: the middle of the largest brush, else random points of the body at
            # least 8 units under its top, outside the solid world
            big = max(brushes, key=lambda b: math.prod(b.bounds[1][k] - b.bounds[0][k] for k in range(3)))
            camera = [(big.bounds[0][k] + big.bounds[1][k]) * 0.5 for k in range(3)]
            camera[2] = max(big.bounds[0][2] + 2.0, min(camera[2], big.bounds[1][2] - 24.0))
            tries = 0
            while (not inside_body(brushes, camera, 0.5) or camera[2] > hi[2] - 8.0 or solid.inside(camera)) and tries < 400:
                b = brushes[rng.randrange(len(brushes))]
                camera = [rng.uniform(b.bounds[0][k], b.bounds[1][k]) for k in range(3)]
                tries += 1
            if tries >= 400:
                print(line + ' (no camera point 8 units under the top: too shallow or inside solids)')
                continue
            inside, accept, given = spawn_test(brushes, solid, camera, radius, 4000, rng)
            parts = []
            for p in ('clean', 'murky', 'dirty'):
                target = int(DENSITY[p] * volume * 1e-6 + 0.5)
                parts.append('%s %d/%d (%d f)' % (p, round(target * accept), target, fill_frames(target, accept, given, inside)))
            print(line + ' in body %4.0f%%, in sight %4.0f%% | alive: %s' % (inside * 100, accept * 100, ', '.join(parts)))
    print('%d maps with liquids, %d bodies' % (maps, total_bodies))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
