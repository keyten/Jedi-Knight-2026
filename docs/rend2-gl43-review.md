# Rend2 GL 4.3 port review (2026-10-16)

Scope: every rendering technology added from `00b8a5703` (Enhanced tone mapping + linear lighting) up to
`6f2c61fdb`, reviewed for work that should move to the GL 4.3 modern tier
(`R_HasModernFeatures`, see [rend2-opengl43.md](rend2-opengl43.md)). Every change keeps the GL 3.2 path
unchanged: `r_gl43 0`, a 3.2 context, or a failed compute compile still select the existing code.

Before this review only the froxel fog used the modern tier (compute injection, media kernel and column
integration, `8f8d4ba01` … `42651db6a`). Every other effect still ran its GL 3.2 implementation on a 4.3
context.

## What makes a port worth it

A port has to remove a real cost, not just replace a fragment pass with a dispatch of the same work:

1. **CPU ↔ GPU synchronisation.** A `glReadPixels` / `glGetTexImage` / blocking query in the frame stalls
   the whole pipeline (the CPU waits for every command queued before it). This is the largest possible
   win and it does not depend on the GPU.
2. **Per-frame CPU work that scales with the screen or the light count** (binning, per-cell loops) and the
   upload that follows it.
3. **Chains of tiny fragment passes** where the FBO switches dominate. This is usually a small win
   (tens of µs); only worth it when the chain runs several times a frame.

Plain full-screen fragment passes (ray marches, blurs, composites) are not faster as compute by
themselves; they would need a new algorithm (shared-memory tiling, wave intrinsics) and are left alone.

## Inventory

