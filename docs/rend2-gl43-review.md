> Historical planning/review document. The grid-reconstructed static sources and
> portal shafts mentioned below have since been removed; current froxel lists
> contain ordinary scene/dynamic lights. See [removal report](rend2-grid-reconstruction-removal.md).

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
| Weather: rain simulation, splashes | `ddc0df4b5`, `1a0f16741`, `f701b2964` | transform feedback update + splash geometry shader; splash query read only when available | compute particle update; splash/streak expansion without GS (plan 3) | **no**: TF is already a GPU-only point update; a compute rewrite has the same cost. GS removal measured and dropped: ≈ 0.2 ms removable on Intel, see below |
| Wetness persistence, puddle ripples, runoff trails | `bb27fecf5`, `933cf8693`, `f701b2964`, `af2cb2a18` | ping-pong fragment fields at ≤ 10–30 Hz | none | **no**: measured 0.2 ms per update at 256², amortised < 0.01 ms/frame |
| Rain lens / lens water | `4a1976496` … `331855edb` | CPU droplet agents (0.2 ms at the caps), PBO upload ring, fragment field + composite; GPU timers read only when available | GPU agents | **no**: the agents are coherent CPU logic with events; 0.2 ms does not justify a rewrite |
| Foliage: auto grass, wind, flutter | `5010f1e84`, `acdda3729`, `c6600e7fc` | one draw per BSP surface with all its sprites, in prepass, velocity, every CSM cascade and colour; no per-sprite frustum culling, the distance fade only collapses vertices after the vertex shader ran; strips / 3 cards chosen per surface, so one near point puts the whole surface on the expensive LOD | compute per-sprite frustum / fade culling and per-sprite LOD into compact lists, indirect instanced draws | **candidate, gated on a measurement** ([plan 6](rend2-gl43-plan-6-grass-gpu-culling.md)); corrected 2026-10-19, the first review missed it |
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
| 4 | Water mesh tessellation LOD | prepass / reflection vertex cost of big water bodies | **done** (`r_waterTessellation`, off by default): mesh memory and build time drop ~150×; with `r_waterTessPixels 96` the `t3_hevil` water pass on Intel goes from 99–120 ms to 73 ms (NVIDIA 6.5 → 5–6 ms); the rest is mostly water SSR (plan 5 status) |
| 5 | Single-pass Hi-Z / AO depth chain | a few tens of µs | not planned |

Detailed plans for the remaining work (what, why, where, what to change, how to validate):

1. [In-game validation and tooling of the done ports](rend2-gl43-plan-1-validation.md): GPU timers, verify
   commands, in-game scenarios, stale tests. Do first.
2. [Froxel fog light lists on the GPU](rend2-gl43-plan-2-froxel-light-lists.md) (item 3 above).
3. [Rain streaks and splashes without geometry shaders](rend2-gl43-plan-3-weather-no-gs.md): vertex expansion +
   compute preparation and indirect splash draws; gated on a measurement. **Dropped 2026-10-16** after step 0
   (see "Weather geometry shaders: step 0 measurement" below).
