# Enabled defaults and pixel specialization integration

Implementation main: `d1c5ff8a5` (2026-11-26). Local no-ff merges:
`000e270ec` (defaults), `df17352ec` (shader7), `d1c5ff8a5` (independent runtime).
The newer FX commits `f7f105891` / `ee768b736` remain in history.

## Defaults and behavior

Both SP and MP register `r_worldSurfaceList 1`, `r_surfaceSpritesCompute 2`,
`r_modelOcclusion 1`, `r_md3Instancing 1`, and `r_lightallSpecialize 1`. Explicit
archived settings keep priority. Every 0 path remains available with hardware,
program-failure, capacity and unsafe-material fallbacks. No installed configuration
was rewritten. The requested defaults cover 3–6; experimental shadow modes and
unrelated feature/quality controls were not enabled.

Optimization 7 lazily selects an ordinary lightall program without local-light code
when the exact final per-draw predicate already produces a zero light mask. Baked
light, sun, emission, screen outputs, geometry and UBO layouts remain intact.
Local-light draws and unsupported MD3/Phong/POM/reflection programs retain their
existing program. Ordinary shaders gain no inactive heavy code.
[Design and driver evidence](rend2-lightall-specialization.md).

## Validation

Merged SP/MP renderer-only MSVC Release builds passed, followed by four CPU tests
and the SP/MP source-default/archive/off contract. Engines/game were reused.

The final bounded Yavin replay resets all four batching controls to their actual
registered defaults, with specialization 1, character culling 1, initialized sun
cascades and fused velocity. It contains native grass, 40 repeated MD3s and six
Ghoul2 characters. Default/repeat and off/restore captures are byte exact. Samples:
40 MD3 draws grouped into 2 (38 saved, 21,504 instance bytes); 5,852 candidate plants
reduced to 774; LOD geometry 1,858 cards / 13,312 vertices; seven cached pass reuses.
One nonblocking query sample confirmed 3 skipped draws / 4,062 indices; this
availability-based count is a lower bound. No timing was measured by this smoke.
It started with no foreign OpenJK process and exited normally.

The independent same-DLL 1280x720 runtime checked six exact specialization A/B and
repeat pairs. Eight lights changed 1,667,580 channels; removal restored the image
exactly. Actual specialized/full bindings were checked. Optional 640 was dropped
after foreign-process preflight; the process was untouched.

Both Intel UHD and RTX 2060 passed seven actual-GL program/ABI pairs and six exact
color/screen/depth rendering pairs. Successful suites were reused. GPU queries
measured Intel 97.674→8.891, 66.787→8.075, 84.605→9.480 ns/submitted fragment
for the three documented workloads. RTX grid/screen measured 2.079→1.638 and
3.206→2.864. Its lightmap case mixed clock regimes (aggregate 6.393→3.616),
so only approximately 43% paired reduction is supported. These are program
measurements, **not FPS gains**.

Background CPU work and runtime drift in unchanged passes prevent net frame-time
claims. The full user-quality diagnostic did not reach active MainView within
bounds and is rejected as performance evidence. User-reported resolution scaling
was not independently reproduced. Enabling all 3–6 follows the user's preference
despite earlier timing limitations.

## Preservation

A fresh snapshot saved 82 dirty/untracked files and the index, including two
concurrent relight-probe edits before merge. Immediately after merging, all 82
SHA-256 hashes were unchanged. During build/runtime an unrelated
`tools/rend2/relight_probe/gridsun.py` edit continued; its before/after contents
were saved and the newer contents retained. The other 81 hashes remained unchanged.
User staging stayed empty. Private snapshots are under the integration worktree's
`build/pixel7-preservation`. No user file was restored, stashed or committed.
The final build includes preserved dirty main shader/volumetric changes. Installed
assets were read only; no installed DLL/config was changed, deployed or pushed.

[Machine-readable receipt and hashes](rend2-performance-defaults-and-lightall-integration.json)
record merged builds, captures, query samples and preservation.
[Registered-default smoke](rend2-enabled-batching-defaults.md) separately fixed
specialization off to isolate that change.
