import numpy as np, struct, sys
for m in sys.argv[1:]:
    f = open(m + '.vrg', 'rb'); dims = struct.unpack('<3i', f.read(12)); org = struct.unpack('<3f', f.read(12)); sz = struct.unpack('<3f', f.read(12)); f.read(16)
    n = dims[0] * dims[1] * dims[2]
    A = np.frombuffer(f.read(n * 12), np.float32).reshape(dims[2], dims[1], dims[0], 3)
    D = np.frombuffer(f.read(n * 12), np.float32).reshape(dims[2], dims[1], dims[0], 3)
    lum = D.sum(-1) + 1e-6
    chroma = D / lum[..., None]
    best = []
    # red-ish vs blue-ish directed light within 2 cells horizontally, both reasonably bright
    redness = chroma[..., 0] - chroma[..., 2]
    bright = D.max(-1) > 0.3
    for dx, dy in ((2, 0), (0, 2), (3, 0), (0, 3)):
        a = redness[:, :dims[1] - dy, :dims[0] - dx]; b = redness[:, dy:, dx:]
        ok = bright[:, :dims[1] - dy, :dims[0] - dx] & bright[:, dy:, dx:]
        diff = np.where(ok, np.abs(a - b), 0)
        idx = np.argsort(diff.ravel())[-3:]
        for i in idx:
            z, y, x = np.unravel_index(i, diff.shape)
            best.append((float(diff[z, y, x]), [org[0] + (x + dx / 2) * sz[0], org[1] + (y + dy / 2) * sz[1], org[2] + z * sz[2]],
                         D[z, y, x].round(2).tolist(), D[z, y + dy, x + dx].round(2).tolist()))
    best.sort(reverse=True)
    for b in best[:3]:
        print(m, '%.2f' % b[0], [round(v) for v in b[1]], b[2], b[3])
