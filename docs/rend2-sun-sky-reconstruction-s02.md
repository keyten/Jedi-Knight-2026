# S02: profile-scoped directional sun and diffuse sky estimation

S02 implements a bounded CPU sun/sky estimator using the production baked-light
forward evaluator. It recovers plausible directional **hypotheses** on supported
controlled data and returns canonical candidate assets, not runtime sunlight or
proof of the historical Raven bake. Direct sun, diffuse sky, explicit fixed
local-light contributions, known recipe ambient and unexplained transport remain
separate. No `tr.sunDirection`, `r_forceSun`, CSM, atmosphere, shader permutation,
consumer cvar or renderer map-load path is changed.

The [machine-readable witness](../tools/rend2/sun_fit/results/validation.json)
and [resource measurements](../tools/rend2/sun_fit/results/measurements.json)
record actual source hashes and numerical results. The base commit is
`0387b328170853374eaaa59c37b9e57c110bdb72`; working-tree LF-normalized hashes
identify this implementation. Stock JA profiles remain **UnknownProfile**.

## API and ownership

`shared/rd-rend2/lightrecon/sun_fit.h/.cpp` is compiled in the same CPU library
shared by SP, MP and standalone tools. Public entry points:

- `FromObservation`: copies original canonical decoder RGB8 plus receiver
  geometry into a bounded input record; no renderer transform or re-encoding.
- `AuthoredHints`, `GridHint`, `DeluxeHint`: adapt existing captured metadata;
  grid/deluxe values are directional-mixture **seeds**, not certified sunlight.
- `Fit`: receives only a leased training sample chunk, explicit profile
  hypotheses, hints, units and immutable visibility geometry. It returns frozen
  proposals, training diagnostics and an identity hash, without any accepted asset.
- `ValidateAndPublish`: trusted independent validation of those frozen proposals;
  it never refits their parameters. It returns independently owned canonical assets.
- `Assess`: value-only scoring of one frozen model for final held-out evaluation.
- `StateName`: stable diagnostic names for unavailable/unknown/environment/sky/sun states.

No API performs filesystem access or owns renderer/BSP pointers. Input chunks
hold strong immutable backing leases for the call. Output vectors own all their
data, and no training RGB buffer is retained in `FitResult`. Caller mutation of
frozen parameters/options/profile rules is rejected by the certification hash.
There are no background threads or shared mutable scratch caches.

Each `Hypothesis` owns **one joint explanation**, its canonical asset, and its
optional 17-record environment payload. Different hypotheses are alternatives:
their assets must not be concatenated or their sunlight summed. This avoids
misrepresenting a correlated sun/sky/profile alternative as independent lights.
Canonical dataset descriptors contain no pointers; keep the `Hypothesis`/payload
alive or persist it in an external dataset store before publishing its asset
through another manager. An unresolved locator means unavailable data, not zero.

The existing general asset schema remains version 1; this producer and environment
payload are explicitly `solar-v1` / encoding version 1. Certification hashes and
source IDs include map content, static geometry identity, profiles, source
parameters, sampling/search options, unit policy and algorithm version. Future
certification-rule changes must increment the producer version. No underspecified
disk-cache key or automatic runtime cache integration is introduced.

## Direction, colour and physical scale

All query positions use original JA world coordinates, right-handed, +Z up.
`toSource` is unit **receiver → sun**, including grid/deluxe hint vectors.
The canonical directional emitter's `Frame.forward` is its negative: photon
travel. Frame axes are constructed orthonormally; no camera or atlas transform
participates. Directional sources have no finite source position or range.

Supported observation comparison is original RGB8 under the selected gamma-1,
compensate-1, exposure-0, non-sRGB compiler output branch. Values before encoding
are **compiler-relative RGB**, not current runtime PBR illumination. Chromaticity
uses the canonical nonnegative Rec.709/D65, luminance-Y=1 convention, while scalar
intensity is Y of the relative RGB amplitude. Absolute photometry is
`NotComparable`; no q3map sun parameter becomes measured lux. Geometry units and
`physicalToScene` are retained as metadata and never used to infer historical
brightness. Current texture/PBR albedo is never divided into lightmap RGB.

`FromObservation` preserves the raw bytes even when source transfer/primaries are
unknown, but does not attest their encoding. By default those samples are excluded.
Unknown HDR/deluxe encodings or non-lightmap data return Unavailable. An explicit
assumed-observation-encoding option is possible for future conditional experiments;
its provenance is marked unreliable, never verified source-linear physical data.

