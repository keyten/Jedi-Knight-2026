#!/usr/bin/env python3
"""CPU checks for the rend2 atmosphere (r_atmosphere, tr_atmosphere.cpp, glsl/atmosphere_*.glsl).

1. Analytic aerial perspective (AtmosphereAerial in atmosphere_common.glsl) against a fine reference
   ray march, over distance, view elevation, camera altitude, sun elevation and r_atmosphereAerialScale.
2. LUT parameterization round trips (transmittance and sky-view).
3. Sun transmittance from the ground (r_atmosphereSunColor 1, r_atmosphereInfo).

Model: Hillaire 2020 with the Earth parameters of Bruneton 2017 (same constants as the GLSL / C code).
Lengths in metres. Run: python tools/rend2/atmosphere_check.py
"""

import math
import sys

import numpy as np

# --- model (keep in sync with atmosphere_common.glsl / tr_atmosphere.cpp) ---------------------------------

R_GROUND = 6360e3
R_TOP = 6460e3
RAYLEIGH_SCATTERING = np.array([5.802e-6, 13.558e-6, 33.1e-6])
RAYLEIGH_H = 8000.0
MIE_SCATTERING = 3.996e-6
MIE_EXTINCTION = 4.40e-6
MIE_H = 1200.0
OZONE_ABSORPTION = np.array([0.650e-6, 1.881e-6, 0.085e-6])


def densities(h):
    """Rayleigh, Mie, ozone density profiles at altitude h (m)."""
    rho_r = math.exp(-h / RAYLEIGH_H)
    rho_m = math.exp(-h / MIE_H)
    # Bruneton's ozone tent: rises from 10 km, peaks at 25 km, gone at 40 km
    rho_o = max(0.0, 1.0 - abs(h - 25e3) / 15e3)
    return rho_r, rho_m, rho_o


def extinction(h):
    rho_r, rho_m, rho_o = densities(h)
    return RAYLEIGH_SCATTERING * rho_r + MIE_EXTINCTION * rho_m + OZONE_ABSORPTION * rho_o


def phase_rayleigh(nu):
    return 3.0 / (16.0 * math.pi) * (1.0 + nu * nu)


def phase_mie(nu, g=0.8):
    # Cornette-Shanks
    k = 3.0 / (8.0 * math.pi) * (1.0 - g * g) / (2.0 + g * g)
    return k * (1.0 + nu * nu) / (1.0 + g * g - 2.0 * g * nu) ** 1.5


def ray_sphere(r, mu, radius):
    """Distance along a ray from radius r with cos zenith mu to the sphere 'radius' (nearest positive), or -1."""
    b = r * mu
    c = r * r - radius * radius
    disc = b * b - c
    if disc < 0.0:
        return -1.0
    s = math.sqrt(disc)
    t0, t1 = -b - s, -b + s
    if t0 > 0.0:
        return t0
    if t1 > 0.0:
        return t1
    return -1.0


def transmittance_to_top(r, mu, steps=400):
    """T from radius r along cos zenith mu to the top of the atmosphere; 0 if the ray hits the ground."""
    if ray_sphere(r, mu, R_GROUND) > 0.0:
        return np.zeros(3)
    d = ray_sphere(r, mu, R_TOP)
    if d <= 0.0:
        return np.ones(3)
    dt = d / steps
    tau = np.zeros(3)
    for i in range(steps):
        t = (i + 0.5) * dt
        ri = math.sqrt(r * r + t * t + 2.0 * r * mu * t)
        tau += extinction(ri - R_GROUND) * dt
    return np.exp(-tau)


# --- LUT parameterizations (Bruneton transmittance, Hillaire sky-view) --------------------------------------

