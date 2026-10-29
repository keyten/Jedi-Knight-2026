# S07: geometry-constrained point-position refinement

This is an offline CPU **fixed-count point XYZ and compiler-relative RGB diagnostic**.
It does not discover sources, select source count/type, recover the stock Raven
baker, publish a `ReconstructedLightingAsset`, or enable a renderer consumer.
The implementation extends the S06 baseline at commit `54e319d84`; S06's forward
laws, unit separation and linearity proof are reused. No runtime renderer entry
point, cvar, shadow map, lightmap upload or shader is changed.

## Interface and ownership

`shared/rd-rend2/lightrecon/point_refine.h/.cpp` exposes
`PointRefine::Refine(Request, ReadContext, Result)` and `MakePlacement`.
It is linked into the shared CPU core for SP, MP and standalone tools.

The caller supplies an immutable **training-only** S06 observation stream,
explicit supported bake profile, candidates and geometry generations. There is
no file, ground-truth or held-out loader in this API. Candidate point IDs/count
remain fixed; other supplied source types remain fixed and are evaluated only
when S01/S06 support them. Every refined point retains its original XYZ.

Each point has hard world-space bounds, initial/maximum neighborhood half-width,
placement support radius, clearance, optional mounting plane/offset interval and
a provenance-bearing position prior. These placement radii do **not** silently
change the historical point emitter into a finite-area source. Unknown region
membership is permitted when the BSP/visual geometry can certify placement.

`PlacementProvider` is immutable and may serve concurrent independent callers.
The built-in implementation owns shared immutable BSP-domain/visibility/optional
region references and copied moving-model bounds. Its map, geometry and policy
keys must match the lighting request. An external provider must uphold the same
volume/path certification contract; `Free` is not an invitation to use a sparse
point test or PVS. The caller's `domainKey` must fingerprint the actual domain and
classification policy; arbitrary callback code cannot be hashed automatically.

The optimizer owns its temporary factor cache, streamed weight totals, current
states and bounded trace. It has no global scratch, thread, asynchronous write or
GPU resource. Failed/cancelled calls return diagnostics/work, with no partial
position or amplitude result. A successful result is a numerical diagnostic,
not evidence of physical source existence.

## Objective, units and S06 admissibility

For positions `p` and nonnegative RGB amplitudes `Q`:

```text
F(p) = bounded_inner_fit_Q [
    sum_i w_i sum_c rho(distance(Phi_profile,i(p,Q)_c, interval_ic) / sigma_i)
    + S06 source/chromaticity/ridge priors
] + 1/2 sum_jk lambda_j ((p_jk - prior_jk)/sigma_position_jk)^2
  + 1/2 sum_d weight_d max(0, angle(direction_to_source_d,p)-cone_d)^2.
```

`bounded_inner_fit` is an approximate numerical solve with its own reported stop,
not a claim to compute a global minimum. `Phi` is the **strict production C++ bake
observation operator**, never runtime PBR attenuation. S06 rechecks linearity over
the full declared Q bounds **at every trial XYZ**. Proven pre-encoding/uncoupled
branches use normalized bounded coordinate descent with projected KKT checks.
FAST/intensity-dependent cutoff, linear-falloff support, reachable shared-peak
encoding/gamma and grid reduction use S06's bounded nonlinear strict-operator
path. A contribution matrix from one XYZ/Q is never extrapolated to another.

Surface weights preserve covered world area. By default, equal-weight region/type
groups normalize their own area; unknown regions use deterministic 128-WU spatial
bins, separately by observation type. This is a sampling policy, **not invented
room topology**. It stops a large bright floor/room from simply winning by pixel
count. Disable `balanceRegions` for globally area-weighted S06 behavior. Grid
ambient/directed each have their own opt-in weights and observation/calibration
rules; neither is relabelled physical indirect/direct irradiance.

S06 quantization intervals, censored RGB8 channels, robust loss, peak-Q/chromaticity
coupling and finite bounds remain intact. A free ambient/GI nuisance baseline is
explicitly unsupported in this XYZ stage. Fixed known terms remain allowed.
No residual field or extra emitter is inserted to improve the fit.