Known q3map2 recipes have source-locked sun/sky rules. An unidentified stock map
must supply explicit candidate profiles and applicability/encoding assumptions.
`UnknownProfile` itself has no numerical falloff/sky rule and produces no estimate.
Multiple admissible profiles retain distinct predictions; a preferred angle never
identifies a production compiler. Gamma/filter/reduction variants outside the
implemented combination return unsupported instead of receiving modern defaults.

## Observation model and numerical method

For receiver i and tested incoming direction d, the supported pre-encoding model is

```
L_i = C_i + Q_sun max(0,n_i·d) V_i(d) + Q_sky K_i
K_i = (1/17) sum_j max(0,n_i·s_j) V_i(s_j)
p_i = Encode_profile(L_i)
```

`C_i` is an explicitly provided fixed local-light/transport contribution plus
declared recipe ambient. It is never inferred by subtracting encoded RGB.
`V_i` comes from production static geometry queries. `s_j` are the verified
profile's two eight-direction elevation rings (15° and 45°, staggered azimuths)
plus zenith. Their weights are compiler quadrature weights, **not steradians**.
The payload encoding describes a discrete relative angular measure; it is not
a declaration of a calibrated continuous radiance function or an SH texture.
Physical calibration/resampling for another representation needs an explicit adapter.

Every basis response is evaluated through `Bake::CpuForwardEvaluator` with the
actual source-specific directional/sky kernels. Only the verified linear
pre-encoding amplitude block is solved as a small constrained problem; the final
sum is always passed through production `Bake::Encode`. Shared-peak RGB packing,
quantization and clipping are not treated as independently additive byte values.

For byte b<255, use the compiler truncation bin `[b,b+1]`; b=255 is the encoded
endpoint reached by shared-peak normalization. Saturated measurements are not
exact 255-unit irradiances. Any sample with a saturated channel is excluded from
the linear amplitude solve, while its coupled encoded interval remains in final
model assessment. Fully saturated data therefore cannot certify source intensity
or a sun. Black has its ordinary quantization bin. Raw bytes are never changed
to smooth shadows or invert lost clipping.

Search and fitting are deterministic:

1. Admit at least 12 supported training receivers in at least three spatial blocks,
   with at least four positive excess observations across two blocks and a black
   baseline RMSE ≥4 code units. Unknown geometry/encoding is counted separately.
2. Test authored/grid/deluxe/shadow seeds, six world axes and 128 Fibonacci-sphere
   directions. A valid authored style-0 directive is a privileged seed/tie-break,
   not a guaranteed active bake light or a reason to bypass RGB evidence.
3. For each direction, fit bounded nonnegative independent RGB sun and sky
   amplitudes (default `[0,2000]`) with six Huber IRLS updates and an explicit
   two-variable bounded least-squares solve per channel. Huber transition is
   four codes. It handles outliers, not unknown optical transmission or censored
   amplitude identifiability.
4. Refine the six best seeds with eight tangent-ring directions per level, at
   10°, 5°, 2.5°, 1.25°. Also fit sky-only and uniform-fill-only controls. There
   is no scalar ray visibility derivative or arbitrary shadow-width correction.
5. Freeze proposals before reading validation RGB. Retain distinct directions
   within 0.5 training code-RMSE of the best, with an explicit alternative cap.
6. Validation requires at least eight receivers/two blocks, positive excess across
   two blocks, ≥75% comparable requested coverage, normal Gram eigenvalue ratio
   ≥0.015 for a sun, RGB interval RMSE ≤1.5 and maximum excess ≤3. A directional
   model must improve on both fixed training-fitted sky-only and fill-only controls
   by at least two codes. No validation amplitude refit is allowed.
7. Preserve competing validated explanations. If removing fitted sky fails to
   worsen independent error by at least one code, that offset is not published
   as recovered skylight. It receives unresolved metadata without a fabricated
   numeric transport field. Final held-out data is evaluated only with `Assess`.

Weights give each occupied spatial block equal total influence and use actual
receiver area within that block. Coverage, positive support and normal diversity
remain separate from fit error. The normal-rank gate is a conservative admission
rule for this first implementation, not a theorem that shadows on planar receivers
can never constrain direction. Such cases remain unsupported here.

The default positive thresholds are quantization/search-scale gates for these
controlled fixtures, not calibrated existence probabilities. A black predictor
and an always-Unavailable predictor fail positive acceptance. The report distinguishes
supported-sample accuracy from requested coverage; unavailable samples never become black.

## Sky visibility and shadow constraints

