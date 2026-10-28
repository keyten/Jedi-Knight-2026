# X07: production forward evaluation and blind fixed-geometry RGB fitting

The production C++ forward evaluator and a real bounded blind amplitude
optimizer now operate together on `point_room`, `two_colour` and
`door_occlusion`. The protocol is **FixedGeometryBlindIntensityFit**, not
recovery of original emitter geometry. XYZ, point type, count and a selected
known compiler profile are supplied priors. Only compiler-relative RGB
amplitudes are estimated. No canonical physical emitter asset is published,
and no renderer consumer, cvar, shader or runtime lighting path changes.

Versioned evidence:
[semantic results](../tools/rend2/fixed_geometry/results/validation.json),
[time/RSS](../tools/rend2/fixed_geometry/results/measurements.json),
[commands/tool guide](../tools/rend2/fixed_geometry/README.md).
The witness records base commit `df7da6eb1a9f98c8bd22a57a7e21a2c712fb81d3`
and LF-normalized hashes of the actual working sources/dependencies. The
working-tree hashes identify this implementation beyond that base commit.

## Inputs, identities and scope

These are existing real compiled BSPs, not analytic target images. X07 reads
original captured RGB8 and mapped floor geometry from the immutable canonical
archives. It validates the original archive, runs `public-check` on the sanitized
geometry, matches archive/BSP identities, checks compiled/source-map hashes and
the compiler binary witness, and compares every admitted RGB to the original
BSP lightmap lump. The trusted controller separately verifies the private
recipe's lights against authored `.map` entities. Those authoring files never
enter worker inputs. This run reused the pinned compiled outputs; it did not
invoke q3map2 again or run gameplay.

| Fixture | Original BSP SHA256 |
|---|---|
| point_room | `890a15b75b0ae24ddad2725ba24327f7e1949bf17d0e7fd6baef100a59feb2af` |
| two_colour | `0969f5a631e5a9bac95224f1831df1587bad62f883761c89b92231119eef6ec1` |
| door_occlusion | `5763cba0befb01a84686664338c7ce8cc8a1feb333e4a90bb9a74343ab5f9c39` |

Compiler: NetRadiant `2.5.17n-git-d9338309e`, executable SHA256
`894049bd136e7828b0f068d12c932833153b3ada87b99655dac90e4a60c85ad4`.
Profile family: `NetRadiant-d9338309e-subsets-v2`, source-lock SHA256
`56e385069a4dfbb50244b71370c7a75adaf5dd7b52ed41ead41d09ff2578b314`.
The selected inverse-square profile key is
`bf4382d99bbc93b9a8158e2d387ad68311ce36bf13e246018946b7c9d30e2231`.
Private source recipes, original archive hashes, partition/member hashes,
candidate positions and profile identities are also recorded in the witness.

Supported here: direct point sources, style 0, planar interior receivers,
cosine response, pointscale 7500, minimum distance 16 world units, gamma 1,
compensate 1, exposure 0, zero bounce and the declared FAST recipe. No diffuse
ambient/GI is fitted in the emitter model. The ambient-only control is a
separate explicit profile operation. These kernels are verified for the stated
compiler subset. General compiler-tree traces are still tagged conditional;
this does not extend the special verified `multi_sun` replay to arbitrary BSPs,
patches, alpha masks or Raven. Stock JA remains `UnknownProfile`.

## Blind boundary and frozen evaluation

The worker receives only a sanitized `TrainingView`, public geometry with no
images/grid RGB/vertex RGB/HDR/source answers, a geometry-only request, explicit
profile assumptions, bounds and allowed fixed point positions. All source seed
RGB values in the C++ request must be **zero**. Actual initialization to RGB 64
is the declared optimizer policy, independent of answers.

The request contains BSP trace planes/nodes/leaves and relevant opaque/sky
triangles, but no observed target colours, BSP entity lump, lightmaps, recipe
answers or validation/held-out colours. Public geometry validation checks
indirect colour/answer aliases as well. Dataset checksums, map identity,
source/receiver IDs, XYZ and normals are cross-checked against the adapter's
actual preparation handshake; a different request cannot silently substitute
receivers or candidates.

