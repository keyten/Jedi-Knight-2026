# Rend2: froxel volumetric fog

A new volumetric fog mode, `r_volumetricFog 2`: the BSP fog volumes lit in a camera aligned froxel volume by the
baked light grid, the sun (with its cascaded shadow maps) and the dynamic lights (with their shadow maps), with a
Henyey-Greenstein phase function and temporal reprojection. The legacy mode `r_volumetricFog 1` is kept unchanged
as the fallback, and is still used by every view that has no froxel volume (portals, mirrors, sky portal, LA
goggles, UI scenes).

Code: `shared/rd-rend2/tr_volumetric.cpp`; shaders `volumetric_common.glsl` (library), `volumetric_inject.glsl`,
`volumetric_integrate.glsl`, `volumetric_composite.glsl`, `volumetric_debug.glsl`; froxel branches (`USE_FROXEL_FOG`)
in `fogpass.glsl`, `generic.glsl`, `surface_sprites.glsl`.

Off by default. With `r_volumetricFog 0` or `1` no resources are created, the library is not inserted and
`USE_FROXEL_FOG` is not defined: the preprocessed sources of the fog pass, generic and surface sprite programs are
identical to the previous ones (160 permutations compared), and every draw is submitted as before.

## Old system (`r_volumetricFog 1`)

- **What the volumetric light map holds.** `world->volumetricLightMaps[0]` (`R_BuildLightGridTexture`,
  `tr_bsp.cpp`) is a 3D texture with one texel per BSP light grid cell (`lightGridBounds`, default cell 64 x 64 x
  128 units). Each texel is the non directional merge of the cell's baked light: `max(ambient, direct)` of light
  style 0 for LDR grids (`GL_RGB8`, `GL_SRGB8` with forced linear light), `ambient + direct` for HDR grids
  (`GL_RGB16F`). The light direction (`latLong`) is dropped.
- **Static.** It is built once at map load and never updated.
- **Relation to the light grid.** Same data, same layout; the fog shaders map a world position to texture
  coordinates with `u_LightGridOrigin = lightGridOrigin - 0.5 * lightGridSize` and
  `u_LightGridCellInverseSize = lightGridInverseSize` divided by the texture size.
- **Lights it contains:** everything q3map2 baked into the grid: static lights, the sky / sun light (shadowed at
  the resolution of the grid cells), bounce / ambient. All isotropic.
- **Lights it does not contain:** dynamic lights (sabers, blaster bolts, explosions, muzzle flashes, force
  effects), the actual sun direction, the cascaded sun shadows, the dynamic light shadow maps, entities, anything
  that changes at runtime. No phase function.
- **Rendering.** Every fogged surface gets a fog pass (`RB_FogPass`, `fogpass.glsl`) that ray marches the light
  map from the camera (or the fog plane) to the fragment with `r_volumetricFogSamples` steps, and blends
  `ONE, ONE_MINUS_SRC_ALPHA`: `color * T + S`. Generic (non lightall) stages and surface sprites do the same in
  their own shader (`u_FogColorMask`). A global fog gets a plane at the top of the world and a "fog cap" quad at
  `depthForOpaque` for the sky. Extinction: `depthToOpaque = -ln(1.5 / 255) / depthForOpaque * volumetricFogScale
  * r_volumetricFogScale` per unit.

## New system (`r_volumetricFog 2`)

```
frame: shadow views (sun cascades, dynamic light cubes)          [unchanged]
main view:
  depth prepass -> screen-space AO                               [unchanged]
  froxel inject      N slices: media, baked + sun (+ history) | dynamic lights
  froxel integrate   N slices: front to back, S and T per slice
  main pass: layers <= SS_FOG (opaque, sky, decals, see-through, banners, fog faces)
             (SSR after the opaque layer, as before)
             froxel composite: color = color * T + S, glow = glow * T   (from the depth buffer)
             layers > SS_FOG: fog passes / in-shader fog look up the volume at the fragment
post process: bloom, tone mapping, ...                           [unchanged]
```

The media are exactly the legacy ones: only the BSP fog volumes (and the global fog) have an extinction, so maps
without fog are not turned into a haze (and a map without fog volumes skips the froxel passes entirely). Light
that is identical in both modes (baked, isotropic, `g = 0`) gives the same in-scattering as the legacy ray march:
the phase function is normalized to 1 for isotropic scattering and the integration uses the same
discretisation (see Integration).

### Froxel grid

- `Fx = ceil(width / gridScale)`, `Fy = ceil(height / gridScale)`, `Fz = slices`; x, y cover the main view.
- Slices are exponential in view depth (distance along the view forward axis):
  `B(k) = near * (far / near)^(k / Fz)`, slice k spans `[B(k), B(k+1)]`, slice 0 starts at the camera. The slice
  coordinate is linear inside slice 0 (`d = z * B(1)`, `FroxelWToDepth` / `FroxelDepthToW`), so its samples lie
  inside its own integration domain `[0, B(1)]` (center 4.6 units, not 8.5 at the near plane).
  `near = 8` units, `far = r_volumetricFogFar` (0 = 4096). With 48 slices every slice is 13.9% deeper than the
  previous one: ~1 unit thick at 8 units, ~14 at 100, ~140 at 1000.
- Beyond `far` the media are integrated analytically per pixel (`FroxelTailMedium`): every BSP fog volume clipped
  to its bounds and visible side along the segment `[far, d]` (exact), and the height fog: exact below its soft top
  (exponential above its cap, constant below), a 3 point Gauss-Legendre rule inside the fade, the same
  `1 - smoothstep` as the injection (transmittance error < 1e-4 against a numeric integral; the former cut in the
  middle of the fade was off by up to 1 for level rays through the upper half of the fade). A bounded fog that ends
  at 4500 stops there, one that starts beyond far is still seen. **Approximations** of the tail: the light is the
  tail texture, the baked + sun light at the far side of each froxel column without albedo (the tail pass of the
  injection), not the light along the segment; the density noise is its mean (1).
- Tail cost (full resolution, every opaque pixel and every froxel-fogged transparent fragment beyond far): only the
  BSP fogs whose bounds reach beyond far inside the frustum are looped (`u_FroxelTailFogs`, a mask built with the
  per slice fog masks), and a pixel whose transmittance at far is below 1e-4 skips the tail (dense fog maps: every
  pixel). Fog volumes closer than far cost one bit test per pixel.
- Presets (`r_volumetricFogQuality`), **starting points, not profiled yet**:

| quality | pixels per froxel | slices | 1920x1080 grid | froxels | volume memory |
|---|---|---|---|---|---|
| 0 low | 16 | 32 | 120 x 68 x 32 | 0.26 M | 7.3 MB |
| 1 medium (default) | 8 | 48 | 240 x 135 x 48 | 1.56 M | 43.5 MB |
| 2 high | 8 | 64 | 240 x 135 x 64 | 2.07 M | 58.1 MB |

  `r_volumetricFogGridScale` / `r_volumetricFogSlices` override the preset (latched). Draws per frame: one
  instanced injection draw, the tail pass, `slices` integration draws and the composite on raster;
  two compute dispatches and the composite with the GL 4.3 fast path.

### Froxel data

| texture | format | size | contents |
|---|---|---|---|
| `froxelInjectImage[2]` | RGBA16F 3D | Fx Fy Fz | rgb = emission of the baked light and the sun, `extinction * albedo * L_in`; a = extinction per unit. Temporally filtered; the two images are this frame and the history (ping-pong) |
| `froxelDynamicImage` | R11G11B10F 3D | Fx Fy Fz | emission of the dynamic lights, this frame only |
| `froxelIntegratedImage` | RGBA16F 3D | Fx Fy Fz | rgb = in-scattering S, a = transmittance T, between the camera and the far side `B(k+1)` of slice k |
| `froxelCarryImage[2]` | RGBA16F 2D | Fx Fy | integration state between two slices (ping-pong, no feedback loop) |
| `froxelTailImage` | RGBA16F 2D | Fx Fy | light at the far side of the volume (baked + sun with phase, no albedo): lights the media beyond far |
| `world->volumetricStaticGrid` | RGBA16F 3D | light grid | non-sun baked baseline B (rgb), sun fraction f = align * vis (a, traced at load) |
| `world->volumetricSunGrid` | R11G11B10F 3D | light grid | baked sun part S |
| `world->volumetricDirMomentR/G/B` | RGB16F 3D | light grid | `r_volumetricFogStaticDirectional 1, 2` only: first angular moment of the attributed baked light per colour channel (xyz, towards the light, `|M_c| <= B_c`) |
| froxel light lists | buffer textures | per frame | lights (RGBA32F, 2 texels) and cluster headers + indexes (R32UI) |

Emission (not radiance) is stored because it is linear in the medium: blending two frames of emission and
extinction is correct at fog boundaries and under jitter.

### GL 4.3 compute fast path

With `r_gl43 1` and `r_volumetricFog 2`, GL 4.3 compute and image load/store use the same shader
math as raster injection and integration. Injection dispatches a 3D grid (4 x 4 x 4 workgroups);
the first invocation of each column also writes its tail light. Integration dispatches a 2D grid
(8 x 8 workgroups), one invocation per XY column, and sequentially integrates all Z slices in registers
with `imageStore` for each slice. The column invariants (ray length and geometric slice ratio) are computed
once: one `exp2` per column, then multiplication for subsequent boundaries, with the last boundary anchored
to `far`. Slice 0 still starts at the camera. Image outputs are `restrict writeonly` (distinct textures).
The far depth of a slice is the near depth of the next, and the texel fetches of the next slice are issued before the math
of the current one to hide their latency. No per-slice draws, attachment changes or carry texture accesses
are needed. Carry textures are allocated only for raster, including fallback after compute compilation fails.

RGB extinction, temporal history, local/particle media, particle lighting, shadows and cookies use
the same inputs and outputs. Self-shadowing adds one compute media dispatch before injection, with its
own kernel (`USE_FROXEL_MEDIA_PASS`: only the medium evaluation, so its register use is not sized by
the lighting path). Image access and texture fetch barriers order each producer before its consumers
and order image overwrites after previous-frame reads; image units are unbound after integration.
Partial workgroups check their bounds. Compute integration keeps FP32 running state, whereas raster
rounds it to RGBA16F after every slice; results can differ slightly from reduced accumulation rounding.

`r_gl43 0` (followed by `vid_restart`) selects the existing raster implementation. Missing capabilities,
insufficient workgroup/image/sampler/UBO limits, or a failed optional compute compile/link also select
raster. The compute programs (injection, media with `r_volumetricSelfShadow`, integration) are compiled
first; the raster injection / integration are compiled only when one of them fails, so the large
injection is not compiled twice. Compute programs use the program binary cache (`r_shaderProgramCache`)
like the raster ones. Startup reports the selected path. With volumetric fog disabled no volumetric
compute programs are loaded.

GPU validation: `python tools/rend2/test_volumetric_compute.py` on Windows uses bundled SDL2 and a
hidden GL context. It compiles raster/compute/media permutations for scalar/RGB extinction and both sun
shadow modes, checks injection/media/tail/particle-light outputs, checks all integrated slices against a
Beer-Lambert reference (including empty media and dimensions that are not multiples of the workgroup
size), and renders the raster injection (layered draws, tail draw) with the same inputs as compute —
temporal history, jitter, reprojection between froxel centers, a varying light grid — and compares all
outputs.

NVIDIA (2026-10-02): the compute injection used to fail to compile on the NVIDIA driver (RTX 2060,
`error C1068: array index out of bounds`), so the game fell back to "Froxel volumetric fog: raster path".
The tail pass runs the injection with `var_Slice = -1`; NVIDIA inlines it and constant-folds the
per-slice mask lookups `[slice >> 2]` to index -1, even though the tail returns before reaching them.
The slice is now clamped with `max(slice, 0)` in `FroxelParticleSliceHeader` (volumetric_common) and for
the fog volume mask in `FroxelMedium` (volumetric_inject), as was already done for the liquid mask. Raster
slices are never negative, so the raster output is unchanged. All 16 compute injection variants (scalar/RGB,
`USE_SHADOWS2`, media pass, with/without `USE_LIQUIDS`) now compile and link on the RTX 2060 (offline GL 4.3
compute checker), and `test_volumetric_compute.py` still passes on Intel UHD. The compute path has **not
been run in game on NVIDIA**: A/B `r_gl43 0` / `r_gl43 1` (with `vid_restart`) and check that startup
reports the compute path.

### Pipeline without compute shaders

The GL 3.2 fallback has no compute shaders or image load/store. The injection renders every slice
in one instanced draw into layered attachments (`glFramebufferTexture`): the geometry shader sends instance k to
layer k (`gl_Layer`). The integration needs the previous slice, so it stays one draw per slice with that layer
attached (`glFramebufferTextureLayer`). The integration carries its running state in a 2D texture ping-pong, because sampling another
layer of the texture that is being rendered to is a feedback loop in GL.

## Injection (`volumetric_inject.glsl`)

Per froxel (instance / layer = slice):

1. **Position.** The sun and the media are sampled at a jittered position (Halton 2, 3, 5 over 8 frames, a
   full froxel wide) when temporal accumulation is on; the dynamic lights and the static baked light field
   (baseline and moments: coarse, smooth, static; a directional response of a jittered sample would linger in
   the history) at the froxel center.
2. **Medium.** Sum of the extinction of the fog volumes that contain the point: their axial bounds and the plane
   of their visible side (the same `inFog` test as `CalcFog`); the global fog everywhere below its cap plane.
   Albedo = extinction weighted fog color. Only the fog volumes of the slice's CPU mask are tested (bounds against
   the frustum sides and the view depth range, one slice wider for the jitter; `fogSlices`). The height fog, the
   density noise and the local fog volumes (see Local fog volumes) are in `FroxelMedium` too; all media add.
3. **Baked light.** `volumetricStaticGrid` (B) `* r_volumetricFogStaticScale`. With
   `r_volumetricFogStaticDirectional 1 / 2` (permutation `USE_FROXEL_STATIC_RECONSTRUCTION`) per channel
   `max(B + 3 clamp(g, -1/3, 1/3) (M_c . viewDir), 0)`, the first order Henyey-Greenstein response of the
   moments (see Directional baked light); otherwise B, isotropic.
4. **Sun.** Phase `4 pi HG(g, dot(sunDir, viewDir))`, `* r_volumetricFogSunScale`:
   - with cascaded shadow maps this frame (`VPF_USESUNLIGHT`): `sunRadiance * shadow`, blended to the baked sun
     part at the far end of the last cascade (same fade as lightall);
   - without them: the baked sun part `volumetricSunGrid`.
5. **Dynamic lights.** The lights of the froxel's cluster with lightall's attenuation
   `clamp(0.5 * r^2 / d^2 - 0.5)`, phase `4 pi HG(g, dot(toLight, viewDir))`, their shadow map (see Shadows),
   `* r_volumetricFogDlightScale`. The dynamic light system is the only source: sabers, bolts, explosions are
   volumetric exactly when game code adds a dynamic light for them. There are no spot / projected lights in the
   renderer. Clustered like Forward+ but on the froxel grid (`R_VolumetricBuildLightLists`): tiles of 8 x 8
   froxels per slice (`r_volumetricFogLightTile 4 / 8 / 16`, not archived, for A/B; `r_vfogLightStats` prints
   the lights, clusters, average / max lights per cluster, overflow and CPU build time of the last view), each light sphere binned into the clusters its projected bounds and depth range touch, at
   most 32 per cluster (the least important drop out). With `r_forwardPlus 1` every point light of the scene is a
   candidate (no `MAX_DLIGHTS` limit), otherwise the lights of the Lights block. The medium at the froxel center is
   evaluated only where the cluster has lights.
6. **Temporal filter** of baked + sun (see Temporal). The dynamic light emission is written to its own volume
   without history.

### Light grid split by the sun direction (`R_BuildStaticLighting`)

The reconstruction is owned by the Static Lighting Reconstruction (`tr_staticlighting.cpp`, see
[rend2-static-lighting.md](rend2-static-lighting.md)); `tr_volumetric_reconstruct.cpp` is only the fog adapter that
uploads its textures. With `r_recoveredVolumetricLights` the promoted lamps are removed from B / M
(B + P + S == legacy) and injected as exact point / spot lights after the dynamic ones; debug view 25 then shows
P as the difference to the legacy grid.

The legacy light map merges the baked sun with everything else; adding a realtime sun on top would count it
twice. At map load (mode 2 only, `tr_staticlighting.cpp`, after the area lights: `RE_LoadWorldMap`), every
grid cell is split with the light direction of the cell and the part of the cell that sees the sky:

```
align  = smoothstep(cos 25, cos 10, dot(cellDir, sunDir))       // 0 when the map has no sun shader
vis    = rays reaching the sky / 5                               // centre + 4 tetrahedron corners, half a cell out
f      = valid ? align * vis : 0                                 // wall cells (LS_LSNONE) are never sun
S      = clamp(f * direct, 0, legacy)                            // baked sun part
B      = legacy - S                                              // B + S == legacy value
```

The direction of a grid cell is a mix of all its lights, so a lamp straight above can look like a high sun: the sky
visibility is part of the split, an aligned indoor lamp (no ray reaches the sky) stays static light B and is
handled by the directional reconstruction below, never with the sun phase. Rays are traced only for cells with a
sun alignment and a direct part (collision world: `CM_BoxTrace`, SP `SV_Trace`; reaching a `SURF_SKY` surface
counts as the sky), in two passes: the centre ray of every candidate cell, then the four corner rays only for the
cells whose centre result differs from a candidate neighbour (window edges, shadow rims); elsewhere vis is 0 or 1.
A large open outdoor area costs about one ray per cell instead of five. The alpha of `volumetricStaticGrid` is the
sun fraction f (trilinear between cells). Inside the cascades the realtime sun replaces exactly the classified
part: `mix(S, f * sun * shadow, coverage)`, so a window edge cell (f = 0.2, B holding 0.8 D) never gets the full
sun on top of its B; deep indoors (f = 0) it stays baked light and is not darkened. The central realtime cascade
lookup stays, so characters still cut the beams.

The realtime sun radiance is the 90th percentile luminance of the sunlit cells (`align * vis > 0.5`, at least 16
cells), with their average color, so light beams have the brightness the map was compiled with. Without sunlit
cells the refdef sun color is used. `developer 1` prints "Froxel fog sun visibility: N cells traced, M see the sky,
T msec" and the split error. A disk cache (BSP checksum + sun direction) is only worth it if this shows more than
~300 ms on the big maps; a CPU cache would not survive `vid_restart` (the renderer DLL is reloaded).

### Directional baked light (`r_volumetricFogStaticDirectional`)

The light grid stores per cell ambient A, directed D and **one** dominant direction (`latLong`, towards the light,
decoded as `R_SetupEntityLightingGrid`: 256 steps per turn): q3map collapsed every lamp that reaches the cell into
that direction. Fed straight into Henyey-Greenstein it is wrong wherever lamps overlap. The reconstruction recovers
a conservative **first angular moment** of the baked light per colour channel instead, once at map load, and the
injection applies the first order (L1) phase response. Nothing is added: the moments only redistribute the baseline
over the view directions, their mean over the sphere is zero, and low confidence stays isotropic.

```
known exact light          sun, dynamic point / spot lights   full HG (medium g)
reconstructed baked light  moments M_R, M_G, M_B              B + 3 g_s (M_c . v), g_s = clamp(g, -1/3, 1/3)
unexplained / diffuse      baseline B                          isotropic
```

The Legendre moments of HG are `g^l`; keeping `l = 0, 1` gives `1 + 3 g cos`. With `|M_c| <= B_c` (enforced after
the half rounding too) and `|g_s| <= 1/3` the result is never negative (the shader still clamps at 0). For
`g = 0.2` a perfectly coherent moment modulates the baked light between 0.4 and 1.6 times its baseline, far below
the full HG forward peak: the intended behaviour for uncertain baked light. Opposite lamps of the same colour
cancel to isotropic (the safe failure mode); red and blue lamps keep their own directions because every channel
has its own moment. Moments are linear, so trilinear filtering between cells is well defined (explicit per cell
lobes would swap source identities between neighbours).

Modes (latched: the moment textures and the permutation are built at map load / program load):

| mode | moments | use |
|---|---|---|
| 0 | none: no moment texture, no sampler, fetch or ALU in the injection | default |
| 1 | reconstructed (below) | the feature |
| 2 | `M_c = Q_c * bspDir` (raw light grid direction, disagreeing neighbours cancel by filtering) | developer A/B |

**Directional budget.** Only part of B may become directional:

| grid | legacy | Q (per channel, f = align * vis, invalid cells 0) |
|---|---|---|
| HDR | `A + D` | `clamp((1 - f) D, 0, B)` |
| LDR | `max(A, D)` | `clamp((1 - f) max(D - A, 0), 0, B)` |

The whole B is evidence for finding lamps; Q is the most that may be attributed. A lamp q3map folded into the
ambient part helps locating it, but that ambient energy stays isotropic (so at the exact midpoint of a strong red
and a weaker blue lamp the blue channel stays nearly isotropic).

**Reconstruction** (`tr_volrecon.cpp`, no renderer globals, effectively linear in the grid size):

1. *Gradients* of B.rgb, six-neighbour central differences, one-sided next to a wall cell, never through
   `LS_LSNONE` cells (the luminance gradient is their `LUMA` combination, not stored). Strength
   `|grad B_c| * cellDiagonal / (B_c + eps)`.
2. *Seeds*: local maxima (26 neighbours, non-maximum suppression) of Q per channel and luminance, favoured where the
   BSP direction field (its convergence computed once per cell) and the gradient field converge (`-div`), at most
   512. The threshold is 3% of the brightest cell of the channel on the whole map: a dim lamp in a dark room next
   to a bright hangar may get no seed (it stays isotropic, the safe failure).
3. *Point proxy fit* per seed: support probes flood-connected (six neighbours, no wall crossing) within 4 cells, at
   most 256. BSP direction rays (weight Q) and gradient rays (weight |grad|) are separate observations; the closed
   form least squares point `p = A^-1 b`, `A = sum w (I - d d^T)`, with Huber IRLS passes (delta half a cell
   diagonal) and a tiny ridge towards the seed (parallel doorway rays stay near the brightest cell); the
   conditioning uses A rebuilt with the final Huber weights.
   Confidence `C = C_ray * sqrt(C_gradient * C_profile * C_chroma)`: ray residual, the share of observations pointing
   at p, a monotonic (isotonic) radial profile of the brightness projected on the proxy colour (a flat field is no
   lamp), and the stability of the colour from the positive radial derivatives near / far. The conditioning of A
   raises the positional uncertainty sigma_p, it does not reject: a lamp smaller than a cell or a doorway proxy is
   fine, the fog only needs the incoming direction.
4. *Merge* fits of one lamp (closer than max(0.75 diagonal, sigma sum), at most 1.5 diagonals, same colour); two
   lamps of the same colour apart stay two. The strongest fit is kept whole (position, radial profile, sigma_p,
   start cell stay one consistent model); the weaker one only adds its energy and range.
