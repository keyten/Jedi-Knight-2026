#!/usr/bin/env python3
"""
Preview of r_autoPBRRoughness (docs/rend2-auto-pbr.md): a numpy port of
R_BuildAutoRoughnessORMSImage (shared/rd-rend2/tr_image.cpp). Keep both in sync.

For every texture it writes one PNG row:
  diffuse | generated roughness (what lightall gets: min(1, class rough / mean * m))
          | constant class roughness | naive 1 - luma (NOT used, for contrast)
          | authored _rmo / _orm roughness when the PK3s have one

usage: rend2_autorough_preview.py <base dir with pk3s> <out.png> <class>:<path> [...]
       class = generic metal skin cloth leather plastic hair
"""
import glob, io, os, sys, time, zipfile

import numpy as np
from PIL import Image, ImageDraw

MAX_SIZE = 512
AMPLITUDE = 0.25
CLASS_ROUGHNESS = {'generic': 0.85, 'metal': 0.55, 'skin': 0.65, 'cloth': 0.95,
                   'leather': 0.72, 'plastic': 0.60, 'hair': 0.80}


def srgb_to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def box1d(a, radius, axis, wrap):
    n = a.shape[axis]
    idx = np.arange(-radius, n + radius)
    idx = np.mod(idx, n) if wrap else np.clip(idx, 0, n - 1)
    p = np.take(a, idx, axis=axis)
    c = np.cumsum(p, axis=axis, dtype=np.float64)
    zero = np.zeros_like(np.take(c, [0], axis=axis))
    c = np.concatenate([zero, c], axis=axis)
    k = 2 * radius + 1
    return ((np.take(c, np.arange(k, n + k), axis=axis) - np.take(c, np.arange(0, n), axis=axis)) / k).astype(np.float32)


def blur(a, radius, wrap):
    for _ in range(2):
        a = box1d(box1d(a, radius, 1, wrap), radius, 0, wrap)
    return a


def auto_roughness(rgba, wrap=True):
    """returns (multiplier m in 0..1 as float image, mean of the 8 bit m)"""
    h0, w0 = rgba.shape[:2]
    step = 1
    while max(w0, h0) // step > MAX_SIZE:
        step *= 2
    w, h = max(w0 // step, 1), max(h0 // step, 1)
    lin = srgb_to_linear(rgba[:h * step, :w * step, :3].astype(np.float32) / 255.0)
    lum = lin @ np.array([0.2126, 0.7152, 0.0722], np.float32)
    lum = lum.reshape(h, step, w, step).mean(axis=(1, 3))
    logl = np.log(lum + 0.02).astype(np.float32)

    radius = max(2, max(w, h) // 32)
    low = blur(logl, radius, wrap)
    d = logl - low

    # 3x3 std-dev
    def box3(a):
        idx = lambda n: (np.mod(np.arange(-1, n + 1), n) if wrap else np.clip(np.arange(-1, n + 1), 0, n - 1))
        p = a[idx(h)][:, idx(w)]
        return sum(p[y:y + h, x:x + w] for y in range(3) for x in range(3)) / 9.0
    v = np.sqrt(np.maximum(box3(d * d) - box3(d) ** 2, 0.0))

    flat = v.ravel()
    p90 = np.partition(flat, int(0.90 * (flat.size - 1)))[int(0.90 * (flat.size - 1))]
    scale = 1.0 / p90 if p90 > 0.01 else 0.0
    v = 1.0 - np.exp(-2.0 * v * scale)
    mean_v = float(v.mean()) if scale else 1.0

    lf = low.ravel()
    p97 = np.partition(lf, int(0.97 * (lf.size - 1)))[int(0.97 * (lf.size - 1))]
    g = np.clip((low - (p97 - 0.3)) / 0.3, 0.0, 1.0)
    g = g * g * (3.0 - 2.0 * g)
    if not scale:
        g[:] = 1.0
    v = v + (mean_v - v) * g

    m8 = np.clip(np.floor((1.0 - AMPLITUDE * (1.0 - v)) * 255.0 + 0.5), 0, 255)
    m = m8 / 255.0
    return m.astype(np.float32), float(m.mean())


class Pk3s:
    def __init__(self, base):
        self.files = {}
        for p in sorted(glob.glob(os.path.join(base, '*.pk3'))):   # later PK3 wins, like the game
            z = zipfile.ZipFile(p)
            for n in z.namelist():
                self.files[n.lower()] = z

    def load(self, name):
        stem = os.path.splitext(name.lower())[0]
        for ext in ('.png', '.tga', '.jpg'):
            z = self.files.get(stem + ext)
            if z:
                return np.array(Image.open(io.BytesIO(z.read(stem + ext))).convert('RGBA'))
        return None


def gray(a, size):
    return Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8)).convert('RGB').resize(size, Image.BILINEAR)


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    pk3 = Pk3s(sys.argv[1])
    cell = 256
    rows = []
    for arg in sys.argv[3:]:
        cls, path = arg.split(':', 1)
        rgba = pk3.load(path)
        if rgba is None:
            print('missing', path)
            continue
        t = time.perf_counter()
        m, mean = auto_roughness(rgba)
        ms = (time.perf_counter() - t) * 1000
        base = CLASS_ROUGHNESS[cls]
        final = np.minimum(min(base / mean, 2.0) * m, 1.0)   # lightall clamps per texel
        lum = srgb_to_linear(rgba[..., :3] / 255.0) @ np.array([0.2126, 0.7152, 0.0722])
        tiles = [Image.fromarray(rgba[..., :3]).resize((cell, cell), Image.BILINEAR),
                 gray(final, (cell, cell)), gray(np.full((4, 4), base), (cell, cell)), gray(1 - lum, (cell, cell))]
        stem = os.path.splitext(path)[0]
        authored = None
        for suffix, ch in (('_rmo', 0), ('_orm', 1)):
            a = pk3.load(stem + suffix)
            if a is not None:
                authored = a[..., ch] / 255.0
                tiles.append(gray(authored, (cell, cell)))
                break
        print('%-8s %-45s %4dx%-4d rough %.2f  gen mean %.3f sigma %.3f  final %.3f..%.3f  %.1f ms (numpy)%s' % (
            cls, path, rgba.shape[1], rgba.shape[0], base, mean, m.std(), final.min(), final.max(), ms,
            '  authored mean %.3f' % authored.mean() if authored is not None else ''))
        rows.append((cls + ' ' + path, tiles))

    labels = ['diffuse', 'generated (r_autoPBRRoughness 1)', 'constant (0)', 'naive 1-luma (not used)', 'authored']
    sheet = Image.new('RGB', (cell * 5, (cell + 18) * len(rows) + 18), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for i, l in enumerate(labels):
        draw.text((i * cell + 4, 3), l, fill=(230, 230, 230))
    for r, (name, tiles) in enumerate(rows):
        y = 18 + r * (cell + 18)
        draw.text((4, y + 2), name, fill=(255, 200, 80))
        for i, t in enumerate(tiles):
            sheet.paste(t, (i * cell, y + 16))
    sheet.save(sys.argv[2])
    return 0


if __name__ == '__main__':
    sys.exit(main())
