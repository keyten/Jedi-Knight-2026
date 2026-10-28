# SSR cache fixes and validation

Work starts at `6e6305f91af5f932743aa1bd41f5152dcef65c93` on branch
`codex/ssr-cache-fixes`. The separate checkout is
`C:/Users/Keyten/.codex/worktrees/ssr-cache-fixes/OpenJK-rend2`.

The changes are now merged into `rend2-unified-wip` in the main checkout,
`C:/Users/Keyten/Desktop/projects/OpenJK-rend2`, by merge `40a5b4979` (source
commit `b71df52cc`). The merge retains `7c494746c`'s rough reflection occlusion
and `e1e1baa87`'s emissive/liquid-linking fixes. The `lightall.glsl`, emissive
and shade changes from those commits were not replaced. See the post-merge
section below for validation of the combined main branch.

## Changes

- Receiver classification clears trace confidence to zero. The old
  `colorBlack` has alpha 1, so every early-depth-culled texel looked like a
  confident reused hit at UV/depth zero, including surfaces too rough to trace.
  This also made the old green-cache debug coverage misleading.
- Cached-hit visibility uses a bounded conservative Hi-Z segment query. A
  nearer blocker invalidates reuse even when it is farther than the assumed
  thickness from the ray; exhausting the query budget also invalidates reuse.
- Opaque SSR reserves velocity and previous transforms independently of color
  accumulation. If the current view has no valid velocity prepass, it traces
  fresh hits.
- Hit history uses the projection and logarithmic depth range of the frame
  that produced it. Turning the camera can change zFar without a history cut.
- Temporal rejection compares reflected points in 3D, including depth changes
  at identical screen UV. Current/previous half-res phases have separate lookups.
- Rotating half-res phase and forced-refresh cycles are independent, covering
  every trace cell/phase combination in 16 frames.
- Color mip 0 masks the viewmodel and outside-view area. All downsample levels
  use four bilinear taps, and sharp reflections use the same coverage handling
  as rough reflections. MSAA first resolves into the existing render image.
- Classified trace variants avoid repeating stored-depth/specular eligibility
  checks after early depth classification.
- Final MSAA color/depth resolve selects color attachment 0 explicitly. The
  old generic blit broadcast scene color into every enabled destination MRT,
  overwriting resolved material buffers before the SSR/SSGI debug overlays.
  This is separate from the deferred representative-sample MSAA edge resolve.
- `r_ssrResolveCache 1` resolves current-frame radiance once per half-res ray,
  then filters those values with depth, normal and roughness weights. This is a
  spatial approximation of the direct per-pixel cone footprint; switch it off
  to compare material boundaries or preserve direct footprint evaluation.

## Build and regression commands

Configure an independent MSVC x64 CMake build, then build:

```powershell
cmake --build build/ssr-msvc --config RelWithDebInfo --target rdsp-rend2_x86_64 rd-rend2_x86_64 openjk_sp.x86_64 jagamex86_64 --parallel 6
```

The engine and game library, baseline renderer DLLs, and fixed renderer DLLs
were built with Visual Studio 2019. Existing `/arch:SSE2` x64 command-line
warnings remain; there are no build errors. Baseline renderer DLLs are kept in
`build/ssr-baseline`; final DLLs are in `build/ssr-msvc/RelWithDebInfo`.

```powershell
python tools/rend2/test_ssr_gl.py --baseline-ref 6e6305f91a
$env:SHIM_MCCOMPAT = '0x800000001'
python tools/rend2/test_ssr_gl.py --baseline-ref 6e6305f91a
```

The test uses actual production GLSL 1.50 on hidden OpenGL 3.2 contexts.
Fifteen SSR permutations compile/link on Intel UHD (27.20.100.8280) and RTX
2060 (616.92). Regression checks cover blockers missed by the old four taps,
same-UV temporal depth changes, changing zFar, motion rejection, exhausted
visibility budget, all four sample phases, sharp weapon-boundary filtering,
current-frame lighting changes, depth/normal/roughness edge rejection, actual
RGBA16 confidence-bit storage, classified-miss clear semantics, and MSAA
material preservation (the old all-MRT broadcast is reproduced).
The baseline revision reproduces the original visibility and temporal bugs.
The focused `test_watersurface_gl.py --motion-only` test also compiles/links
eight camera-only/wave-aware water SSR combinations on RTX. The full unrelated
water feature suite on Intel was stopped in favour of that targeted check.

## Isolated game runs

```powershell
python tools/rend2/test_ssr_runtime.py --installation C:/Users/Keyten/Desktop/projects/OpenJK/build-rend2 --renderer build/ssr-msvc/RelWithDebInfo/rdsp-rend2_x86_64.dll --label fixed-intel --compare-resolve --wait-for-idle 1200
```

