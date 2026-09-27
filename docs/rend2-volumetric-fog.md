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
  to its bounds and visible side along the segment `[far, d]`, and the exact integral of the height fog
  (exponential above its cap, constant below, top cut in the middle of its fade). A bounded fog that ends at 4500
  stops there, one that starts beyond far is still seen. They are lit by the tail texture: the baked + sun light
  at the far side of each froxel column, without albedo (the tail pass of the injection).
- Presets (`r_volumetricFogQuality`), **starting points, not profiled yet**:

| quality | pixels per froxel | slices | 1920x1080 grid | froxels | volume memory |
|---|---|---|---|---|---|
| 0 low | 16 | 32 | 120 x 68 x 32 | 0.26 M | 7.3 MB |
| 1 medium (default) | 8 | 48 | 240 x 135 x 48 | 1.56 M | 43.5 MB |
| 2 high | 8 | 64 | 240 x 135 x 64 | 2.07 M | 58.1 MB |

  `r_volumetricFogGridScale` / `r_volumetricFogSlices` override the preset (latched). Draws per frame: one
  instanced injection draw, the tail pass, `slices` integration draws and the composite.

### Froxel data

| texture | format | size | contents |
|---|---|---|---|
| `froxelInjectImage[2]` | RGBA16F 3D | Fx Fy Fz | rgb = emission of the baked light and the sun, `extinction * albedo * L_in`; a = extinction per unit. Temporally filtered; the two images are this frame and the history (ping-pong) |
| `froxelDynamicImage` | R11G11B10F 3D | Fx Fy Fz | emission of the dynamic lights, this frame only |
| `froxelIntegratedImage` | RGBA16F 3D | Fx Fy Fz | rgb = in-scattering S, a = transmittance T, between the camera and the far side `B(k+1)` of slice k |
| `froxelCarryImage[2]` | RGBA16F 2D | Fx Fy | integration state between two slices (ping-pong, no feedback loop) |
| `froxelTailImage` | RGBA16F 2D | Fx Fy | light at the far side of the volume (baked + sun with phase, no albedo): lights the media beyond far |
| `world->volumetricStaticGrid` | RGBA16F 3D | light grid | isotropic baked light I (rgb), sun trust (a, traced at load) |
| `world->volumetricSunGrid` | R11G11B10F 3D | light grid | baked sun part B |
| `world->volumetricDirGrid` | RGBA16F 3D | light grid | directed non-sun baked light D (rgb), luminance of D (a) |
| `world->volumetricDirVecGrid` | RGBA16F 3D | light grid | direction towards the light * luminance of D (rgb) |
| froxel light lists | buffer textures | per frame | lights (RGBA32F, 2 texels) and cluster headers + indexes (R32UI) |

Emission (not radiance) is stored because it is linear in the medium: blending two frames of emission and
extinction is correct at fog boundaries and under jitter.

### Pipeline without compute shaders

Rend2 runs on a GL 3.2 core context: no compute shaders, no image load / store. The injection renders every slice
in one instanced draw into layered attachments (`glFramebufferTexture`): the geometry shader sends instance k to
layer k (`gl_Layer`). The integration needs the previous slice, so it stays one draw per slice with that layer
attached (`glFramebufferTextureLayer`). The integration carries its running state in a 2D texture ping-pong, because sampling another
layer of the texture that is being rendered to is a feedback loop in GL.

## Injection (`volumetric_inject.glsl`)

Per froxel (instance / layer = slice):

1. **Position.** The baked light and the sun are sampled at a jittered position (Halton 2, 3, 5 over 8 frames, a
   full froxel wide) when temporal accumulation is on, the dynamic lights at the froxel center.