Sun/sky rays must terminate with explicit `SkyReach::Reached`. A miss into arbitrary
empty space is Unknown. The wrapper additionally requires geometric reliability
and rejects unknown optics, incomplete/unsupported blockers and policy assumptions.
Opaque blockage remains blocked. Alpha/translucent/patch uncertainty is not made
transparent or opaque to improve fit. A solid BSP origin remains unknown, with its
exclusion counted. Region/PVS membership is neither required nor used as visibility.

Known receiver placement uses the profile's explicit normal push; original sample
positions are unchanged. Surface footprints are used to guard partition separation.
This evaluates the declared compiler single-sample recipe, not a physical integral
over every point of a luxel. General traces remain **conditional compiler behaviour**;
the exact controlled `multi_sun` replay scope is not generalized to these fixtures,
patches, alpha masks or Raven. Soft angular sun extent and bake-filter width are
not identified by the current operator: angular extent remains unknown.

The angular search reevaluates visibility as direction moves. Cast-shadow patterns
therefore constrain proposals; no frozen binary-visibility gradient is advertised.
The controlled wall fixture includes positive and blocked receivers. It has three
unknown held-out placements, explicitly excluded; no universal two-unit band is
used to claim shadow parity.

## Canonical output and uncertainty

An admitted sun becomes a directional `PhysicalEmitter` plus a `SunSource`
reference, with `HypothesisState::Candidate`, relative photometry, provenance and
unknown angular extent. `physicalEmitterConfidence` and hypothesis probability
remain unknown; `exactLight` is Unsuitable. No value is promoted to physical lux
or resolved exact sunlight. These are verified conditional fits on controlled
observations, not certified original source recovery in an arbitrary map.

Independently supported sky becomes `EnvironmentLighting` and a bounded external
17-record relative angular payload. `SunCoverage::ExcludesExplicitSun` explicitly
prevents a future fog/IBL consumer from counting direct sunlight twice. Sky-only
assets have no physical emitter. A small fitted but unidentifiable sky-shaped
offset is represented by `UnattributedIncidentRadiance` metadata with dataset 0:
its field is unavailable, not zero or verified GI. Fitted nuisance RGB remains
in diagnostics so no prediction component silently disappears from the report.

Known local contributions, known ambient, reflected transport and unmodelled
discrepancy are not relabelled as sun. This estimator does not solve GI or infer
a physical cause for every residual. Poor models are rejected. A distant local
lamp can still be observationally equivalent to a sun; positive sky/normal/shadow
tests provide conditional evidence, not unique astronomical attribution.

Direction uncertainty reports the spread of retained tested alternatives plus
the angular search resolution. It is **not** a statistical confidence cone or a
bound on all unsearched directions. Output-cap diagnostics remain visible.
Intensity intervals are local conditional fixed-colour/fixed-other-component
feasible slices with declared validation tolerances, not joint 95% intervals.
Profile, calibration, geometry, angular-size and model-discrepancy uncertainty
are not combined by arbitrary confidence multiplication.

## Compiled fixtures and measured results

Eight self-authored JA maps were actually compiled with pinned NetRadiant
`2.5.17n-git-d9338309e`, binary SHA256
`894049bd136e7828b0f068d12c932833153b3ada87b99655dac90e4a60c85ad4`.
The recipes pin shaders/textures, compiler commands, gamma 1, compensate 1,
exposure 0, bounce 0, `-super 1`, `-nocollapse`, JA and one compiler thread.
Source-map/BSP/recipe/partition hashes are in JSON; generated maps/BSPs remain
under ignored `build/`. No proprietary map or screenshot is needed.

Geometry-only stratification samples original luxel centres across receiver normals
and 128-unit blocks, usually 72 per role. Split seed 20261030, origin `(64,64,64)`,
gap 1 plus a three-footprint filter guard are fixed across all fixtures. The harness
checks every pair of folds for same-block/near-footprint leakage. True sun direction
and RGB are withheld from the optimizer; `--authored-prior` is an explicit optional
different protocol. The local-lamp fixture provides its fixed nuisance contribution
from production C++ as an allowed test prior; that lamp is not reconstructed.

