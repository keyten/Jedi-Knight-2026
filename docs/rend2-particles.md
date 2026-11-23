# Legacy particle rendering improvements

## Stage 1: soft intersections and near-camera visibility

World sprite and oriented-quad stages can fade against a separate scene-depth
copy. It is taken after opaque/decal/fog and modern water depth, before the
remaining transparent layers. The copy is never the attachment being drawn to;
MSAA depth is resolved by the same blit. The default main view has a null
`targetFbo` (resolved to `renderFbo` by the backend); explicit renderFbo views
are supported too. Orthographic, shadow, sky-portal, view-model and no-world
draws do not use this effect.

The shader fades alpha for straight-alpha blending, and RGB plus alpha for
premultiplied/additive ONE blending. Ordinary geometry uses its existing generic
permutations. No new texture or stock EFX override is needed.

| Control | Default | Meaning |
| --- | --- | --- |
| `r_softParticles` | 1 | 0 off, 1 alpha sprites, 2 includes additive sprites; restart |
| `r_softParticleDistance` | 8 | Intersection fade distance/cap in world units; 0 disables contact fading |
| `r_particleNearFade` | 1 | Fade from the actual near plane across the next 16 units |
| `r_particleCulling` | 1 | Conservative near-camera FX culling; 0 restores the old cutoff |
| `r_particleSort` | 1 | Particle-only ordering when general transparency sorting is disabled |
| `r_transparencySort` | 1 | Compatible glass, sprites and translucent surface packets ordered together |
| `r_particleVolume` | 1 | Rounded view-depth thickness for supported alpha billboards |
| `r_particleSoftScale` | 0.1 | Flat fallback distance as a fraction of radius; 0 retains fixed-distance fading |
| `r_particleBatching` | 1 | Batch adjacent compatible depth-ordered single-pass sprites |
| `r_particleDepthOnDemand` | 1 | Skip the depth copy when no draw reads it |

All controls in this table are **latched** and require `vid_restart`, including
the controls introduced by the previous particle changes. See the complete
`docs/samples/particles-legacy.cfg` and `particles-modern.cfg` presets. Diagnostic
profiling and fixture placement commands remain immediate.

In both JA SP and MP, near fading replaces the old distance cutoff for world
FX particles when using rend2. Camera-facing quads are discarded behind the
camera plane; oriented quads use a conservative corner bound, including the
possible overshoot of size waves. Depth-hack/player-view exclusions retain
their legacy behavior. Setting `r_particleCulling 0` restores legacy culling,
independently of the shader's near fade.

### Validation

- `tests/fx_visibility_tests.cpp`: 20,000 geometry/size-wave cases, comparing
  the production culler with projected corners of independently rotated quads.
- `python tools/rend2/test_particles_gl.py`: 68 hidden-context cases with the
  production generic fragment shader, covering intersection, near fade,
  straight alpha, premultiplied alpha and both additive forms.
- SP/MP engine, SP game and both rend2 DLLs built. This checkout's existing
  generic lambda in lens water requires C++14 for GCC; Windows GCC renderer
  test builds also need `-Wl,--export-all-symbols` for their entry points.
- `tools/rend2/capture_particles.py` mounts only stock assets0–3, uses an
  isolated home and controlled renderer puffs with the stock smoke material.
  These are renderer integration fixtures, not replacement EFX assets or a
  claim of visual review of every gameplay effect. A/B/A controls permit at
  most 0.02% of pixels differing by one display code due to sparse FP16/output
  rounding (also observed at one pixel without MSAA); larger changes fail.
  Reports retain exact equality, maximum delta and changed-pixel counts.
  Contact fixtures use stock alpha-smoke sprites. A separate no-smoke control
  must differ by at least 12 display codes in 2,000 pixels, with a peak
  difference of at least 24 (smaller thresholds for tiny particles and actual
  stock EFX). The contact A/B must change at least 2,000 pixels by 12 codes,
  with a peak of at least 24. Captures are also inspected visually.