Use `--nvidia` for RTX, `--temporal 1` for rotating phase/history, `--half-res 0`
for full-resolution rays, and `--msaa 4` for multisample input resolve. Every
run has its own engine, renderer, game DLL and home directory below
`build/ssr-runtime/<label>`. Installed assets are read through `fs_basepath`.
The launcher waits for other games when requested and marks any overlap; do
not use overlapping runs for performance conclusions. `execution.json`
records overlap and return code even if capture validation later fails.

Render settings are put in the private autoexec before its first wait. The SP
engine accepts only 32 `+` command-line sections, so a long list of `+set`
arguments would silently merge the tail into one cvar value. MSAA uses the
public cvar `r_ext_multisample`. The actual GPU and fitted viewport are recorded
in each run's `results.json`; a 1920x1080 request on this desktop produces a
1779x1001 framed window. Prefer `--width 1600 --height 900` on this desktop
for an exact viewport. The TGA pixel counter handles the engine's retained
GL row padding at odd fitted widths. Both focused/unfocused FPS are capped
during loading to outlive the real-time level dissolve, then uncapped for
timing sections.

The test loads `mp/ffa3` in SP and uses position `(671, -769, -70)` at yaw 290
and 310, captures beauty/hits/cache/pan views, then compares hit cache and
receiver culling. `--compare-resolve` also compares the radiance cache within
the same run and captures direct/cached/cubemap-only beauty views. It checks
the no-depth-prepass path before quitting; `--check-no-prepass-cache` asserts
zero reused pixels. This test disables saber shadow mode 2 too, since that
mode independently forces a velocity prepass. A debug capture without real
SSR hits is rejected, even if the game exits without OpenGL errors. GPU times
come from `r_speeds 100`; warm-up and pipeline-tail samples are removed before
median/mean/p95 calculation. These static views are not a full moving-NPC,
weapon-animation, or gameplay-quality certification.

## Initial worktree game results (2026-10-21)

Both SP/MP renderer DLLs build. The final production GLSL regressions pass on
both GPUs, including the additional classified-clear and all-MRT resolve bugs
found during game validation. No temporary diagnostic readbacks remain in
the renderer.

The final game checks include Intel full-resolution rays (`final-validated-full`),
half-resolution rays on both GPUs (`release-fixed-*`), and RTX half-resolution
with temporal accumulation and MSAA 4x (`final-validated-rtx`). The fixed runs
report **zero reused pixels** after disabling both depth-prepass sources.
The temporal/MSAA run has 33,900 real reused-hit pixels and 26,176 fresh-hit
pixels at 640x360, rather than counting the old alpha-1 clear as cache use.

The four `release-baseline-*` / `release-fixed-*` runs are sequential, all
record `overlap: false`, and use an exact 1600x900 viewport. Settings are
medium/Hi-Z, half resolution, temporal off, MSAA off, hit cache on, receiver
culling on; fixed resolve cache is on. Other screen-space/volumetric effects
are disabled. The baseline is the original starting revision, including its
incorrect culled-hit clear, so this is not a comparison of identical images.

Median `Main View 0` GPU time (ms, includes the view's depth/velocity work;
does not include CPU time or all final presentation work):

| GPU | yaw | baseline | fixed | change |
|---|---:|---:|---:|---:|
| Intel UHD | 290 | 25.944 | 22.666 | -12.6% |
| Intel UHD | 310 | 26.975 | 22.369 | -17.1% |
| RTX 2060 | 290 | 2.188 | 2.105 | -3.8% |
| RTX 2060 | 310 | 2.373 | 1.845 | -22.3% |

These are short diagnostic snapshots of two static views, not a general FPS
claim. GPU clocks were not locked, and the RTX run shows substantial timing
variation even in unrelated passes; its percentage range needs longer,
interleaved/clock-controlled measurements before treating it as a benchmark.

The clearest win is the color pyramid: Intel `SSR inputs` changes from
3.424/3.552 ms to 1.910/1.899 ms; RTX from .453/.506 to .198/.182 ms.
Intel cached resolve costs 4.276/4.201 ms versus 4.768/4.664 ms for the
direct resolve within the fixed run. It stores current-frame radiance, not
old lighting, but is still a spatial approximation near material edges.

An additional warmed RTX A/B/A quality run (`final-quality-rtx`) disables
hit reuse and captures direct/cached/direct resolve at the same static view.
The repeated direct images are byte-identical; cached vs direct has mean
absolute error 0.00585 on the 0..255 screenshot scale. This is a check of this
view, not a guarantee for every material. Initial map-start screenshots can
still change with scene warm-up and are not used as a quality metric.