| Technology | Commits | Current implementation | GL 4.3 opportunity | Verdict |
|---|---|---|---|---|
| Tone mapping, LUT, physical camera, auto exposure | `00b8a5703`, `7bde8d012`, `b4f550f70` | 256² log-luminance chain: 6 tiny blits, readback only with `r_cameraDebug` | compute reduction | **no**: ~6 passes on ≤ 256² texels, < 0.05 ms |
| GTAO / contact shadows | `93fb54ce8`, `2d51d7c99` | linearize + 3 depth mips + main + denoise ping-pong | single-pass depth mips | **low**: 4 small passes; the main GTAO pass dominates and is ALU bound |
| SSR, SSR emitters, hit cache | `d5614b7bf`, `bfb2ce4b5`, `a2d0046ff` | Hi-Z (7 mips, shared with SSGI), trace / resolve / temporal / composite | single-pass Hi-Z (SPD) | **low**: mip 0 is the only bandwidth-heavy level; the odd-size coverage rule makes a bit-exact tiled reduction intricate |
| SSGI | `fc5f85258` | MRT source in the opaque pass + `glGenerateMipmap` + trace / temporal / denoise | none | **no** |
| Forward+ clustered lights | `9c1e850c4`, `52660944a` | **CPU binning** of every light into tiles × slices per colour view (two passes + prefix sum), uploaded as buffer textures | **compute cluster build**: cull every cluster against the light's sphere on the GPU, write the grid / index buffers in place | **yes, implemented** (`r_forwardPlusCompute`) |
| LTC area lights, saber lines, LTC shadows | `bd149dc49` … `7b6f8db61` | data rides the Forward+ light buffer; screen traces are fragment passes | through Forward+ | covered by the Forward+ port |
| Spot lights, cookies | `ffd5eea03`, `f6ee1fcb5` | bounding sphere in the Forward+ ranges; cookie 2D array | through Forward+ | covered |
| CSM / Shadows 2.0 / PCSS / character shadows | `5f55edd8b`, `93c82eced`, `2e1f98f32` | raster shadow passes | layered rendering needs `ARB_shader_viewport_layer_array` (not core 4.3) | **no** |
| Froxel volumetric fog and its media (height fog, noise, local volumes, FX media, emission, phase, self-shadow, multi-scatter, RGB extinction, liquids) | `239d3ee7e` … `4789c8b27`, `837c7a8e8` | **compute** inject / media / integrate; dynamic-light lists use the shared Forward+ cluster kernel | GPU froxel light lists | **yes, implemented** (`r_volumetricFogLightCompute`) |
| Atmosphere LUTs, clouds, cloud shadows | `13fc62bdc`, `2455fe5e0` | fragment LUT passes / reduced-resolution march | none | **no**: the cloud noise readback is a one-off at the first frame |
| Weather: rain simulation, splashes | `ddc0df4b5`, `1a0f16741`, `f701b2964` | transform feedback update + splash geometry shader; splash query read only when available | compute particle update | **no**: TF is already a GPU-only point update; a compute rewrite has the same cost |
| Wetness persistence, puddle ripples, runoff trails | `bb27fecf5`, `933cf8693`, `f701b2964`, `af2cb2a18` | ping-pong fragment fields at ≤ 10–30 Hz | none | **no**: measured 0.2 ms per update at 256², amortised < 0.01 ms/frame |
| Rain lens / lens water | `4a1976496` … `331855edb` | CPU droplet agents (0.2 ms at the caps), PBO upload ring, fragment field + composite; GPU timers read only when available | GPU agents | **no**: the agents are coherent CPU logic with events; 0.2 ms does not justify a rewrite |
| Foliage: auto grass, wind, flutter | `5010f1e84`, `acdda3729`, `c6600e7fc` | instanced cards, vertex libraries | none | **no** |
| Foliage bend field | `70283ae4c`, `e6fc2c783` | one fragment ping-pong pass per frame | none | **no** |
| Plant skeleton (XPBD fronds) | `f53fecc92`, `08f805613` | CPU XPBD, bones in a buffer texture | compute XPBD | **no** (for now): ~0.3 ms CPU at 96 plants, a GPU port needs capsule contacts and hysteresis state on the GPU |
| Water surface, Snell window, waves, geometry | `deddcdad2` … `0b0abb643` | fragment / vertex; static subdivided mesh | tessellation (GL 4.0 core) for distance-adaptive subdivision | **later, large**: level 6 builds 144 640 triangles for one `t3_hevil` body, processed by every water pass whatever the distance; needs TCS/TES permutations for every water pass and the motion vectors (not measured per pass) |
| Water interaction solver | `b8654888c`, `5ec894e54` | GPU fragment ping-pong per body (GL 3.2) + CPU fallback | compute batching of all bodies | **no**: already on the GPU, 0.2–0.75 ms per 256²–512² body; batching would only save the per-body FBO attach |
| Water foam / whitewater fields | `a7cbcdf07`, `9fb0d0c22` | fragment ping-pong; CPU resamples the intersection foam into the source texture | via the intersection port | covered below |
| **Water intersection foam** | `a783498dc`, `4ff98bf0a` | **`glReadPixels` of the whole visible depth (float) every frame**, then a CPU loop over every simulation cell (projection, depth fetch, 4 neighbour reconstructions) | **compute kernel** reading the depth copy directly; results stay on the GPU; wake flags come back through a fenced, non-blocking readback | **yes, implemented** (highest priority) |
| Waterfalls, mist, impact pools | `d8c7c5cc8` … `dc61fc9a2` | fragment / CPU source lists | none | **no** |
| Underwater particles | `089a0d9ae` | CPU, a few hundred particles, < 0.5 ms | none | **no** |
| Static lighting reconstruction, L1 probes, portals | `5d0a03b7e` … `05a91f7e9` | load-time CPU, runtime uniform data | none | **no** |
| Shader program cache, material DDC, map loading | `afd083fa2`, `6ae1a6eb2`, `53632cd54` … `8a54ecb88` | load time | none | **no** |

Other GL 4.3 features considered: `glCopyImageSubData` for the water / SSR scene copies (no gain over the
existing blits, they are bandwidth bound), `glInvalidateFramebuffer` (no tile-based GPUs on the target
platforms), raising the 16 KiB UBO caps of the froxel blocks with SSBOs (a capacity change, not a speed-up;
the current caps are not reached by stock content).

## Plan

| # | Change | Gain | Status |
|---|---|---|---|
| 1 | Water intersection foam on compute: no readback, no CPU cell loop, no CPU foam resample | removes a full pipeline stall per frame (`r_waterIntersectionFoam 1`) | **done** |
| 2 | Forward+ cluster build on compute (`r_forwardPlusCompute 1`) | removes the CPU binning of every colour view; tighter sphere vs cluster test (fewer lights per cluster in the lighting loop) | **done** |
| 3 | Froxel light lists on the GPU, reusing the Forward+ kernel | removes CPU cluster binning and sphere-culls conservative boxes | **done** |
| 4 | Water mesh tessellation LOD | prepass / reflection vertex cost of big water bodies | later, needs its own design |
| 5 | Single-pass Hi-Z / AO depth chain | a few tens of µs | not planned |

