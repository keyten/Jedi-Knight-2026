# rend2 foliage wind (`r_foliageWind`)

A light, spatially coherent breeze for surface sprite vegetation. `r_foliageWind 0` (the default) keeps the stock
formula. No physics, bend field, collision or tree deformation.

## Legacy sway (`r_foliageWind 0`, unchanged)

`surface_sprites.glsl`, `CalculateVertexOffset`, for every sprite that is not `FX_SPRITE` (effects / weather) or
`FACE_UP`:

```
isLower   = offset.z == 0                          // vertices 0 and 3 stay on the anchor
offset.xy += isLower ? 0 : skew                    // attr_Position2.zw (ssVertSkew)
angle     = (pos.x + pos.y) * 0.02 + t_ms * 0.0015 // t_ms = u_frameTime * 1000
offset.xy += isLower ? 0 : vec2(cos(angle), sin(angle)) * height * u_WindIdle * 0.075
```

The tip moves in a circle with a radius of 7.5 % of the height × windIdle and a period of about 4.2 s. The phase
changes only along x+y (stripes 314 units wide), so neighbouring blades circle in step. `attr_Position.w` is not
used, and `u_Wind` is uploaded but never read. `ssWind v` sets `wind`, and also sets `windIdle` when it is unset.
`ssWindIdle` sets only windIdle. Both default to 0 (no sway).

## Coherent breeze (`r_foliageWind 1`)

`FoliageWind(anchor.xy, seed = attr_Position.w, t, height)` is a pure function of the anchor, the stable seed,
time and the cvars. It has no camera or instance input, so:

* all cards of an `r_grassCardMode` tuft move as one;
* the sun cascades match the main view;
* the velocity pass evaluates it again at `u_previousFrameTime` and gets exact motion vectors.

```
d, s   = wind direction, perpendicular             tw = t * r_foliageWindSpeed
gustL  = valueNoise(p / 512 - d * tw * 0.12)        // patchy ~512u swells, ~60 u/s downwind
gustS  = sin(dot(p,d)*0.035 - tw*2.45 + 1.3*sin(dot(p,s)*0.013 + tw*0.21))  // ~180u fronts, ~70 u/s, bent crests
gust   = max(0.25 + 0.75*gustL + 0.18*gustS, 0)
ripple = sin(tw*3.0 - dot(p,d)*0.05 + seed*1.5)     // ~125u ripple, ~0.5 Hz, per blade phase offset
lean   = gust * (0.6 + 0.3*ripple)                  // always downwind: no orbiting
flutter= 0.2 * gust * sin(tw*(4.5 + 2*seed) + seed*6.2832)   // faint cross wind, per blade rate
disp   = (d*lean + s*flutter) * |h| * 0.12 * r_foliageWindStrength * u_WindIdle
|disp| <= 0.5|h|; tip z -= |disp|^2 / (2|h|)        // bend, not stretch (sign aware for ssHangdown)
```

* Only the upper vertices move, with the same `isLower` test as the legacy code. The lower vertices stay on the anchor.
* The value noise uses an integer lattice hash wrapped at 1024 cells. It stays continuous and precise at large
  world coordinates.
* A material with `u_WindIdle == 0` takes a uniform early out and does not move.

At defaults, Yavin grass (`ssWind 0.5`, height 24–36) moves its tip by 2.4 % of the height on average, 4.5 % at p95
and 6.4 % at most. The mean tip speed is about 1.3 u/s. Legacy moves in a 3.7 % circle at 1.8 u/s.

## Cvars

| cvar | default | |
|---|---|---|
| `r_foliageWind` | 0 | 0 = legacy circular sway, 1 = coherent breeze |
| `r_foliageWindStrength` | 1 | 0–4, multiplied by the material's ssWind / ssWindIdle |
| `r_foliageWindSpeed` | 1 | 0–4, time scale of gusts and sway |
| `r_foliageWindDirection` | 30 | yaw in degrees, 0 = +X |
| `r_foliageWindDebug` (cheat) | 0 | 1 = ×4 strength, 2 = colour by bend (red lean, green flutter, blue calm), 3 = freeze time, 4 = large gust wave in greyscale |

The debug colours apply only to colour passes. Freeze (mode 3) uses the same frozen time in both velocity frames,
so any remaining motion comes from the camera.

## Cost

The numbers below count instructions in the NVIDIA vertex program assembly (RTX 2060, `glGetProgramBinary`). They
are static counts, not a timing. The compiler hoists the wind evaluation out of the per-corner code, so it runs once
per vertex invocation.

| permutation | HEAD | legacy branch only | breeze only | shipped (both paths behind a uniform branch) |
|---|---|---|---|---|
| vertical | 86 | 100 | 236 | 252 |
| + velocity | 116 | 128 | 406 | 439 |
| auto grass + velocity | 172 | 186 | 471 | 505 |

One evaluation of the breeze costs about 135 instructions (4 integer hashes, 4 `sin`). Legacy costs about 5. Of
the +14 instructions in the legacy path, most come from the debug colour code; the rest is the branch. On the
default (legacy) setting only the branch executes.

## Validation status

Done:

* MSVC SP and MP Release builds.
* Offline compile and link on Intel UHD and RTX 2060 for 13 permutations.
* A CPU mirror of `FoliageWind` checked that the function is deterministic and that freeze gives zero delta.
* Continuity across noise cells and the 1024-cell wrap: the jump is below 1e-4 units.
* Displacement along the wind is 4.7× the cross-wind displacement.
* Float32 phase ulp is 4e-3 rad at a level time of 1e4 s.

**Not run in game.** In-game checklist (stock Yavin):

1. Stationary camera for 10 s: gust patches drift downwind, and no blade orbits.
2. `r_foliageWindDebug 3` while rotating and moving the camera: the grass stays still.
3. Moving camera, dense grass, and `r_grassCardMode 1/2`: the cards of a tuft stay together.
4. `r_motionBlurDebug` or the velocity view, and TAA / SMAA 2: no smearing or ghosting on still grass.
5. `r_volumetricFog 1/2`, fogged areas, and sun shadows: the shadows follow the blades.
6. A shader with ssWind 0 does not move. High ssWind is clamped to half the height. FX / fog sprites are unchanged.