Correct cache validation is **not automatically faster**: on Intel these
short-ray views trace at 5.093/4.607 ms with hit reuse versus 4.289/3.915 ms
with `r_ssrHitCache 0`. Receiver culling also loses slightly in these dense
views (5.093/4.607 versus 4.529/4.108 ms with culling off). Keep both runtime
A/B switches; long rays/sparser scenes may have different winners. The
defaults are not changed globally based on a single map.

Raw logs, screenshots and per-section median/mean/p95 are in
`build/ssr-runtime/release-*/home/OpenJK/qconsole.log`, `screenshots/` and
`results.json`; `build/ssr-runtime/release-summary.json` collects the values.
Earlier exploratory/overlapping runs and pre-clear-fix timings are excluded
from the table. Representative-sample MSAA edge resolve, compute tracing,
material packing and stochastic GGX remain out of scope.

## Post-merge validation in the main checkout

Merge `40a5b4979` combines the fixes with main's `7c494746c`; all results in
this section are from `C:/Users/Keyten/Desktop/projects/OpenJK-rend2`.
The pre-merge baseline DLLs are in `build/ssr-baseline-main-7c494746c`, not
the earlier worktree baseline. Final SP/MP renderers and the SP engine/game
build successfully in `build/ssr-msvc/RelWithDebInfo` (only the existing
`/arch:SSE2` x64 warnings). Build log: `build/ssr-msvc/ssr-merged-build.log`.
The build runner normalizes the child environment to avoid the host's
duplicate `Path`/`PATH` entries confusing MSBuild; no system settings changed.

The production GLSL regression suite passes on Intel and RTX against baseline
`7c494746c`. Logs: `ssr-merged-tests-intel.log` / `ssr-merged-tests-rtx.log`
in the same build directory. The other agent's `lightall.glsl`, emissive and
shade code remains unchanged; both `r_ssrRoughOcclusion` and
`r_ssrResolveCache` are registered in SP/MP and documented.

All post-merge game runs record `overlap: false`. Performance uses 1600x900,
half-resolution/Hi-Z, temporal off, MSAA off, and `r_ssrRoughOcclusion 1`
in both baseline and merged renderers. Measured sections retain 588 samples
on RTX and 168 on Intel after warm-up/tail trimming. The test launcher now
also controls `com_maxfpsMinimized`: its default 50 cap can otherwise affect
a hidden test window. RTX results below use the repeated `main-unlimited-*`
runs with all three FPS caps disabled. Intel `main-before-intel` /
`main-merged-intel` were GPU-bound below that cap already.

Median main-view GPU time in ms, default hit cache/culling/resolve cache:

| GPU | yaw | main before merge | merged | change |
|---|---:|---:|---:|---:|
| Intel UHD | 290 | 30.009 | 25.315 | -15.6% |
| Intel UHD | 310 | 30.669 | 25.511 | -16.8% |
| RTX 2060 | 290 | 2.407 | 2.442 | +1.4%* |
| RTX 2060 | 310 | 2.897 | 2.451 | -15.4% |

\* RTX's first measured section still has a different warm-up/boost profile:
the otherwise unchanged screen-geometry pass measures .149 ms before versus
.182 ms after. Do not read that row as an isolated SSR regression or normalize
it into a claimed speedup. In the warmed yaw-290 cache-off sections, geometry
is comparable (.180/.182 ms) and main-view GPU time is 2.875 -> 2.346 ms.
Clocks are not locked, so these remain diagnostic GPU timings, not a general
FPS promise. The baseline also retains its incorrect culled-hit clear and
missing default velocity validation, so it does not produce identical images.

The clearest per-pass gains persist with rough occlusion enabled: Intel
`SSR inputs` 3.828/3.922 -> 2.302/2.294 ms; warmed RTX .623 -> .223 ms.
Merged RTX cached resolve is .374/.366 ms versus .430/.408 ms direct.
Strict hit-cache validation still costs time on these short rays: Intel trace
5.597/5.184 ms with reuse versus 4.865/4.435 ms without it; RTX .640/.576
versus .544/.495 ms. No blanket claim that more caching always wins.

Late direct/cached/direct captures remain visually close: Intel mean absolute
cached/direct difference is .00541 on the 0..255 scale with an identical
repeated direct image. RTX cached/direct difference is .00565, smaller than
the .00953 difference between its repeated direct captures. This covers this
static view only, not all roughness/material boundaries.

Additional game smoke tests pass:

- `main-msaa-temporal-rough1`: RTX, temporal + MSAA 4x, rough occlusion on;
  31,032 reused / 24,512 fresh hit pixels in debug 12.
- `main-msaa-temporal-rough0`: same combination with rough occlusion off;
  30,260 reused / 28,215 fresh hit pixels. Both lightall paths compile/run.
- `main-full-intel`: full-resolution rays, rough occlusion on;
  39,628 reused / 21,837 fresh hit pixels.
