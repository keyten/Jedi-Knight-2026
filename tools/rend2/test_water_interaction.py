"""Deterministic reference checks for rend2's body-local water disturbance solver.

This mirrors the finite-difference update in tr_watersurface.cpp without an
OpenGL dependency.  It checks the invariants that are easiest to regress:
fixed-timestep independence, mask isolation, expanding finite waves,
interference, eventual sleep, and distance-sampled wake density.
"""
from __future__ import annotations

import math
import sys


DT = 1.0 / 60.0


class Field:
    def __init__(self, w=40, h=32, dx=16.0, speed=64.0, damping=0.5):
        self.w, self.h, self.dx = w, h, dx
        self.speed, self.damping = speed, damping
        self.mask = [1] * (w * h)
        self.state = [0.0] * (w * h * 2)
        self.next = self.state.copy()
        self.energy = 0.0
        self.quiet = 0.0
        self.active = False
        self.accum = 0.0

    def impulse(self, x, y, radius, strength):
        rr = max(radius, 1.5 * self.dx)
        stamp = []
        for iy in range(self.h):
            for ix in range(self.w):
                i = iy * self.w + ix
                if not self.mask[i]:
                    continue
                wx, wy = (ix + 0.5) * self.dx - x, (iy + 0.5) * self.dx - y
                d2 = (wx * wx + wy * wy) / (rr * rr)
                if d2 < 1.0:
                    delta = strength * 48.0 * (1.0 - d2) ** 2 * (1.0 - 4.0 * d2)
                    stamp.append((i, delta))
        mean = sum(delta for _, delta in stamp) / len(stamp) if stamp else 0.0
        for i, delta in stamp:
            self.state[i * 2 + 1] += delta - mean
        self.active, self.quiet = True, 0.0

    def step(self):
        if not self.active:
            return
        inv = 1.0 / (self.dx * self.dx)
        cmax = 0.68 / (DT * math.sqrt(2.0 * inv))
        c = min(self.speed, cmax)
        energy, wet = 0.0, 0
        for y in range(self.h):
            for x in range(self.w):
                i = y * self.w + x
                if not self.mask[i]:
                    self.next[i * 2] = self.next[i * 2 + 1] = 0.0
                    continue
                hc, vc = self.state[i * 2:i * 2 + 2]

                def height(sx, sy):
                    if sx < 0 or sy < 0 or sx >= self.w or sy >= self.h:
                        return hc
                    n = sy * self.w + sx
                    return self.state[n * 2] if self.mask[n] else hc

                lap = (height(x - 1, y) - 2 * hc + height(x + 1, y)) * inv
                lap += (height(x, y - 1) - 2 * hc + height(x, y + 1)) * inv
                v = max(-512.0, min(512.0, vc + DT * (c * c * lap - 2 * self.damping * vc)))
                nh = max(-64.0, min(64.0, hc + DT * v))
                self.next[i * 2:i * 2 + 2] = nh, v
                energy += nh * nh + v * v / max(c * c, 1.0)
                wet += 1
        self.state, self.next = self.next, self.state
        self.energy = energy / wet if wet else 0.0
        self.quiet = self.quiet + DT if self.energy < 1e-5 else 0.0
        if self.quiet > 1.0:
            self.state[:] = [0.0] * len(self.state)
            self.energy, self.active = 0.0, False

    def frame(self, elapsed):
        self.accum = min(self.accum + min(max(elapsed, 0.0), 0.25), 4 * DT)
        steps = 0
        while self.accum + 1e-6 >= DT and steps < 4:
            self.step()
            self.accum -= DT
            steps += 1

    def heights(self):
        return self.state[0::2]


def run_fps(fps, seconds=2.0):
    f = Field()
    f.impulse(20 * f.dx, 16 * f.dx, 48.0, 1.0)
    for _ in range(round(seconds * fps)):
        f.frame(1.0 / fps)
    return f


def wake_samples(fps, seconds=2.0, speed=180.0, spacing=12.0):
    """Mirror the renderer's carried-distance sampler for straight motion."""
    last = 0.0
    carry = 0.0
    samples = []
    for frame in range(1, round(seconds * fps) + 1):
        position = speed * frame / fps
        distance = position - last
        first = spacing - carry
        count = 1 + math.floor((distance - first) / spacing) if first <= distance else 0
        samples.extend(last + first + n * spacing for n in range(count))
        carry = carry + distance - count * spacing
        if carry >= spacing:
            carry = math.fmod(carry, spacing)
        last = position
    return samples


def main():
    results = []
    # Source stamps are spatial, not one-per-render-frame.
    wake_30, wake_60, wake_144 = wake_samples(30), wake_samples(60), wake_samples(144)
    wake_error = max(abs(x - y) for a, b in ((wake_30, wake_60), (wake_60, wake_144))
                     for x, y in zip(a, b))
    results.append((len(wake_30) == len(wake_60) == len(wake_144) and wake_error < 1e-5,
                    f"wake distance sampling: {len(wake_60)} stamps, max FPS error {wake_error:.3g}"))

    a, b, c = run_fps(30), run_fps(60), run_fps(144)
    for name, other in (("30", a), ("144", c)):
        err = max(abs(x - y) for x, y in zip(b.heights(), other.heights()))
        results.append((err < 1e-5, f"fixed step: 60 vs {name} FPS max height error {err:.3g}"))

    centre = 16 * b.w + 20
    outside = max(abs(h) for i, h in enumerate(b.heights()) if abs(i - centre) > 5)
    results.append((outside > 1e-4 and all(math.isfinite(x) for x in b.state),
                    f"stable expanding wave reaches non-source cells ({outside:.4g})"))

    barrier = Field()
    for y in range(barrier.h):
        barrier.mask[y * barrier.w + barrier.w // 2] = 0
    barrier.impulse(10 * barrier.dx, 16 * barrier.dx, 48.0, 1.0)
    for _ in range(300):
        barrier.step()
    leaked = max(abs(barrier.heights()[y * barrier.w + x])
                 for y in range(barrier.h) for x in range(barrier.w // 2 + 1, barrier.w))
    results.append((leaked == 0.0, f"mask blocks disconnected region (leak {leaked:.3g})"))

    pair = Field()
    pair.impulse(17 * pair.dx, 16 * pair.dx, 48.0, 1.0)
    pair.impulse(23 * pair.dx, 16 * pair.dx, 48.0, -0.7)
    for _ in range(120):
        pair.step()
    signs = any(x > 1e-4 for x in pair.heights()) and any(x < -1e-4 for x in pair.heights())
    results.append((signs, "multiple signed impulses interfere naturally"))

    sleeper = Field(damping=1.2)
    sleeper.impulse(20 * sleeper.dx, 16 * sleeper.dx, 48.0, 0.25)
    for _ in range(60 * 90):
        sleeper.step()
        if not sleeper.active:
            break
    results.append((not sleeper.active and sleeper.energy == 0.0, "decayed field reaches exact sleeping state"))

    for ok, message in results:
        print(("PASS" if ok else "FAIL") + ": " + message)
    return 0 if all(ok for ok, _ in results) else 1


if __name__ == "__main__":
    sys.exit(main())