2. **Medium.** Sum of the extinction of the fog volumes that contain the point: their axial bounds and the plane
   of their visible side (the same `inFog` test as `CalcFog`); the global fog everywhere below its cap plane.
   Albedo = extinction weighted fog color. Only the fog volumes of the slice's CPU mask are tested (bounds against
   the frustum sides and the view depth range, one slice wider for the jitter; `fogSlices`). The height fog, the
   density noise and the local fog volumes (see Local fog volumes) are in `FroxelMedium` too; all media add.
3. **Baked light.** `volumetricStaticGrid` (I, isotropic) + `volumetricDirGrid` (D), `* r_volumetricFogStaticScale`.
   With `r_volumetricFogStaticDirectional 1` D gets `mix(1, 4 pi HG(g, dot(L, viewDir)), coherence)` along its
   baked direction L (see Directed baked light); otherwise D is isotropic too.
4. **Sun.** Phase `4 pi HG(g, dot(sunDir, viewDir))`, `* r_volumetricFogSunScale`:
   - with cascaded shadow maps this frame (`VPF_USESUNLIGHT`): `sunRadiance * shadow`, blended to the baked sun
     part at the far end of the last cascade (same fade as lightall);
   - without them: the baked sun part `volumetricSunGrid`.
5. **Dynamic lights.** The lights of the froxel's cluster with lightall's attenuation
   `clamp(0.5 * r^2 / d^2 - 0.5)`, phase `4 pi HG(g, dot(toLight, viewDir))`, their shadow map (see Shadows),
   `* r_volumetricFogDlightScale`. The dynamic light system is the only source: sabers, bolts, explosions are
   volumetric exactly when game code adds a dynamic light for them. There are no spot / projected lights in the
   renderer. Clustered like Forward+ but on the froxel grid (`R_VolumetricBuildLightLists`): tiles of 8 x 8
   froxels per slice, each light sphere binned into the clusters its projected bounds and depth range touch, at
   most 32 per cluster (the least important drop out). With `r_forwardPlus 1` every point light of the scene is a
   candidate (no `MAX_DLIGHTS` limit), otherwise the lights of the Lights block. The medium at the froxel center is
   evaluated only where the cluster has lights.
6. **Temporal filter** of baked + sun (see Temporal). The dynamic light emission is written to its own volume
   without history.

### Light grid split by the sun direction (`R_BuildVolumetricLightGrid`)

The legacy light map merges the baked sun with everything else; adding a realtime sun on top would count it
twice. At map load (mode 2 only), every grid cell is split with the light direction of the cell:

```
s      = smoothstep(cos 25, cos 10, dot(cellDir, sunDir))     // 0 when the map has no sun shader
sun    = min(s * direct, legacyMerge)
static = legacyMerge - sun                                     // static + sun == legacy value
```

The realtime sun radiance is the 90th percentile luminance of the sunlit cells (`s > 0.5`, at least 16 cells), with
their average color, so light beams have the brightness the map was compiled with. Without sunlit cells the
refdef sun color is used.

The direction of a grid cell is a mix of all its lights, so a lamp straight above can look like a high sun. The
injection therefore trusts the baked sun part only where the sky is visible from the grid cell: at map load, for
every cell with a sun part, rays towards the sun from the cell center and four corners of a tetrahedron half a
cell out (collision world: `CM_BoxTrace`, SP `SV_Trace`; reaching a `SURF_SKY` surface counts as the sky). Trust
= 1 if any reaches it, stored in the alpha of `volumetricStaticGrid` (trilinear between cells). Deep in shadow
(indoors) the sun part stays baked light and is not darkened. The central realtime cascade lookup stays, so
characters still cut the beams; the trust no longer depends on the cascade range or on moving occluders. The load
time is printed with `developer 1` ("Froxel fog sun trust").

### Directed baked light

The remainder `R = legacy - B` is split once more into an isotropic part I and a directed part D, which keeps the
light grid direction (`latLong`, towards the light, decoded as `R_SetupEntityLightingGrid`: 256 steps per turn;
the sun split used 255 before, a slightly different sun fraction f). Per channel, style slot 0 as the legacy map:

| grid | legacy total | D | I |
|---|---|---|---|
| HDR | `ambient + direct` | `(1 - f) * direct` | `ambient` |
| LDR | `max(ambient, direct)` | `min((1 - f) * max(0, direct - ambient), R)` | `R - D` |

`I + D + B == legacy` in float for every cell; `I, D >= 0`. The LDR legacy value is a max, not a sum:
`max(a, d) = a + max(0, d - a)`, so only the excess of the direct light over the ambient light is directed; where
the max picked the ambient light nothing is. The sun-aligned share `f * direct` is already in B and never gets
the non-sun phase. Cells inside walls (`styles[0] == LS_LSNONE`) keep their light in I.

The direction is stored multiplied by the luminance of D. Trilinear filtering between cells lit from different
directions shortens the vector: `coherence = |v| / lum(D)` fades the phase to isotropic, so a disagreeing
neighbourhood does not produce the "noodles" the legacy code comment warns about. With `g = 0` the phase is 1
and the result is the previous one up to half rounding (at load, `developer 1` prints "Froxel fog directed light
grid: N cells, reconstruction error max / mean"; debug view 25 shows it on the GPU, including the R11G11B10F sun
grid). Memory: +16 bytes per grid cell (two RGBA16F volumes; a 1M-cell grid = 16 MB, typical grids 1-4 MB).
Cost: two more trilinear 3D fetches and one phase per injected froxel (and per tail texel).

The light grid has one dominant direction per cell for all its non-ambient light: several lamps become one
lobe between them, and a lamp aligned with the sun goes partly into B. No asset change or rebake is needed.

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
sigma0   = -ln(1.5 / 255) / r_volumetricFogHeightOpaque * volumetricFogScale * r_volumetricFogScale
sigma(h) = sigma0 * min(exp(-h / r_volumetricFogHeightFalloff), r_volumetricFogHeightMax)
                  * (1 - smoothstep(top - fade, top, h))       top = r_volumetricFogHeightTop (0: no cutoff)
                                                                fade = min(falloff, top)
