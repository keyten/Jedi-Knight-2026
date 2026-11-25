# Rend2 performance defaults audit

Work started 2026-11-23 and runtime validation resumed 2026-11-25, on baseline
`d6d239a659f0b0f9e417cec379501147931c90d3`. The worktree, binaries, configs and
homepaths are private; installed assets are read-only. SP/MP registrations are
changed together. JK2 Rend2 reuses the SP renderer sources.

## Decisions

| Control | Factory default | Decision |
| --- | --- | --- |
| `r_gl43` | 0 -> **1** | Prefer modern context/policy. Existing hidden probe, engine retry and feature-local capability/compile fallbacks remain. No visual effect is enabled by this alone. |
| `r_depthPrepassFast` | 0 -> **1** | Use the lighter depth-only velocity-program family when a camera prepass does not need velocity. Shadow depth and weather-impact classification keep their original programs. |
| `r_shadowSinglePass` | 0 -> **1** | One scene walk per point-light cube, with conservative face masks. Spot lights keep their single cone view. |
| `r_shadowLayered` | 0 -> **1** | Share command construction and a layered target where supported; unsupported surfaces/programs/capabilities retain per-face submission. Geometry still draws per covered face. |
| `r_skyPortalCull` | 0 -> **1** | Cull skyportal pixels covered by simple opaque main-world geometry. Bypass with modern sun alpha casters; see below. |
| `r_sunShadowCache` | 0 -> **1** | Exact projection reuse only. Mode 1 falls back during movement/unbounded damage. Modern sun-shadow mode itself remains opt-in. |
| `r_shadowCache` | **0**, unchanged | Fresh same-scene depth verification reports unresolved differences. Full-slot reuse is not promoted. Mode 2 is now available to reproduce/check it. |
| `r_shadowStaticCache` | **0**, unchanged | Fresh static split/restore depth verification also reports unresolved differences. It is not promoted. |
| `r_forwardPlus` | **0**, unchanged | Also changes light capacity/selection, so it is not an unconditional equivalent-output speed-up. Its existing compute builder remains selected when Forward+ and modern capabilities are enabled. |
| `r_waterTessellation` | **0**, unchanged | Adaptive water geometry can reduce work but changes geometric sampling and quality policy. It was incorrectly grouped with equivalent-output optimizations in the review. No water setting changes here. |
| sun cache modes 2/3 | opt-in, unchanged | Scrolling and persistent disk pages remain experimental. |

Archived explicit settings are preserved: a saved `0` is not overwritten.
Dynamic-shadow options only matter with shadowed lights (`r_dlightMode 2/3`);
the default light mode remains 1. Sun mode, Forward+, water and quality features
are not enabled by the performance-default changes.

## Fixes and improvements

Point-shadow keys previously omitted world draw/culling controls. The common
world key now includes `r_drawworld`, `r_nocurves`, `r_facePlaneCull`, `r_nocull`
and curve LOD through the existing sun caster-settings key, plus Force Sight.
The full-slot key additionally tracks model LOD/Ghoul2 controls and Phong
character tessellation. Camera origin/axes/FOV are tracked for camera-selected
caster LOD/tessellation. Model/camera values stay out of the static BSP key so
ordinary camera movement does not rebuild immutable world depth.

Voronoi relief was absent from the immutable-world predicate despite possible
depth displacement. It now stays in dynamic sun/point overlays and prevents
full-slot reuse near the light. Surface-sprite and Force Sight world materials
also conservatively prevent full-slot reuse. These correctness improvements are
retained even though the two point caches remain opt-in.

`r_shadowCache 2` now snapshots a full-cache hit (or normal draw after a miss),
redraws that slot in the same scene and compares exact D16 values. The existing
static-cache verifier shares the snapshot/reference infrastructure. Verification
runs outside every timing window; it resets the requested cache mode to 1 after
the check. Set the mode back to 0 to return to the factory policy.

## Skyportal limitation

