# Dedicated stock-waterfall renderer

## Installed-asset audit

The machine's real `build-rend2/base` installation was scanned, including all
shader/MTR definitions and every BSP in every installed PK3. The reproducible
machine-readable result is
[`rend2-waterfall-stock-audit.json`](rend2-waterfall-stock-audit.json); rerun it
with:

```text
python tools/rend2/waterfall_audit.py <game-base> <output.json>
```

The only stock shader used by a BSP as a waterfall sheet is exactly
`textures/h_evil/wfall`. Eleven surfaces in three maps use it:

| Map | BSP surface | Geometry | BSP control mesh | Bounds (min -> max) | Width x fall | Vertices / 1000 area | Nearby waterfall EFX |
|---|---:|---|---|---|---:|---:|---:|
| `t3_hevil` | 317 | patch, vertical | 3x5 | `(-800,-1096,-2048) -> (-672,-616,-416)` | 480 x 1632 | 0.0687 | 0 |
| `t3_hevil` | 318 | patch, vertical | 3x5 | `(-608,-1096,-2048) -> (-480,-616,-416)` | 480 x 1632 | 0.0687 | 0 |
| `t3_hevil` | 319 | patch, vertical | 3x5 | `(-416,-1096,-2048) -> (-288,-616,-416)` | 480 x 1632 | 0.0687 | 0 |
| `t3_hevil` | 320 | patch, vertical | 3x5 | `(-224,-1096,-2048) -> (-96,-616,-416)` | 480 x 1632 | 0.0687 | 0 |
| `t3_hevil` | 321 | patch, vertical | 3x5 | `(1120,-1096,-2048) -> (1248,-616,-416)` | 480 x 1632 | 0.0687 | 0 |
| `t3_hevil` | 322 | patch, vertical | 3x5 | `(1312,-1096,-2048) -> (1440,-616,-416)` | 480 x 1632 | 0.0687 | 0 |
| `t3_hevil` | 323 | patch, vertical | 3x5 | `(1504,-1096,-2048) -> (1632,-616,-416)` | 480 x 1632 | 0.0687 | 0 |
| `t3_hevil` | 324 | patch, vertical | 3x5 | `(1696,-1096,-2048) -> (1824,-616,-416)` | 480 x 1632 | 0.0687 | 0 |
| `yavin1` | 57 | patch, sloped | 5x3 | `(-744,-4736,136) -> (-328,-4400,824)` | 416 x 688 | 0.0630 | 7 |
| `yavin1b` | 1 | patch, sloped | 5x3 | `(-1360,2944,308) -> (-1092,3408,1020)` | 464 x 712 | 0.0696 | 8 |
| `yavin1b` | 2 | patch, sloped | 5x3 | `(-744,-4736,136) -> (-328,-4400,824)` | 416 x 688 | 0.0630 | 7 |

All eleven have contents `2147483652`, surface flags `13`, and shader
surfaceparms `nonsolid nonopaque water trans`. None has a meaningful linked
liquid brush, so scene depth to the cliff cannot be treated as water thickness.
The listed vertex density is for the 15 BSP patch control points; the engine's
ordinary patch tessellation is still too coarse for controlled sheet breakup,
so the dedicated render-only subdivision path is used.

The winning definition is `shaders/hiddenevil.mtr` in `assets8_pbr1.pk3`:

| Stage | Texture | Blend | tcMod |
|---:|---|---|---|
| 1 | `textures/h_evil/wf3` | `GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA` | `scroll 0.02 -0.27` |
| 2 | `textures/h_evil/wfn2` | `GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA` | `scroll -0.02 -0.2` |
| 3 | `textures/h_evil/waterf1` | `GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA` | `scroll 0 -0.45` |
| 4 | `$lightmap` | `GL_DST_COLOR GL_ZERO` | none |

There are no `tcMod turb`, `tcMod stretch`, or `deformVertexes` directives.
The three authored scrolls are retained as direction/style evidence; UV
tangents are transformed into world space and reconciled with projected
gravity. Texture `-V` is never assumed to mean world down.

The approximate audited top -> impact points are:

- `t3_hevil` 317..324: one point for each strip at X `-800, -608, -416,
  -224, 1120, 1312, 1504, 1696`; top `(X,-616,-416)`, bottom
  `(X,-1096,-2048)`. Resolved fall direction `(0,-0.2822,-0.9594)`.
- `yavin1` 57 and `yavin1b` 2: `(-744,-4736,824) ->
  (-328,-4400,136)`. Direction `(0.5205,0.2258,-0.8235)`.
- `yavin1b` 1: `(-1360,3408,1020) -> (-1092,2944,308)`.
  Direction `(0.1943,-0.5792,-0.7917)`.

The audit also found stock effect runners whose names mention waterfalls but
which do not own a water sheet: two `env/waterfall_mist.efx` runners in
`vjun1`, and seven mist plus one `world/waterfall3.efx` runner in `yavin2`.
These remain EFX/volumetric-particle effects and are deliberately not promoted
to geometry. `yavin1` has 12 and `yavin1b` has 24 named waterfall EFX entities;
the spatially associated entities for each sheet are recorded individually in
the JSON audit.

## Classification and authoring

Waterfalls have a dedicated body flag and profile; the horizontal-water
classifier is unchanged. Resolution order is:

1. a matching per-map `Waterfalls` rule;
2. steep liquid semantics plus the exact audited stock mapping
   `textures/h_evil/wfall`;
3. legacy stages.

The exact mapping additionally requires `abs(planeNormal.z) < 0.7`. There is no
runtime filename substring search and generic vertical liquid-brush sides are
not selected. An author rule can select `Shader`, `ShaderPrefix`, `BodyId`,
`Point`/`Origin`, or `Bounds`. Profiles are `small`, `medium`, and `heavy` and
carry flow speed, mean sheet thickness, breakup, normal scale, aeration,
opacity, spray strength, and impact strength.