4. [Point-light and CSM shadows in one scene pass per light](rend2-gl43-plan-4-layered-shadows.md): stage 1 (one
   traversal, face masks, any GL) and stage 2 (layered rendering, needs `ARB_shader_viewport_layer_array`).
   **Stage 1 done for point lights 2026-10-19** (`r_shadowSinglePass`, front 5× lower, depth bit-identical);
   stage 2 done the same day (`r_shadowLayered`, submit ≈ 3× lower, depth bit-identical); CSM open (see "Point light
   shadows: step 0 and stage 1" below).
5. [Distance-adaptive water tessellation](rend2-gl43-plan-5-water-tessellation.md) (item 4 above).
6. [GPU culling and per-sprite LOD of surface sprites](rend2-gl43-plan-6-grass-gpu-culling.md): compute + indirect
   draws, sprite data through a buffer texture (no SSBO in the vertex stage); gated on a measurement.
7. [Occlusion culling of hidden models: investigation](rend2-occlusion-culling-investigation.md): measure how much
   work goes to fully occluded models and world surfaces before choosing conditional rendering (any GL), a compute
   Hi-Z test with indirect draws (GL 4.3) or CPU-side skipping; the existing Hi-Z keeps the closest depth and does
   not fit.

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

### Weather geometry shaders: step 0 measurement (plan 3, dropped)

2026-10-16, hidden SP run of the HEAD Release renderer: `yavin1`, `setviewpos 1024 -3900 210 90`, `r_we heavyrain`
(5000 drops per chunk, 45,000 TF points), `r_rainSplashes 1`, `r_speeds 100`, median of 30 frames. The view drew
3–4 rain chunks (15,000–20,000 points) and 3–6 splash slots.

| GPU, resolution | Weather simulation (TF) | Weather draw, streaks 0 / `r_rainStreaks 1` | Weather splashes, streaks 0 / 1 | Main View |
| --- | --- | --- | --- | --- |
| Intel UHD 27.20.100.8280, 1920x1080 | 0.21 ms | 0.22 / 0.52 ms | 0.25 / 0.22 ms | 57 / 82 ms |
| Intel UHD, 640x360 | 0.21 ms | 0.17 / 0.20 ms | 0.24 / 0.24 ms | 8.6 / 9.0 ms |
| RTX 2060 616.92, 1920x1080 | 0.045 ms | 0.011 / 0.011 ms | 0.032 / 0.028 ms | 2.0 ms |

Both Intel timers are just above the plan's 0.2 ms gate, but the resolution split shows what the rewrite could
remove. Splashes don't depend on resolution, so all ~0.24 ms is vertex/GS work. The streak time above
~0.2 ms at 1080p is fill, and fill stays the same without a GS. The replacement also adds a compute pass over the
same particles; the TF update over 45,000 points already takes 0.21 ms on this GPU. Estimated net gain: ~0.2–0.3 ms of a
57 ms Intel main view, ~0.03 ms on NVIDIA. That is the same order as the lens-agent cost judged not worth a
rewrite above, so plan 3 was not implemented; the GS path stays the only path.

Reproduce: the hidden-window test install with `+set cl_renderer rdsp-rend2` (a fresh `fs_homepath` otherwise
writes `cl_renderer rdsp-vanilla` and the run silently uses the vanilla renderer); NVIDIA on the Optimus laptop
via the per-process environment variable `SHIM_MCCOMPAT=0x800000001`.

### Point light shadows: step 0 and stage 1 (plan 4)

2026-10-19, hidden SP runs of the Release renderer at 1920x1080 with `r_dlightMode 2`, `r_forwardPlus 1` and
`r_shadowStats 1`. Front = `R_RenderView` CPU time (BSP walk, entity/G2/MD3 surfaces, sort). Submit = execution
of that view's backend commands (GL calls, driver). Scene: `vjun2`, the player's saber against 3–4 saber NPCs, so
4 shadowed saber lights (radius ≈ 40–60), 24 cube faces. Values are per frame and averaged per second.

| | Intel UHD front / submit | RTX 2060 front / submit |
| --- | --- | --- |
| 24 separate face views (`r_shadowSinglePass 0`) | 10–14 / 20–24 ms | 5.1–5.4 / 4.4–4.8 ms |
| 4 cube passes (`r_shadowSinglePass 1`) | 2.1 / 6.2 ms | ≈ 1.0 / 2.8–3.0 ms |
| GPU time of the 24 faces (`r_speeds 100`) | ≈ 4 ms | ≈ 0.4 ms |

The old face views have no far plane (`planeBits` 15) and skip the PVS like every depth shadow view. So each face
walked the BSP through an infinite pyramid and drew ≈ 540 surfaces, almost all of them beyond the light radius and
clipped by the GPU. The cube pass walks the light's cube (`origin ± radius`) once, keeps a 6-bit face mask per
draw surface and hands each face a filtered copy of one sorted list. Its 4 cubes carry ≈ 225–340 surfaces each,
copies included. `r_shadowVerify 1` renders every cube both ways in the same frame and reads back the 6 faces. All
8 Intel and 16 NVIDIA cube comparisons (with `r_shadowCasterLod` 0 and 1) were bit-identical, at ≈ 500k covered
texels per cube.

Not done: CSM in one walk. The sun cascades' front is 1.5 ms on Intel and 0.56–0.63 ms on NVIDIA for 3 cascades,
so at most ≈ 1 / 0.4 ms could be saved, and G2 LODs differ per cascade, so the depth can't stay bit-identical.
Also not done: stage 2 (layered rendering). After stage 1, submit is the larger cost, ≈ 0.7 ms per cube on NVIDIA
and ≈ 1.5 ms on Intel. That is mostly the fixed price of 6 draw commands per cube, which stage 2 would remove.

Side fix: GPU timer names (`r_speeds 100`) were stored as pointers into rotating `va()` buffers and printed with
other views' names. The name is now copied into the command and the timed block.

Stage 2 step 0 (same day). Layer selection from the vertex shader is available on both machines: the RTX 2060
exposes `GL_ARB_shader_viewport_layer_array` and `GL_AMD_vertex_shader_layer`, the Intel UHD only
`GL_AMD_vertex_shader_layer`. Both have `ARB_shader_draw_parameters`, `ARB_multi_draw_indirect` and
`ARB_texture_view`. `r_shadowStats` now also splits the backend time of every cube face command. After stage 1 a
face command costs ≈ 92 µs on Intel and ≈ 98 µs on NVIDIA:

| Phase (µs per face command, stage 1) | Intel | NVIDIA |
| --- | --- | --- |
| `RB_BeginDrawingView`: FBO bind, viewport, state, clear | 8 | 35 |
| Recording (draw surfaces → draw items) | 30 | 21 |
| Executing (`RB_SubmitRenderPass`) | 53 | 40 |

A face has only ≈ 4–5 draw items, because consecutive surfaces with the same shader are already merged. The
cost is in what repeats per face: the FBO switch and clear on NVIDIA, item execution (≈ 12 µs each) and surface
recording on Intel. Each face also re-records copies of the same ≈ 43 unique surfaces per cube.

Layered rendering would bind one FBO and clear once per cube, record each surface once, and execute ≈ 5 items per
cube instead of ≈ 26. Estimated saving: ≈ 1.7–1.9 ms of CPU per frame with 4 lights on both machines (submit
≈ 2.2 → ≈ 0.5 ms). There is no cheap halfway step without `gl_Layer`: a layered FBO without layer selection
draws everything into layer 0. Open prerequisite: texture views need immutable storage, and
`R_Create2DImageArray` calls `glTexStorage3D` with `levels = 0`, which the spec rejects; this has to be checked.

Stage 2 implemented (same day, `r_shadowLayered 1` on top of `r_shadowSinglePass 1`, default 0). The capability bit
`MODERN_VS_LAYER` requires a GL 4.3 context, the AMD or ARB layer extension and `glTextureView`, and a test compile
of a `#version 150` vertex shader writing `gl_Layer`. All generic and lightall variants then get `USE_VS_LAYER`
(no new permutation bit). The face comes from `u_CubeLayer` and the face matrices from `u_CubeFaceViewProj[6]`.
Each cube slot has one layered FBO on a 6-layer texture view, so a whole cube is one command: one bind, one clear,
each surface recorded once, draw items batched by face mask and drawn once per face. Cubes holding surfaces that
build their own draw items (sprites, weather, POM shells, water particles, entity beams) fall back to stage 1.
`r_shadowVerify 1` with `r_shadowLayered 1` compares against stage 1: 12/12 cubes on Intel and 12/12 on NVIDIA were
bit-identical. Submit with 4 lights dropped from ≈ 2.0–2.1 to ≈ 0.7–0.9 ms per frame on Intel and from 2.1 to
0.7 ms on NVIDIA, so ≈ 1.2–1.4 ms saved; the estimate was 1.7–1.9 ms. What remains is item execution (≈ 12–17 µs per
item, each drawn up to 6 times).

The `glTexStorage3D(levels = 0)` call in `R_Create2DImageArray` was dead code, since every caller passed
`IMGFLAG_MUTABLE`. It now passes 1 level, and the point shadow array is immutable when `MODERN_VS_LAYER` is set.

### Skyportal view on vjun2: hidden pixels (r_skyPortalCull)

2026-10-19, same runs and scene as above (vjun2 spawn, 1920x1080, `r_dlightMode 2`, `r_forwardPlus 1`). Before the
fix, the "Skyportal View" timer was 173–235 ms per frame on the Intel UHD, against 2.7–4.4 ms on the RTX 2060 and
≈ 9 ms on Intel on yavin1. That was 60–75 % of the Intel frame.

How it was narrowed down (temporary instrumentation, since removed):

- **CPU time inside the block: 0.7–3 ms.** The GPU timestamps are real. A glFinish wall clock around the
  skyportal main pass gave the same 185–255 ms.
- **Where the time goes:** begin view (clears) ≈ 0, depth prepass 1.5–2 ms, main pass 170–250 ms.
- **The bisection didn't help:**
  - `r_shadowPCSS`, `r_forwardPlus`, `+set r_sunShadows 0`: within noise.
  - Turning dynamic lights off for the skyportal: within noise.
  - `+set r_dlightMode 1`: ≈ 125 ms instead of 230. It is a smaller lightall permutation (no `USE_DSHADOWS`); the
    light loop itself costs nothing here.
- **Per draw item** (glFinish plus a `GL_SAMPLES_PASSED` query around each item):
  - The skyportal on vjun2 is a detailed hangar room (`vjun/hangerwall`, `largewall`, `wall_garrison`,
    `basemetal*`…).
  - It shades ≈ 1.95 M fragments, the whole 1080p screen, with `lightall_LMAP_N_MR_SUN` at 110–120 ns per fragment
    on Intel. The main view's surfaces with the same program cost the same per fragment.
- **None of it is visible.** After the main view's main pass, 0 of 2,073,600 pixels are at the far plane. Main-view
  sky triangles are not drawn when a skyportal exists, and sky shaders are not in the depth prepass. So the main
  view covers the whole skyportal at this spot.

So the cause is not an Intel-specific skyportal path. The skyportal view is rendered first, at full resolution,
under a main view that hides it. On vjun2 it is a full screen of the most expensive lightall permutation, at
≈ 115 ns per fragment on this GPU (≈ 1.5 ns on the RTX 2060).

Fix, `r_skyPortalCull` (default 1):

- Every view is executed right after its frontend (`R_IssuePendingRenderCommands` per view). So the skyportal's
  `R_RenderView` also walks the world once with the main view's parms, which `R_GatherFrameViews` already computed
  (`R_SkyPortalCollectOccluders`, tr_main.cpp).
- It keeps the surfaces the main pass draws opaque over every pixel they cover: world, `SS_OPAQUE`,
  `DEPTHPREPASS_SIMPLE` (no blend, alpha test or deform), no distortion, water interface or POM shells.
- After the skyportal's `RB_BeginDrawingView`, `RB_SkyPortalOccluders` draws them depth-only with the main camera,
  and `GL_DepthRange` is forced to 0. Every skyportal fragment there then fails the depth test before shading. The
  main view clears depth before its own passes.
- The extra walk costs 0.1–0.25 ms of frontend CPU on the skyportal view.

| vjun2 spawn, per frame | Intel UHD skyportal / main | RTX 2060 skyportal / main |
| --- | --- | --- |
| `r_skyPortalCull 0` | 173–182 / 69–77 ms | 2.7–3.0 / 3.8–4.3 ms |
| `r_skyPortalCull 1` | 4.3–4.4 / 73–75 ms (occluder pass ≈ 3 ms) | 0.2 / 3.6–3.7 ms |
| yavin1 (85 % of the screen shows the portal), 0 → 1 | 9.2–9.5 → 5.5–7.5 ms | not measured |

Checks:

- With the cull on, every skyportal draw item passed 0 samples on vjun2.
- **Sentinel test** (same frame): the skyportal's color was cleared to (0, 65000, 0) before its passes, then the
  pixels still carrying it were counted after the main view's main pass. Result: 0 with the cull on and off, on
  vjun2 (Intel and NVIDIA) and on yavin1 (Intel). Every culled pixel is overwritten by the main view.
- Screenshots don't work in the hidden-window runs, so there was no visual A/B.

Known differences:

- A translucent surface that writes the main depth prepass (`r_sunShadowAlphaCasters` alpha casters) in front of
  an occluder used to blend over the skyportal there. Now it blends over the skyportal's clear color. Either way,
  the opaque wall behind it is hidden.
- With `r_skyPortalCull 0` the behaviour is unchanged.

Not addressed: `lightall_LMAP_N_MR_SUN` is ≈ 75× slower per fragment on this Intel GPU than on the RTX 2060, and
that also makes the main view 70–100 ms here. The `r_dlightMode 1` result (a smaller permutation runs faster
everywhere on screen) suggests register pressure or spilling in the big permutations, not the lighting work
itself. That needs its own look at the compiled program (next section).

### lightall per-fragment cost on Intel (debug views, cascade loop)

2026-10-19, follow-up of the skyportal work. On the Intel UHD (driver 27.20.100.8280),
`lightall_LMAP_N_MR_SUN` and its `GRID` / `VERT` siblings cost 80–95 ns per fragment at the vjun2 spawn, against
≈ 1.5–3 ns on the RTX 2060. That made the main view 133–137 ms per frame here (1080p, `r_dlightMode 2`,
`r_forwardPlus 1`, PCSS on).

**Cause: register pressure from code that does not run.** It is not the lighting or shadow work:

- **Offline harness.** The program the game compiles (dumped from the renderer, preprocessed) was drawn full
  screen in a hidden GL 4.3 context, with every uniform zero (no lights, PCSS off). It still took 66–75 ns per
  fragment on the Intel GPU. `lightall_SUN` took 0.6 ns.
- **SIMD width.** A `GL_ARB_shader_group_vote` probe (`allInvocationsARB` around one marked pixel) shows every big
  permutation runs as SIMD8, including the fast variants below. `lightall_SUN` runs as SIMD16. So the slow tiers
  are SIMD8 programs that spill, not a SIMD16 / SIMD8 switch. The driver gives no ISA or spill counts:
  `GL_INTEL_performance_query` only has pipeline statistics, and the program binary is opaque.
- **Dead code costs.**
  - With no lights, `CalcDynamicLightContribution` returns before its loop. Removing it anyway took the program
    from 68 to 28–33 ns.
  - Removing only the cube / spot shadow code of the DSHADOWS loop gave 41–45 ns. That is the `r_dlightMode 1`
    effect.
  - Removing the five debug exits at the end of `main()` gave 25–38 ns. That includes the flat
    `u_MaterialDebug` color: on its own, that exit is worth ≈ 15 ns.
- **The work is cheap.** Without the debug exits, the sun cascade code is most of what is left (30 → 4 ns
  without it). Its texture fetches cost nothing measurable: removing every fetch, or the PCF and blocker loops,
  changed nothing. What matters is the structure. `sunShadowModern` inlined `EvaluateSunCascade` five times (one
  call per branch, two in each blend band).

Bisection in the harness (Intel, ns per fragment, two interleaved rounds):

| `lightall_LMAP_N_MR_SUN` variant | ns / fragment |
| --- | --- |
| stock | 66–75 |
| no `CalcDynamicLightContribution` / no sun shadow / neither | 28–33 / 16–22 / 4 |
| no debug views | 31–38 |
| no debug views, cascades in one loop (same math) | 24–25 |
| no debug views, one cascade, no blend band, gradients from world derivatives | 7–8 |

The same variants in game, through the renderer's source override (per draw item: glFinish plus
`GL_SAMPLES_PASSED`, vjun2 spawn, two runs each, interleaved):