| Fixture | State | Angular error | Validation RGB RMSE | Final held-out RMSE | Held-out black RMSE | Coverage |
|---|---|---:|---:|---:|---:|---|
| sun_oriented | SunHypotheses | 0.4045° | 0.00923 | 0.10610 | 59.80 | 72/72 |
| sun_shadow | SunHypotheses, partial | 0.4045° | 0 | 0.09346 | 35.17 | 69/72 |
| sky_aperture_positive_v2 | SkyOnly | Not applicable | 0.01632 | 0.01890 | 22.54 | 72/72 |
| sun_with_lamp | SunHypotheses | 0.4045° | 0.09335 | 0.15966 | 39.64 | 69/69 supported subset |
| sky_aperture v1 | UnknownSun | Unavailable | Insufficient positive blocks | Not admitted | Not a passed positive | validation 5 positives in one block |
| fixed known ambient | Unavailable, no excess sun evidence | Unavailable | No inferred source | Not admitted | Bright but already explained by recipe ambient | no publication |
| dark indoor | Unavailable | Unavailable | No positive observations | Not admitted | 0 | no publication |
| two coloured directional sources | UnknownSun | Unsupported joint-source explanation | One-sun model rejected | Not admitted | positive | no arbitrary average sun |

RGB RMSE is distance to encoded byte intervals, not radiometric error. Three
sun-direction alternatives survive for each compiled sun positive, with sampled
angular ambiguity floor 1.25°. Sun RGB relative vector errors are 0.705%, 0.662%
and 0.555%; positive sky RGB relative error is 0.472%. Those are compiler-relative
truth errors, not calibrated lux accuracy. Small fitted sky-shaped nuisance terms
in the true sun-only cases fail independent component support and are not canonical
environment lights.

The shadow case's complete-coverage gate **remains failed**; supported RGB/angle
results are explicitly partial. The lamp fixture separately excludes 2 training
and 3 held-out nominal receiver placements unsupported by the local point-light
operator, leaving 70/72/69 samples. Its original requested counts and exclusion
records are preserved, not scored as zero-error full coverage.

Skylight v1 was initially admitted by an earlier prototype without a positive-block
check. The final guard correctly rejects it. Its original 384-unit opening and
120-unit skylight are unchanged. V2 is a separately named/hashed fixture with
a 544-unit opening and 240-unit sky, designed to provide broad positive exposure;
all 72 validation/held-out observations are positive across 14/13 blocks. No target
RGB, partition or tolerance was adjusted to pass v1. An initial controller also
requested the grid domain for the lamp nuisance by mistake; this was corrected
to surface pre-encoding, and that invalid experiment is not physical evidence.

## Independent CPU cases and failure controls

The dependency-free C++ test performs 14,899 checks, including actual immutable
BVH sky/opaque queries. Its analytical target generation uses independently stated
cosine/sky quadrature and original quantization; these checks complement real
compiler observations rather than replacing them.

| CPU fixture | Result |
|---|---|
| coloured sun + sky, multiple normals | angle 0.2224°, zero validation interval RMSE; relative sun/sky RGB errors 1.36%/7.51% |
| positive interior skylight without sun | SkyOnly, RMSE 0.00184; sky relative RGB error 0.271% |
| sun/opening + known powerful local lamp | angle 0.1817°, RMSE 0.0013 |
| matching/conflicting authored prior | both data-supported fits; conflicting prior does not override observations |
| uniform unexplained fill | UnresolvedEnvironment, no physical sun |
| single receiver normal | no admitted sun |
| strong localized reflected-colour model | UnknownSun, no reflection promoted to a physical emitter |
| two distinct coloured suns | UnknownSun under the unsupported one-sun model |
| full saturation | UnknownSun, no exact intensity |
| dark sealed interior / sparse data | Unavailable |
| no sky hit / unknown alpha mask | Unavailable, no fake sky exposure |
| two explicit assumed profiles | both retained; no historical compiler identification |

False sun publications are zero in the two compiled **true-no-sun** controls
(ambient-only and dark indoor). The multiple-sun rejection is an unsupported-model
control, not a true-no-sun detection score. There is no stock-map false-positive
rate or general sun-recall claim. Conditional direction/intensity sets are not
calibrated probabilities.

Tests also cover cancellation, work exhaustion, missing geometry, invalid values,
unknown profile/encoding, canonical raw-byte preservation, missing grid/deluxe hints,
source/map/geometry mutation and deliberate training/validation reuse. Python
controller tests reject black/missing predictions, empty positive skylight, false
suns, poor angular error and hidden incomplete coverage. The trusted C++ adapter
opens validation RGB only after freezing; held-out RGB is read after validation
selection. This is a staged trusted API boundary, not a hostile-native-code OS sandbox.

## Cost, determinism and validation commands

The default search evaluates 326 directions and 327 forward batches per profile.
The compiled positives use about 13.8–16.1 thousand training sky/shadow rays,
24.7 thousand expanded source/receiver pairs and 23.7 thousand output encodes.
Validation and final held-out work are reported separately. Failed forward
operations explicitly mark incomplete partial counters instead of claiming exact
work measurements after an aborted provider call.