Project overlay rules live in
[`water_body_overrides.json`](../tools/rend2/water_body_overrides.json). They
configure `t3_hevil` as heavy (8-unit authored thickness) and `yavin1` /
`yavin1b` as medium (4-unit thickness). Build the non-destructive overlay with:

```text
python tools/rend2/build_water_body_overlay.py <game-base> <output.pk3>
```

Neither original BSPs nor installed PK3s are modified.

## Rendering

The render mesh is subdivided on the CPU at map load and uses the existing
GL3.2 VBO/IBO path. Quality targets maximum triangle edges of 128, 64, and 32
world units. Collision/BSP geometry is untouched; culling bounds expand by the
profile displacement envelope. Shared midpoint subdivision avoids cracks
inside a surface and the stock strips remain independent where their BSP
bounds have real gaps.

The vertex program creates a low-frequency coherent sheet and downstream-
increasing medium breakup, displaced only along the authored sheet normal.
The fragment program adds two advected tangent-space detail scales plus the
legacy stage detail. It computes a stable 0..1 coordinate from each body's own
top and bottom projections.

Refraction samples the existing pre-tonemap scene color/depth and keeps the
foreground rejection logic, but absorption/scattering path length is the
profile thickness field divided by refracted-ray cosine—not depth to the wall.
The sheet is two-sided air -> water -> air. SSR/environment reflection is
attenuated and roughened. Aeration grows downstream and blends toward lit,
rough scattering; it never writes glow/emission.

When `r_waterfall 0`, no surface gets the waterfall key, the dedicated draw is
not queued, and the original four shader stages execute unchanged. Modern
horizontal water and generic distortion selection are separate.

## Controls and diagnostics

| CVar / command | Meaning |
|---|---|
| `r_waterfall 0/1` | master switch; default `0`, latched |
| `r_waterfallQuality 0..2` | profile/render quality |
| `r_waterfallGeometry 0/1` | render-only subdivision; latched |
| `r_waterfallRefraction 0..4` | sheet refraction scale |
| `r_waterfallWhitewater 0..4` | aeration/whitewater scale |
| `r_waterfallDebug 0..12` | debug view |
| `r_waterfalls` | list every resolved sheet, surfaces, profile/source, flow, thickness, top/bottom and impact region |
| `r_waterGeometryInfo` | vertices, triangles, level, map-load time, and GPU mesh bytes |

Debug modes: 1 classification, 2 flow vector, 3 along-fall coordinate,
4 displacement, 5 thickness, 6 raw refraction, 7 turbulence, 8 aeration,
9 foam source, 10 spray source, 11 impact region, 12 subdivided wireframe.

## Performance and integration

`test_watersurface_gl.py --bench` uses `GL_TIME_ELAPSED`. On the test machine's
Intel UHD OpenGL 3.2 driver at 1920x1080, the shared color/depth copy was
3.309 ms. Dedicated waterfall shading without SSR measured 3.881 ms at 10%
coverage, 20.057 ms at 50%, and 38.477 ms fullscreen. With Hi-Z SSR at 24
steps it measured 8.639, 33.171, and 66.734 ms respectively. These are a
deliberately conservative synthetic shader microbenchmark, not representative
of a discrete GPU. With no visible waterfall there is no waterfall draw; scene
copies are the existing shared water copies rather than new targets.

The installed `t3_hevil` runtime at medium quality built each of the eight
strips to 2,145 vertices / 4,096 triangles (level 5): 2,589,696 GPU bytes total
(2.47 MiB) in 6.0 ms at map load. The legacy-off run built no waterfall mesh.
`yavin1b` built its two medium sheets to 561 vertices / 1,024 triangles each,
168,192 GPU bytes total in 1.0 ms.
The test writes old/new and all diagnostic captures beneath
`build/waterfall-runtime/{legacy,modern}/home/OpenJK/screenshots`.

The renderer exposes body-local top/bottom projections, impact center/width,
flow/energy, foam/spray/impact scalar sources, and profile identity. Existing
stock mist/splash EFX continue through volumetric particles. Existing
`LensWaterEmitters` and lens-water event APIs remain the consumers for camera
wetness. The optional localized impact-pool path feeds the existing ripple and
persistent-foam fields; no second ripple simulation was added. It is described
in [`rend2-waterfall-impact.md`](rend2-waterfall-impact.md).

Persistent foam and object-intersection fields remain top-down XY fields for
horizontal water. A waterfall sheet is never projected into those fields;
instead its resolved base is connected to one receiving horizontal body and
stamped locally. A future sheet-local field could still use the supplied
across/fall basis for object contacts on the falling sheet itself. This follows the
conceptual architecture in Rare's
[The Technical Art of Sea of Thieves](https://history.siggraph.org/wp-content/uploads/2022/09/2018-Talks-Ang_The-Technical-Art-of-Sea-of-Thieves.pdf):
project object depth into the texture space of a shallow-water simulation,
rather than drawing a screen-space contact outline.

Waterfall-base spray, froxel mist, LensWater coupling, authored sidecars and
the stock-map overlay are described in
[`rend2-waterfall-mist.md`](rend2-waterfall-mist.md).

Validation commands:

```text
python tools/rend2/test_watersurface_gl.py
python tools/rend2/test_watersurface_gl.py --bench
python tools/rend2/test_waterfall_runtime.py --installation <OpenJK-build> --map t3_hevil
```

The GL test compiles/links 144 shader permutations and checks the existing
horizontal optics, flow, waves, SSR, Snell/TIR, interaction and fog behavior,
guarding the requirement that horizontal water remains unchanged.