Detailed plans for the remaining work (what, why, where, what to change, how to validate):

1. [In-game validation and tooling of the done ports](rend2-gl43-plan-1-validation.md): GPU timers, verify
   commands, in-game scenarios, stale tests. Do first.
2. [Froxel fog light lists on the GPU](rend2-gl43-plan-2-froxel-light-lists.md) (item 3 above).
3. [Rain streaks and splashes without geometry shaders](rend2-gl43-plan-3-weather-no-gs.md): vertex expansion +
   compute preparation and indirect splash draws; gated on a measurement.
4. [Point-light and CSM shadows in one scene pass per light](rend2-gl43-plan-4-layered-shadows.md): stage 1 (one
   traversal, face masks, any GL) and stage 2 (layered rendering, needs `ARB_shader_viewport_layer_array`).
5. [Distance-adaptive water tessellation](rend2-gl43-plan-5-water-tessellation.md) (item 4 above).

### 1. Water intersection foam

Old path (`RB_WaterIntersectionInject`, unchanged for GL 3.2): right after the opaque pass the depth copy
was read back to system memory (`glReadPixels(GL_DEPTH_COMPONENT, GL_FLOAT)` over the copy rectangle,
up to 8 MiB at 1080p), which waits for the GPU to finish the whole opaque pass, then the CPU projected
every wet simulation cell, reconstructed five positions and wrote the source texture and the foam source.

Modern path (`glsl/water_intersection.glsl`, `tr.waterIntersectionComputeShader`): one dispatch per visible
interaction body, one invocation per simulation cell, same math as the CPU loop (band, normal, depth
confidences, the same strict copy-rectangle test). It writes:

- `sim.gpuIntersection` (R16F): the fresh contact source. The interaction step shader takes
  `max(state.a, source.a, intersection)` in exactly the places where it used the CPU source alpha, on the
  first step after an injection;
- the foam field samples the same image (`u_WaterFoamIntersectionMap`) in place of the CPU resample of
  `intersectionFoam` into the source green channel, scaled by `r_waterFoamIntersection`;
- a per-body maximum (`atomicMax` on the float bits) into a small SSBO. The buffer is copied once per frame
  and read with a fence when the GPU is done with it (never waited for); a contact wakes a sleeping body
  one or two frames later. Active bodies don't need it (the step runs anyway).

The injected value used to be one frame late as well (inject after the opaque pass, step in the next frame's
`RB_WaterSurfaceBeginView`), so the response timing is unchanged.

### 2. Forward+ cluster build

Old path (unchanged with `r_forwardPlusCompute 0` or without GL 4.3): per colour view the CPU computes a
conservative tile / slice box per light, counts the lights per cluster, prefix-sums, fills the index list,
and uploads the index list and the cluster grid.

Modern path (`glsl/fplus_cluster.glsl`, `tr.forwardPlusClusterShader`): the CPU still computes the light
boxes (a few hundred operations) and uploads them with the light's view-space bounding sphere. One invocation
per cluster walks the lights in importance order, keeps those whose box contains the cluster and whose sphere
touches the cluster's view-space AABB (point and spot lights; area lights keep the box only), caps at
`r_forwardPlusMaxLightsPerCluster`, allocates its index range with one `atomicAdd` and writes the grid texel.
The buffers are the same buffer textures lightall already samples, so lightall is unchanged. The result is
always a subset of the CPU lists (same order, same cap) that still contains every light touching the cluster.

Cluster statistics of `r_forwardPlusStats` need the counts on the CPU and are therefore only printed for the
CPU build; the build time shows the CPU cost of either path.

### 3. Froxel fog light lists

`fplus_cluster.glsl` is compiled a second time with `FROXEL_LIGHT_LISTS`. The CPU still selects and sorts dynamic,
recovered-static and portal-shaft lights, uploads their five RGBA32F texels, selects self-shadowed lights and
computes conservative tile/slice boxes. One compute invocation per froxel cluster then sphere-tests those boxes
and writes the existing R32UI layout (`first | count << 24`, followed by 32-bit indexes) in place.

