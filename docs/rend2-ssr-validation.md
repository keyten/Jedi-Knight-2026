# SSR cache fixes and validation

Work starts at `6e6305f91af5f932743aa1bd41f5152dcef65c93` on branch
`codex/ssr-cache-fixes`. The separate checkout is
`C:/Users/Keyten/.codex/worktrees/ssr-cache-fixes/OpenJK-rend2`.

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

## Final game results (2026-10-21)

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
