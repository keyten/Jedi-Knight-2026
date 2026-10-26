# rend2 underwater suspended particles and micro-bubbles (`r_waterParticles`)

A cheap depth cue under water: a few dozen to a few hundred tiny specks and small bubbles in a sphere around the
camera, only while the camera is inside a water (or slime) brush. They are small lit billboards, not media. No
particle becomes a froxel fog blob, and the froxel volume is not touched. The medium between the camera and a
particle fogs it like any other transparent draw. Far specks fade into the water colour, near ones read as
specks, and that gradient is the depth cue.

Off by default. With `r_waterParticles 0` nothing is simulated, no draw surface is added, and the frame is
unchanged. The program is always compiled, but it is small.

Files:

- `shared/rd-rend2/tr_waterparticles.cpp`: bodies, profiles, simulation, lighting, draw.
- `shared/rd-rend2/glsl/waterparticles.glsl`: point → camera-facing quad (geometry stage), speck and bubble
  coverage, froxel fog in front.
- Hooks:
  - `SF_WATER_PARTICLES` and `RB_SurfaceWaterParticles` in `tr_surface.cpp`.
  - The `<water particles>` internal shader in `tr_shader.cpp`.
  - `R_AddWaterParticleSurfaces` in `tr_main.cpp`.
  - `R_WaterParticlesWorldLoaded` in `tr_bsp.cpp`.
  - The program and `u_WaterParticles` in `tr_glsl.cpp`.
  - The cvars and the shutdown in both `tr_init.cpp`.
- Tools:
  - `tools/rend2/test_waterparticles_gl.py`: GPU checks.
  - `tools/rend2/water_particles_audit.py`: stock bodies and expected counts.
  - `docs/samples/water-particles.cfg`: test binds.

## What was there before (the study)

| Area | Finding | Used how |
|---|---|---|
| Regular particle rendering | FX sprites are `RT_SPRITE` / `RT_ORIENTED_QUAD` refEntities drawn through `generic.glsl`, one entity each. Polys share `r_maxpolys` with cgame marks. | Not used. Hundreds of entities per frame would eat the cgame entity budget. A single points draw with its own small program costs one draw item. |
| Soft particles | rend2 has none. No program fades a sprite by scene depth, and a depth copy only exists for `r_waterSurface`. | Not added (see Known limits). The specks are tiny (0.1–0.3 units), so wall intersections are sub-pixel. |
| Light grid / particle lighting | `R_LightForPoint` (the entity lighting grid, CPU). The merged grid 3D texture (rain streaks). `r_particleLight`, a froxel light field for lit FX sprites. Its gain is relative to the map average (`particleLightReference`). | CPU `R_LightForPoint` per particle, refreshed every 8 frames, with the same `/255` and sRGB decode as the entity lighting. The field is not used: it is latched and optional, and it is relative, while the specks need absolute light to sit correctly against the medium's in-scattering. |
| Camera `R_LiquidPointClass` | Collision contents at a point → `LIQUID_*`. | Gate: the camera must be in water or slime. Lava gets no particles. |
| Liquid brush point tests | `world_t::liquids` holds every kept convex liquid brush of model 0, loaded for every map whatever `r_volumetricWater` is. `R_LiquidPointBrush` is gated by the medium's class mask. | Own tests over the same planes, independent of `r_volumetricWater`: the camera's brush, then body membership of every candidate and of every particle each frame. |
| Dynamic lights | `refdef->dlights` (point, spot cone, area lights). lightall attenuation is `clamp(0.5 r²/d² − 0.5, 0, 1)`. The froxel path absorbs light through water (`LiquidLocalLightAbsorption`). | CPU sum per particle: the same attenuation, the spot cone, `exp(−σ d)` through the body's medium, and the particle phase. It is cheap: the lights are pre-culled against the camera sphere. Area lights are skipped. |

## Placement: camera volume and water body

- **Body.** At map load, the liquid brushes of one class whose bounds overlap or touch (1 unit tolerance) are
  grouped into one body (union-find, `R_WaterParticlesWorldLoaded`). Two pools are two bodies. A pool split into
  several brushes is one body.
