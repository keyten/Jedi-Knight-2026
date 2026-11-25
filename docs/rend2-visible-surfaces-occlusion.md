# Visible BSP submission and model GPU occlusion

`r_worldSurfaceList 1` (default) replaces the post-walk world-array scans with a
compact bitset of IDs reached by the existing PVS/area/frustum BSP walk. First
membership also replaces the large view-stamp lookup in this mode. Lighting and
projected-shadow masks still accumulate with OR from every contributing leaf.
Occupied words are listed in ascending order and set bits use native bit scans;
dense views enumerate the small bitset directly. Original world IDs precede
merged IDs exactly as before, preserving equal-key and translucent submission.

The list generation follows both world pointer and `tr.viewCount`. Preliminary
zFar, portal and skyportal bounds walks therefore remain part of the same view.
Portal candidates are snapshotted before recursive mirror traversal changes the
view generation. `r_nocurves` selects the existing unmerged marksurface path.
Point/sun/static shadow views use the same membership mechanism; the existing
cached dynamic-overlay short path remains intact.

`r_worldSurfaceList 0` restores the stamp-only traversal and full-array reference
submission. `2` retains independent stamps and checks every compact membership
against them before submission. It intentionally costs more and is a diagnostic,
not the performance setting.

## Current-frame model occlusion

`r_modelOcclusion 1` enables exact-depth conditional rendering for sufficiently
large ordinary opaque Ghoul2 surfaces (at least 1024 indices). The existing depth
prepass draw is wrapped in `GL_SAMPLES_PASSED`. The material draws use that query
with `GL_QUERY_NO_WAIT`; an unavailable result draws normally. There is no extra
bounds draw, no approximate skeletal box, and no preceding-frame visibility
rejection. Current frame/view depth and the exact currently deformed mesh define
the decision. Occluders submitted later may cause missed culling, which is safe.

Models whose entity origin is within 256 world units of the camera (including
the boundary) retain the original batching and unconditional rendering. Invalid
distances also bypass admission. This explicit close-camera fallback avoids
changing close-model submission while distant candidates still use exact mesh
queries. It is an admission rule, not an approximate visibility bound.

Only a single ordinary LEQUAL depth draw can guard the surface. Deformation,
displacement, tessellation, gore, depth hacks, post-render and unknown entity
render effects bypass admission. MD3 remains on its ordinary/instanced path.
Shadows, portal/mirror/skyportal views, skyportal occluder walks, refraction and
no-world views bypass camera occlusion. Disabling the depth prepass restores
unconditional rendering. Near-plane intersections, attached or moving surfaces
and camera cuts need no guessed bounds or history recovery: every eligible
surface is re-tested using its actual geometry in this view. Independent saber,
Force and projectile surfaces retain independent rendering.

The bounded 128-object pool never reuses a pending query. A nonblocking
availability poll releases finished slots; pool exhaustion draws normally. The
ordinary frame never waits on a query or reads an unavailable result. GL3.2's
core samples-passed query is used, so occlusion-query2 is not required. Query
objects are released during renderer shutdown/restart.

This saves **GPU material draw work only**. Front-end visibility, bones, depth
geometry and CPU material recording still run. Default-off depth prepass is not
silently enabled. `r_modelOcclusion 0` restores unconditional material draws. `2` keeps the query
and batching path but disables conditional rejection, providing a matching
submission reference for numerical precision comparisons.

`r_modelOcclusionDebug 1` reports resolved and hidden queries, conditional draw
coverage and a **confirmed lower bound** of skipped GPU draws/indices. NO_WAIT
may draw before a result is available; those draws are excluded from the skip
count. Debug statistics poll availability before the conditional draw and read
completed results only in a later view. Disable debug for performance captures.

## Verification

Build `rd-rend2_x86_64`, `rdsp-rend2_x86_64` and, with
`BuildWorldSurfaceListTests=ON`, `rend2_world_surface_list_test`.
The CPU test checks each production admission gate, compact membership and
boundary sizes, overlapping-leaf deduplication, ascending order, generation reset
and sparse/dense traversal cost. The cost measurement includes membership
bookkeeping and enumeration, not total engine frame time.

`tools/rend2/test_model_occlusion_gl.py` runs a hidden GL context and checks
fully hidden, partially visible, visible, coplanar, near-clipped and camera-cut
cases. A primitive-generation query proves that a ready hidden conditional draw
actually executes zero geometry, while every visible case retains its draw and
matches the unconditional pixels. Only this deterministic test uses a finish;
production uses no synchronous wait.

`tools/rend2/test_visibility_occlusion_runtime.py` stages a private runtime using
read-only installed assets and engine/game binaries, and this worktree's renderer.
It freezes SP through the UI pause menu plus a fixed shader clock, then compares
reference, compact, membership-compare, occlusion, repeat, curves-disabled,
prepass-disabled and close-camera captures. Optional startup velocity/shadow
modes exercise their initialized FBO/program paths. All binaries/config/logs
remain under the worktree. Foreign game processes block launch.

## Recorded validation (2026-11-25)

Both private Release renderer targets built successfully. CPU admission,
membership, deduplication, ordering and reset tests passed. Hidden Intel UHD
GL3.3 checks passed seven clipping/occlusion/cut cases and proved zero generated
primitives for the fully hidden conditional draw.

The private Intel UHD SP `mp/ffa3` run used twelve stormtroopers and completed
without shader, GL, framebuffer or membership errors. Reference versus compact
(default and membership-compare), far occlusion, `r_nocurves`, disabled-prepass
fallback and reference repeat captures matched every channel. In the close
camera comparison, one player-hair channel differed by 1/255. No missing geometry
was observed. The strict runtime oracle reported this difference; its source
was not isolated. Mode 2 now allows a matching-batching reference without
conditional rejection; that extra diagnostic mode was compiled for SP/MP but
was not re-run in the initial matrix.

The frozen occlusion phase resolved 520 queries, of which 78 were hidden;
806 material draws were conditional. The CPU availability-based conservative
counter confirmed at least one skipped draw / 1200 indices. The close view
confirmed at least three skipped draws / 3240 indices. These are actual skipped
GPU material submissions, not saved bones, CPU work or an FPS estimate.

The initial matrix did not initialize velocity or modern shadow FBOs. Shadow and
portal views are excluded by the tested admission policy. Their combined runtime
coverage remains an integration check. No isolated frame-time benefit is
claimed: other worktree builds contaminated the preliminary microbenchmarks.
Their sparse cases favored compact enumeration, while the dense synthetic case
showed bookkeeping overhead; this is not evidence of whole-frame improvement.
The standalone benchmark is included for repeatable sparse/dense measurements
once the machine is otherwise idle.

Machine-readable capture differences, query counts and limitations are in
[`validation/rend2-visible-surfaces-occlusion.json`](validation/rend2-visible-surfaces-occlusion.json).


A follow-up adds the explicit 256-unit near-camera bypass and its production-policy
regression gate. The combined integration build/runtime validates this change;
the initial one-LSB capture above predates the bypass.


## Final combined validation

The [integration receipt](rend2-render-batching-integration.md) supersedes the
outstanding initial velocity/shadow coverage note: initialized sun cascades and
fused velocity passed in the combined Yavin scene with native grass, repeated MD3s
and character culling enabled. Query-only/conditional captures, including the moved
near camera, matched every channel. The near view issued zero model queries with
the 256-unit bypass. Far current-frame queries were exercised; the combined CPU-ready
skip lower bound was zero, so no additional avoided draws are inferred from its
hidden-result count. Four CPU tests and both renderer builds passed after merging.