`Q`, `max(Q)` and peak-normalized RGB are compiler-relative parameters, **not cd,
lm or lux**. Unknown absolute scale remains unknown. XYZ is in canonical BSP world
units. Fixture metre errors use the explicitly reported canonical default
`Units::worldUnitsPerMeter` (approximately 33.3333); this geometric conversion
does not calibrate brightness. Stock `UnknownProfile` has no numerical law;
an explicit admissible hypothesis is required and retains conditional status.

## Geometry constraints

Every initial point, proposal and final point is certified:

1. Intersect the hard bounds and initial neighborhood; enforce any mounting
   normal-offset interval. A mounting prior never overrides solid geometry.
2. Outward-round the support-radius-plus-clearance cube to float bounds. Require
   the entire BSP-domain box and its representative point to be Free/Exterior.
   Solid, sky, physically Outside and unresolved boxes are rejected distinctly.
3. Use `StaticVisibility::OverlapBox` with **local** coverage certification.
   Unknown unbounded blockers remain a limitation. Alpha/translucent/unsupported
   contacts and patch approximation envelopes stay uncertain, not transparent.
4. Certify the previous accepted point and new point, then trace the intervening
   segment with CPU geometry. An opaque wall rejects the move; unknown/assumed
   optics reject it as an unknown path.
5. Certify enclosing boxes of the full swept support cube, adaptively splitting
   a segment if the conservative larger box is inconclusive. Bounded work,
   subdivision count and minimum segment length prevent indefinite refinement.
   An unresolved segment remains rejected. This avoids jumping through a thin
   wall merely because its two endpoints are free.
6. Known swept moving-model bounds are excluded locally. Unknown moving extent
   remains a conservative global limitation. Regions are optional metadata;
   same-region membership never proves optical visibility.

This is conservative. A feasible curved route around a corner may need several
accepted straight segments; the method cannot teleport an initially invalid
point out of a wall or through a closed door. Initial invalid placement returns
`Unavailable`. It deliberately does not rescue hypotheses with unproven geometry.

Supplementary direction cones require explicit **isolated-source attribution**,
profile validation, provenance and actual geometric visibility. Mixed historical
grid/deluxe directions lacking those attestations are excluded with a reason.
Even valid parallel cones do not determine source depth. Grid RGB can still be
used through its separate S06 operator without inventing source attribution.

## Search and factor lifetime

The optimizer polls six bounded axial positions per point, alternates each XYZ
trial with warm-started S06 RGB fitting, accepts decreasing total cost and halves
the poll radius after a stall. Optional parabolic cost proposals are admitted
only when neighboring inner fits prove linear and discrete optical signatures
match. They remain proposals: their placement, RGB fit and strict value are
evaluated again. No shadow gradient is advertised.

The method is appropriate to hard visibility and small candidate blocks. It is
not a global optimizer: a shadow discontinuity, narrow valley or bad inner solve
can trap it. Optional deterministic Halton starts seek alternatives within the
declared neighborhood. No random global room search occurs. Expansion is opt-in,
requires encountering neighborhood bounds with unresolved data error, doubles
only up to the explicit maximum, and still enforces hard bounds and swept paths.

Factor keys include actual source XYZ/geometry, receiver geometry, map/material,
profile and visibility policies. Repeated Q fits reuse only geometry-safe data;
XYZ changes create a new dependency generation. Old keyed blocks may remain
until bounded cache eviction but cannot be read as current factors. The reported
`cacheInvalidations` counts newly fitted position generations, not bytes erased.

Comparable observation IDs/types/channel availability are hashed at initial
admission. A trial with changed admission is rejected: it cannot win by moving
into unknown visibility and dropping difficult rows. This does not certify all
raw observations; callers must still inspect initial S06 coverage/exclusions.
Blocked comparable rows remain actual zero contributions; Unknown is not black.

Stopping distinguishes minimum-poll stationarity, sweep exhaustion, no positive
signal, cancellation, work limit and numerical failure. Sweep exhaustion can
return an improved diagnostic with `converged=false`. Work/cancellation failures
publish no partial solution. A final cache-cleared **value-only** S06 `Assess`
replays the frozen XYZ/Q against the strict operator: it does not secretly refit
amplitudes during acceptance. Source movement invalidates source-dependent rays.

## Uncertainty and non-identifiability

There is no heuristic existence probability or calibrated XYZ confidence. Bounded
diagnostics select training rows deterministically by ID, compute finite
differences of strict predictions and check optical/admission stability. XYZ
steps `h` and `h/2` are compared; a relative change above 0.2 disables the local
identification flag. This is a numerical safeguard, not a calibrated derivative
error bound. Binary visibility changes or infeasible perturbations make smooth
local information unavailable. No frozen-visibility shadow derivative is used.