- All three report **zero reused pixels without velocity/prepass**. The
  latest smoke runs also include an explicit 180-degree camera history cut.

Main-branch artifacts are under `build/ssr-runtime/`: `main-unlimited-*-rtx`,
`main-*-intel`, `main-msaa-temporal-rough*`, `main-full-intel` and the collected
`main-summary.json`. Each `results.json` records DLL path/hash, source revision,
GPU, viewport, settings, section medians/mean/p95 and cache checks.
The initially capped RTX runs are retained but excluded from this table.

Example longer run (use `--nvidia --bench-frames 1200 --bench-warmup 1200`
for RTX instead):

```powershell
python tools/rend2/test_ssr_runtime.py --installation C:/Users/Keyten/Desktop/projects/OpenJK/build-rend2 --renderer build/ssr-msvc/RelWithDebInfo/rdsp-rend2_x86_64.dll --label repeat-main-intel --width 1600 --height 900 --rough-occlusion 1 --bench-frames 360 --bench-warmup 120 --compare-resolve --check-no-prepass-cache
```

The earlier coverage limits still apply: moving NPC/weapon gameplay QA,
representative-sample MSAA resolve and wider maps/materials remain unverified.
Installed game files were only read; each run has its own executable and home.

## 2026-10-28 review follow-up (branch `fix/ssr-review`)

Second external review plus an own review; the changes are described in
`rend2-ssr.md` (emitter occlusion, hit cache, classification, fused resolve,
footprint depth). Worktree `C:/Users/Keyten/Desktop/projects/OpenJK-rend2-ssrfix`;
the baseline is a clean SP renderer build of `4104a4f71`
(`C:/Users/Keyten/Desktop/projects/OpenJK-rend2-ssrbase`).

GPU regressions: `test_ssr_gl.py` passes on Intel UHD and RTX 2060 (17
permutations). New checks: emitter occlusion by a zero-confidence hit (the
baseline reproduces the leak), fused composite = resolve pass for direct and
cached half-res rays in every phase, footprint depth narrowing next to a
foreground texel, the cached-ray length gate, the 14 bit confidence /
geometry / reused bits in real RGBA16 storage, and LESS-only classification
(no-ray pixels keep the clear, ray pixels run). The old classification check
created its render target on texture unit 0, which is the Hi-Z sampler, so
its receivers were never valid and it could not fail. Water SSR (8 motion
permutations) and the SSGI / LTC saber / skin programs that embed
`ssr_common` still compile on both GPUs.

Game timings, Intel UHD, mp/ffa3 `(671, -769, -70)`, 1600x900, medium preset
(half res, Hi-Z), temporal off, resolve cache on, hit cache and culling
forced per section. Two interleaved rounds each (`build/ssr-runtime/ab-*`
in the worktree), all without overlap; medians in ms, round 1 / round 2:

| section | yaw | baseline | fixed |
|---|---:|---:|---:|
| Main View 0, cache 1 cull 1 | 290 | 24.59 / 24.08 | 22.06 / 21.87 |
| Main View 0, cache 1 cull 1 | 310 | 24.33 / 24.13 | 22.42 / 23.44 |
| SSR resolve + composite | 290 | 7.03 / 6.78 | 4.91 / 4.85 |
| SSR trace, cache 1 cull 1 | 290 | 5.44 / 5.33 | 4.99 / 5.00 |
| SSR trace, cache 0 cull 1 | 290 | 4.56 / 4.55 | 4.49 / 4.59 |
| SSR trace, cache 1 cull 0 | 290 | 4.70 / 4.67 | 3.95 / 3.88 |
| SSR trace, cache 1 cull 0 | 310 | 4.42 / 4.39 | 3.81 / 3.83 |

- The fused resolve and the composite discard save ~2 ms per view: the
  resolve section keeps only the per-ray radiance pass (~0.45 ms).
- Tile refresh, the length gate and no-reuse-with-linear make the cache
  ~0.8 ms cheaper without culling, but caching still loses to tracing
  every ray (cache 1 vs 0 with culling: 5.0 vs 4.5 ms). `r_ssrHitCache`
  now defaults to 0.
- The LESS classification made no measurable difference on Intel (cache 0
  cull 1 unchanged). Culling still loses in these dense views; a four-mode
  run (`--modes 00,10,01,11`, overlapped by another game, ordering only)
  ranks cache 0 cull 0 < cache 1 cull 0 < cache 0 cull 1 < cache 1 cull 1 at
  both yaws. `r_ssrReceiverCull` stays 1: it is meant for sparse receivers,
  which these views are not; measure on such scenes before changing it.
- Not measured: RTX timings, temporal accumulation, full resolution rays,
  MSAA, moving NPC / saber gameplay and the visual effect of the footprint
  depth test.