With modern sun alpha casters, the existing camera prepass also admits
non-opaque `q3map_alphashadow` stages. Such a stage can hide the opaque wall
behind it and blend over the skyportal background. Replacing that background
with the clear colour changes the image. The optimization now conservatively
bypasses when `r_sunShadowMode != 0 && r_sunShadowAlphaCasters != 0`.

This guard preserves existing camera-prepass/material semantics. That combination
receives no skyportal speed-up. Submitted models with forced entity alpha or
distortion also conservatively bypass, since their opaque material depth can
precede transparent colour. A narrower mask-aware solution is separate work.

## Validation and evidence

MSVC 19.27 / Visual Studio 2019 x64 Release: SP renderer, MP renderer, SP engine
and SP game DLL built in `build/default-audit-msvc`. Renderer rebuilds after
final key/verifier/default changes passed. Existing warnings include x64
`/arch:SSE2` and the old `cg_syscalls.cpp` uninitialized-mask warning.

Actual GL4.3 kernel checks passed on Intel UHD and RTX 2060 (NVIDIA 616.92):
Forward+ lists including bounded allocator, froxel lists across tile/slice
choices, water depth contacts and interaction/foam consumers. These validate
existing modern paths, not water tessellation or a GPU speed-up.

Initial Intel `shadow-intel-gl43` run: all eight point-cube comparisons
(separate faces vs union walk, then union vs layered) were exact. Empty faces
explicitly skipped by the verifier are not counted as compared texels.
The same run exposed static-cache differences: e.g. slot 0 had 668,007 of
1,572,864 D16 texels different, max delta 475/65535. It included another OpenJK
process, so its timing is diagnostic only; same-scene depth checks remain valid.

Resumed Intel `shadow-intel-final` run, no NPC fixture: full-slot same-scene
checks also found differences in slot 0 (e.g. 778,930 texels, max 212/65535),
while slots 1-3 matched. The `r_nocull 1` interval matched all four slots.
The exact cause is not established; this may involve reuse and/or verification
state. These paths are **not considered validated**, and both stay default 0.
The JSON/logs retain the failures; the test does not silently accept them.

Runtime evidence is under `build/optimization-audit/`, `build/prepass-runtime/`
and `build/default-audit-msvc/`. Numerical receipts are retained in
[`validation-2026-11-25.json`](../tools/rend2/optimization_defaults/validation-2026-11-25.json).
Nested main-view/pass GPU timestamps must not be summed. Other OpenJK PIDs are
recorded before and during each run. Concurrent CPU builds are also a reason
not to infer an isolated full-frame speed-up. `timescale 0` is not a reliable
SP simulation freeze, so only same-scene depth checks and demonstrably stable
repeat captures are treated as exact-image evidence.

## Reproduction

```powershell
python -X utf8 tools/rend2/test_optimization_defaults_runtime.py --installation C:/path/to/game --binaries build/default-audit-msvc/Release --label shadow-intel --case shadow --no-npcs
python -X utf8 tools/rend2/test_optimization_defaults_runtime.py --installation C:/path/to/game --binaries build/default-audit-msvc/Release --label shadow-legacy --case shadow --gl43 0 --no-npcs
python -X utf8 tools/rend2/test_optimization_defaults_runtime.py --installation C:/path/to/game --binaries build/default-audit-msvc/Release --label shadow-nvidia --case shadow --nvidia --lazy-shaders
python -X utf8 tools/rend2/test_optimization_defaults_runtime.py --installation C:/path/to/game --binaries build/default-audit-msvc/Release --label full-cache --case shadow --full-cache 1
python -X utf8 tools/rend2/test_optimization_defaults_runtime.py --installation C:/path/to/game --binaries build/default-audit-msvc/Release --label static-cache --case shadow --static-cache 1
python -X utf8 tools/rend2/test_optimization_defaults_runtime.py --installation C:/path/to/game --binaries build/default-audit-msvc/Release --label sky --case sky --map vjun2
python -X utf8 tools/rend2/test_optimization_defaults_runtime.py --installation C:/path/to/game --binaries build/default-audit-msvc/Release --label sky-alpha --case sky --map vjun2 --sun-mode 1 --alpha-casters 1
python -X utf8 tools/rend2/test_depth_prepass_runtime.py --installation C:/path/to/game --build build/default-audit-msvc/Release --label prepass-intel
python -X utf8 tools/rend2/test_sunshadow_cache_runtime.py --installation C:/path/to/game --binaries build/default-audit-msvc/Release --modes 1 --window 640 360 --maxfps 60
```