For prediction Jacobian `[Jp Jq]`, amplitude nuisance directions are projected out:

```text
Hposition = Jp' W Jp - Jp' W Jq (Jq' W Jq)^+ Jq' W Jp.
```

The singular spectrum/rank uses relative cutoff `1e-4` plus numerical absolute
floor `1e-7`. Priors are excluded from this information calculation so a strong
fixture prior cannot manufacture data identifiability. Full rank, stable steps,
positive data and no censoring permit only a **local conditional** identification
flag. This is not proof of a unique global solution, correct profile/material,
correct source count, well-calibrated confidence, or historical source recovery.

Axis ranges refit Q at a bounded set of `±h, ±2h, ±4h` positions and retain those
within declared objective slack. They are tested envelopes, not continuous
feasible sets, joint confidence intervals or guaranteed bounds. Geometry/search
limits and unsampled axes are explicit. Sparse/symmetric receivers may preserve
an incorrect XYZ with perfect RGB; that is an expected unresolved result.

## Validation and resources

Owned synthetic room/brush geometry is independently classified by analytic box
volumes and by the production triangle BVH. Targets use the supported production
forward operator; these are optimizer tests, not new historical-compiler parity
claims. Controller-only truth XYZ and RGB amplitude vectors never enter
`PointRefine::Request`. The two-nearby-source fixture deliberately provides
known fixture chromaticities as a declared prior; its excellent localization is
conditional on that information, not a blind proof of general two-light recovery.
The existing pinned compiled-BSP S06/S01 regressions are run separately.

The test suite covers an open room, different receiver normals, wall-mounted and
recessed fixtures, partial shadow changes, incorrect initial neighborhood with
controlled expansion, a broad single plane with multiple starts, symmetric
receivers, parallel-direction depth ambiguity, two nearby differently colored
sources and sparse grid-plus-surface observations. Negatives include a thin wall,
opaque path, invalid initial position, alpha uncertainty, unbounded missing
geometry, moving bounds, stale keys, black signal, UnknownProfile, cancellation
and memory/work exhaustion. IDs/counts, finite nonnegative Q, final placement and
strict replay are asserted. Two full runs must match numerical JSON **and trace**.
A deliberately biased position prior with only one receiver reaches the supplied
prior location and perfect RGB while remaining rank deficient. Its result is
reported as prior-driven ambiguity rather than data-derived localization.

Machine-readable numerical/source-hash witnesses, resources, CSV and the open-room
trial trace are under `tools/rend2/point_refine/results/`. Position error is RMS
over retained source IDs (one source except the two-source row).

| Owned scene | Initial error, WU | Final error, WU | Interpretation |
|---|---:|---:|---|
| Open room, three receiver normals | 51.2250 | 0.0000191 | Local rank 3/3 |
| Wall adjacent, support cube/mount policy | 16.4924 | 0.0002257 | Admissible wall clearance |
| Recessed fixture | 13.4164 | 0.0005982 | 43 optical-state changes explored |
| Partial occlusion | 46.6476 | 0.0000786 | 45 optical-state changes explored |
| Wrong initial neighborhood, three expansions | 72 | 0.0017895 | **Sweep limit**, not convergence |
| Broad single plane, two starts | 57.8273 | 0.0000813 | Strong spatial extent; not every plane is unobservable |
| Two symmetric receivers | 40 | 40 | RGB objective 0; rank 1/3; unresolved |
| Parallel direction / depth ambiguity | 40 | 40 | Rank 0/3 after Q projection; unresolved |
| Two nearby coloured sources, declared colour priors | 24.2074 | 0.0002015 | Both IDs retained; joint local rank 6/6 |
| Sparse unpacked grid + floor | 46.3033 | 0.2938332 | Nonlinear Q path; geometry diagnostics explicitly disabled |
| One receiver, biased explicit position prior | 48 | 28.2843 | Reaches biased prior, objective 0, data rank 0/3 |