def transmittance_uv(r, mu):
    H = math.sqrt(R_TOP * R_TOP - R_GROUND * R_GROUND)
    rho = math.sqrt(max(r * r - R_GROUND * R_GROUND, 0.0))
    disc = r * r * (mu * mu - 1.0) + R_TOP * R_TOP
    d = max(-r * mu + math.sqrt(max(disc, 0.0)), 0.0)
    d_min = R_TOP - r
    d_max = rho + H
    x_mu = (d - d_min) / (d_max - d_min)
    x_r = rho / H
    return x_mu, x_r


def transmittance_rmu(x_mu, x_r):
    H = math.sqrt(R_TOP * R_TOP - R_GROUND * R_GROUND)
    rho = H * x_r
    r = math.sqrt(rho * rho + R_GROUND * R_GROUND)
    d_min = R_TOP - r
    d_max = rho + H
    d = d_min + x_mu * (d_max - d_min)
    mu = 1.0 if d == 0.0 else (H * H - rho * rho - d * d) / (2.0 * r * d)
    return r, max(-1.0, min(1.0, mu))


def skyview_dir(u, v, r):
    """uv -> (cos view zenith, cos(view azimuth - sun azimuth))."""
    v_horizon = math.sqrt(max(r * r - R_GROUND * R_GROUND, 0.0))
    beta = math.acos(v_horizon / r)
    zenith_horizon = math.pi - beta
    if v < 0.5:
        c = 1.0 - 2.0 * v
        c = 1.0 - c * c
        zenith = zenith_horizon * c
    else:
        c = 2.0 * v - 1.0
        zenith = zenith_horizon + beta * c * c
    cos_light = 1.0 - 2.0 * u * u
    return math.cos(zenith), cos_light


def skyview_uv(cos_zenith, cos_light, r):
    v_horizon = math.sqrt(max(r * r - R_GROUND * R_GROUND, 0.0))
    beta = math.acos(v_horizon / r)
    zenith_horizon = math.pi - beta
    zenith = math.acos(max(-1.0, min(1.0, cos_zenith)))
    if zenith < zenith_horizon:
        c = zenith / zenith_horizon
        v = (1.0 - math.sqrt(max(1.0 - c, 0.0))) * 0.5
    else:
        c = (zenith - zenith_horizon) / beta
        v = math.sqrt(max(c, 0.0)) * 0.5 + 0.5
    u = math.sqrt(max(0.0, (1.0 - cos_light) * 0.5))
    return u, v


# --- aerial perspective ---------------------------------------------------------------------------------------

def sun_transmittance_v(h, mu_s, steps=120):
    """Vectorized T toward the sun from altitudes h (array), cos zenith mu_s."""
    h = np.maximum(np.asarray(h, dtype=float), 0.0)
    r = R_GROUND + h
    b = r * mu_s
    disc_g = b * b - (r * r - R_GROUND * R_GROUND)
    hit_ground = (disc_g >= 0.0) & (-b - np.sqrt(np.maximum(disc_g, 0.0)) > 0.0)
    d = -b + np.sqrt(np.maximum(b * b - (r * r - R_TOP * R_TOP), 0.0))
    x = (np.arange(steps) + 0.5) / steps
    t = d[:, None] * x[None, :]
    ri = np.sqrt(r[:, None] ** 2 + t * t + 2.0 * r[:, None] * mu_s * t)
    hh = ri - R_GROUND
    rho_r = np.exp(-hh / RAYLEIGH_H)
    rho_m = np.exp(-hh / MIE_H)
    rho_o = np.maximum(0.0, 1.0 - np.abs(hh - 25e3) / 15e3)
    dt = (d / steps)[:, None]
    ir = (rho_r * dt).sum(axis=1)
    im = (rho_m * dt).sum(axis=1)
    io = (rho_o * dt).sum(axis=1)
    tau = (RAYLEIGH_SCATTERING[None, :] * ir[:, None] + MIE_EXTINCTION * im[:, None] +
        OZONE_ABSORPTION[None, :] * io[:, None])
    T = np.exp(-tau)
    T[hit_ground] = 0.0
    return T


