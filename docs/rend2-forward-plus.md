# Rend2 Forward+ / clustered dynamic lighting

`r_forwardPlus 1` replaces the 32-light per-surface mask with clustered light lists, so a scene can have up to 256
dynamic lights (sabers, blaster bolts, explosions, FX). Material shading stays forward: there is no G-buffer. The GL 3.2
/ GLSL 150 baseline is unchanged. It is **off by default**, and `r_forwardPlus 0` keeps the legacy path.

On a GL 4.3 context the cluster lists are built by a compute shader (`r_forwardPlusCompute 1`, default), see
"GL 4.3 cluster build" below.

Code: `shared/rd-rend2/tr_forwardplus.cpp`, `glsl/lightall.glsl` (EvaluateDynamicLight, FPlus* functions).

## Audit: what limited the light count

| Place | Limit |
|---|---|
| `MAX_DLIGHTS 32` in `rd-common/tr_types.h` (public, "bit flags are used on surfaces") | per-frame cap in `RE_AddDynamicLightToScene` |
| `backEndData->dlights[MAX_DLIGHTS]` | storage |
| `R_AddWorldSurfaces`: `num_dlights = min(num_dlights, 32)` | clamp |
| `R_RecursiveWorldNode`, `R_DlightSurface`, `R_DlightBmodel`, `R_DLightsForPoint` (`1 << i`) | 32-bit masks in `drawSurf_t`, `srfBspSurface_t`, `tess.dlightBits` |
| `LightsBlock::lights[32]` / GLSL `u_Lights[32]`, `u_LightMask` | shader loop over mask bits |
| `pointShadowArrayImage` (MAX_DLIGHTS*6 layers), `shadowCubeFbo[MAX_DLIGHTS*6]` | shadow cube = light index |
| froxel fog `lightMask[slice]` | 32-bit mask |

cgame has no caps of its own (`cg_players.c` saber, `cg_ents.c` missiles, `cg_localents.c` explosions,
`cg_weapons.c` muzzle flash, `FxPrimitives`). The renderer is the only limit.

**Legacy bug fixed:** `(1 << num_dlights) - 1` with exactly 32 lights is undefined behaviour. On x86 it evaluates to
0, so world surfaces lost **every** dynamic light when 32 were present. `R_LegacyDlightMask` returns all bits
instead. This is the only intended change to legacy output.

## Data flow

Legacy (unchanged):

```
cgame lights -> RE_AddDynamicLightToScene (32/frame) -> BSP / entity culling -> 32-bit mask per draw surface
-> u_LightMask + Lights UBO (u_Lights[32]) -> lightall: for i < 32, if bit i: EvaluateDynamicLight(light i, shadow cube i)
```

Forward+:

```
cgame lights -> RE_AddDynamicLightToScene (256/frame)
-> R_ForwardPlusPrepareScene: importance order + shadow slots (R_GatherFrameViews)
-> surfaces only get "lit" = 1 (no mask work, better batching)
-> RB_UpdateForwardPlus (per scene, before the camera UBOs): per colour view (main, portal/mirror, sky portal)
   bin light spheres into tiles x tiles x depth slices -> upload light data / cluster grid / index list
-> CameraBlock carries the view's grid parameters
-> lightall: cluster of the fragment (gl_FragCoord tile, view depth slice) -> loop its lights -> EvaluateDynamicLight
```

The single-light BRDF lives in `EvaluateDynamicLight` (per pixel) and `EvaluateDynamicLightSimple` (vertex-lit
path). Both loops call it. The legacy loop's math is unchanged, only moved. It has a receiver visibility hook
(`DynamicLightReceiverVisibility`, currently 1.0) for parallax self-shadowing. The per-pixel dynamic light sum is
kept in its own variable (`dynamicLight`) for later consumers (SSGI).

## GPU resources (buffer textures, GL 3.1 core)

There is one set per `gpuFrame_t` slot (MAX_FRAMES = 2). Write offsets are reset once per frame, so uploads never
overwrite data in flight.

| Buffer | Format | Content | Capacity per frame |
|---|---|---|---|
| light data | RGBA32F, 5 texels/light | origin/radius · color/type · shadow/cookie/emitter data · spot axis or area-light right axis · cookie roll or area-light up axis | 3 scenes × 256 lights |
| cluster grid | RG32UI | absolute offset into the index list, light count | 512K clusters (≤ GL_MAX_TEXTURE_BUFFER_SIZE) |
| index list | R16UI | light index within the scene's light data | 2M entries |