The sparse-grid final error is approximately **0.008815 m** under the declared
geometric scale; the symmetric/depth alternatives differ by **1.2 m** with
essentially identical RGB. Small errors in internally generated noiseless
fixtures are optimizer checks, not submillimetre accuracy claims on real BSPs.
Maximum relative Q-vector error in informative cases is about **0.175%** (sparse
grid); the depth-ambiguous fixture can differ in amplitude by about **9.2%** while
matching its lone receiver. No confidence interval is inferred from those errors.

The initial sparse-grid test stopped at 4.2205 WU error using a 0.25-WU minimum
poll. With the same observations/profile/RGB tolerances and a 0.025-WU minimum
poll it reached approximately 0.294 WU. This exposes a narrow profiled XYZ/Q
valley and resolution-dependent stationarity; a coarse poll must not be reported
as accurate localization merely because its iterations have stopped.

## Reproduction

From the repository root, using the existing local toolchains:

```powershell
$env:PATH='C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;'+$env:PATH
cmake -S tools/lightrecon_test -B build/s06-gcc -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build/s06-gcc --parallel 2
python -X utf8 tools/rend2/point_refine/benchmark.py --tool build/s06-gcc/lightrecon_point_refine_test.exe --out tools/rend2/point_refine/results/gcc
ctest --test-dir build/s06-gcc --output-on-failure
cmake --build build/lightrecon-core-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 lightrecon_point_refine_test lightrecon_amplitude_test lightrecon_bake_forward_tool --parallel 2
python -X utf8 -m unittest discover -s tools/rend2/point_refine -p 'test_*.py' -v
```

`build/lightrecon-core-renderers` is the existing configured MSVC SP/MP build;
standalone CMake needs no engine/GL. The absolute local Python used is
`C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`.
No dependency was installed. GCC Release uses `-O3 -DNDEBUG`; MSVC uses its existing
Release configuration. Timing/RSS are separate from deterministic witnesses.

### Final execution results

Final GCC Release: **1,492 assertions, 0 failures, twice**, with identical
numerical witnesses and open-room trace bytes. The runner also checked that
source/executable hashes did not change during the experiment. Whole-process
times were **233.50 / 204.69 seconds** under concurrent local load; OS peak RSS
was **13,697,024 / 13,692,928 bytes** (about 13.06 MiB). These are actual Windows
process counters, not summed STL capacities. Conservative owned/scratch preflight
bounds for these configurations were **35.98–36.00 MB**, below the
configured 96-MiB overall cap; immutable caller-owned geometry is separately owned.

Eleven recorded cases together use 6,784 inner fits, 220,695 amplitude objective
evaluations and 745,418 rays, including strict checks/placement. The complete
executable additionally repeats the open case and runs negative controls, so
those totals are not represented as its entire work. Detailed per-case work is
in `summary.csv`/`numerical.json`.

| Example | Inner fits | Q objective evaluations | Rays | GCC time range |
|---|---:|---:|---:|---:|
| Open multinormal room | 242 | 1,472 | 29,647 | 1.72–1.79 s |
| Wall adjacent | 1,144 | 6,880 | 139,989 | 9.99–10.21 s |
| Two nearby sources | 946 | 93,266 | 230,637 | 97.07–107.29 s |
| Sparse grid + floor | 919 | 97,751 | 44,962 | 72.95–87.89 s |

This implementation prioritizes bounded fidelity over full-map throughput. The
nearby-source case exposes ill-conditioned nested intensity work; the grid case
uses the conservative nonlinear operator rather than assuming linear reduction.
Its maximum final channel residual is 0.5291 in its relative comparison domain,
not zero. The two-source inner S06 stop is **`Stagnated`**, despite tiny residual
and outer `PatternStationary`; both statuses remain available in the returned
result. Neither is a proof of a global optimum or calibrated source confidence.

Existing GCC CPU CTest suite excluding the new repeated executable: **22/22**,
32.35 s. It includes source decoding/capture, surfaces/patches, visibility, regions,
offline datasets, evidence, candidate pool, sun and S06 amplitude tests. Final
MSVC Release CTest: **S06 + S07 2/2**, 401.89 s, including S07's **1,492/0** in
384.85 s. SP and MP Release DLLs compiled/linked after the final source changes.
Existing `/arch:SSE2`-on-x64 and CMake packaging/tag warnings remain unrelated.

Python benchmark witness tests **4/4**, amplitude/blind protocol **4/4** and
production-forward protocol **6/6** passed, with no skips. `git diff --check`
passes. `results/validation.json` records successes, unsupported features and the
separate old shader-baseline failure below. No compiled-BSP XYZ fit, new bake,
game run, GPU timing or visual verification is claimed.