The froxel AABB uses the unjittered `froxelProjection`, expands XY by half a froxel and depth to fractional
boundaries `B(k - 0.5)..B(k + 1.5)`. The last slice keeps the CPU rectangle exactly, without sphere culling,
because its list also serves the tail beyond `far`. Fixed per-frame buffers reserve 32 indexes per cluster;
`r_volumetricFogLightCompute 0` switches back to CPU binning immediately. `r_vfogLightStats` reports CPU candidate,
range and upload time in compute mode, while `r_speeds 100` exposes `Froxel fog light lists`. The on-demand
`r_vfogLightVerify` command checks allocation bounds, ordering, the cap and exact last-slice lists.

## Validation

`tools/rend2/test_gl43_compute.py` (hidden SDL GL 4.3 context; the established Forward+/water cases pass on
Intel UHD 27.20.100.8280 and RTX 2060 616.92; the new froxel cases have currently been run on Intel):

- `fplus_cluster.glsl` on 3840 clusters with ~90 lights (jitter, area-light boxes, caps 4 and 255): every
  GPU list is an ordered subsequence of the CPU list, keeps every light that reaches a sample of the
  cluster's frustum chunk, respects the cap, the even allocation and the R16 packing. The sphere test drops
  10–14 % of the CPU light references in these scenes.
- the `FROXEL_LIGHT_LISTS` permutation at 64/128 slices and tile sizes 4/8/16, including small and large lights
  and a sphere crossing the camera plane: GPU lists are ordered CPU subsequences, stay at 32 entries, retain every
  sampled point of the jitter-expanded cluster, and the last slice equals CPU binning.
- `water_intersection.glsl` on a synthetic scene (floor under shallow water, a pillar through the surface,
  sky, a round wet mask): the sources equal a port of `RB_WaterIntersectionInject` (≤ 3e-3, R16F), dry cells
  stay 0, the hit buffer holds the body maximum in its slot only.
- `water_interaction_field.glsl` / `water_foam_field.glsl` (GLSL 150): the contact map is consumed only when
  flagged (a garbage map is ignored otherwise), the foam resample hits the nearest interaction cell.

Both renderer targets build (MSVC Release). Not run in game: the in-game checks are `r_waterInteractionInfo`
(path and dispatch count), `r_waterIntersectionFoamDebug`, and `r_forwardPlusBenchmark` /
`r_forwardPlusStats` with `r_forwardPlusCompute 0` vs 1.

Validation tooling added for the in-game pass:

- `r_forwardPlusVerify` reads the main-view compute lists on demand and checks their ordering, cap, allocation and
  index bounds against uncapped CPU binning;
- `r_waterIntersectionVerify` compares the compute R16F contact images with a state-free run of the legacy depth
  projection (3e-3 tolerance);
- `r_speeds 100` exposes `Forward+ cluster build` and `Water depth contacts` as separate GPU timers;
- `tools/rend2/gl43_validation/` contains reproducible configs and an exact image comparator.

On 2026-10-16 the hidden-context GL 4.3 test passed on Intel UHD 27.20.100.8280: four Forward+ scenes
(caps 255/4; 3840 clusters; 67,042/78,537/9,266/83,097 GPU references) and the water contact/readback tests
(31 contact cells, peak 0.913). Both SP and MP MSVC Release renderer targets built. A hidden SP run on `t2_trip`
reported 49,728 CPU -> 11,078 GPU references, no overflow and **0 verifier violations**; `yavin1` reported equal
CPU/GPU contact counts (106 on the visible body), maximum difference 0.000485, **0 mismatches**, three body
dispatches and a rising wake-readback counter. A later repeat with the controlled capture config also returned zero
violations/mismatches.

The froxel-list addition passed the Intel hidden-context cases at 64/128 slices and tile 4/8/16. A hidden SP
`t2_trip` run with 64 renderer test lights reported 33,563 CPU-box references -> 17,803 GPU references and
**0 verifier violations** over a 10x8x48 cluster grid; `r_vfogLightStats` reported 0.024 ms of CPU preparation
for the sampled frame. GL 4.3 compute and GL 3.2 raster startup smoke tests both exited cleanly. NVIDIA,
bit-exact CPU/GPU captures and controlled performance measurements remain pending.

The first screenshot pass is not accepted as bit-exact evidence: its CPU/CPU control pair changed too, so the
capture still contains frame-varying output unrelated to the compute switch. Interactive 30-second contact,
1080p/1440p timings and the NVIDIA repeat remain pending and therefore no benchmark numbers are claimed here yet.