- **Camera body.** The camera must be in water or slime by collision contents, and inside a kept liquid brush.
  Its body is the active body. A change of body, or a jump farther than the radius (a teleport), clears the
  particles. The refill fades in.
- **Candidates.** A candidate is drawn uniformly in the sphere of radius `r_waterParticlesDistance` around the
  camera. It is kept only if both of these hold:
  - it is inside a brush of the active body, at least 1 unit from its sides. This rules out "above water" and
    "another pool".
  - a collision trace from the camera against `CONTENTS_SOLID` reaches it. This rules out "behind a wall" and
    "inside a solid". The trace is `CM_BoxTrace` in MP and `SV_Trace` in SP, the same split as
    the independent sun helper in `tr_light.cpp`.
- **Spawn budget.** Each frame allows 48 + target/8 candidates and 24 + target/16 traces. A fresh fill takes
  1–10 frames (see the audit). The first generation of a fill gets random lifetimes, so it does not all expire
  at once.
- **Every frame.** A particle whose next position would leave the body stays where it is and fades out. A bubble
  at the surface pops in 0.12 s. A speck at a wall or floor dissolves in 0.8 s. So nothing crosses the surface or
  a brush side.
- **Drifting into solids.** 32 live particles per frame (round robin) get a `CM_PointContents` test, and those
  inside a solid fade out.
- **Leaving the sphere.** A particle that leaves the sphere (the camera moved) is moved to the mirrored point on
  the far side, at 0.98 R, inside the far fade. It is kept only if that point passes the same body and sight
  tests. Otherwise it dies and respawns later. The density follows the camera without a visible edge.

## Body profile

| Profile | Density (per 10⁶ u³) | Target at R 160 | Bubble share | Speck opacity / radius | Speck albedo |
|---|---|---|---|---|---|
| none | 0 | 0 | – | – | – |
| **clean** (default) | 3 | 51 | 12 % | 0.30 / 0.10 | pale grey-green |
| murky | 12 | 206 | 6 % | 0.40 / 0.13 | olive |
| dirty | 32 | 549 | 4 % | 0.50 / 0.16 | brown silt |

**Every body is clean unless the map says otherwise.** There is no inference from the map name, the shader
name or the outdoors. A map opts in through its `env.json` (`cubemaps/<map>/env.json`, the same file and rule
form as `"Liquids"`):

```json
"WaterParticles": [
  { "Shader": "textures/yavin/swamp_water*", "Profile": "dirty" },
  { "Shader": "textures/t2_port/harbor_water", "Profile": "murky" },
  { "Shader": "textures/vjun/*", "Profile": "none" }
]
```

A rule matches the BSP shader of a brush of the body (its contents shader) or the shader of that brush's upward
side (the drawn surface). A trailing `*` makes it a prefix. When different brushes of one body match different
rules, the earliest rule in the file wins. `r_waterParticlesProfile 1/2/3` forces a profile for testing, and `-1`
forces none. The stock maps have no rules, so every stock body is clean.

## Motion

There is no fluid solver. Velocities are analytic in time:

- **Current.** One slow current per frame: 0.9 u/s × `r_waterParticlesSpeed`, mostly horizontal. Its heading
  wanders over minutes.
- **Specks.** Current, plus a per-particle wander: three sines at 0.08–0.25 Hz with random phases, ±0.7 u/s.
  Each speck also has near-neutral buoyancy, from −0.35 (settles) to +0.15 u/s, with a small vertical wobble.
  Lifetime is 10–24 s, with a 0.6 s fade-in and a 1.5 s fade-out.
- **Micro-bubbles.** They rise at 3–12 u/s; larger bubbles rise faster. They zigzag across at 1.2–2.6 Hz and
  take half the current. Their radius is up to 0.3 u, and small bubbles are more common (the radius is drawn
  from s²). Lifetime is 6–12 s, unless they reach the surface first and pop.

## Lighting and look

The particle colour is a **reflectance times the light at the particle**. Nothing emits. Against the medium's
in-scattering (≈ albedo × the same light), a speck shows by contrast. A speck in a lit patch or a sun shaft is
lighter than the water around it. One in shadow is darker, and it shows when seen against a brighter background.