Developer command `r_particleTest <distance> [radius] [count]` draws controlled smoke
in front of the rendered view. `r_particleTest 0` removes it. With `surface`
in place of distance it probes the center depth once and places
the puff two units in front of that surface (developer-only GPU readback).
It is registered and removed with the renderer. `r_hiddenWindow 1` creates an SDL window hidden
from the start and reports fatal errors to the log without a dialog. Capture
launches also set `s_initsound 0`, `in_mouse 0`, `in_nograb 1`, `r_fullscreen 0`;
the harness checks that its process creates no visible windows.

## Stage 2: alpha-particle order

With `r_transparencySort 0`, `r_particleSort 1` sorts compatible world sprites and
oriented quads back to front by view depth. Each authored render layer remains
separate. Ordinary surfaces retain their slots; particle passes are kept in
their authored stage order, instead of sorting all first passes before all
second passes. Entity-merge batches are split where independent particle
ordering is needed. Pure additive layers retain their previous batching/order.

Depth-writing, modulate, deformed, refraction, forced-post, view-model and
shadow draws retain their existing handling. This is center-depth particle
sorting, not general order-independent transparency for intersecting geometry.

`tests/particle_order_tests.cpp` checks 1,000 shuffled multi-pass layers,
depth order and preservation of ordinary slots. `capture_particles.py --sorting`
adds near red/far blue stock-smoke clouds. It requires a clear color change,
an A/B/A repeat and the same corrected image when the cloud submission order
is reversed. Developer fixtures: `r_particleTest sort` / `sort-reverse`.

Neither stage reconstructs 3D smoke from its texture.

## Stage 3: measured submission cost

Scene depth is now copied only when an actual draw binds the particle-depth
texture. The developer contact fixture's pending depth probe is an exception.
Empty scenes, near-fade-only sprites and additive-only scenes with the default
alpha-only soft mode do not pay for an unused depth blit. Dependency scanning
uses the remaining draw order after modern water items leave the main list.

`r_particleBatching 1` (default, latched) depth-sorts the eligible surface
list before tessellation, then merges adjacent compatible single-stage sprites.
Quad index order remains back to front. Each batch keeps its first/farthest
particle's sorting tag; tessellation overflow splits retain this tag and their
submission order. A final particle sort still handles other materials and
multi-pass particles. This reduces submission work without simulating FX on
the GPU or changing their assets.

Batching requires the same shader, fog, render state, shader time and existing
surface-batch keys. Only two-sided, generic stages using vertex RGB and vertex
or identity alpha qualify. Entity flags, frame animation, deforms, separate fog
passes, lighting stages, texture-coordinate modifiers and video textures retain
per-particle draws. Pure additive layers retain their existing path.

The general state sort now breaks equal-key ties by original draw-item index.
Previously, changing the number of particle draws could also permute unrelated
transparent surfaces, observed on stock bacta bubbles. The integration check
compares the entire image, including those surfaces.

`r_particleProfile 1` prints CPU microseconds for main-pass recording and
submission, plus the particle sorting subset, total draw items, tagged particle
draw items (including overflow splits/stages, not logical particles), and depth
copies. CPU timings include ordinary geometry in that pass. Output is emitted
outside the main-view GPU timestamps. With `r_speeds 100`, the existing Main View
timestamp and a separate Particle depth timestamp are reported too; they are
render-path elapsed times, not a gameplay FPS claim.

The original stage-3 capture harness checked all visual
fixtures, then measured empty/64/512-puff scenes with batching off/on/off in the
same process. Each case warmed up for 120 waits and sampled for 240 waits; the
first 20 timing records were discarded. The 48-puff batching fixture interleaves
two stock materials, distinct depths and vertex colors, and also reverses input
order. Its complete output must agree with unbatched rendering within the
rounding tolerance above. Controls: `r_particleTest batch` / `batch-reverse`.
The unit test additionally checks 1,000 interleaved 16-layer surface lists.

### Recorded result (historical: 52c2cf8be)