Use fresh labels. `--cache-from <private home/OpenJK>` seeds only an earlier own
program cache on the same GPU; entries retain their normal hash validation.
Startup is outside timing. The matrix requires all 15 capture variants; missing
files cannot count as a pass. A/B images are inconclusive if the repeated
reference is unstable. The opt-in cache commands above intentionally fail while
the recorded depth discrepancy is unresolved.

AMD hardware, truly unsupported GL4.3 drivers, MP gameplay, JK2 runtime, all
mod-authored materials and long gameplay sessions are outside the sample.
Explicit legacy policy on a modern GPU does not substitute for testing an old
driver. Existing capability and conservative redraw fallbacks are retained.
Old October timing figures remain historical evidence, not new measurements.

## Final runtime receipts

- Intel and RTX2060 depth-only prepass: 60 same-frame checks each, both orders,
  zero nearer samples, 265 surfaces including two NPCs. NVIDIA A/B/A captures
  were byte-identical; Intel differed by four pixels in both A/B and A/A.
- Intel cube traversal/layering: eight cube comparisons exact. NVIDIA:
  32 cube comparisons exact across draw/cull/LOD/camera settings. Shadow beauty
  captures span a running simulation and are diagnostic, not an image oracle.
- Sun-cache mode 1, Intel: 90/90 comparisons byte-exact, maximum depth error 0,
  including motion and draw/culling switches. No verifier/GL/FBO error.
- Skyportal, Intel: ordinary and modern-alpha guarded cases completed without
  GL/FBO errors. A/B differed by six/three pixels with unstable A/A repeats, so
  those captures are inconclusive. NVIDIA ordinary skyportal A/B differed by
  ten pixels and its repeat was also unstable; it is likewise inconclusive.
  The conservative opaque-only selection and alpha/distortion bypasses were
  reviewed in source; no exact full-frame sky image claim is made.
- Fast prepass GPU medians were 0.258 -> 0.146 ms on Intel and 0.118 -> 0.082 ms
  on NVIDIA at 640x360. Other OpenJK processes overlapped the Intel windows. No full-system-isolated
  benchmark was established; these numbers are diagnostic, not a controlled
  FPS improvement.

NVIDIA runs used on-demand lightall compilation only to reduce cold startup;
that switch's factory default is unchanged. Startup was outside timing.
The final short-sky runner waits at a capped frame rate before quit so pending
screenshot PBOs can finish; its first uncapped attempt was a harness failure
(missing final capture), not a renderer correctness failure.

## Combined integration check (2026-11-25)

Both worktree changes were merged into `rend2-unified-wip` at `898a2864978261846fc1468cc8fbb302e6d235f1`.
The combined MSVC x64 Release SP/MP renderers, SP game, MP cgame and character
policy test built successfully; all 25 CPU policy/history checks passed.
A private hidden runtime run exercised GL4.3, the velocity prepass, modern sun
shadows and exact sun-cache mode 1 together with character culling. All seven
off/on/turn-back/near phases completed. All four capture comparisons had zero
differing RGB channels, distant-body culling was exercised, and protected bodies
remained present. Installed game files were not modified.

The run observed foreign game processes and is correctness evidence only;
no isolated FPS improvement is claimed. See the retained
[combined receipt](../tools/rend2/optimization_defaults/integration-2026-11-25.json).
