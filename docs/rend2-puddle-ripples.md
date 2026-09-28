# rend2 puddle rain ripples (`r_weatherPuddleRipples`)

Small expanding rings on the standing water of `r_weatherPuddles`. This is a shading feature only. The rings tilt the
water normal and change nothing else: no color overlay, no emission, no extra pass, no texture and no CPU ring
state. There are no real raindrop impact events. The rings are procedural.

## Where it runs

`lightall.glsl`, in the `USE_WETNESS` block, directly after the puddle flattening
(`N = normalize(mix(N, wetGeoNormal, flatten))`). From there `N` is the only normal that the following consumers read:

| consumer | reads |
|---|---|
| sun / primary light, light-vector direct specular | `N` (NL, NH) |
| Forward+ / legacy dynamic lights | `CalcDynamicLightContribution(…, N, …)` |
| cubemap IBL | `CalcIBLContribution(…, N, …)` → `reflect(-E, N)` |
| SSR | `out_SSRNormal = SSREncodeNormal(N)` (the rings also enter the cubemap term `C` that SSR replaces) |
| SSGI receiver | `SSGIWriteReceiver(N, …)` |

So SSR needs no separate path. The rings distort the reflected scene, the cubemap and the highlights in the same way.

## Ring function

`RippleRing(p, center, phase, amp, maxRadius, width)` returns `(height, dh/dp)`. It accepts any unit, as long as
`p`, `center`, `maxRadius` and `width` use the same one:

```
x      = (|p - center| - phase * maxRadius) / width     // -1..1 inside the wave packet
w      = max(1 - x^2, 0)
life   = min(16 phase, 1) * (1 - phase)^2                // quick onset, fades while it grows
height = amp * life * width * x * w^2                    // leading crest, trailing trough (no trig)
dh/dd  = amp * life * w * (1 - 5 x^2)                    // peak slope = amp at the front
grad   = dh/dd * (p - center) / |p - center|
```

The packet has compact support, so a ring is one narrow front that fades as it grows. It never becomes a repeating
hard circle.

## World-space seeding

`RippleLayer` is one rotated, jittered cell grid in **world XY**. It uses no UVs and no screen space, so the rings
don't depend on the material's texture scale or on the camera. `PuddleRipples` adds up 3 layers:

| layer | rotation | scale | seed | clock offset |
|---|---|---|---|---|
| 0 | 0° | 1.00 | 0.0 | 0 |
| 1 | 37° | 0.79 | 3.3 | 4/3 |
| 2 | 71° | 1.27 | 7.7 | 8/3 |

Per layer, in cell units (`toCell = scale / r_weatherPuddleRippleScale`):

```
clock  = T + clockOffset                     T: CPU ring clock in cycles, mod 256
epoch  = floor(clock / 4)                    the grid moves every 4 cycles
q      = rot * worldXY * toCell + seed + fract(mod(epoch, 64) * R2)
cell   = floor(q);  h = hash3(cell + seed)
cycle  = clock + h.z                         per cell phase offset
r      = fract(h + mod(floor(cycle), 256) * (0.7549, 0.5698, 0.6180))   // per cell R2 sequence
center = cell + 0.5 + (r.xy - 0.5) * 0.36    new center for every ring
amp    = (r.z < density) * (0.6 + 0.4 r.x) * [ring born and dying inside this epoch]
ring   = RippleRing(q, center, fract(cycle), amp, 0.3, 0.07)
```

* **One ring per cell and layer.** The center jitter (±0.18), radius (0.3) and half packet (0.07) keep a ring
  inside its cell except in the last ~17 % of its life, when its amplitude is below 3 %. The harness measured a
  maximum slope of 1.5 % of the peak at cell borders. No neighbour search is needed.
* **Epoch shift.** A fixed jitter box would make rings start from the same few spots in a long static shot. So each
  layer's grid moves every 4 cycles. A ring that would live across the shift is skipped (1 cycle in 4 per cell), so
  no ring is ever cut. A layer is empty right at its shift, so the layers' clocks are staggered by 4/3 cycles. The
  total ring activity then stays within about ±15 % of its mean.
* **Wrap.** The CPU clock wraps at 256 cycles, which is 64 epochs. The ring index and epoch use the same modulus,
  so the wrap is seamless. The harness measured no jump in slope across it.

## Normal perturbation

```
ripple      = PuddleRipples(worldXY, footprint)        // (height, dh/dx, dh/dy), world units
rippleMask  = smoothstep(0.35, 0.9, puddle)
rippleSlope = ripple.yz * r_weatherPuddleRippleStrength * rippleMask
tilt        = (-rippleSlope, 0)
N           = normalize(N + tilt - wetGeoNormal * dot(wetGeoNormal, tilt))
```

This is the height-field normal `(-hx, -hy, 1)` on horizontal water. The projection keeps the tilt in the water plane
on the slightly inclined surfaces that the puddle slope gate still accepts.

## Masking