`run.py` is trusted and `worker.py` is a separate optimizer process. The worker
imports `TrainingView`; it never imports the trusted evaluator or Python light
kernels. Validation/held-out files live outside its input directory. Actual
read attempts targeting three **existing** private files fail with
`PermissionError`; training API requests for a held-out stream fail. The Python
audit allowlist denies ordinary file/directory, arbitrary process, ctypes and
network escape routes. This tests the intended API/data boundary, not a hostile
native-code OS sandbox. Real untrusted optimizer deployment still requires a
separate user/container and read-only input mounts. File hashes are integrity
and identity checks, not authentication against a malicious data publisher.

The optimizer freezes RGB and a parameter hash before strict replay,
uncertainty probes, validation or held-out evaluation. The trusted controller
checks the complete fit-file hash before and after independent evaluation.
Neither validation nor held-out scores feed back into fitting. Within-run file
freeze hashes include timings; deterministic comparisons use semantic output
and the parameter hash instead.

## Sampling, coverage and independence

Selection is geometry-only: mapped static floor, Z=0, +Z normal,
|X|<=176 and |Y|<=112. Each map has 345 selected original mapped floor luxels.
They all belong to one planar receiver surface; this is not a multi-surface
source-position benchmark or full-map readiness result.

Split: world-space blocks of 128 units, origin `(17,31,47)`, seed 20261030.
The seed was selected using geometry-only block occupancy, before fitting, to
give all roles adequate support; it was not selected from hidden RGB or scores.
A full footprint plus three-pixel filtering guard and one-unit margin must fit
inside a block. Consequently 297/345 candidates are withheld as gaps, and each
map has 18 training, 15 validation and 15 held-out luxels. Their known areas are
4608, 3840 and 3840 world-units-squared respectively. The gap is intentional,
not an unavailable-prediction category or silently dropped page.

Every requested role observation is comparable in the selected controlled
domain and receives a prediction: coverage 100%, unavailable 0. Held-out
contains 15 positive observations across two spatial blocks in every fixture.
Training has 18 positive observations for point/two-colour; doorway has nine
visible and nine genuinely blocked observations. Unknown transfer/primaries,
unsupported styles, invalid normals and unknown visibility are rejected, not
replaced by black. Material uncertainty is retained; this fits the verified
stored-lightmap operator, without division by current PBR albedo.

Doorway validation has nine positive rays through the aperture and six on the
source side. Its held-out rays are all on the source side. Thus the aperture
and wall constraints are exercised, but the final held-out split does **not**
independently establish shadow-edge localization. No source XYZ is optimized.

## Production numerical path and objective

`lightrecon_bake_forward_tool --server request.txt` invokes
`Bake::CpuForwardEvaluator::Prepare` and `Evaluate` in the shared CPU core.
`bake_fit_server.h` is a bounded tool adapter, not a second forward formula.
Its injected visibility provider calls the production `CompilerTrace`; origins
classified solid and uncertain visibility produce Unknown/Unavailable.
Preparation caches immutable fixed geometry/visibility only. Each amplitude
trial reruns source amplitude/cutoff, accumulation and actual output encoding.
Strict `R` replay discards factors and retraces; fitted cached/reference
predictions agree exactly in these cases.

On an active inverse-square branch the unencoded direct channel is
`7500 Q_c max(n·toSource,0) / d_eff²`, subject to the selected compiler recipe's
normal, clamp and FAST rules. `Q` is compiler-relative RGB, **not candela**.
Linear controls use their own compiler formula and amplitude-dependent reach.
Summed pre-encoding contributions then pass through the profile's shared-peak
RGB transform and truncation. The full response is not globally linear in Q.

For observed byte b<255, compare the C++ **encoded prequantization** prediction
p against the truncation interval `[b,b+1]`; endpoint inclusion is a numerical
tolerance at the bin edge. For b=255 the encoded endpoint is 255. The forward
shared-peak normalization maps admissible over-range input to that endpoint;
this is not a penalty for physical irradiance exceeding 255. It also is not
an independent infinite interval for each channel: shared-peak clipping couples
RGB. Fully white-saturated data cannot identify absolute amplitude and return
`CensoredUnidentifiable`. Black uses its quantization bin, not a negative-value
or logarithmic surrogate.