| variant | LMAP | VERT | GRID | main view (r_speeds) |
| --- | --- | --- | --- | --- |
| stock | 81–96 | 78–85 | 84–95 ns | 133–137 ms |
| no debug views | 46.5 | 44 | 51–54 ns | 73–75 ms |
| no debug views, one cascade, no blend | 15–17 | 11 | 16–19 ns | 29.5–32 ms |

**Fix (uncommitted, default behaviour, nothing to set):**

- **`USE_LIGHTALL_DEBUG`.** The debug exits of lightall are now compiled only while one of their cvars is set:
  - the exits: `r_shadowDebug`, `r_pbrEnergyDebug` 1–4, `r_materialModelDebug`, `r_specularAADebug`,
    `r_spotLightCookieDebug`, and the flat material colors;
  - the cvars that drive those colors: `r_autoPBRDebug`, `r_autoFoliageDebug`, `r_leafFlutterDebug`,
    `r_emissiveDebug`, `r_skinSSSDebug`, `r_volumetricFogDebug 69`.

  All of them are runtime cheat cvars. `GLSL_UpdateLightallDebug` (tr_glsl.cpp, called from `RE_BeginFrame`)
  rebuilds the loaded lightall programs when the set flips. It took 3.6–4.2 s for 10 programs on Intel, and the
  debug view then works (78 ns per fragment while it is on).