960x540, stock Hoth3, isolated legacy lighting profile, NVIDIA RTX 2060,
no MSAA. Medians of 100 records per case; the two unbatched controls bound
the values below. CPU is the median of recording + submission per record,
including the ordinary main-pass geometry. Main View is the GPU timestamp
interval, which can include driver/CPU submission gaps. GPU clocks were not
locked, so these are controlled-scene measurements, not universal speedups.

| Fixture | Particle draws off → on | CPU main pass off → on | Main View off → on |
| --- | --- | --- | --- |
| 64 puffs | 64 → 1 | 1.253–1.268 → 1.015 ms | 3.432–3.482 → 2.677 ms |
| 512 puffs | 512 → 3 (overflow splits) | 3.585–3.609 → 1.326 ms | 5.993–7.170 → 2.804 ms |

The empty fixture has zero particle-depth copies (one before demand gating).
The 48-puff colored/material-interleaved fixture is bit-identical with batching
off/on, reversed submission, and the off repeat. In that same native run the
contact/near A/B/A controls are bit-identical, and the sorting change affects
105,480 pixels by at least 12 display codes. Raw output and screenshots are
in the worktree's ignored `build/ja-fx-evidence/profile-final/` directory.
The final MSAA 4x run also passes: contact/near repeats differ at 45/41 pixels,
and the batched/unbatched comparison at 54 pixels, all by at most one display
code. See `build/ja-fx-evidence/msaa-final/`. Both renderer DLLs build; native
game captures exercise SP, not a complete MP gameplay session.

## Glass integration, contact shape and compatibility

`r_transparencySort` extends the depth order to compatible transparent world
surfaces and model surfaces. The center comes from each BSP surface's culling
bounds (transformed for brush entities), polygon vertices, or model origin.
All passes recorded for that surface share a packet owner. This fixes the stock
Hoth infirmary glass tube's environment-reflection pass drawing over nearer
smoke. Authored render layers remain separate; deformed, depth-writing,
refraction and special entity passes retain their previous handling. This is
surface-center ordering, not per-pixel OIT for arbitrary intersecting meshes.

`r_particleVolume` retains the stock texture and estimates rounded thickness
along view depth. The fragment integrates the visible fraction of that interval
up to opaque scene depth, mapping the texture's opacity through optical depth.
The raster proxy uses a conservative front depth without writing it to the
depth buffer, so an opaque surface behind the billboard center can still see
the front part of a club. Fully visible texels retain authored opacity exactly.
Radius is supplied per quad in an existing texture-coordinate stream, preserving
batching for different sizes. Oriented quads and modified UV stages retain the
flat fallback. Multi-pass, auxiliary-texture, lighting, video and deformed
materials also keep their original UV channels and flat fallback. This is a
2.5D rendering approximation, not smoke simulation,
collision response, or a reconstruction of 3D texture density.

The flat fallback can cap its fade width by particle radius using
`r_particleSoftScale`. Volume 0 plus scale 0 reproduces the previously introduced
fixed-distance soft intersection. Disabling all controls with the legacy preset
also restores the old state-sort tie handling and entity-merge boundaries;
the former unconditional stable-sort and particle-isolation changes are gated.
Normal legacy play allocates no particle depth image. Developer contact probes
can allocate one lazily without enabling any rendering feature.

`capture_particles.py` is now a compatible entry point to
`capture_smoke_volume.py`. It starts separate processes per latched variant,
compares repeated controls, requires visible smoke against an empty capture,
and can collect median/p95 CPU and GPU timings across fifteen fixtures. `fxcam`
is a developer-only fixed camera for SP, avoiding player-camera smoothing and
the player model. `r_captureShaderTime` is a latched cheat diagnostic (-1 by
default); captures hold only the shader clock to avoid animated stock materials
invalidating comparisons. `r_particleTest info` reports active/pending settings
and latch flags. The latch test checks that a pending change leaves the image
unchanged and all ten controls become active only after `vid_restart`.