5. *Area anchors*: the static emitters of `tr_arealights.cpp` (`R_CollectStaticAreaSources`: explicit non-dynamic
   `maps/<map>.arealights.json` lamps, and the emissive surface candidates `r_ltcAutoAreaLights 2` would take, not
   animated, whatever `r_ltcAreaLights` is; the candidate scan and texture mask readbacks are shared with the LTC
   path). Their moment per probe is a centre or 3x3 quadrature `sum w_k u_k / sum w_k`, `w_k = area cos / r^2`: a
   close large panel is naturally less directional. They are calibrated against the grid and never add their own
   radiance; a point proxy of the same lamp is dropped. A two-sided emitter floods from both of its sides (a panel
   in a wall between two rooms lights both). Line / tube lights (`DLIGHT_LINE`) are not anchors: the rectangle
   quadrature does not describe them.

   **Budget**: at most 256 sources in total. Anchors first (confidence * energy order), point proxies fill what is
   left; `r_vfogStaticStats` counts the dropped ones.
6. *Attribution*: each source floods its range (where its predicted light falls below 1% of the mean baked light,
   2 to 10 cell diagonals), per cell and channel a match
   `m = C * C_pos * evidence * falloff * colourCompatibility` with `C_pos = r^2 / (r^2 + sigma_p^2)`, evidence the
   RGB gradient towards the source where informative, else the BSP direction, falloff = predicted / observed.
   `E_c = Q_c * max m` (weak candidates never add up to the whole budget), split between the sources by m:
   `M_c = E_c * sum(m u) / sum(m)`, point directions shortened by `r / sqrt(r^2 + sigma_p^2)`.

Invalid cells, the sun part and everything unexplained keep their light in B. `r_vfogStaticStats [proxies]` prints
the probes, seeds, fits, point proxies, area anchors, mean / P95 ray residual, mean sigma_p, the attributed and the
directional (after cancellation) share of the baked light, and the time of each stage; `developer 1` prints a
summary at load. The accepted sources (`vrProxy`: type point / rect, position, colour, confidence, sigma_p, range,
and for a rect its axes, half sizes and sidedness) are kept after the load, independent of the temporary inputs,
for later consumers (light portals, recovered spot lights, static specular lights).

Memory: the reconstruction is transient. The largest parts on a million cell grid are the per cell accumulator
(60 bytes), the three RGB gradients and strengths, Q, B, S and the moments; the renderer frees its input arrays
right after `VR_Reconstruct` and the float moments after the half packing. A cell-centric attribution (spatial
hash of the sources) or slabs would remove the accumulator; not done yet. Also deferred: a disk cache (sun
visibility + half moments, ~19 bytes per cell, keyed by the grid, sun, geometry and area source hashes), a world
space proxy debug draw, per-channel moment debug views, a local-contrast seed threshold, line source quadrature.


Cost: memory +18 bytes per grid cell (three RGB16F volumes, mode 1 / 2 only); injection three more trilinear 3D
fetches and three dots per froxel (and tail texel), the lobes one multiply-add each; no source loop and no per
frame CPU work. Mode 0 compiles without them. Load time (synthetic 64 x 64 x 32 grid, 40 lamps and walls): about
0.25 s, most of it the attribution.

Validation: `tools/volrecon_test` (synthetic q3map-like grids: one lamp, a sub-cell lamp, red / blue, opposing and
same-side white lamps, a doorway, a large ceiling panel, outdoor sun, an indoor lamp aligned with the sun, a diffuse
room, an LDR byte grid; invariants `B + S == legacy`, `|M_c| <= B_c` in float and half, `B + 3 g M.v >= 0`, no NaN,
in modes 1 and 2), and `tools/rend2/test_volumetric_compute.py` (the L1 response, clamp, sign and unchanged
extinction on the GPU, raster = compute with moments).

Offline check on the stock maps (`maps/*.bsp` with fog, sun from the sky shader):

| map | sun | sunlit cells | sun share of the grid energy | realtime sun radiance (luma) |
|---|---|---|---|---|
| hoth2 | yes | 274 344 / 1 044 126 | 46.0% | 0.573 |
| vjun1 | yes | 230 931 / 1 001 520 | 39.0% | 0.724 |
| mp/duel9 | yes | 103 615 / 496 800 | 73.5% | 0.642 |
| mp/ffa2 | yes | 34 773 / 122 766 | 66.3% | 0.646 |
| taspir1 | yes | 15 945 / 52 155 | 53.0% | 0.937 |
| t2_trip | yes | 11 428 / 1 047 540 | 2.2% | 0.576 |
| kor1, t2_rancor, mp/ctf1, ... | no | 0 | 0% (static == legacy) | - |

## Shadows in the volume

- **Sun.** The existing cascades (`sunShadowArrayImage`, `refdef->sunShadowMvp`): the first cascade whose
  interior (minus a PCF margin) contains the point, as `sunShadow()` in lightall. Constant depth bias (no normal
  to offset along). One hardware filtered tap with temporal accumulation (the jittered sample positions average
  to soft, stable beam edges), four taps without it. Fog behind a wall gets no direct sun: the wall is in the
  shadow map.
- **Dynamic lights.** The existing cube shadow maps (`pointShadowArrayImage`, `r_dlightMode 2`, `sampleCube` /
  depth as lightall), 4 taps in a fixed pattern around the light direction, the sample moved slightly towards the
  light. `r_volumetricFogDlightShadows 0` disables them.
- No new shadow maps are rendered.

## Height fog (ground haze)

An optional second medium, evaluated in `FroxelMedium` next to the fog volumes (no extra texture, no extra pass).
Its extinction depends on the world z only, so it is anchored in the world and reprojects like the rest of the
volume. Off by default (`r_volumetricFogHeight 0`): mode 2 is then unchanged, and a map without fog
volumes still builds no volume at all.

```
h        = p.z - r_volumetricFogHeightBase
sigma0   = -ln(1.5 / 255) / r_volumetricFogHeightOpaqueDistance * volumetricFogScale * r_volumetricFogScale
sigma(h) = sigma0 * min(exp(-h / r_volumetricFogHeightFalloff), r_volumetricFogHeightMaxDensity)
                  * (1 - smoothstep(top - fade, top, h))       top = r_volumetricFogHeightTopHeight (0: no cutoff)
                                                                fade = min(falloff, top)
medium   = fog volumes + height fog: extinctions add, albedo = extinction weighted average
```

Units: extinction per world unit, the same conversion as the fog volumes. `r_volumetricFogHeightOpaqueDistance` is a
`fogParms` depthForOpaque: the distance through the medium at the base height after which the transmittance is
1.5/255. There is no separate density scale. Below the base the density grows up to `HeightMax` times the base
density (1 = flat layer below the base). The color is a `fogParms` color (sRGB, converted like the fog volumes).

Height settings resolve per component: map `HeightFog` in `cubemaps/<map>/env.json`, then cvars,
then the automatic base when `r_volumetricFogHeightBase` is `auto` (the default). The automatic base is
the lowest visible opaque floor (`R_SetHeightFogBase`, tr_bsp.cpp), or the world bounds if there is none.
A pit or basement can put it below the main ground, so authored settings are preferred. Loading a map
never overwrites the archived base cvar. Existing numeric archived values remain manual values;
use `r_vfog base auto` to restore automatic placement.

```json
"HeightFog": { "base": 128, "opaqueDistance": 3000, "falloff": 256, "top": 1800 }
```

All four fields are optional; `top` is a height ABOVE the base, not an absolute world Z.
`opaqueDistance: 0` disables the height medium on this map. The object does not enable height fog:
`r_volumetricFogHeight 1` is still required. Map values are stored on the world, never copied into
cvars, so they do not leak into another map. `r_vfog` prints the effective values and notes map overrides.

Injection uses the mean smooth height extinction along each camera-ray slice, including the density
cap and soft ceiling, for both static and dynamic scattering and scalar/RGB extinction. Slice boundaries
do not follow Z jitter; lighting and spatial noise still do. Without noise this makes full-slice optical
depth analytic below the fade and uses the existing three-point quadrature inside it. With noise the
smooth mean is multiplied by the point-sampled modulation, so it remains an approximation. Lighting,
mixed media, partial-slice lookups and temporal reprojection remain discretized. Self-shadow media
keep point density: their rays run toward lights, rather than along the camera's slices.

Only negligible height contributions are discarded before lighting. The threshold is `1e-5 / columnLength`,
applied AFTER noise and scaled by the strongest RGB extinction channel. Across a whole volume column,
discarded optical depth is at most approximately `1e-5` per channel, independent of slice count.
This bounds attenuation error, not brightness under arbitrarily strong lights or forward scattering.
Other media and independent emission are preserved. The analytic tail remains unculled, since its ray
can be much longer than the froxel volume; sprite particle lighting still evaluates light in empty cells.

A finite `top` also saves lighting above its ceiling (and skips height exponentials there).
It stays optional: imposing `6-10 * falloff` by default can remove visible haze on long horizontal rays.

Lighting is the one of the fog volumes (baked grid, sun + cascades, dynamic lights + their shadows, HG phase,
temporal filter, bloom). The global fog stays a fog volume medium; the height fog adds to it and never replaces it.
Transparent surfaces after `SS_FOG` outside every fog volume look the volume up when the height fog is on
(`RB_VolumetricHeightFogSurface`: generic `USE_FOG` permutation, fog pass, surface sprites).

### `r_vfog` console command

Adds this medium to any map (a map without BSP fog volumes has no froxel passes, so no light beams, until it
has a medium). A front end to the `r_volumetricFogHeight*` cvars, so the values are archived:

```
r_vfog                          state and warnings (needs r_volumetricFog 2, r_depthPrepass 1)
r_vfog help
r_vfog on | off | reset
r_vfog opaque 2500 falloff 600 color 0.75 0.8 0.85   any keys, switches the medium on
       keys: opaque <u>, falloff <u>, color <r g b>, base <z> | auto, top <u>, max <scale>
r_vfog uniform 4000 [r g b]     uniform haze (falloff 65536, no ceiling)
```

The sky: a finite falloff leaves almost no medium at the sky distance, so the sky stays clear behind the beams;
a uniform haze continues to the sky through the tail and fogs it like the legacy fog cap.

## Heterogeneous density (world space noise)

Optional, off by default (`r_volumetricFogNoise 0`: every medium stays homogeneous and mode 2 is unchanged). The
extinction of the selected media is multiplied by a world anchored noise field; nothing else changes. The light
(baked grid, sun, dynamic lights) is not modulated: voids, clumps and broken beams come only from the changed
scattering and extinction (emission = extinction * albedo * light).

```
sigma(p) = sigma_plain(p) + m(p) * sigma_noisy(p)        noisy: the media selected by r_volumetricFogNoise
m(p)     = N_M(lod_M) f(n_M; c_M) * N_D(lod_D) f(n_D; c_D)       (detail factor only when c_D > 0)
f(n; c)  = (1 + c) n^c
n_M      = noise.r at  p / P_M - wind_M                            P_M = r_volumetricFogNoiseScale
n_D      = noise.g at  R_z(30 deg) p / P_D + (0.37, 0.61, 0.23) - wind_D   P_D = r_volumetricFogNoiseDetailScale
lod      = max(log2(sliceThickness(depth) * 64 / P) - 1, 0)
```

- **Mean.** The texels of both channels are uniformly distributed over 0..255 (histogram equalized), and
  `E[(1 + c) n^c] = 1` for a uniform n and any c: the modulation keeps the average extinction (the mean optical
  depth). Trilinear filtering and the mips lower the variance of n, so the CPU measures `E[f]` of the real filtered
  texture at lod 0, 0.5, ..., 6 (32768 low discrepancy positions, the same trilinear / mip blend as the GPU) and
  divides it out (`N`). The filtered sample distribution is cached in 1024 bins per half mip; each bin retains
  its measured mean and sample count. Changing contrast evaluates this small distribution rather than sampling
  the texture again, without a LUT along the contrast axis. Macro and detail are independent fields, so their
  product keeps the mean too.
- **Contrast.** `c = 0` gives m = 1, the original homogeneous density. `c = 1` gives a density from 0 to 2x (voids
  and clumps). `c = 3` gives sparse clumps up to 4x. With `c <= 1` the standard deviation of m is at most 0.57.
- **Anti-aliasing.** The mip level follows the froxel: one level sharper than the world-space ray segment
  thickness (`distance(camera, p) * sliceRatio`, including the longer rays at screen edges; the jittered
  positions of the temporal filter average the rest). Far froxels see prefiltered noise with a lower contrast and
  the same mean, so distant fog tends to homogeneous instead of shimmering. Medium preset (48 slices, far 4096):
  macro (4096) lod 0 up to ~900 units, 1.1 at 2000, 2.2 at 4096; detail (900) 1.3 at 500, 3.3 at 2000.
- **World anchoring.** The texture coordinates are world positions (no camera, froxel or screen coordinates). The
  noise is sampled at the jittered position of the baked + sun term (the temporal filter supersamples it inside
  the froxel) and at the froxel center for the dynamic lights (no history, stable).

### Noise texture

| | |
|---|---|
| image | `tr.froxelNoiseImage` (`*froxelNoise`), 3D, 64 x 64 x 64, `GL_RG8`, full mip chain (7 levels), `GL_REPEAT`, `LINEAR_MIPMAP_LINEAR` |
| r | macro field |
| g | detail field (independent seed) |
| memory | 0.57 MiB of VRAM including mips; CPU copy 0.57 MiB plus 0.20 MiB of sample distributions (static, kept for the mean tables) |
| created | at renderer init with `r_volumetricFog 2` only (`R_CreateVolumetricImages`), no asset |
| CPU cost | field generation and distribution sampling once per process; contrast table ~0.67 ms per channel on the test machine; CPU fields, distributions and cached normalization survive `vid_restart` |

Generation (`R_NoiseGenerateField`, deterministic, fixed seeds): per channel, a tileable gradient (Perlin) noise
FBM of 3 octaves with lattice periods 4, 8 and 16 cells per tile (weights 1, 0.5, 0.25, quintic fade, 12 edge
gradients from an integer hash of the lattice point modulo the period, so the tile wraps). Each octave is shifted
by its own fraction of a cell, otherwise the lattice points of the three octaves (where gradient noise is 0)
coincide and form a visible grid. Then rank based histogram equalization: every value 0..255 is taken by exactly
1024 texels. `R_CreateImage3D` gained a `flags` argument (default `IMGFLAG_CLAMPTOEDGE`, as before for every other
caller): without it the wrap is repeat, and `IMGFLAG_MIPMAP` allocates the mip chain. An optional `mipData`
array uploads every CPU mip explicitly, on both immutable and mutable texture paths. Noise uses that array:
normalization and GPU filtering start from identical bytes, independent of the driver's mip generation filter.
`GL_RG8` uploads use `GL_RG` and unpack alignment 1, including the 1x1x1 last level.

The CPU distribution approximation adds at most 0.0038% normalization error versus the previous full estimator
in `tools/rend2/test_volumetric_noise.py` (both fields, all 13 half-mip LODs, 15 contrasts spanning 0.001..4).
This does not remove the existing error from interpolation between half-mip normalization values. The test
machine averaged 0.67 ms per contrast table versus 33.3 ms for just the old pow pass over the saved samples
(excluding its trilinear sampling cost). Shader contrasts 1 and 2 use multiplication instead of `pow`.

### Media

`r_volumetricFogNoise` is a bit mask: 1 height fog, 2 BSP fog volumes, 4 the global fog. The flag of each fog
volume travels in `fogMaxs[i].w`. Bit 8: the local fog volumes with the noise flag (`FOGVOLUME_NOISE`, env.json
`"Noise"`, `r_fogvol add ... noise 1`); they use the same field and periods. With 0, or with both
contrasts at 0, the injection takes a uniform branch and samples nothing.

### Wind and the temporal filter

`r_volumetricFogNoiseWind "x y z"` (units per second, default 0) moves the noise:
`wind offset = fract(phase_at_epoch + wind * (t - epoch) / P)` per octave, computed in double precision and wrapped
to the tile (the detail wind is rotated like its coordinates). Changes to velocity or period first save the
current tile phase and then start a new epoch, so editing wind cannot teleport the field. A stopped wind holds
its last phase. Repeated views at the same time do not advance it twice; time rewind or a new world resets it.
There is no per-frame floating point accumulation. With no wind the noise is completely static in
the world. The weather system's wind is not used (it is gusty, and exists only with weather effects).

The history clamp works on radiance (emission / extinction), which a moving density does not change, so it cannot
catch the drift. The history weight of the noisy media is lowered instead, so that the lag of the temporal filter
stays under a tenth of the finest noise feature (`P / 16` of the finest active octave):

```
lag      = |wind| dt w / (1 - w)  <=  lambda = 0.1 P_finest / 16
w_noise  = min(w, lambda / (lambda + |wind| dt))          dt = frame time, clamped to [1/240, 1/15] s
noisyShare = max(noisy_base / total_base, noisy_modulated / total_modulated)
w_froxel = mix(w, w_noise, noisyShare)
```

At 60 fps with `w = 0.9`: winds up to ~32 u/s keep the full weight. At 128 u/s the weight is 0.90 macro only and
0.73 with detail (lag 19 / 6 units). At 512 u/s it is 0.75 / 0.40. Media without noise keep the full weight in
every case. `P_finest` is the smaller period of the active fields, even when macro is finer than detail.
The base share prevents disappearing clumps from regaining a high history weight in mixed media; the
modulated share also reduces history when current clumps dominate the actual extinction. Height fog retains
its base contribution to these fractions when a noise void trips the density cutoff. This is a conservative
approximation, not a measurement of the previous noise phase. Changing the mask, a scale, a contrast or wind
resets the history.

### Samples and cost

| settings | noise fetches per froxel |
|---|---|
| `r_volumetricFogNoise 0` or no noisy medium at the froxel | 0 |
| macro only (default contrasts) | 1, +1 in slices with dynamic lights |
| macro + detail | 2, +2 in slices with dynamic lights |

The medium at the froxel center (the dynamic light term) is now evaluated only in slices that have dynamic lights,
which saves one `FroxelMedium` call per froxel elsewhere with or without noise (the output is identical). Each
fetch is an explicit lod `textureLod` of a 1 MiB texture, plus one `pow` and a table lookup.

**Timings: not measured** (the game has not been run with this change). Procedure: `r_speeds 100`, "Froxel fog
inject", stationary camera on a fog map, 1920x1080. For each quality preset, compare three runs:
`r_volumetricFogNoise 0`; `r_volumetricFogNoise 7`; `r_volumetricFogNoise 7` with
`r_volumetricFogNoiseDetailContrast 1`.

| preset | inject, noise off | macro | macro + detail |
|---|---|---|---|
| low | | | |
| medium | | | |
| high | | | |

### Repetition

Macro period 4096 by default, the same as the default froxel far: across the whole volume one tile is seen at most
once. Beyond that, mip levels 2 and higher have removed most of the contrast of the finer octaves. Within a tile
there are 4 x 4 x 4 coarse cells: features from ~1024 down to ~256 units. The detail field has a period of 900 (a
non integer ratio of 4.55 to the macro), is rotated by 30 degrees around z and offset, so the product has no short
common period and no shared axis. A top down 16384 x 16384 render of macro x detail shows no obvious tiling, while
macro alone at 8192 wide (2048 period, the first default) did: that is why the default is 4096. Along z a tile
is also 4096 high, and a thin height fog slab crosses a single layer.

### Debug views

| view | shows |
|---|---|
| 13 | m(p) at the scene surface, per pixel, finest mip, world space (no froxels): black 0, white 1, yellow to red 1 to 4 |
| 14 | extinction of the froxel at the scene depth, the injection drops the noise (base sigma), heat of 512 units of it on the view 1 scale |
| 15 | as 14 with the noise (modulated sigma) |

Optical depth is view 1 and the final scattering is view 6 or 9. Views 11 and 12 split the media with the noise.
**World space vs camera grid:** set `r_volumetricFogFreeze 1` and move. The frozen volume keeps its froxel
grid, and inside it the pattern of view 15 must stay at the same world place as view 13 (which has no froxels at
all). A pattern that follows the old frustum's cells, or the screen, is a grid artefact.

## Local fog volumes

Small analytic participating media over the BSP fog: a smoke pocket, a steam cloud, a local haze, a dust region.
They are one more medium of `FroxelMedium`, like the height fog: extinctions add, the albedo is the extinction
weighted average, and every light of the froxel fog applies without any change: the baked grid, the sun and its
cascades, the dynamic lights and their shadows, the HG phase, the temporal filter, the integration, the
composite, and the lookups of the transparent surfaces. There is no extra pass and no new texture. Code:
`shared/rd-rend2/tr_fogvolume.cpp`; GLSL in `volumetric_common.glsl` (`FroxelLocalShapeDensity`, list decoding)
and `volumetric_inject.glsl`. Only in mode 2; modes 0 and 1 ignore them.

### Representation

```c
refFogVolume_t (rd-common/tr_types.h, SP and MP)
  id              stable per volume for the temporal filter, 0 = anonymous
  shape           FOGVOLUME_ELLIPSOID (a sphere has equal extents) or FOGVOLUME_BOX (oriented)
  origin, axis[3] transform; axis all zero = world axes (orthonormalized by the renderer)
  extents         radii / half sizes along the axes
  depthForOpaque  as fogParms: distance through full density after which the transmittance is 1.5/255
  color           scattering color 0..1, as fogParms (sRGB, converted like the fog volumes)
  softness        0..1 of the extents over which the density fades to 0
  flags           FOGVOLUME_NOISE: density noise (r_volumetricFogNoise 8)
```

Extinction: `-ln(1.5/255) / depthForOpaque * volumetricFogScale * r_volumetricFogScale`, the unit of the BSP
fog volumes and of the height fog. With equal `depthForOpaque`, the core of a local volume is exactly as dense
as a BSP fog volume.

**Soft boundary.** It is analytic, and it costs three dot products plus one smoothstep per axis. With
`q = R (p - origin) / extents` (unit local space) and `s = softness`:

```
ellipsoid  rho = 1 - smoothstep(1 - s, 1, |q|)
box        rho = prod over the axes of (1 - smoothstep(1 - s, 1, |q_i|))      (also rounds the corners)
sigma(p)   = rho * extinction * fade(view depth)
```

- The density and its slope are both 0 at the boundary, so there is never a hard edge. The CPU harness measured a
  largest step of 1.4e-4 per 1/20000 of the radius, and a slope of 0.004 just inside the boundary.
- `s` is clamped to at least 0.05, and to at least 8 world units along the smallest extent. A thinner edge than a
  froxel would alias.
- `s = 1` fades from the center (a cloud), `s = 0.3` gives a dense core with a soft skin.
- `fade` takes local volumes out between 0.8 x and 1 x the near side of the last slice. The last slice has no list,
  and the analytic tail beyond far does not contain local volumes, so without the fade they would end with a hard
  cut at far. At the default far of 4096 the fade starts around 2800 units.

### Authoring routes

1. **Runtime scene API** (game / FX code). Volumes are added per scene, like a dynamic light, through the optional
   renderer extension `GetRefFogVolumeAPI` (`rd-common/tr_public.h`). It follows the same pattern as
   `GetRefAreaLightAPI` and `GetRefFoliageAPI`: `refexport_t` and `REF_API_VERSION` are unchanged, so an old engine
   loads the new renderer and a renderer without the symbol simply has no volumes.
   - `AddFogVolumeToScene(const refFogVolume_t *)` must be called between `ClearScene` and `RenderScene`, every
     frame.
   - Storage is `backEndData->fogVolumes[256]` with the dlight lifetime: `RE_ClearScene` / `RE_EndScene` advance
     the first index, and `R_InitNextFrame` resets it.
   - No engine or cgame caller exists yet: a cgame trap is future work.
   - Callers should pass an `id` for anything that moves (see Temporal).
