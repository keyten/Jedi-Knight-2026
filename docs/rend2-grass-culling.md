# Surface sprite GPU culling and per-plant LOD

`r_surfaceSpritesCompute` (default **2**) selects the draw path without a restart:

| Value | Behavior |
|---|---|
| 0 | Compact records, direct submission, original whole-surface LOD |
| 1 | GPU frustum/fade culling; identical whole-surface LOD |
| 2 | GPU culling plus per-plant bend/card LOD |

The implementation is in `tr_spritecull.cpp`, `glsl/sprite_cull.glsl` and `RB_SurfaceSprites`.
GL 4.3, compute, three compute SSBO blocks, adequate buffer limits, indirect entry points and a linked
kernel are required. Other contexts retain direct instanced submission. Effect, weather, face-up,
additive, fog-modulated and **all blended** stages retain that path too; alpha-test plus blending still
needs ordered submission. Ordinary nonblended alpha-test Yavin grass uses the GPU path.

## Records and submission

All paths store one 64-byte record per plant: position/seed, normal, color, dimensions/skew. This replaces
four 56-byte records (224 bytes) per plant. Quads share six 16-bit indices and retain four unique vertices,
the original triangle diagonal and interpolation. Strips generate their configured vertices.

Compute reads the static record VBO as an SSBO. It writes one integer record index per survivor into one
of six lists: quad/strip times one/two/three cards. Raster shaders read that index as an instanced attribute
and fetch the four RGBA32F texels through a texture buffer. No vertex-stage SSBO support, base-instance
extension, CPU counter readback or CPU upload of visible plant lists is required.

The GPU also writes instance counts into 20-byte indirect commands. Indexed quad commands use all five
fields; array strip commands consume their first four (the fourth is zero). The barrier covers command,
vertex-attribute and subsequent SSBO accesses. Each surface reserves six full worst-case lists before
any dispatch; each invocation writes at most one plant, so its selected list cannot overflow. Arena
exhaustion falls back for the complete remaining surface and emits one warning. Allocation/dispatch
errors disable new compute submissions until renderer restart; already cached successful lists remain
valid for later passes of their view.

There are two frame arenas, each sized for 1,048,576 candidate plants across all views and 8,192 surface
jobs. Buffer orphaning protects GPU latency. The cache key includes renderer frame, scene, view and
surface. Prepass, fused/separate velocity and color reuse the same lists and geometry. Light views get
independent lists and never reuse main-view visibility.

## Bounds and LOD

A plant's culling sphere contains the rest stem's complete arc length, its maximum fade-widened card,
the skew, billboard rotation and the original 0.2-width corner lift. `FoliageBladeBend` preserves the rest
stem arc length for arbitrary wind/interaction values, so this bound covers both current and previous
poses without assuming a small displacement field. Camera-facing and flattened variants receive their
additional rotation/width allowance; one world unit covers projection/raster rounding.

The six inward planes come from the actual view's world-to-clip matrix. Depth-clamped light views use
only the four lateral planes. Sun shadows use the main camera for billboard orientation, fade and LOD,
but the light view for frustum rejection. Fully faded plants are rejected at the material's fade endpoint;
partially faded plants keep the original shader calculation.

Mode 1 retains original surface LOD. Mode 2 keeps full bend segments whenever a plant's deformation
sphere reaches `r_grassBendLodDist`; farther plants use the original quad chord of the same deformation.
It removes a third adaptive card only beyond `1.33 * r_grassCardLodDist`, after the original fade has
finished. It adds no one-card tier and never rotates the surviving cards. Near detail is retained; distant
bend detail is an intentional image difference and should be evaluated separately from mode-1 equivalence.
Existing width, bend, wind, interaction, material, fog and comparison/debug settings still apply.

## Verification and diagnostics

`tools/rend2/test_sprite_cull_gl.py --permutations` creates a hidden SDL context and executes the production
kernel. It checks randomized perspective/light frusta, near-camera crossings, fade/LOD boundaries,
nonzero buffer offsets, guarded dispatch tails, exact capacity and all-plants-in-one-bin cases. Transform
feedback compares original duplicated records, compact direct submission and real indirect list draws:
positions, UVs, color, alpha and previous-frame positions must match exactly. It checks deformation bounds
with strong wind, opposite current/previous interaction fields, negative heights and large skew.

`rend2_sprite_cull_test` (enabled with `BuildCharacterCullTests`) tests the actual whole-surface allocator,
including exhaustive small capacities, exact ends, unchanged failure cursors and integer overflow.

`tools/rend2/test_sprite_cull_runtime.py` stages private binaries/home and reads installed assets without
changing them. Its frozen UI pause and shader clock compare modes 0/1 and repeated mode 0 exactly, then
capture intentional mode-2 LOD differences. It records real submitted/culled plants and cached-pass reuse.
`r_speeds 100` prints GPU timers named `Surface sprites` and `Surface sprite cull`; `r_speeds 1/100` also
reads the previous frame's GPU command counters for diagnostics. Those diagnostic modes can wait for the
GPU. Ordinary frames do not read the counters.

## Measured result (25 November 2026)

The [checked-in receipt](rend2-grass-culling-validation.json) records SP/MP Release builds, the Release
CPU allocation test, hidden production-kernel contracts and 196 raster shader variants on Intel UHD
27.20.100.8280 and RTX 2060 616.92. Both GPUs passed exact old/compact/list geometry and velocity parity.
The additional extreme negative-height/skew deformation-bound assertions were run on Intel.

A private native `yavin1` run at `1024 -4700 210 90`, 640x360, enabled sun cascades and fused velocity.
With card mode 3, bend distance 350 and card distance 300, it produced these actual all-pass counts:

| Path | Candidate plants | Submitted | Culled | Cards | Unique submitted vertices | Dispatches / cache reuse |
|---|---:|---:|---:|---:|---:|---:|
| GPU culling, original LOD (1) | 5,852 | 774 | 5,078 | 2,277 | 22,230 | 28 / 7 |
| GPU culling, per-plant LOD (2) | 5,852 | 774 | 5,078 | 1,858 | 13,312 | 28 / 7 |

The repeated reference and same-LOD culling captures have **zero differing channels**. Mode 2 also had
zero differing pixels in this occluded camera despite its lower submitted geometry; this is evidence
of work removal, not a general visual assessment of distant LOD. The hidden tests separately check
near-plant preservation and exact bin/command contents. The installed assets and game binaries were
not modified; no foreign game process overlapped the run.

Measured median grass draw+cull times were 1.858 ms for the first direct reference, 1.199 ms for its
repeat, 1.878 ms for mode 1 and 1.800 ms for mode 2. GPU counter/timer diagnostics may wait, and the direct
reference drift is too large to establish a net improvement. The original validation snapshot retained
mode 0; the subsequent user-selected default is now mode 2. The compute path has real submitted-work
reduction; batching the current per-surface dispatches
is a further performance opportunity. No FPS or percentage speedup is claimed. MP and RTX game timing
were not run.


The user-selected enabled-default update sets mode 2 for fresh/unset cvars in both SP and MP. Previously archived values remain authoritative; no installed configuration is rewritten. The earlier validation timings above describe the prior opt-in decision and do not prove a net frame-time gain.