medium   = fog volumes + height fog: extinctions add, albedo = extinction weighted average
```

Units: extinction per world unit, the same conversion as the fog volumes. `r_volumetricFogHeightOpaque` is a
`fogParms` depthForOpaque: the distance through the medium at the base height after which the transmittance is
1.5/255. There is no separate density scale. Below the base the density grows up to `HeightMax` times the base
density (1 = flat layer below the base). The color is a `fogParms` color (sRGB, converted like the fog volumes).

The base height is set to the lowest floor of the map whenever it loads (`R_SetHeightFogBase`, tr_bsp.cpp: the
lowest point of the visible opaque world surfaces, planar ones only when they face up; the world bounds if there
is none); change the cvar afterwards to move it.

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
  divides it out (`N`). Macro and detail are independent fields, so their product keeps the mean too.
- **Contrast.** `c = 0` gives m = 1, the original homogeneous density. `c = 1` gives a density from 0 to 2x (voids
  and clumps). `c = 3` gives sparse clumps up to 4x. With `c <= 1` the standard deviation of m is at most 0.57.
- **Anti-aliasing.** The mip level follows the froxel: one level sharper than the slice thickness (the jittered
  positions of the temporal filter average the rest). Far froxels see prefiltered noise with a lower contrast and
  the same mean, so distant fog tends to homogeneous instead of shimmering. Medium preset (48 slices, far 4096):
  macro (4096) lod 0 up to ~900 units, 1.1 at 2000, 2.2 at 4096; detail (900) 1.3 at 500, 3.3 at 2000.
- **World anchoring.** The texture coordinates are world positions (no camera, froxel or screen coordinates). The
  noise is sampled at the jittered position of the baked + sun term (the temporal filter supersamples it inside
  the froxel) and at the froxel center for the dynamic lights (no history, stable).

### Noise texture

| | |
|---|---|
| image | `tr.froxelNoiseImage` (`*froxelNoise`), 3D, 64 x 64 x 64, `GL_RGBA8`, full mip chain (7 levels), `GL_REPEAT`, `LINEAR_MIPMAP_LINEAR` |
| r | macro field |
| g | detail field (independent seed) |
| b, a | unused (0) |
| memory | 1 MiB + mips = 1.14 MiB of VRAM; CPU copy of both channels with their mips 0.57 MiB (static, kept for the mean tables) |
| created | at renderer init with `r_volumetricFog 2` only (`R_CreateVolumetricImages`), no asset |
| CPU cost | generation ~120 ms once per process (the CPU copy survives `vid_restart`); mean table ~50 ms per channel, only when its contrast changes |

Generation (`R_NoiseGenerateField`, deterministic, fixed seeds): per channel, a tileable gradient (Perlin) noise
FBM of 3 octaves with lattice periods 4, 8 and 16 cells per tile (weights 1, 0.5, 0.25, quintic fade, 12 edge
gradients from an integer hash of the lattice point modulo the period, so the tile wraps). Each octave is shifted
by its own fraction of a cell, otherwise the lattice points of the three octaves (where gradient noise is 0)
coincide and form a visible grid. Then rank based histogram equalization: every value 0..255 is taken by exactly
1024 texels. `R_CreateImage3D` gained a `flags` argument (default `IMGFLAG_CLAMPTOEDGE`, as before for every other
caller): without it the wrap is repeat, and `IMGFLAG_MIPMAP` allocates the mip chain (`glGenerateMipmap`).

### Media

`r_volumetricFogNoise` is a bit mask: 1 height fog, 2 BSP fog volumes, 4 the global fog. The flag of each fog
volume travels in `fogMaxs[i].w`. Bit 8: the local fog volumes with the noise flag (`FOGVOLUME_NOISE`, env.json
`"Noise"`, `r_fogvol add ... noise 1`); they use the same field and periods. With 0, or with both
contrasts at 0, the injection takes a uniform branch and samples nothing.

### Wind and the temporal filter

`r_volumetricFogNoiseWind "x y z"` (units per second, default 0) moves the noise:
`wind offset = fract(wind * t / P)` per octave, computed in double precision from the renderer time and wrapped
to the tile (the detail wind is rotated like its coordinates). With no wind the noise is completely static in
the world. The weather system's wind is not used (it is gusty, and exists only with weather effects).

The history clamp works on radiance (emission / extinction), which a moving density does not change, so it cannot
catch the drift. The history weight of the noisy media is lowered instead, so that the lag of the temporal filter
stays under a tenth of the finest noise feature (`P / 16` of the finest active octave):

```
lag      = |wind| dt w / (1 - w)  <=  lambda = 0.1 P_finest / 16
w_noise  = min(w, lambda / (lambda + |wind| dt))          dt = frame time, clamped to [1/240, 1/15] s
w_froxel = mix(w, w_noise, noisy share of the froxel's extinction)
```

At 60 fps with `w = 0.9`: winds up to ~32 u/s keep the full weight. At 128 u/s the weight is 0.90 macro only and
0.73 with detail (lag 19 / 6 units). At 512 u/s it is 0.75 / 0.40. Media without noise keep the full weight in
every case. Changing the mask, a scale or a contrast resets the history.

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
3. Sort by `view depth - radius` and keep the nearest `MAX_GPU_FOG_VOLUMES` = 64. The rest are dropped and counted
   (`r_fogvol`, developer print).
4. **Per slice packed lists.** For each slice k (0 .. N-2), in near-to-far order, the uploaded volumes whose sphere
   overlaps the slice depth range `[B(k), B(k+1)]` are appended to a pool of 8 bit indices.
   - Header per slice = `first | count << 16`.
   - The injection of slice k decodes only its list: a froxel never loops over every volume.
   - The jittered sample stays inside its slice, so the slice range is exact.
   - Pool overflow (2048 entries) drops the far slices first, and is counted.
5. The injection evaluates each listed volume with an early reject outside its unit shape.

### Maximum count and UBO budget

The volumes ride in the existing `VolumetricFog` std140 block, so there is no new binding:

| part | size |
|---|---|
| previous block (camera, lights, noise, 24 BSP fogs) | 2256 B |
| `u_FroxelLocalParams` | 16 B |
| 9 vec4 per volume (rows, previous rows, color, shape, motion) x 64 | 9216 B |
| slice headers, `ivec4[32]` (128 slices) | 512 B |
| index pool, `ivec4[128]` (2048 x 8 bit) | 2048 B |
| **total** | **14 048 B** |

- The total is below the 16 384 B of `GL_MAX_UNIFORM_BLOCK_SIZE` that GL 3.2 guarantees. It is checked by a
  `static_assert`, and at init against the driver's value: if the driver is too small, froxel fog is disabled
  with a warning.
- 64 volumes also fit the 8 bit indices.
- The block is appended once per scene, into a scene UBO of 1 MB.
- Pool size, from the CPU harness: 240 frames of random scenes with up to ~45 volumes, radii up to 724 and
  teleporting moves. Median use 324 entries, 95th percentile 1071, maximum 1428. With the first choice (1024), 12
  frames overflowed, which is why the pool is 2048.

### Temporal

The history clamp works on radiance, so a moving density would otherwise ghost. Each frame's volumes are paired
with the previous frame's by `id`; an anonymous volume (id 0) is paired by identical parameters.

- **Changed** (moved by more than 0.01 units, rotated, resized, or density / softness changed by more than 1%):
  the previous rows, extinction, softness and shape are uploaded (`u_FroxelLocalMotion`, `u_FroxelLocalPrev*`).
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
- The radiance clamp and the noise wind weight are unchanged and combine with it.
- An anonymous volume that moves is new every frame: it gets no temporal accumulation (more jitter noise), but no
  ghost either. Pass an id.

### Debug

- Views 16 to 19 (see Debug views): local σ only, local vs BSP / height share, bounds (outer and inner shell),
  volumes per slice.
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

## FX particle media (`r_volParticles`, `tr_volparticle.cpp`)

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
  extinction <= 0 and anything past 1024 are counted as rejected. With `r_volParticles 0` it returns at once.
- MP: the FX system lives in the client executable. `cl_main.cpp` looks the export up next to
  `GetRefFoliageAPI` (`reVolParticles`), and `SFxHelper::AddVolumetricParticle` (`FxSystem.cpp`) calls it.
- SP: the FX system lives in the game module. It sends the particle through the new cgame trap
  `CG_R_ADDVOLPARTICLE` (appended to `cgameImport_t`, `cgi_R_AddVolumetricParticle`), and the engine forwards it
  to the export (`cl_cgame.cpp`). The game module calls the trap only while the mirrored cvar `r_volParticles` is
  set, because older engines lack the trap. This is the pattern of `r_foliageInteraction` / `r_saberAreaLights`.
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
   **deterministic** sort (importance, then id, then live before vanished) keeps the first `r_volParticlesMax`
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
particleKeep = mix(1, r_volParticlesHistory, smoothstep(0.02, 0.25, particleChange))
froxelWeight *= particleKeep
```

- Where the particle density did not change, the history is untouched (a static fog around the smoke keeps its
  full weight).
- Where it changed, the weight drops to `0.9 * 0.3 = 0.27` by default. The ghost decays about 4x per frame, and
  the jittered samples of a drifting puff are still averaged a little (no hard flicker). `r_volParticlesHistory 0`
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
- `r_volParticlesDebug 1` (cheat): the same line every 60 frames.
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

## Sprite particle lighting (`r_particleLight`)

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
  `gain = r_particleLightScale / (mapAverage * r_volumetricFogStaticScale)`.
  - `mapAverage` is the mean luminance of the valid light grid cells (`world_t::particleLightReference`).
  - An average place of the map keeps the authored colour. A dark room darkens smoke down to `r_particleLightFloor`,
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
| `r_particleLight` | 0 | latched (`vid_restart`): creates the field. Needs `r_volumetricFog 2`. The froxel build then also runs on maps without any fog medium. |
| `r_particleLightMix` | 1 | 0 = authored colour (lit/unlit A/B toggle without a restart), 1 = lit |
| `r_particleLightScale` | 1 | gain relative to the map average light |
| `r_particleLightFloor` | 0.03 | minimum light factor |
| `r_particleLightDebug` | 0 | 1 field (just in front of the scene), 2 baked only, 3 sun only, 4 dynamic lights only (2-4 also change what sprites receive), 5 classification: magenta = lit, cyan = unlit sprites |

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
with `r_particleLight 0` / `1` (`vid_restart` between), on a map without fog and on one with fog.

| map | inject off | inject on | total frame off / on |
|---|---|---|---|
| no fog (e.g. yavin1b) | - | | |
| fog (e.g. hoth2) | | | |

## Integration (`volumetric_integrate.glsl`)

Front to back over the slices of every froxel column, with the medium constant inside a slice:

```
length = (B(k+1) - B(k)) * |ray|                     // path length along the froxel's ray
Ts     = exp(-extinction * length)
S     += T * (emission / extinction) * (1 - Ts)      // extinction -> 0: emission * length
T     *= Ts
```

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
  the wind phase is absolute time * wind, so a new wind moves the whole pattern.
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
| `r_volumetricFogAnisotropy` | 0.2 | HG g of sun and dynamic light scattering, -0.9..0.9. The baked light stays isotropic |
| `r_volumetricFogTemporal` | 1 | Temporal accumulation and jitter |
| `r_volumetricFogHistoryWeight` | 0.9 | Weight of the history, 0..0.98 |
| `r_volumetricFogSunScale` | 1 | Sun scattering multiplier (baked and realtime) |
| `r_volumetricFogDlightScale` | 1 | Dynamic light scattering multiplier |
| `r_volumetricFogStaticScale` | 1 | Baked light scattering multiplier |
| `r_volumetricFogDlightShadows` | 1 | Dynamic lights use their shadow maps (needs `r_dlightMode 2`) |
| `r_volumetricFogBloom` | 0 | Bright in-scattering added to the glow buffer |
| `r_volumetricFogReset` | 0 | Set by game code on camera cuts, cleared by the renderer |
| `r_volumetricFogDebug` | 0 | cheat, debug views below |
| `r_volumetricFogFreeze` | 0 | cheat, keep the volume and its camera |
| `r_volumetricFogHeight` | 0 | height fog on / off |
| `r_volumetricFogHeightOpaque` | 3000 | height fog: depthForOpaque at the base height (units) |
| `r_volumetricFogHeightBase` | 0 | height fog: world z of the base, set to the lowest floor on map load |
| `r_volumetricFogHeightFalloff` | 256 | height fog: scale height (density / e per this many units above the base) |
| `r_volumetricFogHeightMax` | 1 | height fog: maximum density below the base, multiple of the base density |
| `r_volumetricFogHeightTop` | 0 | height fog: soft cutoff height above the base, 0 = none |
| `r_volumetricFogHeightColor` | 0.7 0.75 0.8 | height fog: scattering color (albedo), as fogParms |
| `r_volumetricFogStaticDirectional` | 0 | 1 = the directed non-sun light grid part gets the phase function along its baked direction |
| `r_volumetricFogNoise` | 0 | density noise media mask: 1 height fog, 2 BSP fog volumes, 4 global fog, 8 local fog volumes with the noise flag |
| `r_volumetricFogNoiseScale` | 4096 | macro noise tile period (world units) |
| `r_volumetricFogNoiseContrast` | 1 | macro contrast c, 0..4 (0 = homogeneous) |
| `r_volumetricFogNoiseDetailScale` | 900 | detail noise tile period (world units) |
| `r_volumetricFogNoiseDetailContrast` | 0 | detail contrast, 0 = off (no second fetch) |
| `r_volumetricFogNoiseWind` | 0 0 0 | noise drift, world units per second |
| `r_volParticles` | 0 | FX particles with a `volumetricMedia` block add media (mirrored by the SP game module, so off by default) |
| `r_volParticlesMax` | 128 | most important particles uploaded per frame, 0..128 |
| `r_volParticlesScale` | 1 | extinction multiplier of the particle media |
| `r_volParticlesHistory` | 0.3 | share of the history weight kept where the particle density changed |
| `r_volParticlesDebug` | 0 | cheat, 1 = culling statistics every 60 frames |

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
| 20 | isotropic baked light I only |
| 21 | directed baked light D only, without phase |
| 22 | direction of D: rgb = dir * 0.5 + 0.5, dimmed by the incoherence and scaled by the luminance of D |
| 23 | baked sun part B only (no realtime sun) |
| 24 | reconstructed I + D + B, no phase: must look like view 5 with `r_sunlightMode 0` before the split |
| 25 | 100 * abs(I + D + B - legacy merged grid): black = exact |
| 26 | as 1, FX particle media only (the injection drops every other medium) |
| 27 | FX particle media along the ray, opacity weighted: red = history reduction where the particle density changed, green = particle share |
| 28 | FX particle proxy bounds over the frame (uploaded particles), one hue per GPU index, dimmed behind the scene |

Views 2 to 5 keep only that light term in the injection, so the scene behind the overlay also shows it. Changing
the view resets the history. `r_volumetricFogFreeze 1` keeps the froxel volume and its camera: move away to see
the frozen frustum (outside it there is no fog).

GPU timings: `r_speeds 100` lists "Froxel fog inject", "Froxel fog integrate" and "Froxel fog composite" with the
other GPU timed blocks (GL timestamp queries).

## GPU cost

Not measured yet (the renderer has not been run with this mode). Expected shape: injection dominates (one full
screen triangle per slice at froxel resolution; cost grows with the number of fog volumes, the lights of the
slice and the cascade lookups), integration is a few texture fetches per froxel, the composite is one full screen
pass. Maps without fog volumes do no froxel work at all unless the height fog is on. The height fog adds one
`exp`, one `smoothstep` and a few ALU to each of the two `FroxelMedium` calls per froxel (a uniform branch when
off); it makes more froxels non-empty, so more of them take the light path. Profile with `r_speeds 100` on low / medium / high before
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
| fx: black smoke | `r_volParticles 1`, `zz_volumetric_media_test.pk3`, a map spawning `volumetric/black_smoke` (or `playfx`-style test) | dark soft puffs that shadow / absorb the light behind them; sprite still drawn | 26, 28, 1 |
| fx: droid smoke | damage a droid (R2 / R5 / mouse) until it smokes | smoke trail with volume, follows the droid | 26, 28 |
| fx: rocket smoke | fire a rocket at a wall | only the lingering smoke has volume; fireball, dust, flash unchanged | 26, 28 |
| fx: saber through smoke | saber on, swing through the smoke | colored glow inside the smoke only | 4, 6 |
| fx: moving source | smoking droid walking / rocket smoke drifting | no long ghost behind it, no strong flicker | 27, 8 |
| fx: behind a wall from a dlight | smoke on the far side of a wall from a point light, `r_dlightMode 2` | no light leak through the wall | 4 |
| fx: stress | many emitters (several explosions at once) | `r_volparticles`: capped > 0, uploaded 128, no hitch; far slices drop first | 28 |
| fx: temporal off / on | `r_volumetricFogTemporal 0 / 1`, `r_volParticlesHistory 0 / 0.3 / 1` | off: noisier, no ghost; 1: visible trail | 27, 8 |
| fx: camera inside smoke | walk into a smoke column | fog stays (medium submitted while the sprite is culled) | 1 |
| fx: legacy RF_VOLUMETRIC | DEMP2 shot / charged impact | unchanged fake volumetric shading of the model | - |
| fx: off | `r_volParticles 0` | exactly the previous look; `r_volparticles` shows 0 submitted | - |
| fx: timings | see FX particle media, Timings | fill the table | - |
| plight: dark room | `r_volumetricFog 2`, `r_particleLight 1` + `vid_restart`, droid smoke in an unlit corridor | smoke darker than with `r_particleLightMix 0`, not black | pl 1, 2 |
| plight: saber beside smoke | ignite a saber next to `volumetric/black_smoke` | smoke takes the blade colour on the near side | pl 4 |
| plight: red / blue saber sweep | swing red then blue through the smoke | colour follows the blade, no trail (no history) | pl 4 |
| plight: light behind a wall | dlight on the far side of a wall, `r_dlightMode 2` | no light on smoke across the wall | pl 4 |
| plight: sunlit smoke | outdoor map with sun, smoke half in shadow | lit side bright, shadowed side dark | pl 3 |
| plight: additive sparks | sparks / muzzle flash next to smoke | sparks unchanged in the dark, cyan in pl 5; smoke magenta | pl 5 |
| plight: volumetric off / on | `r_volumetricFog 1` vs `2` | mode 1: authored sprites; mode 2: lit sprites, fog applied once | - |
| plight: lit / unlit | `r_particleLightMix 0 / 1` | only alpha-blended sprites change | pl 5 |
| plight: timings | see Sprite particle lighting, Timings | fill the table | - |

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
- Beyond `r_volumetricFogFar` the medium is exact (analytic) but its light is the light of the last slice of the
  column, and the density noise is not applied there (mean 1). Height fog: the soft top is a hard cut in the
  middle of its fade beyond far; thin layers far away are limited by the slice depth; only mode 2 has it; one global layer set by cvars; the
  automatic base is the lowest floor, which can be a pit or a basement below the main ground level.
- Density noise: 64^3 tile. With the macro field alone the period (4096) can show on huge open views when the
  volume far is raised; turn the detail on or raise the scale. The same field modulates every noisy medium (no
  per medium scale). A fast wind lowers the history weight of the noisy media (more
  jitter noise). The mean is kept within 1% (0.8% up to contrast 3, 1.2% at 4 between half mip levels): checked on
  the CPU with the same generator code and the GPU's filtering.
- Local fog volumes: mode 2 and the froxel main view only; they fade out near the froxel far distance (the tail
  cannot carry them); a volume thinner than a froxel column or a slice is blurred by the froxel resolution; at
  most 64 per view (the nearest) and 2048 slice list entries; the density noise is the global field; an
  anonymous moving volume gets no temporal accumulation; no cgame trap yet (game / FX code needs one to call
  the extension); one env.json per map is shared with the cubemaps.
- FX particle media: mode 2 and the froxel main view only; soft ellipsoids along the world axes (no rotation,
  no texture shape); at most 128 particles per view (the most important) and 2560 slice list entries; opt-in per
  `.efx` primitive; `r_volParticles` defaults to 0 because the SP game module mirrors it (older engines lack the
  trap); capped / culled particles are counted but not drawn by view 28.
- Not run in game yet: correctness is verified by builds, offline compilation of every changed / new shader on
  the Intel and NVIDIA drivers, the legacy source comparison and the numeric checks above.

## Possible improvements (not implemented)

- Per map height fog / noise settings, per medium noise scale (local fog volumes use the global noise field).
- A cgame trap for `AddFogVolumeToScene` (game / FX code), and fog volume primitives in the effects system.
- Depth aware (minimum depth per froxel column) skipping of hidden froxels.
- Blue noise instead of a Halton cycle for the jitter.
