import sys, struct, numpy as np
sys.path.insert(0, '.')
import bspinfo, math, re
LUMA = np.array([0.2126, 0.7152, 0.0722])
def lin(x):
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)
for m in sys.argv[1:]:
    d = bspinfo.read_file('maps/%s.bsp' % m)
    L = bspinfo.lumps(d)
    goff, gln = L[15]; aoff, aln = L[17]
    g = np.frombuffer(d[goff:goff + gln - gln % 30], dtype=np.uint8).reshape(-1, 30)
    arr = np.frombuffer(d[aoff:aoff + aln], dtype='<u2')
    cells = g[arr]
    A = cells[:, 0:3] / 255.0; D = cells[:, 12:15] / 255.0
    for name, f in (('gamma', lambda x: x), ('linear', lin)):
        a = f(A); dd = f(D)
        legacy = np.maximum(a, dd) @ LUMA
        q = np.maximum(dd - a, 0) @ LUMA
        hdrq = dd @ LUMA
        lit = legacy > 0
        print('%-9s %-6s cells %d lit %.1f%% | sum Q/legacy (LDR rule) %.4f | cells with Q>0 %.1f%% | D>A cells %.1f%% | sum D/(A+D) %.3f | median A %.3f D %.3f (lit cells)' % (
            m, name, len(cells), 100 * lit.mean(), q.sum() / max(legacy.sum(), 1e-9), 100 * (q > 0).mean(), 100 * ((dd @ LUMA) > (a @ LUMA)).mean(),
            hdrq.sum() / max((a @ LUMA + hdrq).sum(), 1e-9), np.median((a @ LUMA)[lit]), np.median((dd @ LUMA)[lit])))