Let e(p,b) be signed distance to this interval. The training objective is
`sum_i w_i sum_c e(p_ic,b_ic)^2 / 3`. Each occupied 128-unit block has equal
total weight; within a block weight is proportional to luxel area. Reports
separately provide area-weighted and block/area-weighted errors. They do not
silently rescale predicted brightness to match held-out targets.

Algorithm (double Python numerical operations; production accumulation uses
its established double/float API boundaries):

1. Initialize every amplitude to 64, bounds `[0,1200]`, at most six fitted
   channels in the primary fixtures. No answer-dependent warm start.
2. Up to eight local finite-difference Gauss-Newton proposals, step size 1,
   damping `max(1e-8 max diag(JᵀJ),1e-10)`, per-step bound 500,
   projected line search at `1,1/2,1/4,1/8,1/16`. A bin-midpoint target is used
   only to propose a step; acceptance always uses the real interval objective.
3. Coordinate fallback: fixed bounded coarse probes, 15 golden steps per
   coordinate, up to 12 passes. The local Jacobian is not cached as a global
   linear response matrix across encoding/FAST branches.
4. Stop at objective <=1e-8, or improvement <=1e-7 and motion <=0.02, or
   pass/work limit. Stagnation/pass exhaustion is labelled, not a convergence
   certificate. At most 1800 C++ calls including reserved diagnostics; an
   exhausted budget emits no fitted parameters. The adapter caps eight sources,
   512 receivers, 4096 pairs/rays and 1,048,576 work units per request.
5. Freeze, strict replay, conditional feasible slices and local response-rank
   diagnostics. The trusted controller then evaluates all partitions independently.

All finite-difference and line-search predictions come from C++. No NNLS
assumption is made about a globally nonlinear encoding/cutoff model.
Cancellation, unknown visibility, unsupported profile features, malformed
input and budget exhaustion are explicit errors, not unavailable values
counted as black. The controller also imposes a 120-second worker watchdog;
it terminates worker/adapter rather than accepting a timed-out fit.

An earlier coordinate-only two-colour pilot exhausted its reserved work budget
after 1552 calls and published no fit. That failure is preserved in the witness.
Local production finite-difference proposals were added after this training-side
failure. No hidden amplitudes or held-out results were used to initialize or
tune the resulting optimizer.

## Numerical results

RMSE below is distance to the byte quantization interval, area weighted, in
encoded RGB8 code units; it is not lux/candela error. All primary training
objectives are zero within bins. Predeclared positive-light admission requires
at least four positive samples, two spatial blocks and black RMSE >=4; the
held-out gate additionally requires two positive blocks, complete predictions,
RMSE/max interval excess <=1 and >=50% improvement over black. These are scoped
quantization/coverage checks, not universal physical-reconstruction thresholds.

| Fixture | Training | Validation | Held-out | Black held-out | Ambient-only | Wrong linear profile | Wrong XYZ |
|---|---:|---:|---:|---:|---:|---:|---:|
| point_room | 0 | 0 | 0 | 50.5054 | 13.0565 | 9.0894 | 25.0986 |
| two_colour | 0 | 0.06629 | 0.01274 | 35.1707 | 10.7304 | 28.7350 | 14.7798 |
| door_occlusion | 0 | 0.00138 | 0 | 50.5054 | 24.0500 | 9.0894 | 22.8915 |

Dropping the second two-colour source gives held-out RMSE 7.5647. Wrong XYZ
offsets the first supplied source by `(64,48,0)` world units. Wrong profile uses
the same unknown RGB budget and cosine response with the supported linear
attenuation alternative. Ambient-only has three parameters, a distinct simpler
baseline rather than an equal-complexity physical source model. All controls
use the same partitions/domains and independently fitted training amplitudes.

Trusted source-amplitude comparison, performed only after freezing:

| Fixture/source | Hidden compiler Q | Estimated Q | Relative vector L2 error |
|---|---|---|---:|
| point_room | (300,300,300) | (300.04507,300.04507,300.04507) | 0.0150% |
| two_colour/1 | approximately (300,36,9) | (299.84725,36.37721,9.15664) | 0.1443% |
| two_colour/2 | approximately (12,60,300) | (12.52831,59.68380,298.81193) | 0.4370% |
| door_occlusion | (300,300,300) | (299.64149,299.64149,299.64149) | 0.1195% |

Exact source colour decimals from the private recipe, chromaticity-angle errors
and full allocations appear in JSON. Physical photometry is `NotComparable`.
True-amplitude strict C++ replay has zero held-out interval error in all three
fixtures. The small fitted/oracle difference is consistent with fitting only
quantized training bins: exact true amplitudes are not uniquely recoverable.

All true channels fall inside reported one-coordinate feasible slices with
maximum encoded excess <=1 while the other fitted coordinates are frozen.
For example point-room channels admit approximately `[293.489,306.307]`.
These are conditional numerical slices with a declared discrepancy allowance,
not joint confidence regions, Bayesian probabilities or calibrated 95% intervals.
The endpoint bisection describes a local connected branch; it does not certify
all feasible intervals across FAST/encoding changes or exclude remote solutions.

The local encoded-response Jacobian has rank 3/3 for one source and 6/6 for
two-colour (condition estimate 2.96). This concerns amplitudes at fixed supplied
geometry/profile, not XYZ identifiability. The near-duplicate control adds a
candidate 0.01 units from the first: it achieves held-out zero with one candidate
zero and the other approximately RGB 300.03212, rank 3/6 on that active branch.
Moving the allocation to the other candidate also gives held-out zero. FAST
cutoffs can make a zero-amplitude column locally flat; the rank estimate alone
is not a global uniqueness test. The explicit alternative demonstrates joint
allocation ambiguity. No second source is certified; existence confidence is null.

## Work, memory and reproducibility

| Correct fit | Optimization calls | Strict/final calls | Uncertainty calls | Total C++ calls | Worker rays including preparation/replay |
|---|---:|---:|---:|---:|---:|
| point_room | 9 | 2 | 96 | 107 | 36 |
| two_colour | 171 | 2 | 192 | 365 | 72 |
| door_occlusion | 9 | 2 | 96 | 107 | 36 |

No visibility rays are reused incorrectly after source motion: XYZ never changes
within a fit, and wrong-XYZ/other models rebuild preparation. Cached amplitude
trials need zero rays, but still recompute source-response/encoding. Total worker
source responses are 1926/13140/1926. Trusted train/validation/held-out plus
black/oracle evaluation is separately counted: 174/348/174 rays, including those
clients' preparation. The JSON records node/triangle tests and unsuccessful
controls as well as accepted work.

On this Windows/MSVC Release workstation, run A correct-fit wall times including
worker startup are about 0.21/0.36/0.23 s. C++ evaluation portions are about
0.038/0.148/0.060 s; Python worker CPU times are 0.016/0.063/0.047 s (coarse
OS timer resolution). Loading/auditing each immutable source takes about 0.6–0.7 s.
Complete 14-model runs plus three repeat fits take 9.20 and 9.03 s. These are
small controlled workloads on a shared workstation, not isolated performance
claims or whole-stock-map throughput.

Peak process working sets are measured using `GetProcessMemoryInfo`: workers
about 17.4–17.9 MiB, C++ about 4.1 MiB, controller about 34 MiB. Processes are
reported separately; the sum of independent peaks is not called simultaneous
peak memory. Sanitized model inputs total 21,386/21,568/30,545 bytes. There is
no mandatory all-map dense source-by-luxel matrix. The optimizer holds at most
small local finite-difference columns; processes release geometry at exit.

Both complete final runs have identical parameters, profile/partition hashes,
metrics, errors, uncertainty diagnostics and work counters. Each also restarts
the correct fit from RGB64 and reproduces the same semantic output. Variable
time/RSS and timing-containing file hashes are excluded, not numerical results.
Semantic SHA256: `fef5dcec72ea697aea086ebcaa131b89e8c55f27a6ddfcb6fad2c75a7f238371`.