2. **Per map, `cubemaps/<map>/env.json`**. This is the existing environment config of rend2, read by
   `R_LoadEnvironmentJson`. A new optional `"FogVolumes"` array sits next to `"Cubemaps"`:
   - It is read on every world map load, whatever `r_cubeMapping` / `r_diffuseIBL` are. `"Cubemaps"` keeps its old
     behavior exactly.
   - A file with only `FogVolumes` no longer prints "no Cubemaps" and falls back to the cubemap entities as before.
   - The BSP format is not changed, and no map needs a new asset.
   - The map volumes are added to every world scene by `R_FogVolumesBeginScene` (`tr_scene.cpp`).

   ```json
   { "FogVolumes": [
       { "Shape": "sphere", "Origin": [x, y, z], "Radius": 96, "Opaque": 250,
         "Color": [0.55, 0.55, 0.58], "Softness": 0.6 },
       { "Shape": "box", "Origin": [x, y, z], "Size": [320, 160, 48], "Angles": [0, 315, 0],
         "Opaque": 600, "Noise": 1 } ] }
   ```

   Keys:
   - `Shape`: sphere | ellipsoid | box
   - `Origin`: required
   - `Radius` or `Size`: `Size` gives the half extents
   - `Angles`: pitch, yaw, roll
   - `Opaque`: default 600
   - `Color`: default 0.75 0.75 0.78
   - `Softness`: default 0.5
   - `Noise`: 0 / 1 / true / false

   Limitation: the file is one per map. 28 SP maps already ship an `env.json` with cubemaps (`assets8_pbr1.pk3`),
   so a pk3 that adds fog volumes to one of those maps must repeat its `"Cubemaps"`.
3. **Debug spawn `r_fogvol`**. Renderer-side volumes near the camera, so the feature can be tested with no asset
   (`sv_cheats 1` for `add` / `test`, cleared on map change):

   ```
   r_fogvol                                    map + r_fogvol volumes, last frame statistics, warnings
   r_fogvol add [sphere|ellipsoid|box] [radius r | size x y z] [opaque u] [color r g b] [soft s]
                [angles p y r] [noise 0|1] [at trace|view|eye|x y z] [swing x y z seconds]
   r_fogvol test <count> [spread]              deterministic spheres ahead (timings)
   r_fogvol remove <index> | clear
   r_fogvol slices                             per slice lists of the last frame (GPU indices)
   r_fogvol dump                               the r_fogvol volumes as a "FogVolumes" array for env.json
   ```

   - `at trace` (default): the volume is placed in front of the wall under the crosshair, pulled back by its
     radius. It uses `SV_Trace` in SP and `CM_BoxTrace` in MP.
   - `at view`: 1.5 radii ahead. `at eye`: centered on the camera.
   - `swing`: moves the volume on a sine, for the moving-volume and ghosting tests.
   - Authoring loop: `add`, adjust, then `dump` and paste the result into the map's env.json.

### Culling (`R_FogVolumesBuild`, once per frame for the froxel view)

1. Candidates are the scene's volumes plus the ones that vanished since the previous frame (see Temporal). Each
   gets a bounding sphere: max extent for an ellipsoid, `|extents|` for a box. The sphere of a changed volume
   also covers its previous state.
2. Frustum test: the sphere against the 4 side planes of the view, and depth in `[0, fade end]`. This is the test
   of `R_VolumetricLightRange` for the dynamic lights.
3. Keep the 64 most important candidates: projected tile coverage times optical opacity
   `1 - exp(-sigma * diameter)`. Explicit emission also contributes to importance; containing the camera
   adds a large bonus. Equal scores use a stable volume key as the tie breaker.
4. **XYZ cluster masks.** The projected rectangle of each current/previous union sphere is binned into
   8 x 8 froxel tiles and overlapping Z slices. Two `R32UI` texels per cluster cover all 64 volumes, so
   there is no list overflow. XY tiles widen on drivers with smaller texture-buffer limits. Camera-plane
   intersections conservatively cover the screen; bounds include one froxel of padding.
5. Scattering, emission and the media pass only evaluate the set bits of their cluster. Ellipsoids reject
   the exterior and return full density in the core before taking a square root; boxes skip smoothsteps
   in the core too. Debug view 19 shows the cluster count. `r_fogvol slices` retains CPU-only slice summaries.

### Maximum count and UBO budget

Shape and appearance data remain in `VolumetricFog`. The former GPU slice headers and index pool are
replaced by a 16-byte cluster descriptor, reducing the block from **16 304 B to 13 760 B**, below the
16 384 B guaranteed by GL3.2. The existing static assertion and driver-size check still apply.

Cluster masks use a separate buffer texture, shared by GL3.2 raster and GL4.3 compute, on the injection's
unused `TB_FPLUS_INDICES` unit. At 240 x 135 x 48, 8 x 8 tiles require **195 840 B per frame slot**
(30 x 17 x 48 x 8). No masks are uploaded for an empty local-volume set. CPU diagnostic storage covers
all 64 x 128 possible slice references and has no effect on rendering.

### Temporal

The history clamp works on radiance, so a moving density would otherwise ghost. Each frame's volumes are paired
with the previous frame's uploaded volumes by `id`; an anonymous volume (id 0) is paired by identical
parameters. A volume admitted after culling is new to history even if its stable-ID state is unchanged.

- **Changed** (moved by more than 0.01 units, rotated, resized, or density / softness changed by more than 1%):
  the previous rows, extinction, softness and shape are uploaded (`u_FroxelLocalMotion`, `u_FroxelLocalPrev*`).
- **Appearance changed**: resolved albedo, noise flag, anisotropy or extinction color changed. Bit 32 in
  `localShape.w` marks a history break at the volume, even when density and geometry stay identical.
  Emission has no temporal history and does not set this bit.
- **New**: previous extinction 0.
- **Vanished**: listed for one more frame with current extinction 0 and its previous state.

In the injection, for the jittered sample only:

```
change = sum |sigma_i(p) - sigma_i,prev(p)| / max(sigma_local(p), sigma_local,prev(p))
weight *= 1 - smoothstep(0.02, 0.25, change)
```

- Where a volume really moved, appeared or vanished, the history is dropped in proportion to the change, so no
  smoke ghost follows a fast volume.
- Inside a static volume, or where a moving volume's density did not change, `change = 0` and the history keeps
  its full weight.
- Previous density uses the previous camera projection for the far fade, so camera motion through the
  fade region also reduces history. Local volumes still fade before the last slice: the analytic tail
  supports BSP and height fog, and does not yet support local shape, noise, phase and emission.
- The radiance clamp and the noise wind weight combine with this rejection.
- An anonymous volume that moves is new every frame: it gets no temporal accumulation (more jitter noise), but no
  ghost either. Pass an id.

### Debug

- Views 16 to 19 (see Debug views): local σ only, local vs BSP / height share, bounds (outer and inner shell),
  volumes per XYZ cluster.
- `r_fogvol` shows the counts of the last frame: submitted, invalid, vanished, in view, uploaded, dropped,
  changed, and pool use.
- `r_fogvol slices` prints each slice with its depth range, count and GPU indices, and the id of each GPU index.

### Asset test (persistent authoring)

- `build/test-assets/zz_volumetric_test.pk3` (389 bytes) contains only `cubemaps/hoth2/env.json`. There is no
  BSP, no texture and no shader. The source is next to it in `build/test-assets/zz_volumetric_test/`.
- Why `hoth2`: it is a stock map with a sun and a global fog, and it has no `env.json` in the installed packs.
  `t2_port` has fog brushes but comes from a mod pack, and every other map with fog brushes already has an
  `env.json`.
- To test a local volume crossing a BSP fog volume boundary, use `r_fogvol` on `kor1` / `t2_rogue` / `vjun2`.
  No asset is needed.
- The three volumes were placed offline from the BSP: spawn `-2312 9856 1041`, yaw 315. The points were checked
  to be in empty leaves, with open space (about 1.7k units up to the sky above the spawn). The terrain is a patch,
  so the exact ground height was not verified.

| # | medium | shape | place |
|---|---|---|---|
| 0 | smoke pocket | sphere r 96, opaque 250, soft 0.6 | 300 ahead of the spawn, 80 above the feet |
| 1 | ground haze / steam slab | box 320 x 160 x 48, yaw 315, opaque 600, soft 0.8 | 700 ahead, just above the ground |
| 2 | sunlit cloud | ellipsoid 420 x 300 x 160, opaque 900, soft 0.9, noise | 1400 ahead, 450 up |

Install: copy the pk3 to `base/`, then `r_volumetricFog 2`, `vid_restart`, `map hoth2`, and run `r_fogvol` (it
should report `map volumes: 3`).

### Timings

Not measured: the game has not been run with this change. Procedure: `r_speeds 100`, "Froxel fog inject",
1920x1080, a fog map, a stationary camera, `r_fogvol clear`, then `r_fogvol test N` with the camera kept still.

| preset | 0 volumes | 1 | 8 | 32 | 64 |
|---|---|---|---|---|---|
| low | | | | | |
| medium | | | | | |
| high | | | | | |

Expected shape:
- No local volumes: one uniform branch per `FroxelMedium` call.
- Per listed volume and froxel: three dot products, a `sqrt` or three smoothsteps, a second evaluation for a
  changed volume. Only volumes listed for the slice are evaluated.
- More froxels become non-empty, so more of them take the light path (sun cascades, grid, dlights).
- CPU: one sort and at most 64 x 128 sphere / slice tests per frame.

## FX particle media (`r_volumetricParticles`, `tr_volparticle.cpp`)

Selected FX particles (smoke, steam, gas) add real density to the froxel medium. Once in `FroxelMedium` they get
every light of the froxel fog with no particle lighting code: the baked grid, the sun and its cascades, dynamic
lights (saber, blaster colors) with their point shadows, HG phase, temporal filter and integration.

Nothing is automatic. A sprite becomes a medium only when its `.efx` primitive asks for it: sparks, muzzle
flashes, saber effects, additive glows and view-model effects stay sprites. The renderer never guesses smoke from
a shader name.

### Legacy fields that are NOT used

- `RF_VOLUMETRIC` (0x20) keeps its legacy meaning: fake volumetric shading of models (DEMP2), passed to rend2 as
  `u_FXVolumetricBase`. It is not touched.
- The `.efx` key `density` (`CPrimitiveTemplate::mDensity`) is the spacing of the effects spawned by an emitter
  along its path (`CEmitter::Update`, squared distance between `emitFx` spawns). It has nothing to do with media
  and is not reused.
- `mFlags` has no free bit (the 32 bits are rgb / alpha / size / length / size2 group flags and primitive flags),
  and `mSpawnFlags` never reaches the particle. `shaderRGBA`, `shaderTexCoord` and `frame` are not used to carry
  density either.

### EFX syntax

A new sub-group of `Particle` and `OrientedParticle`. Its presence turns the medium on:

```
Particle
{
	...
	volumetricMedia
	{
		extinction	0.04 0.06		// per world unit at the center at full particle alpha, random range
		albedo		0.07 0.07 0.07	// optional scattering color 0..1 (alias "color"); default: the start rgb
		radiusScale	1.0				// optional, proxy radius = sprite radius * radiusScale (default 0.75)
		aspect		1 1 0.8			// optional, ellipsoid scale along world x y z (default 1 1 1, 0.25..4)
		softness	0.7				// optional, soft part of the radius 0..1 (default 0.5)
	}
}
```

- The extinction is multiplied every frame by the particle's alpha fade (the value `UpdateAlpha` computes, 0..1,
  before `useAlpha` or the rgb modulation), so a smoke puff thins out as its sprite fades.
- The proxy radius follows the sprite size (`mRefEnt.radius` after `UpdateSize`). The proxy is a soft ellipsoid
  along the world axes: full density inside `1 - softness`, a smoothstep to 0 at the boundary. The sprite texture
  is not voxelized.
- On another primitive type the group prints a warning and is ignored. Unknown keys in the group print a warning.
- Older engines and renderers print "Unknown group key parsing a particle" and draw the sprite as before.

Parsers: MP `CPrimitiveTemplate::ParseVolumetricMedia` (`codemp/client/FxTemplate.cpp`, `Q_stricmp` chain of
`ParsePrimitive`'s sub-groups), SP the same name (`code/cgame/FxTemplate.cpp`, `ParseGroup` with a
`StringViewIMap` of `ParseVol*` methods). The template fields `mVolMedia`, `mVolExtinction`, `mVolHasAlbedo`,
`mVolAlbedo`, `mVolRadiusScale`, `mVolAspect`, `mVolSoftness` are copied by `operator=`. `CreateEffect` rolls
them into an `SFxVolumetricMedia` for each spawned particle (`CParticle::SetVolumetricMedia`, which also gives the
particle a unique id for the temporal filter).

### Submission to the renderer

```
// rd-common/tr_types.h
typedef struct {
	int			id;				// stable per particle while it lives (temporal filter)
	vec3_t		origin;
	float		radius;			// proxy radius (world units)
	vec3_t		aspect;			// ellipsoid scale along the world axes (1 1 1 = sphere)
	float		extinction;		// per world unit at the center, alpha fade included
	float		color[3];		// scattering color (albedo) 0..1
	float		softness;		// 0..1 of the radius over which the density fades to 0
} refVolParticle_t;

// rd-common/tr_public.h, optional export "GetRefVolParticleAPI"
typedef struct refVolParticleExport_s {
	void		(*AddVolumetricParticleToScene)( const refVolParticle_t *particle );
} refVolParticleExport_t;
```

- It is an optional renderer export, like `GetRefFogVolumeAPI` / `GetRefFoliageAPI`. There is no
  `REF_API_VERSION` bump and no refEntity flag; other renderers simply lack the export.
- It has the same lifetime as a dynamic light: `RE_AddVolumetricParticleToScene` (`tr_scene.cpp`) appends to
  `backEndData->volParticles[1024]`, and the scene slice goes to `refdef.volParticles`. NaN origins, radius <= 0,
  extinction <= 0 and anything past 1024 are counted as rejected. With `r_volumetricParticles 0` it returns at once.
- MP: the FX system lives in the client executable. `cl_main.cpp` looks the export up next to
  `GetRefFoliageAPI` (`reVolParticles`), and `SFxHelper::AddVolumetricParticle` (`FxSystem.cpp`) calls it.
- SP: the FX system lives in the game module. It sends the particle through the new cgame trap
  `CG_R_ADDVOLPARTICLE` (appended to `cgameImport_t`, `cgi_R_AddVolumetricParticle`), and the engine forwards it
  to the export (`cl_cgame.cpp`). The game module calls the trap only while the mirrored cvar `r_volumetricParticles` is
  set, because older engines lack the trap. This is the pattern of `r_foliageInteraction` / `r_ltcSaberAreaLights`.
- `CParticle::Draw` / `COrientedParticle::Draw` submit the medium after the sprite. When the sprite is culled
  (center behind the camera, or closer than `fx_nearCull` / 16 units), `Update` still computes size and alpha and
  submits the medium, so smoke around the camera keeps fogging the view. Particles with `depthHack` (first person)
  or `playerView` (2D) never submit.

### Culling (`R_VolParticlesBuild`, once per frame for the froxel view)

1. Pairing with the previous frame by id: the previous array is sorted by id, found by binary search, and
   duplicate ids pair one to one. A changed particle's bounding sphere is the union of its old and new spheres.
   Particles that vanished get one more frame with extinction 0 (as local fog volumes do).
2. Frustum: the sphere is tested against the four side planes and the depth range 0 .. fade end (the start of the
   last slice). Particles fade out over the last 20% like the local volumes, because the tail cannot carry them.
3. Importance `max(extinction, previous) * r^2 / max(depth, near)^2` (projected optical footprint). A
   **deterministic** sort (importance, then id, then live before vanished) keeps the first `r_volumetricParticlesMax`
   (default 128, hard cap `MAX_GPU_VOL_PARTICLES` 128). The rest count as capped. The same input in any order
   gives the same selection (checked by the harness).
4. Per slice lists (near to far, 16 bit indices, pool `VOL_PARTICLE_POOL` 2560). When the pool is full, the far
   slices lose their entries first, and the drops are counted. The injection evaluates only the particles of its
   slice, never "all particles in all froxels".
5. `s_vf.frameHeightFog` is also set by particles (transparent surfaces outside BSP fog look the volume up).
   `R_VolParticlesInFrustum` also opens the froxel pass on maps without any fog.

UBO: particles have their own block `VolumetricParticles` (slot 13, 15 888 bytes). The `VolumetricFog` block is
already at 14 048 of the 16 384 bytes GL 3.2 guarantees. Only the injection and debug programs declare it
(`USE_FROXEL_PARTICLES`).

| member | size |
|---|---|
| params: count, fade start, 1 / fade length, history floor | 16 |
| center (xyz, extinction), invExtent (1 / extent xyz, inner), color (albedo, previous extinction), prevCenter (xyz, changed), prevInvExtent: 5 x vec4 x 128 | 10 240 |
| slice headers int[128] | 512 |
| index pool 2560 x 16 bit | 5 120 |

### Temporal (dynamic medium)

The static-fog temporal weight (`r_volumetricFogHistoryWeight` 0.9) would leave a long smoke ghost behind a
moving puff. The injection evaluates each listed particle twice, now and in its previous state, and computes
`particleChange = sum |e - e_prev| / max(sum e, sum e_prev)`, separate from `localChange`. Then:

```
particleKeep = mix(1, r_volumetricParticlesHistory, smoothstep(0.02, 0.25, particleChange))
froxelWeight *= particleKeep
```

- Where the particle density did not change, the history is untouched (a static fog around the smoke keeps its
  full weight).
- Where it changed, the weight drops to `0.9 * 0.3 = 0.27` by default. The ghost decays about 4x per frame, and
  the jittered samples of a drifting puff are still averaged a little (no hard flicker). `r_volumetricParticlesHistory 0`
  drops the history entirely there; `1` treats smoke as static fog.
- Particles never enter `R_VolumetricMediumKey`: smoke does not reset the whole history.

### Debug

- `r_volumetricFogDebug 26`: optical depth of the particle media only (the injection drops the BSP fog, the
  height fog and the local volumes).
- `27`: particle media along the ray, opacity weighted. Red is the history reduction (1 - particleKeep), green is
  the particle share of the medium.
- `28`: proxy ellipsoids of the uploaded particles over the frame: outer shell, inner shell where the soft edge
  starts, one hue per GPU index (0 = most important), dimmed behind the scene. Capped and culled particles are not
  drawn (the block holds only the uploaded ones); their counts are in `r_volparticles`.
- `r_volparticles`: prints the last froxel frame: submitted, rejected, culled, capped, uploaded (changed,
  vanished), pool use / dropped / max per slice, and the CPU build time in microseconds.
- `r_volumetricParticlesDebug 1` (cheat): the same line every 60 frames.
- `r_volumetricFogDebug` is now clamped to 0..28 (it was 0..15, which made views 16 to 25 unreachable).

### Selected assets and the test pk3

All the files come from `assets1.pk3`. It is shared by SP and MP, and the Steam copy has the same files and sizes. Each file
was opened and read before a primitive was chosen:

| file | primitive | why | block |
|---|---|---|---|
| `effects/volumetric/black_smoke.efx` | its only Particle (`gfx/misc/black_smoke`, useAlpha, size 4-10 -> 12-24, alpha 0.6 -> 0) | dark smoke | extinction 0.04-0.06, albedo 0.07, radiusScale 1, softness 0.7 |
| `effects/volumetric/droid_smoke.efx` | its only Particle (damaged droid smoke, same shader, alpha 0.75 -> 0) | dark smoke | extinction 0.03-0.05, albedo 0.1, radiusScale 1, softness 0.7 |
| `effects/rocket/explosion.efx` | only `LingeringSmoke` (`gfx/misc/steam`, size 5-10 -> 35-55, nonlinear fade) | the smoke after the blast | extinction 0.015-0.025, albedo = its rgb, radiusScale 0.9, aspect 1 1 0.8, softness 0.6 |

Not changed:
- In `rocket/explosion`: `OrangeGlow` (fireball), `Dust` (debris), `Light`, `Flash`, `Decal`, `CameraShake`, `Sound`.
- `thermal/explosion` (its `LingeringSmoke` would be a good fourth candidate).
- `noghri_stick/gas_cloud` (it exists in the install; its Particle #1 `gfx/effects/Wcloud` is the gas, and #2
  `fxflare` is sparkle and must stay a sprite).

`build/test-assets/zz_volumetric_media_test.pk3` contains only the three `.efx` files:
- `effects/volumetric/black_smoke.efx` (502 bytes)
- `effects/volumetric/droid_smoke.efx` (464 bytes)
- `effects/rocket/explosion.efx` (2526 bytes)

Each one is the original text byte for byte (CRLF) with the `volumetricMedia` block inserted. There are no
textures and no shaders: the overrides still reference the base assets. The unpacked copy is in
`build/test-assets/zz_volumetric_media_test/`. It is a content proof, not an asset overhaul.

### Timings

Not measured: the game has not been run with this change.

- GPU procedure: `r_speeds 100`, "Froxel fog inject", 1920x1080, a stationary camera, a stream of
  `volumetric/black_smoke` (for example several emitters in a test map).
- CPU procedure: `r_volparticles`, "build N us".

| preset | 0 particles | 16 | 64 | 128 (cap) |
|---|---|---|---|---|
| low | | | | |
| medium | | | | |
| high | | | | |

CPU harness (not the game, Release x64, this machine): 1024 submitted particles, 128 uploaded, pool full. The build
takes 0.3-0.4 ms, dominated by the sort of the visible candidates.

Expected GPU shape:
- Per listed particle and froxel: one ellipsoid distance, one `sqrt`, one smoothstep, and twice that where
  `wantChange` (the jittered sample).
- Smoke makes more froxels non-empty, so more of them take the light path.

## Volumetric emission (glowing media)

A medium can emit radiance by itself: fire, the core of an explosion, plasma haze. The glow is **only seen**,
it lights nothing. Lighting the surroundings stays the job of a dynamic light: a `Light` primitive of the same
efx (the `Flash` of `thermal/explosion.efx`) keeps working unchanged, and emission never creates a dlight.

### Equation

The source term of the integration (per world unit) becomes

```
j_total   = j_scatter + j_emissive
j_scatter = sigma_s * L_in = albedo * sigma_t * (L_baked + L_sun) + albedo * sigma_t * L_dlights
j_emissive(p) = sum over emitters of  E_rgb * d_e * shape(p) * fade
```

- `sigma_t` (extinction) and `d_e` (emissive density) are per world unit, the unit of `depthForOpaque`.
- `E_rgb` is scene linear HDR radiance, the frame buffer before tone mapping. It is used without an sRGB or
  overbright conversion.
- `j` is therefore radiance per world unit, the same unit as the existing source.
- `shape(p)` is the soft density (0..1) of the local volume or particle proxy, and `fade` is the far fade of the
  local lists.
