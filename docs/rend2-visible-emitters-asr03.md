# ASR03: visible emitters from retained game geometry

2026-11-09. Inspected parent `c3431f732`, active `rend2-unified-wip`.
The checkout is `OpenJK-rend2`, with origin `keyten/BetterJK`; the task names
`keyten/Jedi-Knight-2026`. No remote or branch was rewritten.

**Use existing geometry and material evidence to propose emitting regions; add
fixture membership only where object identity is evidenced.** The executable
demonstration associates five existing region candidates with three controlled
fixtures. This is a useful grouping primitive, not a new emitter detector or a
successful stock-map lighting reconstruction. All hypotheses remain conditional;
historical source identity, intensity and bake participation are unverified.

## Primary-source findings

### Li et al., ECCV 2022

Read [the paper and supplementary material](https://arxiv.org/html/2205.09343v2),
especially §3.1–3.2, §4.1–4.4 and supplementary C.2, equations 16–24.
The [authors' project](https://vilab-ucsd.github.io/ucsd-IndoorLightEditing/)
identifies the release.

Visible lamp geometry starts with depth under an instance mask. The predicted
center stays on its initial camera ray. For unit center direction `d`, visible
point `q` and normal `n`, hidden samples are:

```text
q' = q + 2(c - dot(q,d)d)
n' = n - 2 dot(n,d)d
edge center = (q + q')/2
```

This reflects across a plane through `c`, normal to `d`; it is not point inversion
`2c-q`. Eroded mask boundaries define connecting edge patches. Lamps use
Lambertian radiance; windows use rectangles and three directional spherical
Gaussians. Direct shading samples finite emitting surfaces.

Dependencies: an LDR image, camera/depth geometry, visible-source instance masks,
and estimated albedo/normals/roughness. Masks may be manual or Mask R-CNN plus
graph cut; four source networks and a material network predict parameters.
Shadow inpainting, indirect shading and local lighting also use learned models.
Multi-view geometry is not required. Single-view hidden completion, ambiguous
depth scale and the one-invisible-lamp/window simplification are unsuitable here.
Transfer finite source geometry; reject neural inference and hidden-surface
completion when retained triangles exist.

### Peng et al., 2025

Read the publisher's [full paper, pp. 2052–2060](https://www.jcad.cn/cn/article/pdf/preview/10.3724/SP.J.1089.2024-00342.pdf)
and [publication record](https://www.jcad.cn/article/doi/10.3724/SP.J.1089.2024-00342).
The inspected PDF is nine pages; methods are §2.2, §2.4–2.6, pp. 2054–2056.

The exact detection sequence (§2.5) averages RGB channels, repeatedly samples
`Nc` image pixels, retains the brightest, and repeats `K` times to form a mask.
Rendered scene depth lifts/fuses masks into a point cloud. Threshold `delta_e`
selects emitter points; DBSCAN clusters them; the first `k` clusters are retained.
These symbols should not receive invented universal thresholds. Detection does
not require a segmentation network.

Scene/object alignment uses scene SfM, object-image SIFT correspondences, PnP
camera poses, then a least-squares global transform between pose sets. NeuS
reconstructs object geometry; Blender unwraps its mesh; the real scene uses
MonoSDF and COLMAP. Material optimization estimates reflectance, roughness and
tangent-space normals from multi-view images. Explicit nearby emitter samples
and a low-resolution environment term enter rendering separately; emitter
strengths and materials are optimized together.

Transfer finite world-space source support and per-source sampling. Replace
brightness selection, depth fusion, DBSCAN and coordinate recovery with retained
triangles, material masks and instance transforms. Reject image reconstruction,
joint reflectance recovery and neural dependencies for this prototype.

## Current code audit

| Existing code | Observed capability | Consequence |
|---|---|---|
| [`evidence.cpp`](../shared/rd-rend2/lightrecon/evidence.cpp), `SurfaceEvidence` | Starts from `MaterialGlow` or positive `legacySurfaceLight`; skips sky/nodraw. Accepts faces/triangles; unsupported geometry is retained without invented pose. | Already discovers material-supported regions. A bright name, unlit stage or bright lightmap alone does not pass this gate. |
| [`tr_lightrecon.cpp`](../shared/rd-rend2/tr_lightrecon.cpp), original material capture | Copies selected parsed glow/emissive/physical-emissive facts, first emission texture, cull, colour modulation, animation, transforms and emission-layer count. | Reuse these facts. Do not substitute shader names or assume complex stage composition is solved. |
| Same file, `SourceEmissionMasks` | CPU source image decode, assumed sRGB-to-relative-linear RGB, box reduction to 128², bounded temporary cache; no GPU readback. | Colour prior only. Alpha and multiple layers are unresolved; neither texture RGB nor BSP RGB8 is calibrated irradiance. |
| [`emission_geometry.cpp`](../shared/rd-rend2/lightrecon/emission_geometry.cpp) | Exact-position, same-material/plane grouping in source-connected mode; UV barycentrics, nearest mask lookup, repeat/clamp, world-area weights, 8-neighbour blobs, planar PCA rectangle. Default axis limit 96, threshold 0.1. | No new segmentation, planar fitter or full model importer is needed. Group connectivity uses a shared position, not necessarily an edge; it does not recover semantic object identity. |
| `EmitterEvidence` / `EmitterCandidate` | IDs, BSP surface/entity references, region center/frame/area, shape alternatives, colour/unit flags and explicit issues. Glow and compiler hints can alias one candidate. Missing/black masks retain uncertain whole-surface hints. | Iterate candidates once, preserving every evidence alias. Never sum a glow and its compiler hint as two emitting regions. |
| [`emitter_support.cpp`](../shared/rd-rend2/lightrecon/emitter_support.cpp), `SourceSupport::FromSurfaces` | Exact named original triangle union, limited to static connected coplanar support; rejects multi-plane/disconnected assemblies. | A mask blob's bounding rectangle and a whole BSP surface are different footprints. Neither licenses an exact luminous-mask support certificate. |
| [`candidate_pool.cpp`](../shared/rd-rend2/lightrecon/candidate_pool.cpp), `Candidates::FromEvidence` | Adapts the existing immutable evidence lease without replacing canonical lights. | Keep production ingestion and historical fitting unchanged. This task adds only tool-side views. |
| [`offline_bsp.cpp`](../shared/rd-rend2/lightrecon/offline_bsp.cpp), BSP capture | Original BSP geometry/spatial data; non-world brush models retain state uncertainty. | Does not independently load external MD3 instances. Unknown external geometry cannot be declared empty. |

### `misc_model` and MD3 boundary

[`SP_misc_model`](../code/game/g_misc.cpp) documents compilation into BSP triangles
and frees the retained game entity. Those triangles already reach surface
extraction; original model/instance ownership may have been lost. Do not import
the model again and duplicate its geometry, or infer an instance ID from nearby
surfaces with the same material.

`misc_model_static` follows a separate external-model path:
[`code/cgame/cg_main.cpp`](../code/cgame/cg_main.cpp),
[`codemp/cgame/cg_spawn.c`](../codemp/cgame/cg_spawn.c).
The existing [`R_LoadMD3`](../shared/rd-rend2/tr_model.cpp) decodes vertices,
indices, UVs, frames and surface shaders, but is coupled to renderer allocation
and shader registration. It is not a standalone offline evidence importer.
`EntityEvidence` currently recognizes only `light` and `light_spot` as sources.

A future offline adapter should copy already decoded immutable MD3 geometry and
resolved material facts, with model digest, original entity ordinal, selected
frame/skin/remaps and transform provenance. Apply scale before rotation and
translation: `p_world = origin + R S p_model`. Derive geometric normals from
transformed triangle winding; account for negative determinant/cull semantics.
SP and MP differ in scale-key precedence, so reuse their selected semantics.
`zoffset` is culling information, not a luminous position offset. Unknown frame,
attachment, deformation, state or unresolved shader remains conditional.
External instance/surface IDs must stay distinct from original BSP ordinals;
this experiment does not stuff MD3 parts into canonical BSP references.

## Stock asset spot audit

The reproducible [metadata audit](../tools/rend2/visible_emitters/inventory.py)
reads three installed assets, including source digests and matching shader
declarations in `assets?.pk3`. It does not export game geometry or images.
[Recorded metadata](../tools/rend2/visible_emitters/results/v1/stock-inventory.json)
does not include loose/mod overrides, actual instance transforms, or a claim
that these definitions are the renderer's selected variants.

| MD3 in `assets1.pk3` | Frames; surfaces; triangles | Material evidence and next useful experiment |
|---|---|---|
| `models/map_objects/hoth/light_wall.md3` | 1; 3; 122 | `light_wall_bulb` has a separate glow image in `shaders/hoth.shader`; housing shares another material. Test instance grouping across material boundaries, retaining the bulb mask separately. |
| `models/map_objects/factory/ceiling_light.md3` | 1; 1; 154 | `ceiling_light_new` has base and separate glow stages in `shaders/factory.shader`. One mesh contains housing and emission; whole-mesh emission would be wrong. |
| `models/map_objects/taspir/lamp2_double.md3` | 1; 6; 180 | Two `lamp2_bulb` parts have no glow declaration in `shaders/taspir.shader`. `minelamp_on` parts are two-sided glow with an RGB waveform in `shaders/models.shader`. Preserve animation and uncertain bake inclusion; do not promote the word “bulb” into luminous evidence. |

The executable below uses owned objects. End-to-end stock MD3 discovery remains
blocked on the offline instance/material adapter described above, not on a lack
of models or the need for a neural reconstruction system. The three named files
and their recorded hashes are concrete follow-up test assets. No historical
lamp ground truth was available or assumed.

## Deterministic pipeline and smallest prototype

1. Pin original geometry, selected materials and source texture identities. Use
   compiled BSP geometry first. For external models, require an evidenced
   instance transform and frame. World geometry requires no camera recovery.
2. Preserve known object membership when available. For stripped compiled
   models, keep current region IDs until an independently evidenced membership
   exists. World triangle adjacency alone can connect fixtures to entire walls.
3. Run existing emission grouping/mask extraction. Preserve disconnected mask
   blobs, per-face winding, colours, two-sided policy and issue flags. A tube,
   point or rectangle is a shape alternative, not a certified lamp type. Derive
   a spot axis only from a target or independently supported aperture/reflector
   frame; do not invent a cone from a bright patch.
4. Join region candidates to explicit fixture membership. Reject overlapping
   membership, a region crossing two objects, or partially assigned support.
   Report unassigned regions. Keep original IDs/aliases and all per-region
   geometry. A diagnostic fixture ID hashes map identity, experiment tag, source
   key and sorted original surface ordinals; it is not a content/cache key or
   a cross-map matching scheme.
5. Summarize with area-weighted region centers and relative colour. The position
   is a **summary centroid**, potentially inside housing or a dark gap, never a
   certified point emitter placement. Publish a common normal only when every
   region supplies a matching normal (`dot > 0.9999`). Multi-face fixtures keep
   individual normals, not an averaged invented propagation direction.
6. Keep full uncertainty. `staticEligible` is only the inherited motion/style
   restriction; support, existence, optical visibility, bake participation and
   amplitude still require their existing independent checks. Missing geometry,
   alpha coverage or material information must not produce clear rays.

The smallest missing primitive is step 4:
[`fixture_hypothesis.h`](../tools/lightrecon_test/fixture_hypothesis.h), used only
by [`lightrecon_fixture_tool`](../tools/lightrecon_test/lightrecon_fixture_tool.cpp).
The prototype's interface is:

```cpp
struct Membership { std::string sourceKey; std::vector<uint32_t> surfaces; };
Error Build(std::shared_ptr<const EmitterEvidenceSet>,
            const std::vector<Membership>&, const ReadContext&, Views&);
// Views retains the evidence lease; each Region references existing
// candidate/evidence indices. Fixture is a diagnostic grouping and summary.
```

It bounds membership/candidate/reference/work counts and diagnostic allocation,
supports cancellation, and clears output on validation failure. It introduces no
canonical record type, codec, version change, solver provider, runtime callback
or renderer consumer. Explicit membership is required input, not an automatic
instance-recognition result. Region references remain valid through the owned
lease. The fixed research limits are not a whole-process RSS guarantee.

### Emitting-region and fitting limits

Each demonstrated region exposes original surface references, evidence aliases,
center, frame, half extents, sampled area, two-sided policy, type alternatives,
relative RGB and issue bits. The rectangle fits sampled points with a margin;
it is an approximate region descriptor, not an exact clipped mask boundary.
The current evidence lease does not retain individual luminous UV-cell
footprints or per-sample weights. Do not pass the entire parent surface to
`FromSurfaces` and claim an exact mask-blob certificate.

For later geometry-conditioned fitting, retain/recompute clipped mask-cell
footprints with named original triangle provenance in a temporary provider.
Quadrature should use their world-area weights and existing visibility queries:
`E = sum_i a_i Le_i max(0,nr·u_i) max(0,ne_i·(-u_i)) V_i / r_i²`.
Unknown visibility remains unknown. Shared material intensity is a possible
fitting constraint within an attested fixture; multi-colour parts need separate
amplitudes. Rectangle/tube alternatives and per-region models must not all be
added together as simultaneous lamps. Preserve existing candidate aliases and
profile/encoding hypotheses. This experiment does not change fitting, decode
RGB8 as physical irradiance, or evaluate historical-source correctness.

## Executable comparison and measured result

The owned scene contains a dual-slot 32×8 panel at `z=64`, a rotated 32×2 wall
strip, a corner lamp with two perpendicular 8×8 faces, and a non-emitting housing.
Masks contain authored **relative source-linear** RGB `(1,0.5,0.25)`. The panel
has two 8×8 lit slots separated by a 16-unit dark gap, plus a compiler surface
light hint. There are no image reconstruction, game assets, GL calls or baked
lightmap observations in this scene. Its map identity is an owned geometry-digest
record, not a claim to have compiled an RBSP.

| Object | Existing region candidates | Fixture views | Emitting area estimate (WU²) | World summary centroid; common normal | Retained alternatives |
|---|---:|---:|---:|---|---|
| Dual slot | 2 (4 glow/compiler evidence aliases) | 1 | 128 | `(16,4,64)`; `+Z` | Two separate rectangles; dark gap retained |
| Rotated wall strip | 1 | 1 | 64 | `(48,16,17)`; `+X` | Rectangle or tube |
| Corner lamp | 2 | 1 | 128 | `(98,4,18)`; unknown | Two oriented regions; no single direction |
| Total | 5 (7 evidence records) | 3 | 320 | No source placement certified | No region was removed or merged geometrically |

All regions retain `UncalibratedEmission | ShapeHypothesis |
ColourTransferUnknown` (515). Compiler intensity stays in legacy units.
The grouping adds object association and summaries; luminous-region detection
recall, photometric accuracy and historical accuracy are unchanged/unmeasured.
It is not evidence that three canonical lights should replace five regions.

[Frozen diagnostic TSV](../tools/rend2/visible_emitters/results/v1/fixtures.tsv)
contains IDs, aliases and region geometry. The executable passes **28 checks**,
including nearby equal-material lamps, member-order determinism, non-emitting
housing, duplicate hints, cross-object/partial membership, unassigned emission,
missing masks, unsupported geometry, unknown state, empty emission, cancellation
and memory limits. Related existing evidence and support tests also pass (3/3
targeted CTest entries). New code links only into the standalone tool.

### Reproduction

Use any working C++11 CMake toolchain; on this Windows checkout, prepend
`C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin` to PATH
for its bundled `cmake`, compiler and `make`. The measured build was Release GCC
in `build/s06-gcc`. A fresh portable build is:

```powershell
cmake -S tools/lightrecon_test -B build/asr03 -DCMAKE_BUILD_TYPE=Release
cmake --build build/asr03 --target lightrecon_fixture_tool lightrecon_evidence_test lightrecon_emitter_support_test --parallel 4
ctest --test-dir build/asr03 -C Release -R '^lightrecon_(fixture_discovery|evidence|emitter_support)$' --output-on-failure
# Single-config output path; multi-config generators place the executable in Release/.
./build/asr03/lightrecon_fixture_tool.exe build/asr03/fixtures.tsv
python tools/rend2/visible_emitters/inventory.py 'C:/Program Files (x86)/Steam/steamapps/common/Jedi Academy/GameData/base' --output build/asr03/stock-inventory.json
```

The inventory command requires the locally installed commercial assets. The
fixture executable does not. [Artifact hashes](../tools/rend2/visible_emitters/results/v1/manifest.json)
pin the measured tool/header, outputs, compiler, executable and downloaded primary
PDF. PDFs and game assets are not redistributed. Re-run output comparisons only
on the same floating-point/toolchain implementation; PCA signs/rounding and
region IDs are not guaranteed portable across compilers.

**Recommendation:** keep the membership primitive in research tooling. The next
useful work is an offline MD3 instance/material lease adapter and temporary
masked-footprint provenance, followed by geometry/support checks and held-out
fitting through existing interfaces. Neural reconstruction would replace known
evidence with extra assumptions and is not justified for these assets. No GPU
consumer, world shading, double-lighting policy, canonical storage or historical
solver was changed.