On this workstation/MSVC Release, fit plus validation takes about 0.24–0.30 s
on the supported positive controls; final held-out scoring and geometry preparation
are additional. Eight-map frozen-input sweeps take 9.44 and 9.88 s under shared
workstation load. Peak C++ process working sets are about 9.5–10.1 MiB for search,
with smaller abstaining cases around 4.3 MiB; controller peak is recorded separately.
These are actual Windows `GetProcessMemoryInfo` measurements, not STL estimates
or a claim about full-stock-map performance.

Limits cover receiver/profile/candidate counts, memory estimates, total expanded
pairs/rays/traversal/encoding work and cancellation. Defaults allow 256 receivers,
four profiles, 24 alternatives, 64 MiB accounted estimator memory, 1M rays, 2M pairs,
64M traversal units and 1M encodes. Geometry acceleration is reused immutably;
there is no obligatory full-map dense contribution matrix or global ray-pair cache.

Two complete frozen-input sweeps have identical parameters, source IDs, profiles,
coverage/exclusions, decisions, numerical errors and work counters. Time/RSS is
kept in a separate file. Fresh compiler whole-BSP byte identity is not assumed;
the original frozen BSPs are reused and checked for deterministic inference.

Exact executed workflow (from repository root; `python` was the bundled Python 3
runtime, `-X utf8`; CMake/GCC were from `../OpenJK/build/toolchain/w64devkit/bin`):

```powershell
cmake --build build/lightrecon-core-renderers --config Release --target lightrecon_bake_forward_tool lightrecon_sun_test rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 6
cmake --build build/lightrecon-disk-standalone --parallel 6
ctest --test-dir build/lightrecon-disk-standalone --output-on-failure
python -X utf8 tools/rend2/sun_fit/run.py --output build/s02-compiled-verified --tool build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe
python -X utf8 tools/rend2/sun_fit/run.py --output build/s02-final-a --reuse build/s02-compiled-verified --tool build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe
python -X utf8 tools/rend2/sun_fit/run.py --output build/s02-final-b --reuse build/s02-compiled-verified --tool build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe
build/lightrecon-core-renderers/Release/lightrecon_sun_test.exe build/s02-cpu-final-snapshot.json
python -X utf8 -m unittest discover -s tools/rend2/sun_fit -p 'test_*.py' -v
python -X utf8 tools/rend2/sun_fit/report.py --run build/s02-final-a --repeat build/s02-final-b --cpu build/s02-cpu-final-snapshot.json --output tools/rend2/sun_fit/results
$env:JK_X07_FIXTURE_ROOT = 'build/x07-published-a'
python -X utf8 -m unittest discover -s tools/rend2/fixed_geometry -p 'test_*.py' -v
python -X utf8 tools/rend2/bake_model/validate_cpp.py --exe build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe --bsp-dir build/bake-model-comparison-final/authoring/base/maps --output build/s02-production-regression --x05-manifest tools/rend2/evaluation/x05/results/manifest.json --x05-compiler-dir build/x05/compiler-final --cross-map-dir build/x06-controlled-final
python -X utf8 tools/rend2/lightrecon_regression.py --output build/s02-shader-regression
```

SP/MP Release builds pass; standalone CPU tests pass 19/19; S02 Python gates
9/9; X07 blind-fit tests 22/22. Original forward-kernel regression retains all
22 statuses: 20 supported cases within frozen tolerances and two explicit
`UnsupportedBounceReplay` cases. X05 remains passed, and X06 retains its existing
25 unsupported `thin_door` receiver placements. Scoped `multi_sun` remains exact.
Disabled-path comparison compiles/links 27 programs/51 stages with zero differences
on the matching Intel UHD GL3.2 baseline. Existing CMake Git-tag/CPack notices and
MSVC `/arch:SSE2` warnings are unrelated. No gameplay visual test is claimed.

## Remaining limits

No stock historical profile or absolute sunlight scale has been identified.
Grid/deluxe directions only initialize candidates until their sun-specific
observation operators are independently verified. HDR, arbitrary transfer,
dynamic styles, curved/uncertain receivers, soft angular sun size, multiple
simultaneous directional-source fitting and physically constrained GI remain
unsupported numerical combinations. Partial geometry cannot be certified by
outdoor labels or region membership. The current search/uncertainty set is finite,
and an unresolved bright room still does not imply a local physical lamp.

Before runtime use: independently calibrate units, validate broader receiver/sky
recipes and boundary optics, add supported extent/multi-source operators where
data justify them, and implement a separate opt-in consumer adapter. S02 itself
only returns explicit CPU hypotheses and never installs them into rendering.