- **Cascade loop.** `sunShadowModern` first picks the cascade, or the pair blended across a split, then runs one
  `EvaluateSunCascade` in a loop of one or two iterations: `result = r0`, then `MixSunCascadeResults(result, r1,
  t)`. The math is the same, with one inlined copy instead of five.

| vjun2 spawn, 1080p | Intel LMAP / VERT / GRID / GRID_SK (ns per fragment) | Intel main view | RTX 2060 main view |
| --- | --- | --- | --- |
| stock | 84–86 / 79–81 / 84–86 / 156–161 | 133–137 ms | 3.01 ms |
| fix | 35–39 / 25–28 / 38 / 88–101 | 55–62 ms | 2.93 ms |

**Image.** The program the game compiles was compared bit for bit with the stock one, on both GPUs. The inputs
were pseudo-random uniforms, uniform blocks and textures, with orthographic cascades whose blend bands cross the
screen, and two point lights, one of them cube shadowed:

- No dynamic light: identical.
- With the lights: 1–62 of 8.3 M channels differ, by at most one RGBA16F ulp (≤ 0.1 % relative). That is
  rounding. The removed debug accumulators (`g_energy*`, `g_materialSpecular`) change how the compiler associates
  the light loop's products.
- The cascade loop on its own adds no difference.

There was no screenshot A/B: screenshots do not work in the hidden-window runs.

**Not done (recommendation).** The remaining Intel cost is still ≈ 20× the RTX 2060 per fragment, against the
usual 12–15×. The measured way down is the "one cascade" row: LMAP 15–17 ns, main view ≈ 30 ms. It needs two
image changes, so it is a decision for the user:

- **Receiver-plane gradient from world-space derivatives.** One `dFdx` / `dFdy` of the world position, and
  `0.5 * mat3(M) * dP` per cascade. That is exact for the orthographic cascade matrices and avoids the
  cancellation in `dFdx` of the projected position. It is therefore slightly more precise, so not bit-identical.
- **No two-cascade evaluation in the blend band.** For example, dither the cascade choice across the band
  instead of blending two full PCSS evaluations.

Either change alone was not enough in the harness. The other debug views, entity grid (`r_entityLightGridDebug`)
and diffuse IBL (`u_DiffuseIBLParams.y`), are in other permutations and were left as they are. The skinned
permutations (`_SK`) are still 88–101 ns.
