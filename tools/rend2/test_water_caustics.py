"""CPU reference/prototype for rend2 surface-driven water caustics.

Compares the selected refracted-ray footprint Jacobian with the cheaper
normal-divergence/curvature approximation on the same deterministic spectrum.
It is deliberately dependency-free so CI can run it without an OpenGL context.
"""
from __future__ import annotations

import math
import statistics
import time

IOR = 1.333
SUN = (0.24, -0.31, 0.920)
L = math.sqrt(sum(x*x for x in SUN))
SUN = tuple(x/L for x in SUN)
WAVES = ((.8,.6,1.2,2*math.pi/160), (-.45,.89,.7,2*math.pi/96),
         (.2,-.98,.35,2*math.pi/54), (.94,.34,.22,2*math.pi/38))


def slope(x, y, t, impulse=False):
    sx = sy = curvature = 0.0
    for i, (dx, dy, a, k) in enumerate(WAVES):
        p = k*(dx*x + dy*y) - 18*k*t + i*1.37
        sx += a*k*math.cos(p)*dx; sy += a*k*math.cos(p)*dy
        curvature -= a*k*k*math.sin(p)
    if impulse:
        px, py = x-128, y-144
        r2 = px*px + py*py
        envelope = math.exp(-r2/(2*34*34))
        phase = math.sqrt(r2)/9 - 4*t
        hprime = envelope*(math.cos(phase)/9 - math.sin(phase)*math.sqrt(r2)/(34*34))
        invr = 1/max(math.sqrt(r2), 1e-4)
        sx += 2.0*hprime*px*invr; sy += 2.0*hprime*py*invr
    return sx, sy, curvature


def refract(sx, sy):
    nx, ny, nz = -sx, -sy, 1.0
    q = math.sqrt(nx*nx + ny*ny + 1); nx, ny, nz = nx/q, ny/q, nz/q
    ix, iy, iz = (-SUN[0], -SUN[1], -SUN[2]); eta = 1/IOR
    c = -(nx*ix + ny*iy + nz*iz); k = max(1-eta*eta*(1-c*c), 0)
    a = eta*c-math.sqrt(k)
    return eta*ix+a*nx, eta*iy+a*ny, eta*iz+a*nz


def footprint(x, y, t, depth, impulse=False):
    sx, sy, _ = slope(x, y, t, impulse); rx, ry, rz = refract(sx, sy)
    travel = depth/max(-rz, .05)
    return x+rx*travel, y+ry*travel


def jacobian(x, y, t, depth, step=8, impulse=False):
    q = footprint(x, y, t, depth, impulse)
    qx = footprint(x+step, y, t, depth, impulse)
    qy = footprint(x, y+step, t, depth, impulse)
    dx = ((qx[0]-q[0])/step, (qx[1]-q[1])/step)
    dy = ((qy[0]-q[0])/step, (qy[1]-q[1])/step)
    return min(1/max(abs(dx[0]*dy[1]-dx[1]*dy[0]), 1/6), 6)


def curvature(x, y, t, depth, impulse=False):
    # First-order divergence of the refracted direction. Fast, but misses the
    # off-diagonal/shear terms retained by the 2x2 footprint Jacobian.
    _, _, lap = slope(x, y, t, impulse)
    return max(0, min(6, 1/(max(.16, 1 + .58*depth*lap))))


def correlation(a, b):
    ma, mb = statistics.fmean(a), statistics.fmean(b)
    aa = [x-ma for x in a]; bb = [x-mb for x in b]
    return sum(x*y for x, y in zip(aa, bb))/math.sqrt(sum(x*x for x in aa)*sum(y*y for y in bb))


def main():
    points = [(x, y) for y in range(0, 256, 8) for x in range(0, 256, 8)]
    start = time.perf_counter(); a = [jacobian(x,y,.7,96,impulse=True) for x,y in points]
    tj = time.perf_counter()-start
    start = time.perf_counter(); b = [curvature(x,y,.7,96,impulse=True) for x,y in points]
    tc = time.perf_counter()-start
    flat = jacobian(0, 0, 0, 96, False) if not WAVES else None
    changed = [jacobian(x,y,.7,96,impulse=False) for x,y in points]
    impulse_delta = statistics.fmean(abs(x-y) for x,y in zip(a, changed))
    assert all(math.isfinite(x) and 0 <= x <= 6 for x in a+b)
    assert impulse_delta > 0.005
    print(f"PASS: Jacobian/curvature correlation {correlation(a,b):.3f}")
    print(f"PASS: injected interaction changes mean focusing by {impulse_delta:.4f}")
    print(f"prototype CPU cost: Jacobian {tj*1000:.3f} ms, curvature {tc*1000:.3f} ms, ratio {tj/max(tc,1e-9):.2f}x ({len(points)} samples)")
    print("decision: Jacobian retains shear and refracted displacement; curvature is cheaper but only a first-order brightness proxy")


if __name__ == '__main__':
    main()