- Density noise is not applied to the emission, so the glow stays stable without history.

Integration (`volumetric_integrate.glsl`), per slice of length `l`:

```
x   = sigma_t * l
phi = (1 - exp(-x)) / x                                           x >= 0.05
phi = 1 - x/2 + x^2/6 - x^3/24 + x^4/120                          x <  0.05
S  += T * j_total * l * phi
T  *= exp(-x)
```

`phi` is the mean transmittance inside the slice, and it tends to 1 as `x` tends to 0:

- There is no division by zero.
- A medium without extinction adds exactly `j * l` per slice, so its emission is not lost.
- `phi <= 1`, so the brightness is bounded.
- Dense emitting smoke saturates to `j / sigma_t`. In coupled mode that is exactly `E_rgb`.

This replaces the former hard branch (`extinction <= 1e-7 -> emission * length`). That branch was exact only at 0,
and in fp32 its cancellation reached about 1e-4 near the threshold. The series is below 0.05 because `1 - exp(-x)`
loses precision in fp32 there. The CPU harness measured a relative error of at most 2.1e-6 over x = 0 and 1e-9..1e3,
with a step of 6e-7 at the switch.

### Coupled vs independent emissive density

| authored | `d_e` | reads as |
|---|---|---|
| no density (default) | `sigma_t` of the emitter | blackbody-like: an opaque core shows `E_rgb`, a thin edge `E * sigma_t * l` |
| `emissiveDensity > 0` | that value | independent of the extinction; with no extinction the thin limit holds everywhere: `E * d_e * l` |

A pure glow (extinction 0) needs an explicit density.

### Where the emission is evaluated

`FroxelEmission` (`volumetric_inject.glsl`) runs at the froxel center, with no jitter. It reads the same
XYZ cluster masks as the medium for local volumes (skipped when `localParams.w`, "some volume emits", is 0). FX particles use their per-slice lists
(only those with an emission slot).

The result goes to the **dynamic volume** (`froxelDynamicImage`, R11G11B10F), which has no history, next to the
dynamic light scattering:

- A fast explosion or fire leaves no after-image.
- A volume or particle that vanishes stops glowing on the same frame. The "vanished" candidate that drops the
  scattering history carries no emission.
- The history clamp of the scattered source (`radiance = rgb / a`) never sees the emission. Without that, a
  glowing froxel with little extinction would widen the clamp range.

The cost of having no history is aliasing at the froxel scale on the soft edges. The emission shapes are smooth
(smoothstep shells), so this is limited.

`r_volumetricEmission` (default 1, 0 = off) is only a global scale. The API is per volume and per particle.

### HDR and bloom

Nothing is special-cased:

- The emission is part of `S`, which the composite adds to the HDR scene.
- Bloom uses the existing highlight policies: the `r_volumetricFogBloom` soft knee on the luminance of `S`
  (`luma - 0.5`, squared), and the scene-linear threshold of `r_bloom 1`.
- A bright core blooms. Dim emissive haze stays below the knee and does not, however large its volume.
- The emission is never written to the glow buffer directly.

### Local volumes

`refFogVolume_t` gains `emissive[3]` and `emissiveDensity`, appended at the end of the struct.

`depthForOpaque <= 0` (no extinction) is now accepted when the volume emits with an explicit density.
Upload: `localEmission[i]` of the VolumetricFog block (rgb = `E * d_e`). The block grows from 14 048 B to
15 072 B, below the 16 384 B GL 3.2 minimum.

- env.json keys: `"Emissive": [r, g, b]` and `"EmissiveDensity": d`. `"Opaque": 0` gives a pure glow.
- Game or FX code, through `GetRefFogVolumeAPI`: fill the two new fields.
- The debug volume, no asset needed:
  - `r_fogvol add sphere radius 96 opaque 0 emit 0.5 0.8 2 0.01` adds a pure glow.
  - `r_fogvol add sphere radius 96 opaque 150 color 0.2 0.2 0.2 emit 4 1.6 0.4` adds dense glowing smoke.
  - `r_fogvol emittest` spawns both, 400 units ahead and side by side.
  - `r_fogvol dump` prints the env.json keys.

### FX particles (efx)

The `volumetricMedia` group of a Particle / OrientedParticle gains three keys:

```
volumetricMedia
{
	extinction		0.004 0.006	// may be 0 with an emissiveDensity: a pure glow
	emissive		6 3 1.2		// scene linear HDR radiance of the opaque medium (0 = none)
	emissiveDensity	0.02		// optional, per world unit; default: the rolled extinction
	emissiveTint	1			// optional: times the sprite's current rgb (fire fading to black)
}
```

The FX code sends `refVolParticle_t.emission[3]`, which is `j` at the center per world unit:

- The glow fades with the sprite. With `emissiveTint 1` that is its current rgb, which already carries the alpha
  fade for additive art, plus the alpha for `useAlpha` art. Without the tint it is the alpha fade.
- A particle is submitted when it has an extinction or an emission.
- `r_volumetricParticlesScale` (a density scale) does not scale the emission.

On the renderer side:

- **Ranking**: the particle ranks by `max(extinction, luminance(j))`.
- **Emission slots**: the 24 most important uploaded emitters get a slot in `emission[24]` of the
  VolumetricParticles block (15 888 B to 16 272 B). The slot is packed into `prevCenter.w` as
  `changed + 2 * (slot + 1)`, and `FroxelParticleChanged` / `FroxelParticleEmissionSlot` decode it.
- **Over the cap**: further emitters keep their medium but do not glow. `r_volparticles` reports them as
  "glows dropped".

Only the chosen primitive is converted. Additive FX in general are **not** turned into volumetric emission.

### Asset proof

`build/test-assets/zz_volumetric_emission_test.pk3` overrides `effects/thermal/explosion.efx` (from `assets1.pk3`).
It is used by the SP thermal detonator (`wp_thermal.cpp`, `cg_weapons.cpp`) and the MP one (`cg_weaponinit.c`).

- **Changed**: only `Particle explosion_cloud` (fire sprites `exp02_2`, `exp02_3`, `effects/fire`) gets
  `volumetricMedia { extinction 0.004 0.006, albedo 0.25 0.22 0.2, radiusScale 0.6, softness 0.6, emissive 6 3 1.2,
  emissiveDensity 0.02, emissiveTint 1 }`.
- **Unchanged, byte for byte**: `LingeringSmoke`, `Dust`, the `Flash` Light (dlight, 250 ms), the sound and the
  decal. The flash still lights the room; the cloud glows during its 0.5-1 s life and fades with its sprite.

Expected magnitude:

- At the proxy center: `j = 6 * 0.02 = 0.12` per unit.
- A proxy of radius about 0.6 * 25-60 gives an optical source of about 2-4 through the middle at full alpha, well
  above the bloom knee.

### Debug views

| view | shows |
|---|---|
| 30 | scattering source only, `sum j_s * l` along the ray (extinction forced to 0 in the integration) |
| 31 | emissive source only, `sum j_e * l` (idem) |
| 32 | combined source `sum (j_s + j_e) * l` (idem) |
| 33 | final integrated emission: only `j_e`, integrated with the real extinction (self absorption in dense smoke) |
| 34 | history contribution: red = luminance of the history part of the scattered source (weight * source), green = luminance of the emission (never from history) |

Views 31, 33 and 34 also drop the light of the tail beyond far, so they show the emitters alone.

### Validation

Done here:
- Builds: rend2 SP and MP, both engines, SP game (FX code), and MP cgame (MSVC Release).
- Offline GLSL on Intel UHD and RTX 2060: 48 x 2 cases, no failures.
- CPU harness for `phi`:
  - error bound (above);
  - pure glow sigma = 0, where `S = j * 192` exactly;
  - sigma of 1e-12..1e-6, which gives a continuous result (the old branch was also continuous but less precise);
  - dense glowing smoke, which converges to `E` exactly with T = 4e-12.
- CPU harness for the packing round trip (24 slots x changed) and the UBO sizes.

The game was **not** launched. Checklist rows "emit: ..." below; timings to fill:

| scene | inject ms (emission off / on) | integrate ms |
|---|---|---|
| `r_fogvol emittest`, hoth2 | | |
| 4 thermal detonators at once | | |

## Per-medium albedo and anisotropy

Each medium scatters with its own albedo and Henyey-Greenstein g. Before this, every medium used the one global
`r_volumetricFogAnisotropy`. The global g stays the **default**: a medium without its own g uses it, so a map without
metadata looks exactly as before. Per-medium values only override it.

### Medium sample (`volumetric_inject.glsl`, `FroxelMediumSample`)

Every medium i at the froxel has an extinction σt_i, an albedo a_i (rgb) and a g_i. The injection accumulates:

```
σt   = Σ σt_i                                   (extinction, the history alpha, unchanged)
S_i  = σt_i · a_i                               (scattering coefficient σs, rgb)
lobe slots k = 1..3, one per distinct g:        S_k = Σ_{g_i = g_k} S_i
j_scatter = Σ_k S_k · L · P(g_k, θ)             per light term (4π-normalised HG: FroxelPhase)
```

- **Exact** (it is Σ_i S_i P(g_i)) as long as at most 3 distinct g overlap in one froxel. Legacy media all share the
  global g and use one slot. A +g / −g overlap uses two slots and keeps both peaks. The default fog, an authored
  smoke and a back-scattering medium use three.
- **Only a 4th distinct g is approximated.** It merges into the slot with the nearest g, and that slot gets the
  scattering-weighted mean cosine `g_k = Σ w_i g_i / Σ w_i` with `w_i = luma(S_i)`. The energy stays exact (the
  4π HG averages 1 for any g) and so does the first moment. Only the lobe shape changes. g is never averaged
  plainly, as `(g1 + g2) / 2`. Debug view 37 shows where this happens (green).
- Why not a single HG lobe with the mixed g: equal +0.8 / −0.8 media average to g = 0 and give an isotropic
  medium, which loses 96% of both peaks. A fog at 0.2 with smoke at 0.8 loses 74% of the forward peak. Fixed
  sign bins (one forward lobe, one backward lobe) have the same problem for same-sign mixes.
- The weight w_i is the luminance of S_i, not a per-channel weight. This only matters for merged slots.
- A black (fully absorptive) medium adds extinction but no lobe.
- The density noise scales σt and S of the noisy media, not their g.

Light terms and energy:

- Lights, shadows, cookies and the light grid are evaluated **once**. Each g only adds its phase: `FroxelPhases`
  returns a `vec4` (3 slots + the global g).
- The **baked baseline stays isotropic** (no phase) for every g, as before. With
  `r_volumetricFogStaticDirectional` the baked moments take the L1 response of each slot's g (clamped to +-1/3);
  the sun takes the full phase of each slot. Dynamic lights take the phase at the
  froxel centre with the slots of the medium there.
- The global g is still used in two places, both documented limits:
  - The **tail beyond far** (`u_FroxelTail`). It stores light that already has its phase, so BSP fog and height
    fog beyond the last slice use the global g even with `fogAnisotropy`.
  - The **sprite particle light field** (`r_particleLighting`). The sprites are not a medium of the volume.
- The albedo semantics are unchanged: the fog colour is the albedo, in the fogParms convention.

### Defaults

| medium | albedo | g |
|---|---|---|
| BSP fog (`fogParms`) | fog colour | `r_volumetricFogAnisotropy` |
| BSP fog with `fogAlbedo` / `fogAnisotropy` | `fogAlbedo` | `fogAnisotropy` |
| height fog (`r_volumetricFogHeight*`) | `r_volumetricFogHeightColor` | `r_volumetricFogAnisotropy` |
| local volume (`refFogVolume_t`, env.json, `r_fogvol`) | `color` | `anisotropy` with `FOGVOLUME_ANISOTROPY`, else global |
| FX medium (`volumetricMedia`) | `albedo` / start rgb | `anisotropy`, else global |

### Authoring

BSP fog shader. `fogParms` is unchanged, and the new keywords are optional, general (not stage) keywords:

```
textures/test/medium_fog
{
	surfaceparm fog
	surfaceparm nonsolid
	surfaceparm trans
	fogParms ( 0.6 0.6 0.65 ) 800
	fogAnisotropy 0.6          // HG g -0.9..0.9, froxel fog only
	fogAlbedo 0.9 0.4 0.3      // froxel scattering albedo 0..1 (same colour space as fogParms), legacy fog unchanged
}
```

Local fog volumes:

- API: `refFogVolume_t.anisotropy` together with `flags |= FOGVOLUME_ANISOTROPY`. A zero-initialised struct keeps
  the global g.
- env.json: `"Anisotropy": 0.6`.
- Console: `r_fogvol add ... aniso 0.6`. `r_fogvol dump` writes it out.

FX media (MP `codemp/client`, SP `code/cgame`):

```
volumetricMedia
{
	extinction	0.02
	albedo		0.3 0.3 0.3
	anisotropy	0.6         // optional, default r_volumetricFogAnisotropy
}
```

`refVolParticle_t.anisotropy` goes with `flags = VOLPARTICLE_ANISOTROPY`. On the GPU, g is packed into
`invExtent.w` together with the inner shell (`round(inner·255) + 0.001 + 0.998·(g+1)/2`). The particle block is at
16272 of 16384 bytes, so there is no room for another array. The round trip error is 1/510 on inner and 1.5e-5 on g.

### Debug views (`r_volumetricFogDebug`)

These are opacity-weighted along the ray. The injection writes value · σt with no light, and the overlay shows the
mean. Dark grey means no medium.

| view | shows |
|---|---|
| 35 | extinction: the opacity of the medium |
| 36 | single scattering albedo Σ S_k / σt, rgb (dark = absorptive) |
| 37 | lobe slots: red = slots in use / 3, green = merged (more than 3 distinct g), blue = 1 − share of the strongest slot |
| 38 | effective mixed g (scattering-weighted mean cosine): red = forward, blue = backward |
| 39 | phase of the sunlight towards the camera, lobe mixture: P / (1 + P), 0.5 grey = isotropic |

Test without assets: `r_fogvol mediumtest` (sv_cheats). It places:

- A, g +0.8 (white), and B, g −0.8 (white), overlapping in front of the camera.
- C, an isotropic, coloured, absorptive medium (albedo 1 0.2 0.2) beside them.

To test a single isotropic, forward or backward medium: `r_fogvol add sphere aniso 0|0.8|-0.8`.

### Validation

Offline:

- **GLSL** (`glslcheck_vf.py`): 48 program variants on Intel UHD and NVIDIA RTX 2060 compile without warnings.
- **Mixing** (numpy, the slot model against the exact per-medium sum Σ S_i P(g_i), in luminance):

| case | max rel. error | single mean-g lobe (for comparison) |
|---|---|---|
| isotropic g = 0, strong +0.8, negative −0.8 | 0 | 0 |
| overlap +0.8 / −0.8 | 0 | 0.96 |
| overlap +0.8 / −0.4, unequal σs | 0 | 0.65 |
| same sign 0.1 / 0.8 | 0 | 0.79 |
| fog 0.2 + smoke 0.8 + −0.5 | 0 | 0.90 |
| 4 distinct g (merge) | 1.5e-3 | 0.86 |
| coloured absorptive (albedo 1 0.2 0.2) | 0 (S = σt·(1, 0.2, 0.2)) | 0 |
| legacy: 3 fogs with the global g | 7e-16 | 7e-16 |

  Energy (⟨P⟩ = 1) and the first moment match the exact sum in every case.

In game: not yet run. Suggested captures: `r_fogvol mediumtest` with views 0, 35–39, looking towards the sun, at
90° to it and away from it. Also a legacy-fog map (for example hoth2) before and after, which should show no
difference.

### Cost

- **NV fragment assembly of the inject program:** 6852 → 9220 instructions (tex 232 → 240, transcendental 234 → 316).
  Most of the increase is inlining: `FroxelAddScattering` has 4 call sites, and `FroxelMedium` is called at the
  jittered point and at the froxel centre. The noise lookup is inlined at its 3 lazy call sites but still
  evaluated once at runtime.
- **Runtime per froxel:**
  - per medium: a search over ≤ 3 slots;
  - per light term (sun, directed baked, each dynamic light): 3 more HG evaluations (the `vec4` of phases);
  - shadow taps, texture fetches and light lists are unchanged.
- **UBO:** the VolumetricFog block grows by 24 vec4 (15072 → 15456 bytes). The particle block does not grow.

## Sprite particle lighting (`r_particleLighting`)

Ordinary FX sprites, drawn by `generic.glsl` with their authored vertex colour, are lit by the local light. This works
without any medium. It is separate from the FX particle media above: a sprite doesn't need a `volumetricMedia` block,
and a `volumetricMedia` particle is also lit this way.

### Why the froxel volumes can't be reused

- `froxelInjectImage.rgb` is σ·albedo·(baked + sun) and `froxelDynamicImage` is σ·albedo·dynamic.
- In empty air σ = 0, so both are 0.
- The plain incident light L_in only exists inside the inject shader (`staticLight`, `sunLight`, `dynamicLight`).

### Representation: the particle light field (option A)

- The inject pass gets a third layered attachment, `froxelParticleLightImage`. It is R11G11B10F at the full froxel grid
  (W×H×D; layered MRT uses one layer count).
- It is written as `out_SSRNormal`, because outputs bind by name; `#define out_ParticleLight`.
- Contents: baked + sun + dynamic L_in at the **un-jittered froxel centre**, before σ and albedo.
  - It is written in every froxel, with or without a medium.
  - It is this frame only, with no history, so saber trails can't appear.
- Baked + sun use the full 4-tap CSM filter (temporal 0) at the centre. They are reused from the fog evaluation when
  that already ran at the centre (medium present and `r_volumetricFogTemporal 0`).
- Dynamic lights come from the same cluster lists (`FroxelLightCluster`, `DynamicLights`). They are evaluated even
  where `mediumCenter` is empty.
- The shadows are the same CSM and point-cube taps, the attenuation is the same, and the scales are the same
  (`r_volumetricFog{Static,Sun,Dlight}Scale`). Smoke gets no light through a wall where the fog gets none.
- **Phase:** the sun and dlight terms carry the fog's HG phase towards the camera (g = `r_volumetricFogAnisotropy`).
  - That is the radiance a camera-facing billboard scatters to this camera, so it is correct for sprites seen from the
    main view.
  - It is not valid for other views. Reflections, portals and refraction fills have froxel mode 0 and draw sprites
    unlit.
- **Why not option B** (lighting in the sprite shader): it would need the static grid, CSM taps and a clustered
  point-shadow loop (4 cube taps × up to 32 lights) per smoke fragment, with 10-30× overdraw near explosions.
  `generic.glsl` has none of that plumbing. The field reuses the inject evaluation and costs the sprite one 3D fetch.

### Material response and classification

`RB_ParticleLightClass` (`tr_volumetric.cpp`) uses the shader state only, never names.

| condition | class |
|---|---|
| entity not `RT_SPRITE` / `RT_ORIENTED_QUAD`, or `RF_VOLUMETRIC` / `RF_FIRST_PERSON` | none (untouched) |
| shader `particleLighting off` | unlit |
| stage `glow`, or `rgbGen lightingDiffuse[Entity]` | unlit |
| shader `particleLighting on` | lit |
| `blendFunc GL_SRC_ALPHA` or `GL_ONE` / `GL_ONE_MINUS_SRC_ALPHA` | **lit** (smoke, dust: colour is a reflectance) |
| anything else: additive `GL_ONE` / `GL_SRC_ALPHA` over `GL_ONE`, modulate, opaque | unlit (emitters and filters) |

Additive sprites (muzzle flashes, sparks, fireballs, glows) are never multiplied by the light, so they can't turn grey
in the dark.

- Lit stages use the generic fog permutation (`GENERICDEF_USE_FOG`), which adds no new permutations. The fog mode stays
  2 (none) unless the stage is really fogged.
- `generic.glsl` multiplies the colour before the froxel fog:
  `color.rgb *= mix(1, clamp(L * gain, floor, 4), mix)`, with
  `gain = r_particleLightingScale / (mapAverage * r_volumetricFogStaticScale)`.
  - `mapAverage` is the mean luminance of the valid light grid cells (`world_t::particleLightReference`).
  - An average place of the map keeps the authored colour. A dark room darkens smoke down to `r_particleLightingFloor`,
    and a saber or a sunbeam brightens it.
- **Fog is applied once.** The particle light is incident light at the sprite. The fog between the sprite and the
  camera (S, T of `FroxelFog`) is applied afterwards by the existing code, and the field is never multiplied by T.

Stock assets (`classify.py` over assets0-3; `LIT` = automatic):

| effect | shader | class |
|---|---|---|
| `volumetric/black_smoke`, `black_smoke2`, `droid_smoke` | `gfx/misc/black_smoke[2]`, `gfx/effects/alpha_smoke[2]` | lit |
| `repeater/muzzle_smoke` | `gfx/misc/black_smoke` / `gfx/effects/whiteflare` | lit / unlit |
| `chunks/dustfall`, explosion `Dust` | `gfx/effects/alpha_smoke`, `gfx/misc/dotfill_a` | lit |
| `droidexplosion1` LingeringSmoke | `gfx/effects/alpha_smoke` | lit |
| `rocket/explosion`, `explosion1`, `thermal/explosion` LingeringSmoke | `gfx/misc/steam` (GL_ONE GL_ONE) | unlit |
| `bespin/dust`, `env/impact_dust`, `slide_dust` puffs | `gfx/misc/dust`, `gfx/misc/steam` (additive) | unlit |
| fireballs, flashes, sparks | `gfx/exp/*`, `gfx/misc/exp0*` (glow), `whiteflash`, `spark*`, `saberflare` | unlit |
| tails, lines, decals | not sprites | none |

No asset changes were needed: the blend state never classifies an additive sprite as lit. The additive "smoke"
puffs (`gfx/misc/steam`, `gfx/misc/dust`) are drawn as emitters by the original art. They stay unlit unless a shader
override adds `particleLighting on`, which could go in the task-#5 test pk3. The shared stock shader was not changed.

### Cvars

| cvar | default | |
|---|---|---|
| `r_particleLighting` | 0 | latched (`vid_restart`): creates the field. Needs `r_volumetricFog 2`. The froxel build then also runs on maps without any fog medium. |
| `r_particleLightingMix` | 1 | 0 = authored colour (lit/unlit A/B toggle without a restart), 1 = lit |
| `r_particleLightingScale` | 1 | gain relative to the map average light |
| `r_particleLightingFloor` | 0.03 | minimum light factor |
| `r_particleLightingDebug` | 0 | 1 field (just in front of the scene), 2 baked only, 3 sun only, 4 dynamic lights only (2-4 also change what sprites receive), 5 classification: magenta = lit, cyan = unlit sprites |

### Cost

- **Memory:** one R11G11B10F volume, 4 B per froxel:

  | quality | grid | memory |
  |---|---|---|
  | high (1080p) | 240×135×64 | 8.3 MB |
  | medium | 240×135×48 | 6.2 MB |
  | low | 120×68×32 | 1.0 MB |

- **Extra light evaluations per froxel** in the inject pass:
  - Baked + sun at the centre: 4 grid fetches + 4 CSM taps. This is skipped when the fog already evaluated them there.
  - Dynamic lights in froxels whose cluster has lights but no medium: the same loop as the fog.
  - Maps without fog media now run the whole froxel pipeline (inject, integrate, composite of a transparent
    volume).