`puddle` is the submerged core of the existing mask. It already includes rain exposure (no rain under roofs), the
slope gate, world-only eligibility (no entities) and the height-aware micro mask. The ripple envelope is
`smoothstep(0.35, 0.9, puddle)`. As a result:

* surfaces that are only wet (`puddle` = 0) and the fringe film (`puddleEdge`) get no rings;
* the rings fade out inside the puddle before its edge, so they never cross onto dry stone;
* without rain, `RB_WeatherWetnessBind` sets `u_PuddleParams` to zero, so no puddles and no rings;
* the shader skips the branch entirely when `puddle <= 0.35`.

## Anti-aliasing

`rippleFootprint = length(fwidth(u_ViewOrigin - var_ViewDir.xyz).xy)` is computed in uniform control flow before the
wetness block, on the surface without POM displacement. Each layer fades out when its ring (`2 × width` cells) gets
narrower than about 1.4 pixels: `amp *= clamp(1.43 - footprint * toCell * 10, 0, 1)`. At scale 20 the rings are at
full strength up to 0.9 world units per pixel and gone at 2.8. Far and grazing puddles fall back to the flat mirror
instead of shimmering or showing Moiré.

## Future splash events

`RippleRing` takes a world-space center, a phase (from the spawn time) and an amplitude, so real impact centers from a
splash feature can call it directly. `PuddleRipples` is the single place to add `EventRipples(worldXY)`. Procedural
mode can then stay as a cheap fallback, be mixed with the events, or be switched off through `u_PuddleRipple`.
There is no impact buffer yet.

## CPU (`tr_weather.cpp`, `RB_WeatherWetnessBind`)

`u_PuddleRipple` = (strength, 1 / cell size, clock, density):

* strength is 0 when `r_weatherPuddleRipples` is 0 or when the draw gets no puddles (coverage ≤ 0);
* the clock is integrated once per frame: `clock += min(dt, 0.1) * r_weatherPuddleRippleRate`, then `fmod 256`. Changing
  the rate never makes the phase jump, and a paused game (`dt` = 0) freezes the rings;
* density = `clamp(rain particleCount / 2000, 0.3, 1)`: light rain (1000 particles) rings half the cells that a
  downpour does. The epoch skip then removes 1 cycle in 4.

## Cvars

| cvar | default | meaning |
|---|---|---|
| `r_weatherPuddleRipples` | 1 | on / off (visible only with `r_weatherWetness 1`, `r_weatherPuddles 1` and rain) |
| `r_weatherPuddleRippleStrength` | 0.35 | peak slope of a ring (0..2) |
| `r_weatherPuddleRippleScale` | 20 | world size of a cell; a ring grows to a radius of 0.3 × this (6 units) |
| `r_weatherPuddleRippleRate` | 1.1 | ring cycles per second per cell |

All four are runtime cvars (not latched).

## Debug (`r_weatherSurfaceDebug`)

| value | view |
|---|---|
| 17 | ripple height field, not masked (mid grey = flat, ±0.5 = peak), tinted blue on puddles |
| 18 | slope applied to N (red = x, green = y, 0.5 = none) |
| 19 | masking: puddle blue, applied rings yellow, rings suppressed by the mask dark red. Yellow must stay inside the blue |
| 20 | final effective normal (`N * 0.5 + 0.5`) |

These views bypass tone mapping (`RB_AODebugBypassesToneMap`, range extended to 20).

## Cost

NVIDIA program assembly (`glsldump_nv`, lightall `USE_WETNESS;USE_SSR;USE_LIGHT_VECTOR;USE_NORMALMAP;USE_SPECULARMAP;
USE_CUBEMAP`), fragment instructions:

| build | instructions |
|---|---|
| without ripples | 1080 |
| call site + debug views, rings stubbed | 1158 (+78) |
| full | 1405 (+325: 247 for the 3 ring layers, 13 transcendentals) |

All of this sits behind `u_PuddleRipple.x > 0 && puddle > 0.35`. Pixels outside the puddle core, dry maps and
`r_weatherPuddleRipples 0` pay only for the branch and one `fwidth`. As a rough estimate, puddle core covering a whole
1080p screen costs ~0.5 G instructions per frame: about 0.15 ms on an RTX 2060, and a few ms on Intel UHD. The
feature has not been timed in the game.

## Validation status

* The GLSL compiles and links on Intel UHD and on the NVIDIA RTX 2060 (36 lightall variants plus 4 silhouette-POM
  variants). The MSVC build of `rd-rend2_x86_64` and `rdsp-rend2_x86_64` succeeds.
* A C++ port of the ring code (scratch harness) was checked. The analytic gradient matches finite differences to
  within 5 %, the slope at cell borders stays below 1.5 % of the peak, the clock wrap and epoch changes are
  seamless, the measured density matches the target, and a long exposure over 29 cycles shows no grid (coefficient
  of variation 0.21).
* **Not tested in the game.** Still to check in the game: cubemap and SSR reflections, dynamic light and sun / moon
  highlights, puddle edges (debug 19), camera movement, a long static shot (flicker, grid) and GPU time with
  `r_weatherPuddleRipples 0` vs `1`.