## Tests and exact commands

Commands executed from the repository root; `python` here was
`C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`
with `-X utf8`. CMake/GCC were on PATH from
`../OpenJK/build/toolchain/w64devkit/bin`.

```powershell
cmake --build build/lightrecon-core-renderers --config Release --target lightrecon_bake_forward_tool rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 6
cmake --build build/lightrecon-disk-standalone --parallel 6
ctest --test-dir build/lightrecon-disk-standalone --output-on-failure
python -X utf8 tools/rend2/fixed_geometry/run.py --output build/x07-published-a
python -X utf8 tools/rend2/fixed_geometry/run.py --output build/x07-published-b
$env:JK_X07_FIXTURE_ROOT = 'build/x07-published-a'
python -X utf8 -m unittest discover -s tools/rend2/fixed_geometry -p 'test_*.py' -v
python -X utf8 tools/rend2/fixed_geometry/report.py --run build/x07-published-a --compare build/x07-published-b --output tools/rend2/fixed_geometry/results --failed-pilot build/x07-full-first/two_colour/correct/work/fit.json
python -X utf8 -m unittest discover -s tools/rend2/bake_model -p 'test_*.py' -v
python -X utf8 -m unittest discover -s tools/rend2/evaluation/x05 -p 'test_*.py' -v
python -X utf8 tools/rend2/bake_model/validate_cpp.py --exe build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe --bsp-dir build/bake-model-comparison-final/authoring/base/maps --output build/x07-production-regression --x05-manifest tools/rend2/evaluation/x05/results/manifest.json --x05-compiler-dir build/x05/compiler-final --cross-map-dir build/x06-controlled-final
python -X utf8 tools/rend2/lightrecon_regression.py --output build/x07-shader-regression
```

Results: SP and MP Release pass; standalone CPU 18/18; X07 22/22 with no skips;
bake-model Python 24/24; X05 16/16. The X07 tests exercise actual production
cancellation, work limit, unknown visibility, unsupported features, cross-map
identity, corrupted geometry, truncated observations, changed correspondence,
private amplitude injection, forbidden existing hidden reads, deterministic
restarts, zero-signal/fully saturated admission, wrong transfer/primaries,
nonfinite geometry and missing held-out predictions. Wrong profile/XYZ/count,
ambient and near-duplicate controls are run through the real bounded fitter.

The production regression retains the 22 original compiler-case statuses:
supported direct cases remain within their frozen 0/1-byte tolerances; the two
bounce cases explicitly remain `UnsupportedBounceReplay`, not passed bounce
reconstruction. X05 remains passed. Eight X06 cases retain supported/partial
status: `thin_door` still excludes 25 unsupported nominal receiver placements,
without changing tolerance or claiming full support. The scoped 1225-sample
`multi_sun` replay remains exact. Disabled-path GPU regression compiles/links
27 programs/51 stages on Intel UHD GL3.2, compares the matching-driver baseline,
and reports zero differences. Existing CMake no-tag/CPack notices are unrelated.

## Limitations and next steps

This is a diagnostic fit, schema `org.openjk.fixed-geometry-blind-intensity-fit.v1`.
It records assumptions, frozen parameters, coverage, work and trusted metrics;
it deliberately has no exact-light capability or canonical physical-emitter
publication. Actual strict C++ prediction status codes are retained for training
and trusted evaluation; all present fits report `AvailableConditional` (7), not
an unqualified verified-compiler or physical-light result. Source detection
precision/recall, source XYZ accuracy and absolute
photometric error are not estimated. No stock-map inference, GI, source-count
selection, angular fitting, uncertainty calibration or full-map streaming
optimizer is implemented. Two-colour amplitudes are small deviations within
quantization ambiguity, not proof of an exact authored answer.

The next bounded step can reuse this production adapter for larger fixed
candidate sets with rank/alternative-solution tests and more receiver normals.
Localization must separately add admissible XYZ proposals, verified geometry
constraints and shadow-boundary handling. Unknown stock profiles require
explicit competing hypotheses. A malicious native solver still needs OS-level
isolation. Existing game rendering remains independent throughout.