- **Light.**
  - Light grid ambient + directed × phase.
  - The phase for a speck is a diffuse sphere seen from the camera, `((π−θ)cosθ + sinθ)/π`, plus a forward
    lobe `0.8 cos⁸`.
  - The phase for a bubble is `0.35 + 1.6 cos⁸`. Bubbles glint when the camera looks toward the light.
  - Dynamic lights are added as described in the table above.
- **Speck.** A procedural Gaussian `exp(−3r²)(1−r²)` that reaches zero at the quad edge. The stock alternatives
  were checked:
  - `gfx/misc/dotfill_a` is a cluster of dots.
  - `darkpuff` and `snowpuff` are cloudy blobs.

  None is a single soft speck, so no asset was added.
- **Bubble.** The stock `gfx/effects/bubble.tga` (64², bright rim, clear middle). Its alpha is the coverage and
  its brightness shades the rim. Without that file (a stripped install), the shader draws a procedural ring
  (`u_WaterParticles[1].w = 0`). No new asset is needed.
- **Blend.** Premultiplied over (`ONE, ONE_MINUS_SRC_ALPHA`), with no glow and no depth write.
- **Sub-pixel specks.** These are drawn at a minimum radius of 0.75 px, with their opacity scaled by
  (r / r_min)². The coverage of the true area is kept, so far specks thin out instead of sparkling.
- **Fades.**
  - Near: smoothstep from 2 to 10 u, so there are no lens-sized blobs.
  - Far: over the last 20 % of the radius.
- **Medium in front.** The froxel lookup of the transparent layers (`RB_VolumetricFogMode`):
  - mode 1: `c·a·T + S·a`, the algebra of generic's premultiplied liquid blends.
  - mode 3 with RGB extinction.
  - Without the froxel volume (`r_volumetricWater 0`) the particles are unfogged, and cgame's legacy
    underwater tint goes over them.

### Draw order

There is one `GL_POINTS` draw item per frame. The CPU writes `{xyzw, rgba}` per particle, where `w` is the
radius and is negative for a bubble. The data is orphaned into a 64 KB stream buffer. The item sorts last among
the transparent layers (layer 15, stage 15).

From below, a legacy blended water surface (`SS_BLEND0`) is behind every particle. With the `SS_UNDERWATER`
layer it would be drawn after the particles and cover them across the screen. The price of sorting last: the
specks also draw over blended effects that are in front of them. These are faint and small. The modern water
surface (`r_waterSurface`) has its own slot before the composites and is not affected.

Only the main view of the world scene (`VPT_MAIN`, scene 0) gets particles. Portals, mirrors, shadow views, the
depth prepass and the refraction fill do not.

## Cvars

| Cvar | Default | |
|---|---|---|
| `r_waterParticles` | 0 | Master switch (archived, runtime). |
| `r_waterParticlesDensity` | 1 | Scale of the profile density (0–8). |
| `r_waterParticlesSize` | 1 | Size scale (0.25–4). |
| `r_waterParticlesSpeed` | 1 | Drift and rise speed scale (0–4). |
| `r_waterParticlesBubbles` | −1 | Share of micro-bubbles, 0–1. −1 uses the profile's share. |
| `r_waterParticlesDistance` | 160 | Radius of the camera volume (32–512 u). |
| `r_waterParticlesProfile` | 0 | 0 auto (env.json, else clean), 1 clean, 2 murky, 3 dirty, −1 none. Not archived. |
| `r_waterParticlesDebug` | 0 | Cheat. 1 placement (opaque: cyan specks, yellow bubbles, no fog). 2 no medium in front. 3 prints body, profile and its source, alive/target, bubbles, spawned, rejected (body / sight), killed (body / solid) and CPU ms every second. |

## Measured offline (no game launch)