- **Per lit sprite fragment:** 1 trilinear 3D fetch plus the froxel UV math.

### Timings

Not measured: the game has not been run with this change. Measure with `r_speeds 100`, reading "Froxel fog inject"
with `r_particleLighting 0` / `1` (`vid_restart` between), on a map without fog and on one with fog.

| map | inject off | inject on | total frame off / on |
|---|---|---|---|
| no fog (e.g. yavin1b) | - | | |
| fog (e.g. hoth2) | | | |

## Integration (`volumetric_integrate.glsl`)

Front to back over the slices of every froxel column, with the medium constant inside a slice:

```
length = (B(k+1) - B(k)) * |ray|                     // path length along the froxel's ray
x      = extinction * length
Ts     = exp(-x)
S     += T * j * length * phi(x)                     // phi = (1 - Ts) / x, series below 0.05 (-> 1 at 0)
T     *= Ts
```

`j` is the whole source: scattering plus emission (see "Volumetric emission").

This is the discretisation of the legacy ray march (`color += light * T * (1 - exp(-z))`), so extinction is in
the same units as `depthToOpaque` and fog maps keep their density. Checked numerically against the exact
homogeneous solution: the largest absolute error of S or T after the trilinear lookup is 0.0006 (depthForOpaque
256 to 4096, distances 20 to 3000).

## Temporal reprojection

- Every froxel center is projected with the froxel camera of the history volume (the main view projection
  without the SMAA T2x jitter; kept on the CPU, independent of the velocity buffer). Its view depth gives the
  history slice. Outside the previous volume (disocclusion at the frustum edges): no history.
- `current = mix(current, history, weight)`, `weight = r_volumetricFogHistoryWeight` (0.9).
- The radiance of the history (emission / extinction) is clamped to `[current / 4, current * 4]`, so light that
  changed (moving entity shadows, switched lights) does not ghost for long.
- **Dynamic lights have no history** (their own volume), so moving sabers, blaster bolts and explosions cannot
  leave trails. They are sampled at the froxel center with a fixed shadow pattern, so they need none.
- **History reset**: map change, no volume in the previous frame, camera move over 256 units, rotation over 75
  degrees, FOV change over 15%, near / far / debug view change, `r_volumetricFogReset 1` (game code, cleared by the
  renderer), the motion blur cut detection (`tr.temporalHistoryValid`, when `r_motionBlur` is on), and any change
  of the medium key (`R_VolumetricMediumKey`: fog scales, every height fog and noise setting including the wind,
  anisotropy, sun / static scale). The radiance clamp cannot repair a history built with another extinction, and
  wind changes reset history even though the integrated noise phase remains continuous.
- "A volume in the previous frame" means one the GPU passes actually wrote (`RB_VolumetricBuild` records the
  frame and the image), not only one the constants planned: a skipped build (no draw surfaces, the view not on
  `renderFbo`, ...) must not turn a never written image into the history. The volumes are cleared at creation,
  and the inject pass drops a NaN / Inf history and never writes one, so a bad froxel cannot be fed back.
