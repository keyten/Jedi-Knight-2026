#!/usr/bin/env python3
"""Silhouette POM test materials (docs/rend2-silhouette-pom.md).

Writes a mod tree (never touches the base pk3s) with procedural normal+height
maps and a shader file:

  <out>/textures/spom_test/<pattern>_nh.tga   RGB normal, A height (white = high)
  <out>/textures/spom_test/<pattern>.tga      a plain diffuse to see the shape
  <out>/shaders/zz_spom_test.shader

Patterns: blocks (chunky panels, deep grooves), bumps (smooth), rocks (broken
stone edge like noise), steps (a single ramp + step, for the depth / contour
checks).

Stock world shaders can be overridden to try the feature on a real map:

  python make_spom_test.py --out C:/.../base/spom_test --override textures/imperial/basic_floor1:blocks

The override keeps the stock diffuse image when --diffuse is given (it must be
the image of that shader), otherwise the plain test diffuse is used. Zip the
tree as zz_spom_test.pk3 (or use it as a folder with fs_game).
"""
import argparse
import math
import os
import random
import struct

SIZE = 256


def height_blocks(u, v):
	fu, fv = (u * 4.0) % 1.0, (v * 4.0) % 1.0
	e = min(fu, 1.0 - fu, fv, 1.0 - fv)
	return 1.0 if e > 0.12 else max(0.0, (e - 0.06) / 0.06)


def height_bumps(u, v):
	return 0.5 + 0.5 * math.sin(2 * math.pi * 3 * u) * math.sin(2 * math.pi * 3 * v)


_rng = random.Random(7)
_grid = [[_rng.random() for _ in range(17)] for _ in range(17)]


def _noise(u, v, freq):
	x, y = (u * freq) % 16.0, (v * freq) % 16.0
	ix, iy = int(x), int(y)
	fx, fy = x - ix, y - iy
	fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
	a = _grid[iy][ix] * (1 - fx) + _grid[iy][ix + 1] * fx
	b = _grid[iy + 1][ix] * (1 - fx) + _grid[iy + 1][ix + 1] * fx
	return a * (1 - fy) + b * fy


def height_rocks(u, v):
	h = 0.55 * _noise(u, v, 4) + 0.3 * _noise(u, v, 8) + 0.15 * _noise(u, v, 16)
	return min(1.0, max(0.0, (h - 0.25) * 1.8))


def height_steps(u, v):
	if u < 0.5:
		return u * 2.0 * 0.5
	return 1.0 if v % 0.5 < 0.25 else 0.35


PATTERNS = {'blocks': height_blocks, 'bumps': height_bumps, 'rocks': height_rocks, 'steps': height_steps}


def write_tga(path, width, height, pixels):
	# uncompressed 32 bit, origin bottom left; pixels: rows bottom to top, BGRA
	with open(path, 'wb') as f:
		f.write(struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, width, height, 32, 8))
		f.write(bytes(pixels))


def make_maps(out, name, fn, strength):
	h = [[fn(x / SIZE, y / SIZE) for x in range(SIZE)] for y in range(SIZE)]
	nh, diffuse = [], []
	for y in range(SIZE):
		for x in range(SIZE):
			dx = (h[y][(x + 1) % SIZE] - h[y][(x - 1) % SIZE]) * 0.5 * strength
			dy = (h[(y + 1) % SIZE][x] - h[(y - 1) % SIZE][x]) * 0.5 * strength
			n = (-dx, -dy, 1.0)
			l = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
			r, g, b = (int((c / l * 0.5 + 0.5) * 255 + 0.5) for c in n)
			a = int(h[y][x] * 255 + 0.5)
			nh += [b, g, r, a]
			shade = int(90 + 120 * h[y][x])
			diffuse += [int(shade * 0.85), shade, int(shade * 1.05) if shade * 1.05 < 255 else 255, 255]
	tex = os.path.join(out, 'textures', 'spom_test')
	os.makedirs(tex, exist_ok=True)
	write_tga(os.path.join(tex, name + '_nh.tga'), SIZE, SIZE, nh)
	write_tga(os.path.join(tex, name + '.tga'), SIZE, SIZE, diffuse)


def shader_text(name, diffuse, pattern, depth, bias, distance, steps):
	lines = [name, '{', '\tpomSilhouette']
	if distance:
		lines.append('\tpomSilhouetteDistance %g' % distance)
	if steps:
		lines.append('\tpomSilhouetteSteps %d' % steps)
	lines += [
		'\t{',
		'\t\tmap %s' % diffuse,
		'\t\tnormalHeightMap textures/spom_test/%s_nh' % pattern,
		'\t\tparallaxDepth %g' % depth,
		'\t\tparallaxBias %g' % bias,
		'\t\trgbGen identity',
		'\t}',
		'\t{',
		'\t\tmap $lightmap',
		'\t\tblendFunc GL_DST_COLOR GL_ZERO',
		'\t\trgbGen identity',
		'\t}',
		'}', '']
	return '\n'.join(lines)


def main():
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument('--out', required=True, help='mod folder to write')
	ap.add_argument('--depth', type=float, default=0.06, help='parallaxDepth (texture units, as ordinary POM)')
	ap.add_argument('--bias', type=float, default=0.5, help='parallaxBias: 0 recessed only, 1 protruding only')
	ap.add_argument('--distance', type=float, default=0.0, help='pomSilhouetteDistance, 0 = r_pomSilhouetteDistance')
	ap.add_argument('--steps', type=int, default=0, help='pomSilhouetteSteps, 0 = r_pomSilhouetteMaxSteps')
	ap.add_argument('--strength', type=float, default=24.0, help='normal map strength')
	ap.add_argument('--override', action='append', default=[], metavar='SHADER:PATTERN',
		help='replace a stock world shader by a silhouette POM material')
	ap.add_argument('--diffuse', action='append', default=[], metavar='SHADER:IMAGE',
		help='diffuse image of an overridden shader (default: the test diffuse)')
	args = ap.parse_args()

	for name, fn in PATTERNS.items():
		make_maps(args.out, name, fn, args.strength)

	diffuses = dict(d.split(':', 1) for d in args.diffuse)
	text = ['// generated by tools/rend2/make_spom_test.py, see docs/rend2-silhouette-pom.md', '']
	for name in PATTERNS:
		text.append(shader_text('textures/spom_test/' + name, 'textures/spom_test/' + name, name,
			args.depth, args.bias, args.distance, args.steps))
	for o in args.override:
		shader, pattern = o.rsplit(':', 1)
		if pattern not in PATTERNS:
			raise SystemExit('unknown pattern %s (%s)' % (pattern, ', '.join(PATTERNS)))
		text.append(shader_text(shader, diffuses.get(shader, 'textures/spom_test/' + pattern), pattern,
			args.depth, args.bias, args.distance, args.steps))
	os.makedirs(os.path.join(args.out, 'shaders'), exist_ok=True)
	with open(os.path.join(args.out, 'shaders', 'zz_spom_test.shader'), 'w', encoding='utf-8', newline='\n') as f:
		f.write('\n'.join(text))
	print('wrote', args.out)


if __name__ == '__main__':
	main()