def sun_transmittance(h, mu_s):
    return sun_transmittance_v(np.array([h]), mu_s)[0]


def multiscatter_v(h):
    return np.array([0.02, 0.03, 0.05])[None, :] * np.exp(-np.maximum(h, 0.0) / 5000.0)[:, None]


def multiscatter(h):
    return multiscatter_v(np.array([h]))[0]


def aerial_reference(h0, wz, length, k, nu, mu_s, steps=3000):
    """Fine ray march: scaled path length 'length' (m), altitude h0 + wz * t / k."""
    dt = length / steps
    t = (np.arange(steps) + 0.5) * dt
    h = h0 + wz * t / k
    ts = sun_transmittance_v(h, mu_s)
    ms = multiscatter_v(h)
    pr, pm = phase_rayleigh(nu), phase_mie(nu)
    rho_r = np.exp(-h / RAYLEIGH_H)[:, None]
    rho_m = np.exp(-h / MIE_H)[:, None]
    sr = RAYLEIGH_SCATTERING[None, :] * rho_r
    sm = MIE_SCATTERING * rho_m
    st = sr + MIE_EXTINCTION * rho_m
    j = sr * (pr * ts + ms) + sm * (pm * ts + ms)
    seg_t = np.exp(-st * dt)
    T_before = np.vstack([np.ones((1, 3)), np.cumprod(seg_t, axis=0)[:-1]])
    S = (T_before * j / st * (1.0 - seg_t)).sum(axis=0)
    return S, np.prod(seg_t, axis=0)


def exp_integral(h0, a, t0, t1, H):
    """Integral of exp(-(h0 + a t) / H) dt over [t0, t1]."""
    L = t1 - t0
    x = a * L / H
    e0 = math.exp(-(h0 + a * t0) / H)
    if abs(x) < 1e-3:
        return L * e0 * (1.0 - 0.5 * x + x * x / 6.0)
    return e0 * H / a * (1.0 - math.exp(-x))


def aerial_analytic(h0, wz, length, k, nu, mu_s, segments=2):
    """AtmosphereAerial (GLSL): closed form optical depth, source at the transmittance midpoint, N segments."""
    a = wz / k
    T = np.ones(3)
    S = np.zeros(3)
    pr, pm = phase_rayleigh(nu), phase_mie(nu)
    for s in range(segments):
        t0 = length * s / segments
        t1 = length * (s + 1) / segments
        ir = exp_integral(h0, a, t0, t1, RAYLEIGH_H)
        im = exp_integral(h0, a, t0, t1, MIE_H)
        tau_r = RAYLEIGH_SCATTERING * ir
        tau_ms = MIE_SCATTERING * im
        tau = RAYLEIGH_SCATTERING * ir + MIE_EXTINCTION * im
        seg_t = np.exp(-tau)
        # where half of the segment's opacity is reached (green channel, homogeneous approximation)
        tg = tau[1]
        f = 0.5 if tg < 1e-4 else -math.log(0.5 * (1.0 + seg_t[1])) / tg
        h_eff = h0 + a * (t0 + f * (t1 - t0))
        ts, ms = sun_transmittance(h_eff, mu_s), multiscatter(h_eff)
        jr = pr * ts + ms
        jm = pm * ts + ms
        one_minus = np.where(tau > 1e-6, (1.0 - seg_t) / np.maximum(tau, 1e-30), 1.0 - 0.5 * tau)
        S += T * (tau_r * jr + tau_ms * jm) * one_minus
        T *= seg_t
    return S, T


# segments used by AtmosphereAerial (ATMOSPHERE_AERIAL_SEGMENTS in atmosphere_common.glsl)
SEGMENTS = 3


