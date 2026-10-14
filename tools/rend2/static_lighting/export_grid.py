"""Export a stock BSP light grid exactly as tr_staticlighting.cpp feeds VR_Reconstruct
(LDR, r_mapOverBrightBits 0, no forced linear light): <map>.vrg
  int32 dims[3]; float origin[3]; float size[3]; float sun[4] (dir xyz, parsed 0/1)
  float A[n*3]; float D[n*3]; float dir[n*3]; uint8 valid[n]; uint8 animated[n]
"""
import sys, struct, re, math, numpy as np
import bspinfo

def sky_sun(names, shaders_text):
    return None

for m in sys.argv[1:]:
    d = bspinfo.read_file('maps/%s.bsp' % m)
    L = bspinfo.lumps(d)
    ents = d[L[0][0]:L[0][0] + L[0][1]].decode('latin1')
    world = dict(re.findall(r'"([^"]*)"\s+"([^"]*)"', re.findall(r'\{([^{}]*)\}', ents)[0]))
    gs = [float(x) for x in world.get('gridsize', '64 64 128').split()]
    off, ln = L[7]
    mins = struct.unpack('<3f', d[off:off + 12]); maxs = struct.unpack('<3f', d[off + 12:off + 24])
    origin = [gs[k] * math.ceil(mins[k] / gs[k]) for k in range(3)]
    top = [gs[k] * math.floor(maxs[k] / gs[k]) for k in range(3)]
    dims = [int((top[k] - origin[k]) / gs[k]) + 1 for k in range(3)]
    n = dims[0] * dims[1] * dims[2]
    goff, gln = L[15]; aoff, aln = L[17]
    g = np.frombuffer(d[goff:goff + gln - gln % 30], dtype=np.uint8).reshape(-1, 30)
    arr = np.frombuffer(d[aoff:aoff + aln], dtype='<u2')
    assert len(arr) == n, (len(arr), n)
    c = g[arr]
    A = (c[:, 0:3] / 255.0).astype(np.float32)
    D = (c[:, 12:15] / 255.0).astype(np.float32)
    lat = c[:, 29].astype(np.float32) * (2 * math.pi / 256)
    lng = c[:, 28].astype(np.float32) * (2 * math.pi / 256)
    dirs = np.stack([np.cos(lat) * np.sin(lng), np.sin(lat) * np.sin(lng), np.cos(lng)], 1).astype(np.float32)
    valid = (c[:, 24] != 255).astype(np.uint8)
    anim = ((c[:, 25] != 255) | (c[:, 26] != 255) | (c[:, 27] != 255)).astype(np.uint8)
    with open(m.replace('/', '_') + '.vrg', 'wb') as f:
        f.write(struct.pack('<3i3f3f4f', *dims, *origin, *gs, 0, 0, 0, 0))
        f.write(A.tobytes()); f.write(D.tobytes()); f.write(dirs.tobytes()); f.write(valid.tobytes()); f.write(anim.tobytes())
    print(m, dims, n, 'valid', int(valid.sum()), 'anim', int(anim.sum()))