- **Draw buffers 2-4 are color masked by default when SSR is on** (`GL_ResetSSRAuxWrite` after every
  `qglColorMask`, the SSR material attachments of `renderFbo`). Any froxel pass that writes attachment 2 or
  higher, or clears it, must enable it with `GL_SetSSRAuxWrite(true)` and restore it. The integrate pass used to
  write the tail there (it is now written by the injection's tail pass to attachment 0): without it the tail was
  never written, which on a first map is zeroed memory (no fog beyond
  far) and after a map change (the GL context is kept) recycled VRAM: black blurry froxel squares over the whole
  sky (seen on taspir1), history and light term independent, gone after `vid_restart`.
- Depth discontinuities: the froxel volume is world anchored and defined behind geometry too, so reprojection has
  no depth edges; the screen-space disocclusion case is the frustum edge above.
- Without temporal accumulation (`r_volumetricFogTemporal 0`): no jitter, froxel centers, 4 shadow taps.

## Composition

- **Opaque layers** (`sort <= SS_FOG`: opaque, sky, decals, see-through, banners, fog volume faces): one full
  screen pass after these layers, from the depth buffer: `color * T + S` into the HDR scene, before tone mapping
  (blend `ONE, SRC_ALPHA`, destination alpha kept). The first person view model is moved back from the depth
  hack range; the sky (depth 1) is at `max(zFar, depthForOpaque of the global fog)` like the legacy fog cap.
  Their per-surface fog passes, in-shader fog, the global fog cap and the inverted fog plane passes are skipped
  in the froxel view: the volume contains every fog along the ray, nothing is fogged twice.
- **Long range atmosphere** (`r_atmosphere`, [rend2-atmosphere.md](rend2-atmosphere.md)): its composite runs
  just before this one on the same layers, so a pixel is `S_froxel + T_froxel * (S_atm + T_atm * surface)`. The
  froxel media and tail contain no atmosphere, the atmosphere no froxel media.
- **Transparent layers** (`sort > SS_FOG`) keep the existing mechanism (fog pass with its blend, `u_FogColorMask`
  of generic stages, surface sprites) with `(S, 1 - T)` looked up at the fragment instead of the ray march.
  Additive surfaces are only attenuated, as before.
- **Bloom.** The composite attenuates the glow buffer by T like the legacy fog pass did. `r_volumetricFogBloom`
  (default 0) adds the bright part of the in-scattering (soft knee above 0.5) to the glow buffer, so light beams
  bloom and the dim haze does not. The HDR highpass of `r_dynamicGlowBloom` also sees the fogged scene.

## Cvars

| cvar | default | |
|---|---|---|
| `r_volumetricFog` | 0 | latched. 0 off, 1 legacy light grid ray march, **2 froxel volume** |
| `r_volumetricFogQuality` | 1 | latched. 0 low, 1 medium, 2 high (table above) |
| `r_volumetricFogGridScale` | 0 | latched. Screen pixels per froxel, 0 = preset |
| `r_volumetricFogSlices` | 0 | latched. Depth slices (16..128), 0 = preset |
| `r_volumetricFogFar` | 0 | Distance covered by the slices, 0 = 4096 |
| `r_volumetricFogAnisotropy` | 0.2 | HG g of sun and dynamic light scattering, -0.9..0.9, for every medium without its own g (`fogAnisotropy`, local volume / FX `anisotropy`). The baked light stays isotropic |
| `r_volumetricFogTemporal` | 1 | Temporal accumulation and jitter |
| `r_volumetricFogHistoryWeight` | 0.9 | Weight of the history, 0..0.98 |
| `r_volumetricFogSunScale` | 1 | Sun scattering multiplier (baked and realtime) |
| `r_volumetricFogDlightScale` | 1 | Dynamic light scattering multiplier |
| `r_volumetricFogLightTile` | 8 | froxels per side of a dynamic light tile (4, 8, 16; not archived, A/B with `r_vfogLightStats`) |
| `r_volumetricFogStaticScale` | 1 | Baked light scattering multiplier |
| `r_volumetricFogDlightShadows` | 1 | Dynamic lights use their shadow maps (needs `r_dlightMode 2`) |
| `r_volumetricFogBloom` | 0 | Bright in-scattering added to the glow buffer |
| `r_volumetricEmission` | 1 | scale of the emission of local volumes and FX particle media, 0 = off |
| `r_volumetricFogReset` | 0 | Set by game code on camera cuts, cleared by the renderer |
| `r_volumetricFogDebug` | 0 | cheat, debug views below |
| `r_volumetricFogFreeze` | 0 | cheat, keep the volume and its camera |
| `r_volumetricFogHeight` | 0 | height fog on / off |
| `r_volumetricFogHeightOpaqueDistance` | 3000 | height fog: depthForOpaque at the base height (units) |
| `r_volumetricFogHeightBase` | auto | height fog: manual world z, or lowest floor fallback; map HeightFog.base takes priority |
| `r_volumetricFogHeightFalloff` | 256 | height fog: scale height (density / e per this many units above the base) |
| `r_volumetricFogHeightMaxDensity` | 1 | height fog: maximum density below the base, multiple of the base density |
| `r_volumetricFogHeightTopHeight` | 0 | height fog: soft cutoff height above the base, 0 = none |
| `r_volumetricFogHeightColor` | 0.7 0.75 0.8 | height fog: scattering color (albedo), as fogParms |
| `r_volumetricFogStaticDirectional` | 0 | latched: 0 isotropic baked light, 1 reconstructed per channel moments (L1 phase), 2 raw light grid direction moments (developer A/B); see Directional baked light |
| `r_volumetricFogNoise` | 0 | density noise media mask: 1 height fog, 2 BSP fog volumes, 4 global fog, 8 local fog volumes with the noise flag |
| `r_volumetricFogNoiseScale` | 4096 | macro noise tile period (world units) |
| `r_volumetricFogNoiseContrast` | 1 | macro contrast c, 0..4 (0 = homogeneous) |
| `r_volumetricFogNoiseDetailScale` | 900 | detail noise tile period (world units) |
| `r_volumetricFogNoiseDetailContrast` | 0 | detail contrast, 0 = off (no second fetch) |
| `r_volumetricFogNoiseWind` | 0 0 0 | noise drift, world units per second |
| `r_volumetricParticles` | 0 | FX particles with a `volumetricMedia` block add media (mirrored by the SP game module, so off by default) |
| `r_volumetricParticlesMax` | 128 | most important particles uploaded per frame, 0..128 |
| `r_volumetricParticlesScale` | 1 | extinction multiplier of the particle media |
| `r_volumetricParticlesHistory` | 0.3 | share of the history weight kept where the particle density changed |
| `r_volumetricParticlesDebug` | 0 | cheat, 1 = culling statistics every 60 frames |

The existing `r_volumetricFogScale`, `r_volumetricFogDefaultScale` and the `volumetricFogScale` worldspawn key
scale the extinction in both modes; `r_volumetricFogSamples` only concerns the legacy ray march. Mode 2 needs
`r_depthPrepass 1` (also required by the sun shadows); otherwise the legacy fog is used.

Defaults are conservative: the scales are 1, the anisotropy is mild (0.2) and the baked light is isotropic.
Without sun shadow maps (`r_sunlightMode 0`), without dynamic lights and with `r_volumetricFogAnisotropy 0`,
mode 2 shows the legacy in-scattering of the baked light (static + baked sun == the legacy light map).

## Debug views (`r_volumetricFogDebug`, drawn over the frame)

| view | shows |
|---|---|
| 1 | density: optical depth between camera and scene (heat map, red = 4 or more) |
| 2 | sun in-scattering without shadows |
| 3 | sun in-scattering with shadows |
| 4 | dynamic light in-scattering |
| 5 | baked light in-scattering |
| 6 | in-scattering of all lights |
| 7 | transmittance |
| 8 | temporal history weight, average over the fogged froxels in front of the scene (dark red: no fog) |
| 9 | integrated volume: in-scattering over black, blue where the fog is opaque |
| 10 | froxel slice at the scene depth (heat), froxel grid lines |
| 11 | as 1, fog volumes only (the injection drops the height fog) |
| 12 | as 1, height fog only (the injection drops the fog volumes) |
| 13 | density noise m(p) at the scene surface (see Heterogeneous density) |
| 14 | froxel extinction at the scene depth without the noise |
| 15 | froxel extinction at the scene depth with the noise |
| 16 | as 1, local fog volumes only (the injection drops the BSP fog and the height fog) |
| 17 | share of the fog along the ray: red = local fog volumes, green = BSP / height fog, brightness = opacity |
| 18 | local fog volume bounds over the frame: outer shell (bright rim), inner shell where the soft edge starts (thin rim), one hue per GPU index, dimmed behind the scene |
| 19 | number of local fog volumes in the list of the froxel slice at the scene depth (heat, 8 = red), slice stripes |
| 20 | non-sun baked baseline B only |
| 21 | length of the moments per channel, |M_R|, |M_G|, |M_B| (black with `r_volumetricFogStaticDirectional 0`) |
| 22 | direction of the luminance moment: rgb = dir * 0.5 + 0.5, dimmed by |M| / B and scaled by the luminance of B |
| 23 | baked sun part S only (no realtime sun) |
| 24 | B + S, no phase: must look like view 5 with `r_sunlightMode 0` before the split |
| 25 | 100 * abs(B + S - legacy merged grid): black = exact |
| 26 | as 1, FX particle media only (the injection drops every other medium) |
| 27 | FX particle media along the ray, opacity weighted: red = history reduction where the particle density changed, green = particle share |
| 28 | FX particle proxy bounds over the frame (uploaded particles), one hue per GPU index, dimmed behind the scene |
| 30 | scattering source `sum j_s * l` (extinction forced to 0) |
| 31 | emissive source `sum j_e * l` (extinction forced to 0) |
| 32 | combined source (extinction forced to 0) |
| 33 | emission alone, integrated with the real extinction |
| 34 | history contribution: red = history part of the scattering, green = emission (no history) |
| 35-39 | per-medium albedo and anisotropy: extinction, albedo, lobe slots, mixed g, sun phase (see "Per-medium albedo and anisotropy") |
| 57 | baked light after the L1 phase with the global g, `B + 3 g (M.v)` (as 20: no sun, no dynamic lights) |
| 58 | directional fraction per channel: B * |M_c| / B_c |

Views 2 to 5 keep only that light term in the injection, so the scene behind the overlay also shows it. Changing
the view resets the history. `r_volumetricFogFreeze 1` keeps the froxel volume and its camera: move away to see
the frozen frustum (outside it there is no fog).

GPU timings: `r_speeds 100` lists "Froxel fog inject", "Froxel fog integrate" and "Froxel fog composite" with the
other GPU timed blocks (GL timestamp queries).

## GPU cost

Not measured yet (the renderer has not been run with this mode). Expected shape: injection dominates (one full
screen triangle per slice at froxel resolution; cost grows with the number of fog volumes, the lights of the
slice and the cascade lookups), integration is a few texture fetches per froxel, the composite is one full screen
pass. Maps without fog volumes do no froxel work at all unless another medium is on. Height fog now
evaluates a slice integral (two endpoint exponentials on the uncapped exponential branch, plus the cap
factor; up to three quadrature samples inside the soft top), reusing the mean for dynamic lighting when
there is no temporal jitter. It skips fog lighting for negligible height-only cells. Sprite particle
lighting may still need those cells. The integral improves density stability, but is not free; no timing
gain is claimed without a GPU profile. Profile with `r_speeds 100` on low / medium / high before
changing the presets.

## Validation checklist

Launch with `r_volumetricFog 2` (latched: `vid_restart`), `r_depthPrepass 1`, `r_sunlightMode 2` and
`r_dlightMode 2` for the shadowed paths. Compare with `r_volumetricFog 1` (A/B DLLs `build/ab/*-prevfog.dll`
vs `*-vfog.dll`).

| scene | where | what to look for | useful views |
|---|---|---|---|
| sun through a doorway | hoth2, vjun1, mp/duel9 | beams in fog, no sun behind walls, stable edges | 3, 2 vs 3 |
| bright exterior fog | hoth2, mp/ctf2 | brightness close to mode 1 with `r_volumetricFogAnisotropy 0` | 6, 5 |
| dark corridor with lights | t2_rogue, kor2 (black fog) | baked light like mode 1, dynamic lights visible | 5, 4 |
| saber in fog | any fog map, saber on | colored haze around the blade | 4 |
| moving saber | swing fast | no trail | 4, 8 |
| blaster bolt, explosion | fire at a wall in fog | short local haze, no trail | 4 |
| light behind a wall | dynamic light on the other side | no light leaking through (`r_dlightMode 2`) | 4 |
| camera through a fog boundary | walk into / out of a fog volume | smooth transition, no pop | 1, 7 |
| rapid turn | spin | no smearing; history drops at the frustum edge | 8 |
| teleport / cut | `setviewpos`, cinematics | history reset, no ghost frame | 8 |
| different FOV | `cg_fov 60 / 110`, zoom | same fog density, reset on large jumps | 1, 10 |
| legacy maps | `r_volumetricFog 1` and `0` | identical to before | - |
| height fog, flat outdoor | mp/ffa3, t1_surprise: `r_volumetricFogHeight 1` | haze along the ground, clear sky overhead | 12, 1 |
| height fog, camera above / inside | fly up (noclip), then back down | layer stays in place, no pop crossing the base | 12, 8 |
| height fog, sun rays | outdoor map with doorways, `r_sunlightMode 2` | shafts in the haze | 3 |
| height fog, saber / dlight | saber on inside the haze | colored glow like in fog volumes | 4 |
| height fog + fog volume | map with a fog volume near the ground | both visible, additive | 11, 12, 1 |
| height fog + global fog | map with a global fog | global fog unchanged with `r_volumetricFogHeight 0` | 11 |
| no fog map, defaults | any map without fog | no haze, no froxel timers in `r_speeds 100` | - |
| sun trust load time | hoth2, vjun1, t3_hevil with `developer 1` | "Froxel fog sun trust: ... msec" (fill in) | - |
| tail cost | open map with several fog volumes, looking at the sky | composite GPU time before / after the tail mask | 56 |
| light tile A/B | many sabers / bolts in fog | `r_volumetricFogLightTile 4` vs `8`: inject time, `r_vfogLightStats` | 4 |
| noise, stationary | fog map, `r_volumetricFogNoise 7` | clumps and voids, no crawling | 13, 15, 1 |
| noise, translation / rotation | walk, strafe, turn | the pattern stays in the world (compare with 13) | 15, 13 |
| noise, rapid movement | run and spin fast | no smearing beyond the unnoised fog | 15, 8 |
| noise, freeze | `r_volumetricFogFreeze 1`, move away | view 15 inside the frozen frustum matches view 13 | 15, 13 |
| noise, fog boundary | walk through a noisy BSP fog volume boundary | no pop, the boundary stays sharp | 15, 11 |
| noise, sun shaft | outdoor fog with doorways, `r_sunlightMode 2` | broken beams through clumps, no light change in voids | 3 |
| noise, saber | saber on in noisy fog | blade haze follows the density, no flicker | 4 |
| noise, wind | `r_volumetricFogNoiseWind "64 0 0"` | drift without trails; weight drops in 8 | 15, 8 |
| noise, temporal off / on | `r_volumetricFogTemporal 0 / 1` | same mean density; off = sharper, some aliasing far away | 1, 15 |
| noise, mean | `r_volumetricFogNoiseContrast 0` vs `1` / `3` | similar average fog (view 1 far away) | 1 |
| noise, repetition | open outdoor fog, look far | no visible tiling at typical distances | 15, 6 |
| noise timings | see Samples and cost | fill the table | - |
| local: sphere in an empty room | any map without fog, `r_fogvol add sphere radius 96 opaque 300` | soft round puff lit by the grid / sun, no hard edge | 16, 18, 6 |
| local: overlapping spheres | two `add` at close points | densities add smoothly, no seam where they overlap | 16, 19 |
| local: crossing BSP fog | kor1 / t2_rogue / vjun2, volume across a fog brush boundary | both media visible and additive, no pop at the boundary | 17, 11, 16 |
| local: moving volume | `r_fogvol add ... swing 0 300 0 1` | no smoke trail behind it; history weight drops only on its moving edge | 8, 16 |
| local: static volume | stationary camera and volume | full history (view 8 as the BSP fog), no extra shimmer | 8 |
| local: saber inside / outside | saber on inside, then outside the volume | colored glow only where the volume is | 4 |
| local: behind a wall from a dlight | volume on the far side of a wall from a dlight, `r_dlightMode 2` | no light leaks through the wall into the volume | 4 |
| local: sunlit cloud | hoth2 with `zz_volumetric_test.pk3`, `r_sunlightMode 2` | bright on the sun side, shadowed by terrain / walls, beams through it | 3, 6 |
| local: camera through the soft edge | walk into / out of a volume (`at eye` or a big one) | smooth transition, no pop | 1, 7 |
| local: asset | hoth2 + pk3, `r_fogvol` | `map volumes: 3`, bounds in view 18 at the placed spots | 18 |
| local: timings | see Local fog volumes, Timings | fill the table | - |
| fx: black smoke | `r_volumetricParticles 1`, `zz_volumetric_media_test.pk3`, a map spawning `volumetric/black_smoke` (or `playfx`-style test) | dark soft puffs that shadow / absorb the light behind them; sprite still drawn | 26, 28, 1 |
| fx: droid smoke | damage a droid (R2 / R5 / mouse) until it smokes | smoke trail with volume, follows the droid | 26, 28 |
| fx: rocket smoke | fire a rocket at a wall | only the lingering smoke has volume; fireball, dust, flash unchanged | 26, 28 |
| fx: saber through smoke | saber on, swing through the smoke | colored glow inside the smoke only | 4, 6 |
| fx: moving source | smoking droid walking / rocket smoke drifting | no long ghost behind it, no strong flicker | 27, 8 |
| fx: behind a wall from a dlight | smoke on the far side of a wall from a point light, `r_dlightMode 2` | no light leak through the wall | 4 |
| fx: stress | many emitters (several explosions at once) | `r_volparticles`: capped > 0, uploaded 128, no hitch; far slices drop first | 28 |
| fx: temporal off / on | `r_volumetricFogTemporal 0 / 1`, `r_volumetricParticlesHistory 0 / 0.3 / 1` | off: noisier, no ghost; 1: visible trail | 27, 8 |
| fx: camera inside smoke | walk into a smoke column | fog stays (medium submitted while the sprite is culled) | 1 |
| fx: legacy RF_VOLUMETRIC | DEMP2 shot / charged impact | unchanged fake volumetric shading of the model | - |
| fx: off | `r_volumetricParticles 0` | exactly the previous look; `r_volparticles` shows 0 submitted | - |
| fx: timings | see FX particle media, Timings | fill the table | - |
| plight: dark room | `r_volumetricFog 2`, `r_particleLighting 1` + `vid_restart`, droid smoke in an unlit corridor | smoke darker than with `r_particleLightingMix 0`, not black | pl 1, 2 |
| plight: saber beside smoke | ignite a saber next to `volumetric/black_smoke` | smoke takes the blade colour on the near side | pl 4 |
| plight: red / blue saber sweep | swing red then blue through the smoke | colour follows the blade, no trail (no history) | pl 4 |
| plight: light behind a wall | dlight on the far side of a wall, `r_dlightMode 2` | no light on smoke across the wall | pl 4 |
| plight: sunlit smoke | outdoor map with sun, smoke half in shadow | lit side bright, shadowed side dark | pl 3 |
| plight: additive sparks | sparks / muzzle flash next to smoke | sparks unchanged in the dark, cyan in pl 5; smoke magenta | pl 5 |
| plight: volumetric off / on | `r_volumetricFog 1` vs `2` | mode 1: authored sprites; mode 2: lit sprites, fog applied once | - |
| plight: lit / unlit | `r_particleLightingMix 0 / 1` | only alpha-blended sprites change | pl 5 |
| plight: timings | see Sprite particle lighting, Timings | fill the table | - |
| emit: pure glow | `r_fogvol emittest`, left sphere (opaque 0) | blue glow, no black, no NaN squares, T unchanged behind it | 31, 7 |
| emit: dense smoke | `r_fogvol emittest`, right sphere | orange core about the emissive radiance, darker smoky rim, scene hidden behind | 33 vs 31 |
| emit: fire + dlight | thermal detonator with the test pk3, `r_volumetricParticles 1` | cloud glows, room still lit by the Flash dlight only | 31, 4 |
| emit: explosion | several detonators | glow follows the sprites, `r_volparticles` shows the emissive count | 33 |
| emit: bloom | `r_volumetricFogBloom 0 / 1`, `r_bloom 0 / 1` | only the bright core blooms, dim glow haze does not | - |
| emit: rapid disappear | `r_fogvol clear`, end of the explosion | glow gone on the same frame, no after-image | 34 |
| emit: off | `r_volumetricEmission 0` | exactly the scattering-only look | 32 vs 30 |

## Media self-shadowing (`r_volumetricSelfShadow`)

Dense media dim the direct light inside themselves: the light reaching a froxel is multiplied by
`T_light = exp(-∫σt ds)` along the ray towards the light, on top of the geometry shadow (cascades for the sun,
shadow cubes / spot maps for dynamic lights). The geometry shadows are unchanged and are still applied separately.

### Pass architecture

Before this change the injection computed the medium and its lighting in one fragment and wrote
`froxelInject = (scatter · L, σt)` after the temporal blend. A froxel could not see the density of the other
froxels in the same frame, so a light ray march was impossible without a feedback loop.

```
before:  inject (medium + light + history) -> integrate -> composite
after:   media (σt, this frame) -> inject (medium + light + light-ray march in the media + history)
                                -> integrate -> composite
```

1. **Media pass** ("Froxel fog media" timer). This is the injection program in media mode (`u_ParticleLight.z = 1`),
   drawn as one layered instanced draw with every slice. It evaluates `FroxelMedium` at the froxel centers: the BSP
   fogs, height fog and noise, local volumes and volumetric FX particles. It uses no jitter and no history, and
   writes `froxelMediaImage`.
2. **Injection** samples `froxelMediaImage` (`u_FroxelMedia`, unit 13) along the light rays. It writes different
   images, so there is no read/write feedback. It still evaluates `FroxelMedium` itself for its own lobes and albedo.
   The texture is used only for the light rays.
3. Integration and composite are unchanged.

With `r_volumetricSelfShadow 0` the image, the FBO and the media pass do not exist, and the injection takes one
uniform branch.

### Current vs. history density

The light rays read the current frame's extinction only, so moving smoke shadows where it is now. The temporal
accumulation still applies to the injected radiance, and the existing radiance clamp absorbs shadow changes. The
extinction that the integration uses (alpha of `froxelInject`) is still blended with history, as before. That is
unchanged and out of scope here.

### Sun

`FroxelMediaOpticalDepth` marches from the jittered froxel sample towards the sun, over
`r_volumetricSelfShadowDistance` (default 768 units):

- `r_volumetricSelfShadowSamples` samples (default 6, range 2–16). Interval edges are `len · (i/N)²`, dense near
  the froxel where the shadow detail is. There is one sample per interval, at a jittered position inside it.
- The jitter is interleaved gradient noise per froxel, rotated each frame. With temporal accumulation off it sits
  at the interval center. The history averages the offsets.
- Density is fetched with trilinear filtering from the R16F volume.
- The march runs only where the froxel has a medium lit by the sun, or for the sprite particle light field.

The transmittance multiplies the sun term only (realtime and the trusted baked sun part). The isotropic and
directed baked light are not attenuated: their direction is not a single ray. The sprite particle light field
(`r_particleLighting`) gets the same sun attenuation, so sprites inside smoke darken too.

**Sample count cost.** At quality 1 and 1080p (240×135×48 ≈ 1.56 M froxels) and 6 samples, the worst case is
≈ 9.3 M trilinear fetches per frame. Only froxels with a medium pay. The default must be chosen by measurement
(see the timing table).

### Frustum limitation

The froxel volume is camera relative. It knows nothing about media outside the view frustum or beyond the far
distance. When a light ray leaves the volume (side planes, far plane, behind the camera):

- The march stops. The unknown media there count as **empty**. A smoke column just outside the screen edge
  therefore casts no media shadow into the view, and its shadow appears when it enters the frustum.
- With `r_volumetricSelfShadowOutsideHeightFog 1` (default), the height fog is added analytically from the point where the
  march ended to the end of the ray (`FroxelHeightOpticalDepth`, exact below the soft top, Gauss-Legendre inside
  its fade; the noise is not included). The sun ray is
  treated as 32768 units, and a light ray ends at the light. The height fog is the only medium known everywhere.
  BSP fogs, local volumes and particles beyond the march are not included.
- Media beyond `r_volumetricSelfShadowDistance` on a ray that stays inside the volume are also ignored, except
  the analytic height fog.

Expected artefact: a slight brightening of the self-shadow where rays leave the frustum close to the screen
edges. It is most visible with a low sun pointing sideways out of the view, and in view 41 near the screen border.

### Dynamic lights

`r_volumetricSelfShadow 2` adds the media shadow to the `r_volumetricSelfShadowMaxLights` strongest dynamic lights
(default 2, max 4). `R_VolumetricBuildLightLists` chooses them by
`luminance · radius² / max(distance to camera, radius/4)²` and passes their light buffer indices in
`u_FroxelSelfShadowLights`. They use half the sun samples (at least 3) over `min(distance to light,
r_volumetricSelfShadowDistance)`. The analytic height fog applies up to the light. All other lights keep only
their geometry shadows. Mode 1 is sun only.

### Resources

| resource | format | size (1080p, quality 1: 240×135×48) | quality 2 (64 slices) |
|---|---|---|---|
| `froxelMediaImage` | R16F 3D | 3.1 MB | 4.1 MB |
| `_froxelMedia` FBO | layered, 1 attachment | – | – |
| VolumetricFog block | +2 vec4 (`u_FroxelSelfShadow`, `u_FroxelSelfShadowLights`) | 15 488 bytes | |

### Cvars

| cvar | default | |
|---|---|---|
| `r_volumetricSelfShadow` | 0 | 0 off, 1 sun, 2 sun + strongest lights (latched, vid_restart) |
| `r_volumetricSelfShadowSamples` | 6 | sun samples (lights: half, min 3) |
| `r_volumetricSelfShadowDistance` | 768 | light ray march length (world units) |
| `r_volumetricSelfShadowOutsideHeightFog` | 1 | analytic height fog beyond the march |
| `r_volumetricSelfShadowMaxLights` | 2 | number of self-shadowed dynamic lights (mode 2) |

### Debug views (`r_volumetricFogDebug`)

| view | shows |
|---|---|
| 40 | media density of this frame (media pass) at the surface slice, heat as view 14; purple = self-shadow off |
| 41 | sun ray optical depth / 8, opacity weighted along the view ray |
| 42 | sun media transmittance, opacity weighted |
| 43 | sun light with the geometry shadow only |
| 44 | sun light with the media shadow only |
| 45 | sun light with both (= view 3 with self-shadow on) |

### Validation (in game, not run yet)

No new assets are needed. Use a dense debug local volume (see "Local fog volumes", e.g. an `env.json` FogVolume
or `r_fogvol` with a high density) or the smoke test PK3 from the volumetric FX particle task.

- Dense sphere lit by the sun: in view 42 the sun side is bright and the far side is dark. In view 45 the
  side facing away from the sun is darker than with `r_volumetricSelfShadow 0`.
- Moving smoke: the view 44 shadow follows the puff with no lag (current density).
- Smoke next to a wall: view 43 shows only the wall shadow, view 44 only the smoke, view 45 both.
- Thin legacy BSP fog / thin height haze: T ≈ 1, a subtle change. A very dense BSP fog darkens noticeably; that
  is physically right for that density.
- Frustum boundary: turn the camera past a dense volume and watch the shadow inside the view near the screen
  edge (the expected artefact above).
- Timings, `r_speeds 100`, quality 1:

| samples | Froxel fog media | Froxel fog inject (self-shadow on) | inject (off) |
|---|---|---|---|
| 4 | | | |
| 6 | | | |
| 8 | | | |

## Approximate multiple scattering (`r_volumetricMultiScatter`)

The froxel fog is single scattering. With the media self-shadow on, the inside of dense smoke, steam or a cloud
goes almost black: the medium is lit only by light that crossed its whole optical depth `tau` unscattered.
Real media with a high albedo return most of that light through higher scattering orders. This feature adds a
bounded, documented approximation of those orders. It adds no pass, texture, or history, and it reuses `tau`
from the self-shadow march.

### Reference

Wrenninge, Kulla, Lundqvist, *Oz: The Great and Volumetric* (SIGGRAPH 2013 talk), multiple-scattering
octaves. Hillaire, *Physically Based Sky, Atmosphere and Cloud Rendering in Frostbite* (SIGGRAPH 2016) and the
UE4/5 volumetric clouds use the same model in real time:

    L_ms = sum_{i=0..N} b^i * sigma_s * L * P(c^i g) * exp(-a^i tau),   a <= b

**Physical effect.** Light that reaches a deep point after more scatterings travelled along diffuse paths. In
the model this appears three ways:

- It effectively saw less optical depth (`a^i`).
- It carries less energy per order (`b^i`).
- It has forgotten its direction, so the phase tends to isotropic (`c^i`).

**Where the published model breaks energy conservation.**

- It is not a solution of the RTE.
- At `tau -> 0` every octave still adds `b^i` times the single scattering, which creates energy in thin fog.
- It ignores the albedo.
- It ignores the geometry of the medium: a side-lit slab gets the same boost as a cloud core.
- It is local, so there is no lateral transport and no bleeding into geometry shadow.

### Equations used here (`volumetric_inject.glsl`, `FroxelMultiScatter*`)

For every light term that has a media optical depth `tau`: the sun, and in mode 2 the self-shadowed dynamic
lights.

    thickness = 1 - exp(-sigma_t * l)                       l = r_volumetricMultiScatterLength
    q         = min(b * albedo * thickness, 0.95)           albedo = luma(sum S_k) / sigma_t
    w_i       = q^i * exp(-a^i * tau)                       i = 1..N (r_volumetricMultiScatterOctaves)
    L_fill    = mix(L_geometry, L_unshadowed, fill * thickness)
    ms_k      = L_fill * sum_i w_i * P(c^i g_k)             per phase-lobe slot k and the global g
    ms_k      = min(ms_k, L_unshadowed * max(P(g_k), 1) - ss_k)   (energy guard)
    j        += sum_k S_k * ms_k                            (FroxelScatter, like the single scattering)

- **Thin media are left alone.** Order `i` scales as `(sigma_t * l)^i` when `sigma_t * l << 1`. The energy guard
  also leaves no room where the self-shadow removed nothing: at `tau ~ 0`, `ss = L * P` already reaches the
  bound whenever `P >= 1`. In thin fog the feature is therefore invisible (view 49 ~ 0).
- **Albedo.** Each further scattering keeps only the albedo, so `q` contains it. Black smoke gets no octaves,
  and steam or clouds (albedo ~ 1) get the most.
- **Energy guard.** `q <= 0.95`, so the series stays finite (`sum q^i <= q / (1 - q)`). The clamp is the physical
  bound: in a conservative, source-free medium lit from outside, the radiance inside cannot exceed the incident
  radiance (maximum principle). The octaves only *return* light removed by the media shadow. They never make a
  point brighter than the same medium would be without the shadow at an isotropic-or-better phase, whatever
  the albedo, density, or ray length. In addition, `a <= b` is enforced on the CPU (Hillaire's condition), with
  a console warning.
- **Shadows.** The geometry (cascade or cube) shadow is lightened by at most `fill * thickness`, and `fill` is
  at most 0.5. A thick cloud inside a building's shadow glows faintly, but the shadow stays.
- **Baked / isotropic light.** It gets no octaves. It has no media shadow to recover, and boosting it would be
  exactly the uncontrolled ambient boost this feature avoids.
- **Temporal.** The sun octaves are part of the scattering that enters the history, with the same jitter and
  radiance clamp. The dynamic-light octaves go to the dynamic volume, which has no history. The MS cvars are part
  of the medium key, so changing them resets the history.
- **Sprite particle light field.** `r_particleLighting` receives the global-g octaves too, so sprites inside smoke
  match the smoke.
- **Prerequisite.** Without `r_volumetricSelfShadow` the feature is a no-op (console note once). There is no
  `tau`, so there is no lost energy to return.

### Cvars (artist parameters)

| cvar | default | |
|---|---|---|
| `r_volumetricMultiScatter` | 0 | 0 off, 1 sun, 2 sun + self-shadowed dynamic lights (capped by `r_volumetricSelfShadow`) |
| `r_volumetricMultiScatterOctaves` | 2 | octaves beyond single scattering (1-3) |
| `r_volumetricMultiScatterAttenuation` | 0.25 | `a`: optical depth scale per octave (lower = deeper light), kept `<= b` |
| `r_volumetricMultiScatterContribution` | 0.5 | `b`: energy per octave, times albedo and thickness |
| `r_volumetricMultiScatterPhase` | 0.5 | `c`: anisotropy scale per octave (0 = isotropic octaves) |
| `r_volumetricMultiScatterLength` | 64 | `l`: typical size of a dense medium (world units), sets what counts as "thin" |
| `r_volumetricMultiScatterShadowFill` | 0.25 | how far thick media may lighten a geometry shadow (0-0.5) |

### Debug views (`r_volumetricFogDebug`)

| view | shows |
|---|---|
| 46 | sun single scattering only (with the media shadow) |
| 47 | sun multiple-scattering term only |
| 48 | sun single + multiple scattering |
| 49 | MS ratio `ms / (ss + ms)` of the sun, luma, opacity weighted (0 = no effect) |
| 50 | red = sun optical depth / 8, green = `sigma_t * l` / 8, blue = thickness |

Views 2, 43, and 44 stay single scattering, so they isolate the shadows. Views 3 and 45 include the octaves.

### GPU cost

Every change is in the injection fragment shader, behind uniform branches. Mode 0 costs only the branch.

- NVIDIA offline dump of the full inject variant (noise, particles, shadows): 11 930 → 12 438 fp instructions
  (+4.3%), transcendentals 420 → 457, no new texture fetch.
- At runtime, per medium froxel with sun: N exp and N `FroxelPhases` (4 HG each).
- Per self-shadowed dynamic light (mode 2): the same, plus the march now also runs in geometry shadow when
  `fill > 0`.
- No bandwidth change.

### Failure modes

- `l` is global and is not the real thickness. A huge but thin fog bank is underestimated, and a small but
  very dense puff is overestimated at its rim.
- Side-lit slabs get the same compensation as cores. There is no lateral transport and no light bleeding
  around geometry-shadow edges beyond the `fill` term.
- `tau` is truncated where the self-shadow march leaves the frustum or far distance. MS is correspondingly weaker
  there, and it inherits the self-shadow frustum artefact.
- `q` uses the albedo luminance. A chromatic albedo shifts colour only through `S_k`, not through a per-channel
  `q^i`, so the deep reddening of coloured media is under-represented.
- In a backlit view (`P < 1`), the guard allows the octaves up to the isotropic level. This is right for a
  diffused medium, but it can slightly brighten thin back-scattering media where `tau` is already large.

### Validation (in game, not run yet)

Build (MSVC, SP + MP renderers) and offline GLSL checks (48 cases × Intel/NVIDIA) pass. No assets are needed.
Use the dense test volumes from "Media self-shadowing" (`r_fogvol`, `env.json` FogVolume, `r_fogvol mediumtest`,
or the volumetric-particle smoke). Set `r_volumetricSelfShadow 1` (or 2), then run `vid_restart`.

| case | expect |
|---|---|
| thin height fog / BSP fog | view 49 ~ 0; with the feature toggled, no visible change |
| dense smoke sphere, strong sun | 46: dark core; 47: core and far side filled; 48: soft interior, silhouette unchanged |
| colored saber in smoke (mode 2) | halo inside the smoke extends deeper; the colour is kept |
| cloud in a geometry shadow | 43 vs 48: the shadow stays visible, only slightly lifted inside thick media |
| `fogAlbedo 0.1 0.1 0.1` vs `0.95 0.95 0.95` | dark smoke: view 49 ~ 0; white steam: clear fill |
| albedo ~ 1, `r_fogvol` density ×10, long ray | 48 never brighter than view 2 (unshadowed) looking towards the sun; backlit, at most the isotropic level |

Screenshots per case: views 46 / 47 / 48, plus 3 with `r_volumetricMultiScatter 0` vs 1. Also record timings,
`r_speeds 100`: inject at MS 0 / 1 / 2.

## RGB extinction (`r_volumetricFogRGBExtinction`)

A medium can let the color channels through differently: water absorbs red first, a smoke can
look bluish in front of a white wall and yellowish in transmission. The scalar path stores one
extinction σ and one transmittance T in alpha channels, so this is an opt-in extension.
`r_volumetricFogRGBExtinction 0` (default) is the scalar path: same textures, same equations, no extra
memory. `r_volumetricFogRGBExtinction 1` (latched, `vid_restart`) makes the extinction and the
transmittance per channel. It needs more than 28 texture units; otherwise it warns and uses the
scalar path.

### Material input

Each medium gets an extinction color `c` (relative). It is normalized to a mean of 1, so
`depthForOpaque` / `Opaque` keeps its meaning as the mean opaque distance and
σ = mean(σt.rgb). Black, negative or missing values are neutral (1 1 1).

| Medium | Parameter |
|---|---|
| BSP fog / water (shader) | `fogExtinctionColor <r g b>` (next to `fogAlbedo` / `fogAnisotropy`) |
| Local fog volume | env.json `"Extinction": [r, g, b]`, `r_fogvol add ... extinction r g b`, `refFogVolume_t.extinctionColor` + `FOGVOLUME_EXTINCTION` |
| Height fog | `r_volumetricFogHeightExtinction "r g b"` (default `1 1 1`) |
| FX particle media | always neutral |

Media without these parameters are (σ, σ, σ). Because `c` has a mean of 1, a neutral medium
matches the scalar mode up to fp32 rounding (CPU harness: 7.6e-6). Everything that reads the
scalar σ is unchanged: media self-shadow, multiple scattering, history weights, noise fractions.

`refFogVolume_t.extinctionColor` is added at the end of the struct and only read when the
`FOGVOLUME_EXTINCTION` flag is set. No caller outside the renderer uses the fog volume API yet.

### Representation and memory

| Texture | Format | Content |
|---|---|---|
| `froxelInject[2]` | RGBA16F 3D | rgb = σs.rgb · L (unchanged meaning), a = σ = mean(σt.rgb) |
| `froxelExtinction[2]` (new) | RGBA16F 3D | rgb = σt.rgb, temporally filtered like `froxelInject.a` (same weight, same reprojection); history ping-pong with froxelInject |
| `froxelIntegrated` | RGBA16F 3D | rgb = S.rgb, a = scalar T = exp(−∫σ) (the scalar reference) |
| `froxelTransmittance` (new) | RGBA16F 3D | rgb = T.rgb |
| `froxelCarryT[2]` (new) | RGBA16F 2D | T.rgb between slices |
| `froxelTail` | RGBA16F 2D | unchanged (light without albedo) |

We don't try to fit S.rgb and T.rgb into one RGBA16F texture. We use RGBA16F instead of
R11G11B10F because T near 1 would band with a 6-bit mantissa, and the temporal filter drifts
on σ.

The added memory is 24 bytes per froxel plus the 2D carry. Values below are for 1920×1080:

| Preset | Grid | Added volumes | Added carry |
|---|---|---|---|
| Q0 | 120×68×32 | +6.0 MiB | +0.12 MiB |
| Q1 | 240×135×48 | +35.6 MiB | +0.49 MiB |
| Q2 | 240×135×64 | +47.5 MiB | +0.49 MiB |

Scalar mode adds 0. The VolumetricFog UBO gains a 16-entry extinction palette (256 bytes, total
16 288 bytes, below the 16 KB GL 3.2 minimum). Local volumes reference a palette entry through
`localShape.w = noisy + 2 · index`. Entry 0 is neutral. Past 15 distinct colors per frame, the
farther volumes fall back to neutral, and a developer message is printed.

### Injection

Each medium i has σt_i.rgb = σ_i · c_i and scatters σs_i.rgb = σt_i.rgb · albedo_i.rgb. This is
the phase-lobe accumulation with albedo · c, so `froxelInject.rgb` already holds σs.rgb · L.
σt.rgb goes to the 4th MRT output (`out_SSRSpecular`, attachment 3). The debug views that
integrate value · σ (8, 17, 27, 35-50) write (σ, σ, σ) there, so they stay scalar opacity views.

### Integration (per channel)

```
x.rgb   = σt.rgb · Δ
Ts.rgb  = exp(−x.rgb)
phi.rgb = x < 0.05 ? 1 − x/2 + x²/6 − x³/24 + x⁴/120 : (1 − Ts) / x    (per channel)
S.rgb  += T.rgb · j.rgb · Δ · phi.rgb        j = σs·L + dynamic + emission
T.rgb  *= Ts.rgb
T      *= exp(−σ · Δ)                        (alpha: scalar reference)
```

Each channel uses the small-σ series independently. A glowing channel with no extinction adds
j·Δ, as in the scalar mode.

### Tail

`FroxelTailMediumRGB` integrates the BSP fogs and the height fog beyond far per channel:
τ.rgb = Σ c_i τ_i and scatter.rgb = Σ albedo_i c_i τ_i. Then
S += T · light · scatter · phi(τ) and T *= exp(−τ). The tail is not scalar in RGB mode, so the
horizon color stays continuous. CPU harness results: froxels + tail vs analytic 1e-6, jump at far
2e-7.

### Composition

The scalar composite is one fixed-function blend (ONE, SRC_ALPHA). scene.rgb · T.rgb + S can't
be expressed with a scalar source alpha, and GL 3.2 does not guarantee dual-source blending. So
in RGB mode the composite is drawn twice, in order:

1. `u_FroxelFogMode 3`, blend ZERO, SRC_COLOR, out = T.rgb: color · T, glow · T
2. `u_FroxelFogMode 4`, blend ONE, ONE, out = S (color) and bloom(S) (glow)

The destination alpha stays masked as before. Bloom: the glow buffer is attenuated per channel
and the knee still uses the luma of S.

### Transparent surfaces

- **generic** (in-shader fog): `u_FroxelFogMode 3` = `FroxelFogRGB`. Both `u_FogColorMask`
  formulas are applied with the opacity per channel, 1 − T.rgb, and the emissive (glow) is
  attenuated per channel.
- **surface sprites**: rgb · T.rgb + S, and additive sprites use rgb · T.rgb.
- **fog pass** (a blended overlay on multi-stage surfaces after SS_FOG): two draw items per
  surface, `u_FroxelFogMode 3` (ZERO, SRC_COLOR) at sort stage 14, then 4 (ONE, ONE) at stage
  15. Within a layer every multiply comes before every add. The scalar path already applies the
  fog passes after all stages of the layer, so both modes share that approximation.
- `RB_VolumetricSetupFogDraw` turns lookup mode 1 into 3 and binds `froxelTransmittance`
  (unit 26) whenever RGB mode is on. Callers stay the same.

### Debug views

| View | Shows |
|---|---|
| 51 | σt.rgb at the scene depth, as 1 − exp(−512 σt) per channel (red = absorbs red) |
| 52 | T.rgb from the camera to the scene |
| 53 | Color shift of a white surface, (T.rgb − T_scalar) · 4 + 0.5 (grey = none) |
| 54 | Heat of max \|T.rgb − T_scalar\| (0.25 = red) |
| 55 | Extinction chroma σt.rgb / mean / 3 (grey = neutral) |
| 56 | Transmittance of the analytic tail beyond far (dark blue = scene inside the volume) |

In RGB mode the scalar views (1, 7) show the scalar-reference T. Without `r_volumetricFogRGBExtinction`,
views 51-56 are dark magenta.

### Validation assets

`r_fogvol rgbtest` places four white spheres (same mean opacity, 250 units) in a row: neutral,
red absorbing (3 0.5 0.5, cyan), blue absorbing (0.5 0.5 3, yellow) and mixed (1 2 3). The
middle two overlap.

### Validation

These checks have run:

- Both renderers build with MSVC.
- Offline GLSL (Intel UHD and RTX 2060): volumetric inject / integrate / composite / debug,
  fogpass, generic and surface sprites, with and without `USE_FROXEL_RGB`. 180 cases × 2
  drivers, 0 failures.
- CPU harness (`rgbcheck.py`, fp32 mirror of the shaders):
  - neutral RGB matches scalar
  - homogeneous slab per channel matches analytic Beer-Lambert (T error ≤ 4e-6, S relative
    error ≤ 2.4e-6, σ from 1e-7 to 0.1)
  - pure glow gives S = j·L
  - tail continuity at far

In game (not run yet):

1. `r_volumetricFogRGBExtinction 1`, `vid_restart`. The console should print "RGB extinction".
2. White geometry behind media: `r_fogvol rgbtest` in front of a white wall. The spheres should
   look neutral grey, cyan, yellow and orange-red. Check with debug 52 and 53.
3. Colored light inside: a colored dlight or spot inside the red-absorbing sphere. The scattered
   light should lose red with depth.
4. Overlapping media: the middle two spheres should be greenish where they overlap (red and blue
   are both absorbed). Check debug 55.
5. Transparent surfaces: glass, sprites and fog-pass shaders through the spheres should be
   tinted like the opaque background.
6. Sky and tail: a BSP fog with `fogExtinctionColor` reaching past far. Check debug 56, and that
   the horizon has no color step at the far plane.
7. Bloom: a bright light behind a colored sphere. The bloom should take the transmitted color.
8. Scalar compatibility: compare `r_volumetricFogRGBExtinction 0` and `1` with neutral media. Debug 54
   should be black and the frames identical.
9. Timings (`r_speeds` / GPU timers "Froxel fog inject / integrate / composite"): measure Q0–Q2,
   RGB off vs on. The composite adds a second fullscreen draw. The inject writes one more MRT
   and reads one more history texel. The integrate writes 2 more MRTs and reads 2 more texels
   per froxel.

| Pass (1080p) | Q1 scalar | Q1 RGB | Q2 scalar | Q2 RGB |
|---|---|---|---|---|
| inject | | | | |
| integrate | | | | |
| composite | | | | |

### Limitations

- The media self-shadow and multiple scattering use σ = mean(σt.rgb): light rays toward the sun
  are not colored by the medium.
- FX particle media (efx) have no extinction color.
- There are at most 15 distinct local volume extinction colors per frame.
- Dual-source blending (a single-draw composite) is not used. It would need GL 3.3 /
  `ARB_blend_func_extended`.

## Underwater medium (`r_volumetricWater`)

The water, slime and lava brushes of a map become participating media of the froxel volume.
Nothing is rebaked and no asset changes: the BSP already keeps every brush with its side planes,
and the contents of a brush are the `contentFlags` of its BSP shader.

Off by default. `r_volumetricWater` is a latched class mask: 1 water, 2 slime, 4 lava. Changing it needs
`vid_restart`: SP keeps its compiled programs over map loads, so liquids follow the program set
(decided in `GLSL_LoadGPUShaders`); a later map load only can turn the media off (mask 0), and the
console says when a `vid_restart` is missing. It needs
`r_volumetricFog 2` and 31 texture units (fragment, and compute with `r_gl43`). With the mask 0
every program is unchanged. The preprocessed lightall, composite and integrate sources are
identical to before; inject and debug differ only by the new debug view 59-64 tests.

### Brush extraction (`tr_liquid.cpp`, map load)

`R_LoadLiquidBrushes` runs inside `R_LoadBSP`, next to `R_LoadWeatherZones`, while the lumps are
loaded:

- Only the brushes of the world model (`dmodel_t` 0) are read. Brushes of the inline models
  (`*N`: func_door water, moving platforms) are counted and skipped, because they move and the
  renderer has no transform for them.
- The class comes from the BSP shader of the brush (`dbrush_t.shaderNum`): lava over slime over
  water, as cgame orders its tints.
- Brushes that also have `CONTENTS_FOG` are skipped: they are already a BSP fog volume of the
  froxel fog (murky water such as `textures/bespin/water2` with `fogparms`).
- A brush must have 6..32 sides, and its first six sides must be the axial bounds (q3map sorts
  them first, as `R_LoadFogs` assumes).
- Each brush keeps its bounds and all of its side planes (bevels are harmless). At most 256
  brushes and 4096 planes per map.

The planes go to a static RGBA32F buffer texture (`TB_LIQUIDPLANES` 29), uploaded on map load and
again whenever `tr.world` changes. `r_liquids` lists the brushes with their class and medium, the
skipped ones (gameplay liquid the medium leaves out) with their reason, shader and model, the load
time, the last frame's visible count, GPU cap drops and build time, the memory and the camera
contents; `r_liquids dump` prints the same as JSON. `tools/rend2/liquid_audit.py` prints the same
fields for every BSP without the game (see "Production review").

Stock maps (scanned from the PK3s, then run through the real loader in a CPU harness):

| Map | Liquid brushes | Notes |
|---|---|---|
| **t3_hevil** | 11 water | Main test map: a large lake 368 units deep, side pools, sun |
| yavin1b | 8 water | Outdoor sun, shallow streams |
| vjun1 | 5 water, slime medium | 352 deep, green `water2_water1_vjun1` surface (see "Class and medium") |
| t2_port (mod pk3) | 27 water (+11 water+fog skipped) | Many brushes |
| t2_rancor / yavin1 | 6 / 4 water | |
| kor1, taspir2 | 2 / 8 lava (taspir2: +4 water+fog skipped) | Lava stays off by default |
| vjun2 | 0 (1 water brush in a brush model) | The moving-liquid limitation |

The full per-map audit is in "Production review" below.

### Per frame (`R_LiquidsBuild`, `Liquids` block)

Each scene appends a `Liquids` uniform block (std140, binding slot 14, 1680 B). It stays empty
unless the scene builds the froxel volume. The block holds:

- the visible brushes: bounds against the four frustum sides and the depth range against far,
  those around the camera first, then by distance to their bounds, at most 32;
- per slice, a 32-bit mask of the brushes whose depth range touches the slice, one slice wider
  on each side;
- the three media;
- caustic parameters;
- a fade over the last fifth before far. Liquids are not in the analytic tail; like local fog
  volumes, they end with the volume.

A liquid in the frustum also makes the volume build on maps without any other medium, and
transparent surfaces such as the water surface look the volume up (`frameHeightFog`).

### Medium in the froxels (`FroxelMedium`, `liquid_common.glsl`)

A brush is convex, so its inside is `dot(n, p) <= d` for all its planes. For each froxel,
`LiquidCoverage` clips the froxel's own ray segment, from the slice's near side to its far
side along the jittered ray direction, against every brush in the slice mask:

- The axial slab test comes first, then the remaining planes.
- The intervals of a medium are united, not summed, because the mapper's brushes overlap.
- While no medium has more than two intervals on the segment (nearly every froxel: one brush, or
  the seam of two touching or doubled brushes) the union is computed in registers; only otherwise
  does the sorted hit list (8 entries, the earliest kept) run. The list is dynamically indexed and
  costs scratch memory on some GPUs (Intel UHD: 59 ms -> 2.3 ms for a full 1080p grid with one brush).

The extinction is σ × the covered fraction of the segment. A froxel cut by the water surface
therefore integrates its exact share of water, not a step at the jittered sample point. Slice 0
covers [0, B(1)], so a camera just above or below the surface also gets the partial slice.

Each class is added like every other medium:

- `extinction += e`
- `extinctionRGB += e·c` (RGB mode)
- `FroxelAddScattering(e, albedo·c, g)`

As a result, all of these apply to water without a separate light list: sun and CSM, the split
static grid, Forward+ dlights, sabers, spots, light portals, phase lobes, temporal history,
self-shadow and multiple scattering. Liquids are static, so they never reduce the history
weight. Crossing the surface with the camera is a history cut (see below).

### Sun under a liquid (`LiquidSunTransmittance`, `r_volumetricWaterSunPath`)

Sunlight reaching a point inside a liquid has passed the liquid above it:

- The ray from p towards the sun is clipped against all visible brushes.
- The connected run of liquid that starts at p (gaps up to 1 unit) gives the path length per class.
- The transmittance is `T = exp(-Σ σ_c c_c L_c)`, in RGB in both modes, because light is RGB.
- Air beyond the run is left to the cascade shadow (for example a lake above a cave).

The same function is used in two places:

- **Volume:** inside `BakedAndSunLight` it scales the realtime and the baked sun part. The media
  pass of `r_volumetricSelfShadow` leaves liquids out while the sun path is on; otherwise the sun
  would be attenuated twice.
- **Surfaces:** in lightall (`USE_LIQUID_SUN`, latched `r_volumetricWaterSurfaces`), lit permutations
  with a sun. It scales the direct sun of `r_sunlightMode 2`, and the sunlit part of the lightmap
  modulation of `r_sunlightMode 1` (`mix(ambient, lit, shadow·T)`). The pixel only takes it where
  the draw is in the froxel main view and the point is inside a liquid brush. Other points cost
  one bounds test per visible brush.

Stock water shaders are blended (not `SS_OPAQUE`, no `alphaShadow`), so they are not cascade
shadow casters, and nothing underwater is shadowed by the surface itself.

### Caustics (`r_volumetricWaterCaustics`, light modulation only)

Caustics are only a light modulation; they never change the density.

The source is selected independently by `r_waterCausticsLightMask`: bit 1 is
sunlight, bit 2 dynamic point/spot lights on opaque/model receivers, and bit 4
the directional baked share. The default is 1, preserving the previous
sun-only result. The exact source/receiver integration and surface-driven mode
are documented in `rend2-surface-caustics.md`.

The pattern is a tiling 256² R16F texture (`TB_LIQUIDCAUSTICS` 30), generated once:

- Photons are refracted by a tiling 12-wave height field (integer wave vectors) and splatted
  bilinearly onto the floor.
- The result is blurred once, and peaks are clamped at 6.
- It is normalized to mean 1 (sd 0.64), with box mips.

It is sampled at the point where the sun ray left the water, twice:

- two copies at different scales drift at `r_volumetricWaterCausticSpeed`, and are averaged so
  the mean stays 1;
- `1 + s·(pattern − 1)` with `s = strength · smoothstep(0, focus, depth) / (1 + depth/1024)`.

The average sunlight is therefore unchanged for any strength. It applies to water only, and only
when the sun is above the horizon. The volume samples the pattern at the mip of the froxel size
(strongly blurred); surfaces use the pixel footprint. The default strength is 0.35.

Surface-driven mode replaces that arbitrary pattern with the shared modern
water evaluator and refracted-ray footprint Jacobian. Sun and reconstructed
directional baked light use it in both surface and froxel lighting. Dynamic
point/spot lights use it on opaque/model receivers only; a full Jacobian per
light per froxel is intentionally excluded from the GL 3.2 baseline. Ambient,
diffuse IBL and emissive output are never caustic-modulated.

### Camera contents and the legacy tint

Once per view, `ri.CM_PointContents(vieworg, 0)` gives the camera's liquid class. This is the
existing renderer import, so there is no ABI change. Model 0 is the world model, the same brushes
as the GPU media. It is used for:

- the history cut when the camera crosses a surface;
- debug view 61.

There is no per-froxel CPU query.

cgame keeps its FOV warp. Its full-screen tint (`CG_Draw2DScreenTints`, a 2D `CG_FillRect` drawn
after `RenderScene`) would color the medium a second time, so it is skipped only when all of
these hold:

- `cg_underwaterTint` is 1 (the default; 0 always draws the legacy tint);
- the renderer reports that class in the read-only `r_volumetricWaterActive` (1 water, 2 slime,
  4 lava). The renderer refreshes it every frame and sets it only on change, from `r_volumetricWater`,
  the classes present on the map, the resources, and the runtime `r_drawfog` / `r_depthPrepass`.
  Since the integration review (see "Render classes under water") it also clears the camera's class
  while the camera stands in a liquid the medium does not have (a skipped brush, a water+fog brush),
  so that liquid keeps the legacy tint instead of turning clear;
- the world model contents at the camera have that liquid. Water of a moving brush model keeps
  the tint.

With the renderer cvar at 0 (vanilla renderer, or the feature off), the code path is the same as
before, with no extra collision query. The SP cgame lives in `jagamex86_64.dll`, so deploy it
together with the MP `cgamex86_64.dll`.

### Materials (defaults)

| Class | Extinction / unit | Relative extinction rgb | Albedo rgb | g |
|---|---|---|---|---|
| water | 0.0014 (1/e after ~714 units, fogparms D ~3670) | 2.0 0.75 0.25 | 0.10 0.45 0.75 | 0.75 |
| slime | 0.005 | 1.6 0.5 0.9 | 0.25 0.7 0.2 | 0.4 |
| lava | 0.02 | 0.6 1.2 1.2 | 0.6 0.15 0.02 | 0.3 |

The relative extinction is normalized to mean 1, as for every medium:

- With `r_volumetricFogRGBExtinction 1` it gives the RGB transmittance; in scalar mode the view
  transmittance is grey.
- In-scattering is albedo × relative extinction in both modes: water scatters a desaturated
  green-blue while red is absorbed first.
- The sun path is colored in both modes.

Lava has no emission term (absorption and scattering only). Cvars:
`r_volumetric{Water,Slime,Lava}{Extinction,Color,Albedo,Anisotropy}`.

Units: extinction is per world unit, and the 1/e distance is 1/σ. A fogparms `depthForOpaque` D
(BSP and local fog volumes) converts as σ = −ln(1.5/255)/D = 5.14/D. So water 0.0014 ≈ D 3670,
slime 0.005 ≈ D 1030 (close to the fogparms 1024 of `bespin/water2`), and lava 0.02 ≈ D 257.
With RGB extinction the channels are σ·c, with c normalized to mean 1, so the mean opacity stays σ.
The single-scattering albedo is per channel (σ_s = σ·c·albedo). g is clamped to ±0.9 and goes to
the three phase lobes like any other medium.

### Class and medium (per-body looks)

Each kept brush has two values:

- **Class**: the liquid class of its contents, which is what gameplay sees. It decides whether the
  `r_volumetricWater` mask selects the brush, it sets `r_volumetricWaterActive` and the cgame tint,
  and it drives debug view 61.
- **Medium**: the optics the brush gets, one of the three profiles above. By default it is the
  class, with two exceptions:
  1. An env.json rule (optional) in `cubemaps/<map>/env.json`:
     `"Liquids": [ { "Shader": "textures/common/water_1", "Profile": "slime" }, { "Shader": "textures/h_evil/*", "Profile": "water" } ]`.
     A rule matches the brush shader or the shader of the brush's upward side (the drawn surface).
     A trailing `*` makes it a prefix, and the first match wins. r_waterSurface uses the same
     profile for a matching surface shader, so the surface and the medium below it agree.
  2. Automatic: a water brush whose upward side shader has `CONTENTS_SLIME` takes the slime medium.
     r_waterSurface already reads the surface optics this way. The only stock case is vjun1:
     `caulk_water` brushes under `water2_water1_vjun1` (a green texture with
     `surfaceparm slime water`). Before this change the surface was drawn with slime optics while
     the medium under it was clear blue water.

The GPU stores both values (`u_LiquidMaxs.w = planes + 64·medium + 256·class`). Coverage, unions
and the sun path all work per medium, and the material is filled for every medium a drawn brush
uses. The block layout is unchanged (still three media). r_waterSurface's test for "the volume
holds this liquid" uses the media of the drawn brushes (`R_LiquidMediumSlotMask`).

### Cvars

| Cvar | Default | Meaning |
|---|---|---|
| `r_volumetricWater` | 0 | Latched class mask (1 water, 2 slime, 4 lava) |
| `r_volumetricWaterSurfaces` | 1 | Latched: underwater direct-light liquid path on lightall surfaces |
| `r_volumetricWaterSunPath` | 1 | Exact sun path through the liquid (volume and surfaces) |
| `r_volumetricWaterCaustics` | 0.35 | Caustic strength (0 = off) |
| `r_volumetricWaterCausticScale` | 160 | Pattern period in world units |
| `r_volumetricWaterCausticSpeed` | 0.06 | Drift in periods per second |
| `r_volumetricWaterCausticFocus` | 48 | Depth of full caustic contrast |
| `r_waterCausticsLightMask` | 1 | Latched source bits: 1 sun, 2 dynamic point/spot, 4 directional baked |
| `r_waterCausticsLocalMaxLights` | 1 | Dynamic caustic lights per surface receiver, 0..4 |
| `r_waterCausticsBakedMode` | 0 | 0 off, 1 surface-driven with direction, 2 procedural fallback |
| `r_waterCausticsBakedStrength` | 0.35 | Strength on the directed baked-light share |
| `r_volumetricWaterActive` | (ROM) | Set by the renderer for cgame |
| `cg_underwaterTint` | 1 | cgame: 1 = skip the tint of liquids the renderer draws, 0 = legacy |

### Debug views (`r_volumetricFogDebug`)

- 59: density of the liquids only (the injection drops every other medium).
- 60: liquid brushes over the frame, colored by medium: blue water, green slime, orange lava.
  Edges are bright, and brushes behind the scene are dimmed. vjun1 shows green.
- 61: camera contents: the CPU class (collision) as the screen hue, the GPU brushes at the camera
  in the bottom bar, red stripes where they disagree.
- 62: liquid boundary: froxels along the ray cut by a liquid surface (yellow) over the fully
  covered ones (blue).
- 63: exact transmittance of the liquids alone between the camera and the scene (RGB with
  `r_volumetricFogRGBExtinction`).
- 64: sun under the liquids at the scene surface: transmittance × caustics (grey 0.75 = none,
  dark grey = not in a liquid).
- 65-69: see "Render classes under water" below (segment length, liquid-only S and T, medium
  sigma, fog bypass).
- Also useful: 3 (the sun term, which includes the liquid sun path), 52 (T.rgb).

### Validation (run)

- MSVC: both renderers, `cgamex86_64`, `jagamex86_64`.
- Offline GLSL, Intel UHD and RTX 2060, GL 3.2 core: volumetric inject / debug / composite /
  integrate and lightall (lightmap, grid, vertex, parallax × sun modulate / primary light /
  shadows2 / SSAO), with and without liquids. 104 cases, 0 failures, no new warnings.
- GL 4.3 compute inject and media kernel with liquids, scalar / RGB, both shadow modes:
  - Compiled on Intel and NVIDIA.
  - On NVIDIA the compute injection fails to compile even without liquids. The tail pass gives a
    constant `var_Slice` of −1 to two existing slice-mask lookups. The liquid lookup clamps its
    index; the two existing lookups are a separate fix.
- Preprocessor diff against HEAD with liquids off: see the top of this section.
- CPU harness: the real `tr_liquid.cpp` with a stub header, on 8 stock BSPs:
  - class counts, fog / brush-model skips;
  - the polytope vertices of every brush span exactly its axial bounds;
  - culling and slice masks;
  - caustic mean 1.00000, min 0.33, max 6.0, tiling seam below the inner step.
- GPU probes (Intel, GL 4.3, real shader sources, real t3_hevil / t2_port / yavin1b brushes):
  - `LiquidCoverage` against a double-precision clip and union, 15 440 rays (random, grazing at
    ±0.01 and ±3 units from the surface, vertical through it). Max error 0.003 units, except
    0.059 units for a ray 0.01 below the surface and nearly parallel to it. That is fp32
    cancellation on near-parallel planes, 0.2% of that segment.
  - `LiquidSunTransmittance` on 2 965 sun paths (inside, on the bed, near the surface): T error
    ≤ 1e-3.
  - `FroxelMedium`: the liquid extinction of 512 froxels around the t3_hevil lake surface (22
    cut by it), scalar and RGB. Relative error ≤ 4.4e-4.
- `tools/rend2/test_volumetric_compute.py` (the existing raster / compute regression) passes.

### Validation in game (not run yet)

Use t3_hevil for the lake and yavin1b for sun on shallow water: `r_volumetricFog 2`,
`r_volumetricWater 1`, `vid_restart`, then `r_liquids`.

1. Camera above the surface: no haze in the air, and the lake bed seen through the surface gets
   the water's color with depth (debug 63). The surface itself is not hazed.
2. Camera below: haze in every direction, the surface seen from below, and the sky through it.
3. Crossing quickly: no smear or lag of the history (one cut per crossing). Debug 61 has no red
   stripes, and debug 62 shows the boundary slices near the surface.
4. Saber underwater: its in-scattering is attenuated by the water between it and the camera.
   (The light path from the saber to the froxel is not attenuated; see the limitations.)
5. Sun from outside: shafts under the surface fade with depth and get bluer, with caustics on the
   bed and in the shafts (debug 64, debug 3). Rocks and walls keep their cascade shadows.
6. Legacy tint: gone under water with the feature on; with `cg_underwaterTint 0` or
   `r_volumetricWater 0` it is back. The FOV warp is always kept.
7. Lava / slime (kor1): unchanged with the default mask 1.
8. vjun2 (brush-model water): legacy tint, no medium; also through the drain cutscene.
   taspir2 water+fog pools: BSP fog plus the legacy water tint.
8a. vjun1: green from above (r_waterSurface) and from below (the slime medium). `r_liquids` shows
   "medium slime (top side slime)" for the 5 brushes; debug 60 is green.
8b. SP, LA goggles on while underwater: the legacy tint is back (no volume is built there).
9. Timings: GPU timers "Froxel fog inject" (and media with self-shadow) with liquids off / on in
   the t3_hevil lake, and the lightall cost on the bed (frame time A/B with
   `r_volumetricWaterSurfaces`).

| Measure | Liquids off | Liquids on |
|---|---|---|
| Froxel fog inject (Q1, 1080p) | | |
| Frame, camera in the lake | | |
| Frame, looking at the lake from the shore | | |

Measured on the CPU (harness, Release x64):

- brush load: 0.03–0.14 ms per map;
- per-frame culling: 3–6 µs;
- caustic pattern generation: 30 ms, once per renderer start (on the first map with liquids).

### Production review (2026-10-04)

Two tools back this review:

- `python tools/rend2/liquid_audit.py [--json out.json]` is read-only. It scans the 59 BSPs of the
  installed pk3s in 19 s and mirrors `R_LoadLiquidBrushes`, the medium rule and the env.json rules.
- `python tools/rend2/test_liquids_gl.py [--bench]` checks the GLSL on the GPU.

Stock audit. No slime brush exists anywhere, and nothing is dropped for shape or capacity. The
largest brush has 10 sides and the largest map has 258 planes, against limits of 32 and 4096.
W/S/L = water / slime / lava.

| Map | Kept W/S/L | Media W/S/L | Gameplay only | Same-medium overlaps | Touching | Within 4k | Max sun hits | Plane bytes |
|---|---|---|---|---|---|---|---|---|
| kor1 | 0/0/2 | 0/0/2 | - | 0 | 0 | 2 | 1 | 192 |
| kor2 | 0/0/3 | 0/0/3 | - | 0 | 2 | 3 | 2 | 320 |
| mp/siege_korriban | 0/0/43 | 0/0/43 | - | 0 | 59 | 18 | 6 | 4128 |
| t2_rancor | 6/0/0 | 6/0/0 | - | 0 | 0 | 6 | 1 | 576 |
| t2_trip | 2/0/0 | 2/0/0 | - | 0 | 0 | 1 | 1 | 192 |
| t3_bounty | 2/0/0 | 2/0/0 | - | 0 | 0 | 2 | 1 | 192 |
| t3_hevil | 11/0/0 | 11/0/0 | - | 0 | 14 | 11 | 4 | 1184 |
| taspir1 | 0/0/2 | 0/0/2 | - | 0 | 0 | 2 | 2 | 192 |
| taspir2 | 0/0/8 | 0/0/8 | 4 water+fog (`bespin/water2`, BSP fog) | 0 | 0 | 7 | 2 | 1088 |
| vjun1 | 5/0/0 | 0/5/0 | - | 0 | 4 | 4 | 2 | 512 |
| vjun2 | 0 | 0 | 1 brush model (`*72` func_static) | - | - | 0 | - | 16 |
| yavin1 | 4/0/0 | 4/0/0 | - | 0 | 2 | 3 | 2 | 480 |
| yavin1b | 8/0/0 | 8/0/0 | - | 0 | 5 | 7 | 2 | 928 |
| yavin2 | 3/0/0 | 3/0/0 | - | 1 | 0 | 2 | 2 | 288 |
| t2_port (mod pk3) | 27/0/0 | 27/0/0 | 11 water+fog | 17 | 6 | 27 | 3 | 3952 |

- There are no cross-medium overlaps. Froxel segments with three or more intervals of one medium
  (the sorted path): 0 of 20 736 sampled covered segments, across all maps (t2_port included).
- Drawn liquid surfaces facing up were checked against the brushes under them, with exact point
  tests at the triangle centroids and solid world brushes excluded. Every stock surface lies on its
  brush top, with these exceptions:
  - the two sloped yavin stream patches above (9 units or less);
  - the surfaces of the water+fog brushes (BSP fog, by design);
  - one decorative `lakewater` surface on yavin1b at z −168 with no liquid brush under it.
    Gameplay sees no water there either.
- Places where gameplay sees water but the medium does not: the taspir2 water+fog brushes and the
  vjun2 sheet. cgame keeps its tint in both. taspir2 has no drawn water brush, so the water bit of
  `r_volumetricWaterActive` is never set there.
- Moving liquids: only vjun2 (see the limitations). It is not visible as a medium.
- Per-body looks. The shaders on the stock bodies are:
  - `water_1`: the t2_rancor puddles (8–28 deep) and the vjun2 sheet;
  - `water2_still`: t3_bounty (12 deep);
  - `water_quicktrip`: t2_trip (24–32 deep);
  - `h_evil/lakewater` and `water_yavin2`: natural lakes and rivers;
  - `water2_water1_vjun1`: the green water of vjun1.

  Only vjun1 is deep enough to show a medium and calls for a different look, and it gets that look
  from its own slime flag. The stock maps need no env.json rule, and none is shipped.

Physical checks, run on the GPU by `test_liquids_gl.py` on Intel UHD and RTX 2060 against an exact
double-precision reference:

- A doubled brush covers its length once.
- Overlapping and touching brushes give the union with no seam: 64 short segments sliding over the
  shared plane, error under 0.01 units.
- A froxel segment crossed by the surface gets the exact fraction, at 32 view angles.
- A sloped side clips exactly on 512 random rays.
- A water brush with the slime medium puts its length into the slime medium and keeps class water.
- The sun path is correct over two touching brushes (both the register path and the hit-list path,
  with a second body above air correctly left out).
- The sun path is correct over 12 layers, which is more than 8 intervals: the earliest are kept.
  The old code lost the run there and gave full sun.
- The lateral fraction of a froxel cut by a vertical side comes from the XY jitter and the history,
  which liquids never reduce, so there is no one-froxel hard band.

Fixes made in this review:

1. The vjun1 mismatch between surface and medium (see "Class and medium" above).
2. `LiquidAddHit` kept the first 8 intervals in brush order (camera distance), not the 8 earliest
   along the ray. With more than 8 hits, the sun path from a point under them could lose its run
   and give full sunlight.
3. Register paths for `LiquidCoverage` and `LiquidSunTransmittance` (cost below).
4. `r_volumetricWaterActive` is now 0 for scenes that never build the volume (SP LA goggles,
   hyperspace). `RDF_NOWORLDMODEL` scenes (menus, HUD models) leave it alone. Before, cgame dropped
   the underwater tint while the LA goggles were on, even though no medium was drawn.
5. New diagnostics and tools: `r_liquids`, `liquid_audit.py`, `test_liquids_gl.py`. The cvar help
   now gives the fogparms equivalence.

Cost and memory:

| | Intel UHD | RTX 2060 |
|---|---|---|
| `LiquidCoverage`, 160×90×128 grid, 1 brush in every slice, before -> after | 58.9 -> 2.3 ms (empty kernel 2.3) | 0.85 -> 0.17 ms |
| same, 8 brushes in every slice (after) | - | 0.46 ms |
| same, 32 brushes in every slice, before -> after | 57.0 -> 21.9 ms | 4.97 -> 1.42 ms |

The bench is a worst case. In game, the slice masks keep only the brushes near each slice, and
only slices that touch a liquid run the function.

- CPU: brush load takes 0.03–0.14 ms per map, and `R_LiquidsBuild` 3–6 µs (printed by `r_liquids`).
- Memory:
  - plane buffer: 16 B per plane (stock maximum 4128 B);
  - caustic pattern: 256² R16F with mips, about 171 KB, once per renderer;
  - `Liquids` block: 1680 B per scene (unchanged);
  - hunk: 52 B per brush plus 16 B per plane.

`r_volumetricWater 0` stays the legacy path:

- There is no Liquids block and no `R_LiquidsUpdateActive` call, cgame is unchanged, and
  `r_volumetricWaterActive` stays 0.
- The liquid library is not loaded, so every program is unchanged. The only GLSL code line this
  review changed outside `liquid_common.glsl` is inside `#if defined(USE_LIQUIDS)`, in debug view 60.
- The env.json `Liquids` rules are only read into `tr_liquid.cpp`. They are used by brushes that
  exist only when the feature is on, and by r_waterSurface's optics when a map ships a rule (none
  in stock).

There is still a single underwater medium: `tr_liquid.cpp`, `liquid_common.glsl` and their call
sites (inject, debug, lightall `USE_LIQUID_SUN`, r_waterSurface optics). No other code reads liquid
brushes for media.

### Render classes under water (integration review, 2026-10-04)

This review checked the path camera above water -> through the surface -> under water against every
render class. The point: once cgame drops its full-screen tint, nothing may stay clear (unfogged) under
water. There is no fullscreen overlay on top of the medium; where a path cannot read the volume, the
legacy tint stays.

Where the fog comes from in the main froxel view (`RB_SubmitRenderPass`):

1. Layers up to SS_FOG: the opaque surfaces, then SSR/SSGI, decals, the BSP fog passes, the water slot,
   the atmosphere and the clouds.
2. The froxel composite fogs all of them from the depth buffer. This includes the sky (at the sky
   distance) and the first-person weapon (its 0..0.3 depth range is undone).
3. Layers above SS_FOG look the volume up themselves (`RB_VolumetricFogMode` 1). A visible liquid sets
   `frameHeightFog`, so surfaces outside BSP fog volumes take that lookup too.

| Render class | Under water, after this review | Status |
|---|---|---|
| Opaque world and models, decals, Forward+ / legacy dynamic lights (inside lightall), viewmodel | composite, from depth | OK |
| Sky, skybox, sky portal content, atmosphere, clouds | composite at the sky distance, after the atmosphere | OK |
| Sabers: glow, blade, line, cylinder, electricity, trails (`ONE ONE`) | generic lookup, rgb and glow x T | OK |
| FX sprites / polys: `ONE ONE`, `blend`, `ZERO ONE_MINUS_SRC_COLOR` | generic lookup | OK |
| FX stages whose blend the legacy fog leaves out: `SRC_ALPHA ONE`, `DST_COLOR ZERO`, `ZERO SRC_COLOR`, `DST_COLOR SRC_COLOR`, `ZERO ONE_MINUS_SRC_ALPHA`, ... | **fixed**: generic lookup with a blend-derived mask (`RB_LiquidFogBlendMask`) | fixed |
| Premultiplied `ONE ONE_MINUS_SRC_ALPHA` sprites | **fixed**: `c * T + S * alpha` (was `(c * T + S) * T`: a fog-colored quad where alpha is 0, T twice) | fixed |
| Rain, snow, rain streaks, splashes | **fixed**: dropped inside liquid brushes (`WeatherInLiquid`, weather / weatherSplash vertex stage) | fixed |
| Blended lightall stages above SS_FOG (no fog pass) | no fog | unsupported (1) |
| Mirrors, portals (`isPortal`) | legacy fog in the portal view; only camera -> portal plane is fogged | unsupported (2) |
| r_waterSurface seen from below, Snell window | reads the copy taken before the composites: no air fog inside the window | unsupported (3) |
| Sun disc, sun rays, sun flare with a legacy (non r_waterSurface) water surface | drawn after the composite, unfogged | unsupported (4) |
| Distortion pass (`refractionFill`, after tone mapping) | legacy fog only | unsupported (5) |
| Geometry past the froxel far (4096) with the camera in liquid | the liquid ends at far (no tail term): with RGB extinction about 27% blue T beyond it | unsupported (6) |
| Brush-model liquids (vjun2 sheet) | no medium, cgame keeps the tint | unsupported (7) |
| Flares through legacy water | attenuated by the lookup, not occluded (water writes no depth) | unsupported (8) |
| `RT_BEAM` (Q3 debug beam: constant red, drawn at once outside the pass system) | no fog of any kind | unsupported (9), counted |
| UI, 2D, HUD models (`projection2D`, `RDF_NOWORLDMODEL`) | not fogged, as intended | OK |

#### Tint decision per camera

`R_LiquidsUpdateActive` (tr_liquid.cpp) runs once per frame, for the scene that owns the volume. A
class bit of `r_volumetricWaterActive` is set only when all of these hold:

- the class is drawn: `r_volumetricWater`, a brush of that class on the map, the program set has liquids;
- the volume can be built for this scene: not LA goggles or hyperspace, `r_drawfog`, `r_depthPrepass`,
  `tr.renderFbo`, a main view;
- the camera is not in a liquid of that class that the medium does not have. The camera's collision class
  (`CM_PointContents`, world model, the same test as cgame) has no kept brush holding the camera
  (`R_LiquidPointBrush`, the GPU planes, 0.5 unit tolerance).

The third rule is new. It covers a brush skipped for shape or capacity, and a water+fog brush (the BSP fog
medium): those keep the legacy tint, so they look as they did before the feature, instead of turning clear.
Outside liquid the value does not depend on the camera or the frustum, so entering water from the air
never changes it. Only a step between a kept and a skipped brush shows the one-frame cvar lag.

#### Debug

`r_volumetricFogDebug` (range now 0-69):

| View | Shows |
|---|---|
| 65 | Liquid segment length camera -> scene (heat, 1024 units = red). Red stripes where liquid continues past the froxel far (the medium ends there). |
| 66 | In-scattering S of the liquids alone. The injection drops every other medium and the emission, like 59. |
| 67 | Transmittance T of the liquids alone, from the integrated volume (rgb with RGB extinction). Compare with the exact 63: differences come from the far fade, the 32-brush cap or the jitter. |
| 68 | Medium along the ray: hue of the medium with the longest segment, over its sigma_t rgb as the opacity of 512 units. Use it to spot a wrong slot or profile. |
| 69 | Fog bypass: the normal frame, with the draws a froxel view with liquids leaves unfogged tinted through the `u_MaterialDebug` chain. Magenta: lightall blended. Yellow: a generic blend without fog. Legend at the bottom. |

The analytic tail (height fog and BSP fogs past far) is not dropped in 66 / 67.

Console:

- `r_liquids camera` prints:
  - the collision class at the camera, the kept brush (BSP brush, medium) and its GPU block slot;
  - each class bit of `r_volumetricWaterActive`, with its reason and what cgame does with it;
  - the medium's sigma_t (scalar, rgb, 1/e length), albedo, sigma_s rgb and g;
  - the liquid segment length along the view forward and straight up, per medium (exact CPU clip, the
    copy of `LiquidCoverage`), with its rgb transmittance.
- `r_liquids bypass` prints, for the last frame with liquids in the froxel view:
  - the draws without froxel fog, by reason (lightall blended, generic blend without fog, immediate
    draw, refraction / distortion pass);
  - the first 16 shaders, with counts;
  - the weather draws that took the under-liquid cull.

#### Implementation notes

- **ACFF_NONE blends.** The froxel mask is derived only in a froxel view with liquids
  (`R_LiquidClassMask`), so it never reaches the legacy fog.
  - The program gets `USE_FOG` from the same helper in `GLSL_GetGenericShaderProgram`.
  - `USE_LIQUID_FOG_BLENDS` exists only when the program set has liquids.
  - Masks:
    - `(1 1 1 0)`: anything that adds to the frame (`c *= T`).
    - `(0 0 0 -n)`: a filter. `c = mix(n, c, T)` and `alpha *= T`, where n is the neutral color of the
      blend (1 for modulate, 0.5 for 2x modulate).
- **Premultiplied fix.** It is under the same define, in froxel modes only. The plain froxel fog without
  liquids is unchanged.
- **Weather.** The weather programs read the Liquids block of the froxel view, so they only see the 32
  visible brushes there. `WeatherInLiquid` is a vertex-stage copy of `LiquidInside` (a test checks the two
  copies are identical).
  - Streaks are dropped when their center is under the surface.
  - Splashes are dropped when the point 2 units above the impact is inside a liquid: the bed is under
    water.
  - The rain lens already turns off under water (`tr_rainlens.cpp`, water and slime).
- **Byte-identical with `r_volumetricWater 0`.** Every new GLSL line is under `USE_LIQUIDS` or
  `USE_LIQUID_FOG_BLENDS`, and every CPU path checks `R_LiquidClassMask` or the liquid frame. Weather
  draws bind the Liquids block range as lightall does, which is harmless without it. Views 65-69 were out
  of range before.

#### Unsupported, with the concrete fix

1. **Blended lightall stages.** Add a `USE_FROXEL_FOG` lightall permutation for blended stages:
   - lightall uses `TB_CUBEMAP` / `TB_ENVBRDFMAP`, which are the froxel lookup's units, so it needs two
     free sampler units remapped through `GLSL_SetFroxelLookupUnits`;
   - then apply the generic mask math in the lightall output.

   `r_liquids bypass` lists the affected shaders.
2. **Mirrors / portals.** Build a second Liquids block for the portal view (`R_LiquidsBuild` with the
   portal `viewParms`), plus a mode-0 composite in that view. The composite runs `LiquidCoverage` over
   camera -> depth with homogeneous light (ambient x albedo) and the liquid T.
3. **Snell window.** Copy the color after the froxel composite for the above-water branch, or apply
   `FroxelFog` at the refracted world point.
4. **Sun through legacy water.** Do a CPU clip from the camera along the sun direction against the kept
   brushes (`R_LiquidRayCoverage`), then scale the sun disc, sun rays and sun flare color by
   `exp(-sigma c L)`.
5. **Distortion pass.** Look up the froxel transmittance at the distorted surface. That needs the lookup
   with the exposure, because the pass runs after tone mapping.
6. **Liquid tail.** In the tail pass, add `LiquidCoverage(farZ..d)` for the brushes of the camera medium,
   or skip the far fade while the camera is in liquid.
7. **Brush-model liquids.** Transformed brushes. No stock map needs them (see the limitations).
8. **Flares.** Occlusion by water only through the r_waterSurface depth. A legacy-water fix would test the
   liquid planes between the camera and the flare.
9. **`RT_BEAM`.** Route it through `tess` and the pass system (the existing TODO in `RB_SurfaceEntity`).
   cgame's `CG_Beam` (ET_BEAM) is a Q3 leftover.

#### Validation

- **Build.** MSVC: rd-rend2 and rdsp-rend2 build clean. cgame is unchanged.
- **GPU tests.** `tools/rend2/test_liquids_gl.py`, on Intel UHD and RTX 2060 (GL 4.3), adds:
  - generic (froxel fog, scalar / RGB, liquids off / on, rgbagen / tcgen permutations), weather and
    weatherSplash (liquids off / on): 12 programs compile and link;
  - `WeatherInLiquid` on the GPU against the exact point test: 1007 points, a sloped bank, and the cull off;
  - a CPU check of the over-blend algebra of the premultiplied / filter / additive masks.
- **Regressions.** `test_volumetric_compute.py` and `test_watersurface_gl.py` pass.
- **Not run in game.** Checklist:
  - t3_hevil lake crossing, with debug 61, 65, 66, 67 and 69;
  - blaster, explosion and saber FX under water: debug 69 shows no yellow, magenta only on lightall
    blended shaders (see `r_liquids bypass`);
  - yavin1b rain over a stream: no drops or splashes under the surface;
  - a water+fog pool (t2_port, taspir2): BSP fog plus the legacy tint;
  - `r_liquids camera` in and out of the water.

### Limitations

- Brush-model liquids (moving water, `*N` models) are not media; cgame keeps their tint. The stock
  maps have exactly one: the `func_static` "Waterbrush" on vjun2 (`*72`, `water_1`, 512×1088×16
  units at z 160..176), drained by the script `water/water_remove_dan`. A 16-unit sheet has
  τ ≈ 0.02 at the default water, and the camera is never inside it, so transformed liquid brushes
  are not implemented.
- Liquids end at the froxel far (fade over the last fifth); they are not in the analytic tail.
- Only the 32 nearest visible brushes per view are used. The stock maximum within 4096 units is
  18, on siege_korriban, and the far lava dropped there is under an opaque surface anyway.
- Beyond 8 intervals on one froxel segment, the 8 earliest are kept and the later lengths are
  summed (overlaps may count twice). A sun path longer than 8 intervals ends after the 8th; the
  rest is left to the shadow. The stock maximum is 6 intervals on a sun ray (siege_korriban); no
  stock ray goes above 8.
- Overlapping brushes of different media add up, where gameplay would pick lava over slime over
  water. No stock map has a cross-medium overlap.
- Water+fog brushes (`bespin/water2` on taspir2) are BSP fog volumes, not the water medium. cgame
  keeps the water tint there unless the map also has drawn water brushes.
- Sloped stream patches can sit up to 8–9 units off their stepped brush tops (2 patches, on yavin1
  and yavin1b). An 8-unit layer of water medium has τ ≈ 0.01, which is invisible.
- The sun path is the unrefracted straight ray. The light path of dynamic lights inside water is
  not attenuated (only the path to the camera is); sabers and blaster bolts are short-range.
- The baked (non-sun) grid light under water is used as baked; q3map did not attenuate it by the
  water.
- Caustics are a static procedural pattern (not derived from the water surface waves) and only
  modulate the sun.
- Lava has no emission term.

## Known limitations

- Only the main view of the first world scene has a volume; portals, mirrors, the sky portal and the LA goggles
  use the legacy fog in mode 2.
- Transparent surfaces outside every fog volume are not fogged even if a fog volume lies between them and the
  camera (same as the legacy fog, they get no fog pass). Opaque surfaces are fogged by the full ray.
- Froxel resolution: a fogged pixel takes the fog of its froxel column, so thin geometry in front of dense fog can
  show a slightly blurred fog edge (8 pixels at medium).
- The light grid split is a heuristic; `r_volumetricFogSunScale` / `StaticScale` balance it per map.
- The history of the view model region is reprojected like the world (the volume is world space).
- Moving fog volumes (brush entities) are not supported (neither are they in the legacy fog).
- Beyond `r_volumetricFogFar` the geometry / optical depth of the BSP fogs is exact and the height fog nearly so
  (Gauss-Legendre in the soft top), but the light is the light of the last slice of the column (fog that starts
  far behind a dark far point is lit like that point), and the density noise is not applied there (its mean 1; a
  long tail path averages many noise cells anyway). Height fog: thin layers far away still have slice-resolution lighting and partial-slice interpolation; only mode 2 has it; one global layer set by map settings / cvars; the
  automatic base is the lowest floor, which can be a pit or a basement below the main ground level.
- Density noise: 64^3 tile. With the macro field alone the period (4096) can show on huge open views when the
  volume far is raised; turn the detail on or raise the scale. The same field modulates every noisy medium (no
  per medium scale). A fast wind lowers the history weight of the noisy media (more
  jitter noise). The mean is kept within 1% (0.8% up to contrast 3, 1.2% at 4 between half mip levels): checked on
  the CPU with the same generator code and the GPU's filtering.
- Local fog volumes: mode 2 and the froxel main view only; they fade out near the froxel far distance (the tail
  cannot carry them); a volume thinner than a froxel column or a slice is blurred by the froxel resolution; at
  most 64 per view (by importance), with XYZ cluster masks; the density noise is the global field; an
  anonymous moving volume gets no temporal accumulation; no cgame trap yet (game / FX code needs one to call
  the extension); one env.json per map is shared with the cubemaps.
- FX particle media: mode 2 and the froxel main view only; soft ellipsoids along the world axes (no rotation,
  no texture shape); at most 128 particles per view (the most important) and 2560 slice list entries; opt-in per
  `.efx` primitive; `r_volumetricParticles` defaults to 0 because the SP game module mirrors it (older engines lack the
  trap); capped / culled particles are counted but not drawn by view 28.
- Not run in game yet: correctness is verified by builds, offline compilation of every changed / new shader on
  the Intel and NVIDIA drivers, the legacy source comparison and the numeric checks above.

## Possible improvements (not implemented)

- Per map noise settings, per medium noise scale (local fog volumes use the global noise field).
- A cgame trap for `AddFogVolumeToScene` (game / FX code), and fog volume primitives in the effects system.
- Depth aware skipping of hidden froxels: per froxel tile the **farthest** (maximum) scene depth; a slice can be
  skipped only when it is behind the geometry of every pixel of the tile. The nearest depth would let one close
  object remove fog that a neighbouring pixel on a far wall still needs (halos).
- Tail light at 2-4 depths beyond far (logarithmic planes), interpolated per pixel, if the single far light shows
  artifacts in game.
- Blue noise instead of a Halton cycle for the jitter.