def check_aerial():
    print("aerial perspective: analytic vs reference ray march")
    print("relative error of S = max|S - Sref| / max(Sref), absolute error of T")
    worst = {1: 0.0, 2: 0.0, 3: 0.0, 4: 0.0}
    worst_t = {1: 0.0, 2: 0.0, 3: 0.0, 4: 0.0}
    rows = []
    # JA domain: physical path d * unitScale up to ~1.5 km (zFar 20000 units at 0.03 m/unit, plus
    # margin), aerial scale k up to 30: scaled length up to 45 km, altitude change up to 1.5 km
    for k in (1.0, 8.0, 30.0):
        for h0 in (0.0, 500.0, 2000.0):
            for wz in (-0.3, 0.0, 0.3, 1.0):
                for phys in (360.0, 1000.0, 1500.0):
                    length = phys * k
                    if wz < 0.0 and h0 + wz * phys < -200.0:
                        continue
                    for sun_deg in (2.0, 10.0, 45.0):
                        mu_s = math.sin(math.radians(sun_deg))
                        for nu in (0.99, 0.3, -0.5):
                            Sr, Tr = aerial_reference(h0, wz, length, k, nu, mu_s)
                            for n in (1, 2, 3, 4):
                                Sa, Ta = aerial_analytic(h0, wz, length, k, nu, mu_s, n)
                                es = float(np.max(np.abs(Sa - Sr)) / max(np.max(Sr), 1e-12))
                                et = float(np.max(np.abs(Ta - Tr)))
                                worst[n] = max(worst[n], es)
                                worst_t[n] = max(worst_t[n], et)
                                if n == SEGMENTS and es > 0.01:
                                    rows.append((k, h0, wz, length, sun_deg, nu, es))
    for n in (1, 2, 3, 4):
        print(f"  segments {n}: worst S error {100 * worst[n]:.3f} %, worst T error {worst_t[n]:.2e}")
    rows.sort(key=lambda r: -r[6])
    for r in rows[:12]:
        print("  >1%% (%d segments): k %g h0 %g wz %g L %g sun %g nu %g: %.2f %%" % (SEGMENTS, *r[:6], 100 * r[6]))
    print(f"  cases above 1 % with {SEGMENTS} segments: {len(rows)}")
    return worst[SEGMENTS] < 0.01 and worst_t[SEGMENTS] < 1e-3


def check_lut_params():
    print("LUT parameterization round trips")
    err_t = 0.0
    for x_mu in np.linspace(0.0, 1.0, 33):
        for x_r in np.linspace(0.0, 1.0, 17):
            r, mu = transmittance_rmu(x_mu, x_r)
            u, v = transmittance_uv(r, mu)
            err_t = max(err_t, abs(u - x_mu), abs(v - x_r))
    err_s = 0.0
    for r in (R_GROUND + 1.0, R_GROUND + 500.0, R_GROUND + 10e3):
        for u in np.linspace(0.0, 1.0, 33):
            for v in np.linspace(0.0, 1.0, 33):
                cz, cl = skyview_dir(u, v, r)
                u2, v2 = skyview_uv(cz, cl, r)
                err_s = max(err_s, abs(u2 - u), abs(v2 - v))
    print(f"  transmittance uv round trip: {err_t:.2e}")
    print(f"  sky-view uv round trip:      {err_s:.2e}")
    return err_t < 1e-6 and err_s < 1e-5


def check_sun():
    print("sun transmittance from the ground (r_atmosphereSunColor 1 divides the map sun by this)")
    for deg in (90.0, 45.0, 20.0, 10.0, 5.0, 2.0, 0.5):
        T = sun_transmittance_v(np.array([1.0]), math.sin(math.radians(deg)), steps=4000)[0]
        T40 = transmittance_to_top(R_GROUND + 1.0, math.sin(math.radians(deg)), steps=40)
        print(f"  sun {deg:5.1f} deg: T = {T[0]:.4f} {T[1]:.4f} {T[2]:.4f}   (40 steps: max err {np.max(np.abs(T40 - T)):.1e})")
    return True


def main():
    ok = check_lut_params()
    ok = check_sun() and ok
    ok = check_aerial() and ok
    print("OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
