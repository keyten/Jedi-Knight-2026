# Optional body-local water optics

`r_waterBodyOptics 1` enables local reflection coverage and a depth-dependent
participating medium for ordinary water surfaces. It defaults to **0**, is
latched, and requires `vid_restart` after a change. Surface foam, waterfall
columns, wave geometry, and the physical Fresnel coefficient retain their
existing controls.

## Reference and model

The visual targets are the aerated receiving water, its transition to a darker
reflective pool, and the clearer shallow margins in these references:

- [White Drin, aerial photograph](https://testela.travel/tour-item/drin-waterfall-zip-line/)
- [Niagara Falls, aerial photograph](https://www.niagarafallstourism.com/blog/how-to-experience-the-wonder-of-niagara-falls-from-home/)
- [H. Chanson, Advective Diffusion of Air Bubbles in Turbulent Water Flows](https://staff.civil.uq.edu.au/h.chanson/reprints/chapter7_published.pdf)
- [Epic, Single Layer Water](https://dev.epicgames.com/documentation/en-us/unreal-engine/single-layer-water-shading-model-in-unreal-engine)

This is a bounded single-layer rendering approximation. The existing advected
bulk-air and surface-foam concentrations are optical proxies, not measured
void fractions. The added medium is evaluated over the actual refracted liquid
path, so its contribution vanishes at zero depth. It uses Beer-Lambert
transmission and lit in-scattering. Background suspension varies conservatively
between pools, streams, and receiving pools; bubbles add nearly neutral
scattering near impacts and disperse with the existing field. The former flat
whitewater colour blend is replaced by this integration when the flag is on.

The receiving-light cache used by tall spillways also lights the bubble plume,
including the platform shadow on `t3_hevil`. Elsewhere the local environment and
available shadowed sun provide its illumination. There is no emissive floor
and no multiplier on physical Fresnel reflection.

## Local reflections

With `r_cubeMapping 1`, up to eight of the largest eligible world-water bodies
receive local air-side probes. Placement samples actual interface triangles,
checks the BSP leaf and a short solid trace, and prefers an unobstructed point
48 units above the interface. A lower point is allowed below a low ceiling.
Invalid placements are omitted rather than capturing from solid space.

These probes are marked water-only and excluded from generic cubemap selection.
They are captured and convolved once at map load, without diffuse-IBL bounces.
The ordinary water draw uses its body's probe; waterfalls and opaque materials
keep their previous selection. Existing SSR continues to supply visible local
geometry, with the probe covering off-screen directions. The normal cubemap
size and the renderer's 128-probe limit still apply.

## Cost, controls, and limits

- No new per-frame pass, simulation, field texture, or per-pixel texture lookup.
  The medium reuses the existing foam-field sample and adds bounded arithmetic.
- Up to eight extra cubemaps and their map-load capture/convolution cost when
  the flag and cubemaps are enabled. No such probes are created with the flag off.
- `r_waterSurfaceSSR`, `r_cubeMapping`, `r_waterSurfaceReflection`, and the
  original liquid coefficients remain independent controls.
- This addition shades the air-side surface view. The underwater froxel medium
  continues to use the existing `r_volumetricWater*` coefficients. It is not a
  full 3D distribution of sediment or bubbles.
- Static probes do not reproduce changing smoke or sun shafts. Existing SSR
  reflected-fog capabilities and screen-space limitations still apply.

Example:

```text
set r_waterBodyOptics 1
vid_restart
```

`r_waterSurfaceDebug 8` shows final transmission including the added medium;
`7` shows the reflection source. Map-load messages list each generated body's
probe, location and radius. The GPU transport check is
`python tools/rend2/test_water_body_optics_gl.py`; full shader/tessellation checks
remain in `test_water_tessellation_gl.py`.

## Validation (2026-11-16)

Release SP and MP builds pass. The GL 4.3 transport checks cover disabled
transport, shallow/deep separation, advected air, lit bubble scattering,
receiving-pool shadow and unchanged Fresnel. The eleven tessellated permutations,
crack/shore tests and baked-light interpolation checks pass, as do the CPU
impact, intersection-foam, whitewater, flow and interaction checks. Actual-map
captures compare both flag states on the **lower receiving pool** of `t3_hevil`
(body 10, z=-1984), the southern `yavin1b` impact and a northern river section.

An interleaved synthetic draw benchmark is available with
`python tools/rend2/test_water_body_optics_gl.py --bench`. On RTX 2060 at
1920x1080, forty batches of 32 draws per mode measured **2.7224 ms off /
2.7657 ms on**, +0.0433 ms (+1.59%). This isolates medium arithmetic; it excludes
SSR, simulation, map-load capture and probe-memory cost.
An independent repeat measured 2.5513 / 2.5932 ms, +0.0419 ms (+1.64%).

Actual-map 1024x768 water-pass medians, 400 reports per run, order off/on/on/off:

| Scene | Off, runs A/D (ms) | On, runs B/C (ms) |
| --- | --- | --- |
| t3_hevil receiving-pool west | 3.510 / 10.872 | 3.788 / 4.651 |
| yavin1b northern river | 1.726 / 1.800 | 1.765 / 2.125 |

The t3_hevil repeat also slows unchanged passes considerably. These are noisy
whole-pass observations, not a reliable estimate of the feature's frame cost.