### Existing compiler controls and shader baseline limitation

S06 blind compiled-BSP fitting was repeated using the current production
executable (`build/s07-amplitude-regression`). `point_room`, `two_colour` and
`door_occlusion` retain held-out interval RMSE **0**. Wrong profiles give
9.04/9.76/9.04 codes; wrong supplied XYZ give 25.39/15.04/22.97 codes; dropping the
second two-colour source gives 6.68 codes. This is amplitude-regression evidence
with supplied XYZ, not an S07 compiled-BSP localization result.

`validate_cpp.py` reread the existing 22 pinned bake cases: **20 supported cases
passed** (19 byte exact, colour clipping one-byte bound); `bounce1` and
`bouncegrid1` remain explicit `UnsupportedBounceReplay`, not passed GI replay.
The scoped multi-sun comparison is unchanged. No q3map2 executable was rerun and
no new compiler-parity scope is claimed.

The optional old disabled-path shader comparison **failed**: the repository-wide
frozen baseline at `380ee0f7e352300ce329aabfdc477512d532a77a` differs in 3,562 diff
lines, including existing foliage/`LeafForceWave` and other later shader work.
Production reconstruction-dependency audit passed. S07 did not modify the
baseline. A separate comparison of identical current shader assembly/bindings
against `git show HEAD:<input>` at S06 commit
`54e319d842b2dfa87a2d485c2c001d32bfb59d3c` passed **27 programs / 51 stages**.
This proves no S07 shader-input delta; it does not declare the older full baseline
green. Both comparisons used preprocessing, not new GL driver compilation.

Additional commands actually used:

```powershell
ctest --test-dir build/s06-gcc -E '^lightrecon_point_refine$' --output-on-failure
ctest --test-dir build/lightrecon-core-renderers -C Release -R '^(lightrecon_point_refine|lightrecon_amplitude)$' --output-on-failure
python -X utf8 tools/rend2/amplitude_fit/run.py --output build/s07-amplitude-regression --exe build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe
python -X utf8 tools/rend2/bake_model/validate_cpp.py --exe build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe --bsp-dir build/bake-model-comparison-final/authoring/base/maps --output build/s07-forward-regression
python -X utf8 -m unittest discover -s tools/rend2/amplitude_fit -p 'test_*.py' -v
python -X utf8 -m unittest discover -s tools/rend2/bake_model -p 'test_cpp_forward.py' -v
python -X utf8 tools/rend2/lightrecon_regression.py --no-gpu --output build/s07-disabled-regression
python -X utf8 tools/rend2/point_refine/compare_shader_inputs.py --reference 54e319d842b2dfa87a2d485c2c001d32bfb59d3c --output tools/rend2/point_refine/results/shader-delta.json
git diff --check
```

`compare_shader_inputs.py` reuses the existing regression
module's `snapshot`, `binding_inventory`, preprocessor and matrix. It substitutes
cached `git show <reference>:<path>` text for that module's `read` function, compares all
assembled snapshots/bindings, and hashes every source input. The compact result
is retained with the validation witness; it never rewrites a baseline. Repeat
that procedure at the pinned commit when the workspace HEAD has subsequently
changed.

Early development checks were not all green: one test used an undeclared
material enum, corrected to existing `MaterialCutout`; one assertion missed
clipped search-bound proposals in the counter; and the sparse-grid coarse poll
failed its unchanged 2-WU localization criterion. Those issues were fixed and
rerun. The first exploratory full run had 1,318 checks / 2 failures; it is not
presented as an accepted numerical witness.

## Boundaries before later stages

Stock JA position recovery, arbitrary unknown material optics, ordered compressed
grid fitting, finite-source XYZ, unknown candidate count/type, global/profile
selection, physically constrained GI and calibrated joint uncertainty remain
outside this implementation. The method supports explicit S01 candidate bake
hypotheses but does not turn them into Raven compiler identification. Exact
compiler shadow replay remains limited to its existing declared scope; general
triangle rays are conditional geometry, not a claim of universal bake parity.

No game/GPU visual test is needed or claimed for this CPU-only change. The game
was not launched. Future gameplay consumers still require a separately validated
asset and explicit contribution/capability semantics.