- **Build.** MSVC Release `rd-rend2_x86_64` and `rdsp-rend2_x86_64` build clean.
- **GPU tests.** `python tools/rend2/test_waterparticles_gl.py` passes on Intel UHD (27.20.100.8280) and on
  RTX 2060 (616.92, `SHIM_MCCOMPAT=0x800000001` from PowerShell): 3 programs, 18 raster checks, worst pixel
  error 8e-4 (RGBA16F).
  - **Programs.** The program is compiled as the renderer does, with and without the froxel library and with
    RGB extinction.
  - **Raster checks.** Every pixel matches the CPU reference for:
    - specks, bubbles (texture and procedural ring), near fade, far fade, the outside of the volume.
    - the sub-pixel clamp: twice the radius gives 4× the opacity at the same footprint.
    - froxel fog mode 1, mode 0, debug 2, debug 1, and an untouched glow target.
  - **Found by the test.** NVIDIA rejects a draw when two sampler types share a unit. The RGB build declares
    `u_FroxelTransmittance` even when it is unused. The renderer already gives each froxel sampler its own unit
    (`GLSL_SetFroxelLookupUnits`), so the fix was only in the test.
- **Stock audit.** `python tools/rend2/water_particles_audit.py` covers 14 maps with liquids and 47 bodies. The
  camera sits in the largest brush of each body, and the radius is 160.

| Map / body | In body | In sight | Clean / murky / dirty alive (fill frames) |
|---|---|---|---|
| t3_hevil main pool (9 brushes, 368 deep) | 100 % | 96 % | 49 / 197 / 526 (2 / 6 / 10) |
| vjun1 green water (5 brushes) | 100 % | 99 % | 51 / 204 / 544 |
| yavin2 (496 deep) | 100 % | 63 % | 32 / 130 / 347 |
| yavin1b gorge (208 deep) | 81 % | 64 % | 33 / 132 / 352 |
| t2_port harbour basins | 7–26 % | same | 3–13 / 14–53 / 36–142 |
| shallow troughs (≤ 32 deep) | ≤ 14 % | | 0–5 clean |

Clean is "barely there" by design: a few specks in view. Shallow water holds few particles because the sphere
is mostly outside the body.

## Known limits

- **Water outside `world_t::liquids` gets nothing.** That covers water+fog brushes (BSP fog media, which keep
  the legacy fog and tint; taspir2), brush-model water (vjun2's 16 u sheet), and brushes skipped for shape or
  capacity. `r_waterParticlesDebug 3` prints why.
- **No soft particles.** rend2 has no scene-depth fade for sprites. A bubble of up to 0.3 u touching a wall is
  cut sharply. At these sizes this is a pixel or two.
- **No sun shafts on the specks.** The light grid is baked, and the sun is in it, without CSM shadows. The
  froxel particle light field (`r_particleLight`) would add the shadowed sun and the shafts, but it is relative
  to the map average. Using it would need an absolute scale. That is a possible follow-up.
- **Sorting.** The particles are drawn after all blended layers (see Draw order).
- **The SP trace** (`SV_Trace`) also hits solid entities, such as closed doors. MP traces the world only.
- **Camera exit.** Leaving the water kills the particles at once. The camera crossing the surface is a big
  change anyway, and nothing is drawn from above.

## Validation checklist (in game)

Deploy `rd-rend2_x86_64.dll` and `rdsp-rend2_x86_64.dll` only. cgame and the engine are untouched. Use
`devmap`, then `exec water-particles.cfg` (copy it from `docs/samples/`).

1. **t3_hevil pool**, `r_volumetricWater 1`.
   - Specks are sparse and pale.
   - Distant ones melt into the water colour.
   - None are above the surface, and none show through the pool walls (`r_waterParticlesDebug 1` makes the
     placement obvious).
2. **Look up at the surface.** Bubbles rise and vanish at the surface, and the specks are not covered by the
   water surface.
3. **Swim fast across the pool.** The density stays even, and nothing pops in at the edge.
4. **Profiles** (F6). murky and dirty get progressively denser and browner. clean should read as clean water,
   not as a snow globe.
5. **Saber or dynamic light under water.** Specks near it light up, and the light falls off through the water.
6. **`r_volumetricWater 0`.** The specks are unfogged under cgame's tint. Check that nothing looks broken.
7. **taspir2 / vjun2.** Debug 3 reports no kept brush, and nothing is drawn.
8. **`r_waterParticlesDebug 3`.** Note the CPU time. It has not been measured; the expectation is well under
   0.1 ms at clean and under 0.5 ms at dirty.