The two frame slots reserve about 16.1 MiB for these three buffer textures. The GL 4.3 path adds two 512 KiB
range SSBOs and tiny counters, for about 17.1 MiB total. They are allocated on the first Forward+ frame and freed
at renderer shutdown. Units: `TB_FPLUS_LIGHTS 11`, `TB_FPLUS_GRID 12`, `TB_FPLUS_INDICES 13`.

Per-view parameters are appended to `CameraBlock` (lightall's Camera block only): `u_FPlusGrid`, `u_FPlusParams`,
`u_FPlusParams2`, `u_FPlusDebug`. A zeroed block (legacy mode, or a view without lists) means disabled.

## Clusters

- **Tiles:** the viewport is split into `r_forwardPlusTileSize`-pixel tiles.
- **Depth slices:** slice 0 is [0, `r_forwardPlusNearSlice`]. Slices 1..N−1 are logarithmic up to the view's zFar,
  which is the dynamic far clip from `R_SetFarClip`. View depth in the shader is
  `dot(position − viewOrigin, viewForward)`, so it does not depend on the depth buffer or the projection.
  `r_forwardPlusSlices 1` gives plain 2D tiles (stage A). N > 1 gives clustered lighting (stage B).
- **Light bounds:** the view's own modelview/projection matrices are used, so mirrors, off-centre and jittered
  projections work. The eye-space AABB of the sphere is projected (conservative). When a light touches the near
  plane, the whole viewport is used. The depth range [d−r, d+r] selects the slices.
- **Overflow:** the main view reuses the scene importance order. A portal or mirror computes its own order
  (brightness × radius² / distance², ×4 when that view's camera is inside the light) only when more visible lights
  than `r_forwardPlusMaxLightsPerCluster` make dropping possible. A remote view therefore does not inherit the main
  camera's overflow choices, while uncapped views avoid a redundant sort. When a cluster reaches the limit, the less
  important lights are dropped. The shader also clamps the count to 256.
- **Verified offline:** a harness runs the real `R_ForwardPlusLightRange` / `R_ForwardPlusSlice` on 2,400 random
  lights (inside/behind the camera, crossing the near plane, huge radii, off-centre projection, 1/16/24 slices,
  32/64 px tiles). 92,892 visible points inside the spheres were checked against the shader cluster math: 0 misses.

## Shadows

Legacy mode still renders a cube for every light, up to 32. Forward+ separates render lights from shadowed lights:

- `r_forwardPlusMaxShadowLights` (default 4, max 32) sets how many lights get a cube. It needs `r_dlightMode 2`.
- Lights are ranked by importance. **Hysteresis:** a light that matches one of last frame's shadowed lights
  (position within max(16, radius/4), similar color) scores ×1.3 and keeps its slot.
- The mapping is render light → shadow slot (−1 = unshadowed). The shader reads the slot from the light data.
  Shadow views are built per slot (`shadowCubeFbo[slot*6+face]`).
- `r_shadowCache 1` reuses a slot's point/spot depth map when the light's projection/depth state, relevant shadow
  settings, world/remap generation and every overlapping dynamic caster are unchanged. Animated/video/deformed/alpha-tested
  time-dependent casters conservatively force a redraw. `r_shadowStats 1` prints cache hits, misses and dirty
  time-dependent slots. The world's time-dependent casters are collected once per map / shader remap
  (`tr.sunShadowCasterGeneration`), not per frame, and blended polys (marks, particles) are not casters: depth
  shadow views drop every non-opaque surface, so a fading mark no longer dirties the light.
- The froxel fog uses the Lights UBO. Its `origin.w` is now the shadow layer: `i` in legacy mode (unchanged
  behaviour), the slot or −1 in Forward+ mode.

## GL 4.3 cluster build

`glsl/fplus_cluster.glsl` (`RB_ForwardPlusBuildViewGPU`) replaces the CPU count / prefix sum / fill of every colour
view. The CPU still computes the cheap part, the tile / slice box of each light (`R_ForwardPlusLightRange`, in
importance order) and the light's view-space bounding sphere, and uploads them (32 bytes per light). One invocation per
cluster then:

1. walks the boxes in importance order, keeps a light when its box contains the cluster and, for point and spot
   lights, when its sphere touches the cluster's view-space AABB (rectangle and line lights keep the box alone);
2. stops at `r_forwardPlusMaxLightsPerCluster`;
3. reserves an even number of index entries on a per-frame counter (the counter starts at 0 for the first view of the
   frame, every view and scene of the frame shares it) and writes its grid texel and the R16 indexes, two per 32-bit
   word. When the CPU's accumulated worst-case counts prove that all lists fit, reservation is one `atomicAdd`;
   capacity-constrained frames use the bounded `atomicCompSwap` loop. A failed bounded reservation leaves the counter
   unchanged, so it cannot create a hole that starves later, smaller cluster lists.

The grid and index buffers are the buffer textures lightall already samples, so lightall is unchanged. Every list is
the CPU list (same order, same cap) without the lights whose sphere misses the cluster; the corner clusters of each
light's box are where the CPU over-included them. A full index list gives the remaining clusters no light (the CPU
build's capacity rule). Oblique (portal) or non-perspective projections skip the sphere test and produce exactly the
CPU lists. `r_forwardPlusStats` prints the CPU part of the build only; per-cluster statistics need
`r_forwardPlusCompute 0`.

`tools/rend2/test_gl43_compute.py` compares the kernel with a port of the CPU binning on 3840 clusters and ~90 lights
(with jitter, area-light boxes, cap 4 and 255): ordered subsets of the CPU lists, every light that reaches a sample of
the cluster's frustum chunk kept, 10–14 % fewer light references than the CPU lists. Passes on Intel UHD and RTX 2060.
Not measured in game: compare `r_forwardPlusBenchmark` with `r_forwardPlusCompute 0` and 1.

The physical attenuation window is also exactly zero at and beyond the range: at `distance == range`,
`PBRPointAttenuation` evaluates `edge` to 1 and its smooth window to `1 - 1 == 0` (and remains zero beyond it).

## Cvars and commands

| Cvar | Default | |
|---|---|---|
| `r_forwardPlus` | 0 | 0 legacy, 1 Forward+. Switches at runtime; latched per frame, never mid-frame. |
| `r_forwardPlusTileSize` | 64 | 16–256 px |
| `r_forwardPlusSlices` | 16 | 1–64 (1 = 2D tiles) |
| `r_forwardPlusNearSlice` | 48 | depth of the first slice |
| `r_forwardPlusMaxLightsPerCluster` | 64 | 1–255 |
| `r_forwardPlusDebug` | 0 | cheat, **latched** (`vid_restart`: the debug views are only compiled into lightall when it is non-zero). 1 tiles, 2 slices, 3 clusters, 4 lights/cluster heatmap, 5 overflowing clusters (red), 6 shadowed lights only, 7 unshadowed only, 8 light spheres, 9 only light `r_forwardPlusDebugLight` |
| `r_forwardPlusDebugLight` | 0 | light index for debug 9 |
| `r_forwardPlusMaxShadowLights` | 4 | Forward+ shadow budget |
| `r_forwardPlusCompute` | 1 | 1: cluster lists built by a GL 4.3 compute shader when the context allows it (`r_gl43 1`), 0: CPU build. Switches at runtime, latched per frame |
| `r_shadowCache` | 1 | reuse unchanged point/spot shadow slots; 0 forces every selected slot to redraw |

Dependency messages are printed once, when the cvar changes (and at renderer start). Nothing is auto-enabled:

- `r_forwardPlusDebug` with `r_forwardPlus 0`: "requires clustered lighting. Enable r_forwardPlus 1."
- `r_forwardPlusMaxShadowLights` changed with `r_forwardPlus 0`: "applies to r_forwardPlus 1 only".
- Forward+ with a shadow budget but `r_dlightMode` < 2: "require r_dlightMode 2".

Commands:

- `r_forwardPlusStats`: lights (and how many were dropped by capacity), clusters, average/max lights per cluster,
  overflow, shadowed lights, CPU build time (last frame and average), GPU main-view time.
- `r_forwardPlusVerify` (sv_cheats): on the next main view, reads the compute grid/index buffers once and checks
  ordered-subsequence membership against the uncapped CPU lists, the per-cluster cap, even/non-overlapping
  allocations and light-index bounds. It prints CPU -> GPU reference totals, overflowed clusters and the first five
  violations.
- `r_forwardPlusSpawnTestLights N [spread]` (sv_cheats): N deterministic, static renderer-only lights around the
  camera position of the first scene after the command (they no longer follow the camera, whose view bob moved
  them and redrew their shadows every frame). They are not gameplay entities; remaining static makes CPU/compute
  screenshot A/B reproducible and lets `r_shadowStats 1` show point shadow cache hits.
- `r_forwardPlusBenchmark [frames]` (sv_cheats): runs legacy 8/32, then Forward+ 8/32/64/128/256 with test lights,
  and prints a table. The camera should stay still during the run.

## Performance

Not measured yet: the game was not launched in this task (validation = build + offline GLSL). To measure, run
`devmap <map>`, `r_dlightMode 2`, `vid_restart`, `r_forwardPlusBenchmark` and fill in this table:

| mode | lights | cluster build (CPU) | main view (GPU) | frame |
|---|---|---|---|---|
| legacy | 8 | – | | |
| legacy | 32 | – | | |
| Forward+ | 8 | | | |
| Forward+ | 32 | | | |
| Forward+ | 64 | | | |
| Forward+ | 128 | | | |
| Forward+ | 256 | | | |

With `r_speeds 100`, the compute dispatch of the main view is reported separately as
`Forward+ cluster build`.

## Test checklist (in game)

1. `r_forwardPlus 0` vs the `build/ab/*-prefplus.dll` DLLs: identical images (except scenes with exactly 32 lights,
   where the UB fix restores the world lighting).
2. Legacy vs Forward+ with ≤ 32 lights (`r_forwardPlusSpawnTestLights 8/31/32`): nearly identical lighting. Expected
   differences: floating point summation order. With `r_dlightMode 2`, only `r_forwardPlusMaxShadowLights` lights cast
   shadows in Forward+ mode.
3. 33, 64, 128, 256 test lights: all visible in Forward+. `r_forwardPlusStats` shows no capacity drops below 256.
4. Debug views 1–5: tiles align with the screen; slices grow with distance; the heatmap is plausible. Check a long
   corridor with slices 1 vs 16.
5. Camera inside a light, lights behind the camera, lights crossing the near plane, huge-radius lights: no
   missing light at tile edges.
6. Portals, mirrors, sky portal: the lighting in the reflected/portal view is correct.
7. Moving lights and camera, dynamic shadows (`r_dlightMode 2`): shadow slots stay stable (debug 6/7).
8. Saber, blaster spam, explosions, Force effects; PBR metal, rough dielectric, transparent surfaces.
9. Froxel fog (`r_volumetricFog 2`) with Forward+: the fog uses the 32 most important lights.

## Shader compile time

Every lightall permutation (about 500 lit programs in SP, compiled at each start, no disk cache) contains the
dynamic light code. The first version had two light loops (legacy + Forward+), each inlining `EvaluateDynamicLight`
with its 9-tap shadow PCF, plus the debug views. That made lit programs compile 2-4x slower (about a minute more at
startup, whatever `r_forwardPlus` was set to). Now there is one loop with a single call site, and the debug views
sit behind `USE_FPLUS_DEBUG`. Offline compile times are back to the pre-Forward+ level on Intel and NVIDIA.

## Known limitations

- XY bounds are the projected sphere AABB. Only the GL 4.3 build tests each sphere against the cluster box, so the
  CPU build's clusters are conservative (more lights per cluster than strictly needed). Either way lightall rejects a
  point/spot light whose range or cone misses the fragment after two texel fetches, before its shadow lookup.
- Transparent surfaces use the cluster of their own fragment depth. This is correct for forward rendering, but
  lights are not culled against the opaque depth.
- The froxel fog still uses the legacy-sized light list (the 32 most important lights). The light data and lists
  are designed so the fog can later use the clusters instead.
- Shadows are capped at 32 slots by the `pointShadowArrayImage` size.
- Only point lights are implemented. Line/rect (LTC) and spot types have a reserved `type` field and flags.
- If the per-frame buffers overflow (huge resolutions with tiny tiles and many slices), the affected view gets no
  dynamic light and a warning is printed once.
- GPU timing covers the whole main view (the forward pass), not the light loop alone.
