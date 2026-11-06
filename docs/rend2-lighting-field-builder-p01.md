# P01 — prepared CPU spatial lighting fields

X18 follow-up: [independent stock appearance-field evaluation](rend2-legacy-appearance-x18.md)
reuses the conditional observation route. Its L0 encoded RGB proxies remain
separate from P01 physical irradiance/radiance; derived field codec version is now 3.

## Result and scope

P01 implements the second architecture layer: offline canonical-source evaluation,
directional projection, sparse spatial field datasets, explicit preparation, then
immutable CPU queries. It reuses P02 topology/visibility certificates, P03 angular
math, S10 passive transport and P06 membership/composition. Source records remain
intact. There are no GL objects, renderer budgets, renderer cvars, new game
consumers or map-load reconstruction calls.

This is **not** a recovered stock JA physical field. Stock has no accepted original
physical source solution or absolute calibration. `UnknownProfile` and historical
q3map Q cannot be passed to `physical-emission/1` as candela. P01 does not modify the
historical bake evaluator or rerun stock fitting to manufacture usable sources.
P02's frozen hoth3/vjun2/mp/duel4 geometry-only audit remains the stock spatial
coverage evidence; its historical lightgrid observations were never relocated.

The important limitation is measured, not hidden: a deliberately coarse point-light
field has substantial independent spatial error and rejects many back-facing SH
queries. Exact direct lights should normally remain explicit. Direct projection
is disabled by default, and field numerical convergence does not certify source
existence or accurate interpolation everywhere.

## Implementation and integration

- `shared/rd-rend2/lightrecon/field_builder.h/.cpp`: bounded offline builder.
- `field_prepared.cpp`: prepared representation-neutral `CpuEvaluator` backend.
- `field_codec.cpp`: sparse chunk encoding and explicit loading.
- `field_internal.h`: source-scene identity and metadata consistency.
- `emitter_shape.*`: exposes the existing validated emission quadrature nodes.
- `tr_lightrecon_api.*`: separate opt-in conditional query; strict physical guards.
- `tools/lightrecon_test/lightrecon_field_builder_test.cpp`: native end-to-end tests.
- `tools/rend2/field_builder/run.py`: freezes sources/tool/settings before running
  native tests twice; Python performs no lighting predictions.

Call `Fields::Build(request, read, artifact)`, optionally persist canonical
`Disk::Encode`, explicitly `Fields::Decode`, then `Fields::Prepare` and publish
the prepared evaluator with the asset through `Manager::Publish`. Dataset presence
alone never advertises numerical sampling. `Prepared::AcceptsAsset` binds the
backend to the complete immutable metadata digest. Held snapshots retain their
lease/generation after manager reset; stale new handles fail.

One prepared backend currently serves one selected field. Several fields may be
stored in the asset or prepared independently. A later multi-backend dispatcher
must retain explicit field/configuration selection and P06 composition checks;
P01 does not silently combine alternative fields.

## Quantities, directions and integration

World-space angular direction **points from sample toward incoming light**, opposite
photon travel. RGB is linear Rec.709/D65 when making a physical claim. Distances
for physical point intensity convert world units to metres using the asset scale.

For a point/spot source, the incident directional measure at a probe is

\[
 q = I_{RGB}\, f(\omega)\,V/r_m^2.
\]

For a finite Lambertian source quadrature node,

\[
 q = L_{RGB}\,\max(0,n_s\cdot(-\omega))\,dA/r^2\,V.
\]

Area and squared distance use the same world units; the ratio is invariant.
Two-sided emission is explicit. Smooth-cone spots use the canonical constrained
inner/outer smoothstep. Open tubes use the established finite cylindrical side
quadrature, without invented emitting end caps. Source support contracts preserve
original emitting triangle identity and mounting side. Unattested endpoint contact,
probe inside a tube, or probe on an emitting plane is unsupported, not black.

Parallel sun uses perpendicular illuminance and direction `-frame.forward`.
Both sun and environment require an actual `SkyReach::Reached` ray; a no-hit ray
is insufficient. Finite solar extent is currently rejected. Sky requires a decoded,
content-hashed incoming-radiance dataset; mean environment RGB is not an isotropic
fallback. Environment membership must explicitly exclude the separate sun.

For the passive first bounce, reuse the actual fitted S10 transfer/result contract:

\[
 L_{patch,root} = \rho_{patch}\,E_{0,root}/\pi.
\]

Evaluate that Lambertian radiance on original disjoint patch triangles, with
visibility to the new probe. Root units, material knowledge, transfer/geometry keys,
energy-model contract and source availability are checked. No scalar receiver E1
is inverted into a fabricated angular distribution. Unknown material blocks the
lift; bounded material/incomplete transport retains uncertainty. No later bounce,
free patch emission or full inverse path tracer is added.

The controller binds passive E0 preparation to `Fields::SourceIdentity(asset)` in
`Passive::sourceScene`; a changed source/configuration invalidates that input.
The fitted result and frozen-root content keys are retained in field provenance.
This attestation binds an upstream validated result; it is not a new independent
proof of its supplied patch illumination or historical material reflectance.

Using P03 orthonormal real SH and basis indexing, project measures as
`c_lm = sum(q * Y_lm)`. Diffuse-normal irradiance multiplies each band by the
validated clamped-cosine convolution factor. `IrradianceOverPi` explicitly divides
by pi and has a distinct membership domain. Incoming radiance coefficients are
never copied into irradiance coefficients. SH L2 is the initial diffuse codec;
L4 is opt-in. SG fitting remains in P03, not a claimed P01 spatial backend.

## Supported operations

| Input/component | Normal-dependent E | Incoming L | Conditions |
|---|---|---|---|
| Authored point / smooth-cone spot | SH2/SH4 reference projection | Unavailable | Explicit direct-projection opt-in; ideal point is a delta direction |
| Authored rectangle / open finite tube | SH2/SH4 | Implemented with angular error gate | Finite geometry, visibility and independent parametric directional reference; sharp radiance often fails low-order budget |
| Parallel sun | SH2/SH4 | Unavailable | Actual sky reach, explicit sun record; finite extent unsupported |
| Decoded sky/environment | SH2/SH4 | SH2/SH4 | Explicit sky support, payload hash, no unknown sun duplication |
| Source-linked first bounce | SH2/SH4 | Unavailable | Fitted passive result, root E0, known/bounded rho, original triangles |
| Unknown scalar indirect illumination | Unavailable | Unavailable | Insufficient information for this source-conditioned route |
| Conditional source-linear physical-model configuration | Typed relative E | Same finite-source restrictions | Explicit configuration/gauge; never physical Snapshot capabilities |
| Unknown-profile compiler Q / unknown RGB primaries | Unavailable | Unavailable | Not converted into physical emission |
| Observation-driven approximate appearance | Scalar typed approximate value | Unsupported | Independent validation mandatory; unknown root/path; nonphysical |

Implemented finite-area radiance is not a claim that SH can represent sharp panels
well. Its parametric reference/error gate can reject all probes. Physical incoming
radiance is independently demonstrated with the constant-environment case.

## Source selection, membership and calibration

Selections name source IDs; empty does not mean all. Resolved nonexclusive sources
are admitted by default. Conditional/exclusive sources require explicit opt-in,
configuration, matching geometry/frame/shape/angular state and amplitude, plus the
declared `source-linear-physical-model/1` amplitude convention. Rejected sources
remain rejected. A good field fit cannot promote them to Resolved/exact-light.

Nonzero positional sigma, nontrivial intensity bounds, observation issues or a
canonical source-uncertainty record conservatively retain parameter uncertainty.
Those fields have no strict physical capability or permitted additive composition;
they can still expose a typed conditional value with unknown upper bound. Passive
roots currently must be resolved/nonexclusive; alternative passive roots require
a separately admitted upstream configuration, not arbitrary E0 relabelling.

Root IDs use canonical asset IDs. Membership retains source classes, direct versus
first-bounce orders, configuration/alternative group, geometry/source generation,
colour domain, uncertainty, addition and replacement rules. The source-scene hash
is independent of subsequently appended field records, allowing separately prepared
direct and bounce fields from the same scene to be compared. Incompatible root/path
sets require separate fields rather than an ambiguous Cartesian product.

P06 `CanAdd` accepts a supported direct order-0 field plus its separate order-1
field, rejects duplicate direct energy, and rejects unknown-root appearance.
Representations of the same root/path set are substitutes. Replacement is forbidden
for all P01 fields: P01 does not establish source-resolved baked subtraction.

PhysicallyAuthored mode requires physical calibration and known linear Rec.709.
SceneEquivalent and SourceLinearRelative preserve their calibration identity and
explicit domain; neither activates physical sampling. Historical encoded appearance
keeps unknown primaries/transfer where applicable. No gamma inversion or claim of
stock absolute lux is made.

## Exact query contracts

`Snapshot::SampleIndirect(IrradianceQuery)` returns Lux only for a prepared,
physically authored, pure indirect irradiance field with admissible uncertainty.
`SampleIncomingRadiance(RadianceQuery)` returns Nits only for a prepared physical
incoming-radiance field. These calls cannot accept relative or appearance data.
Missing calibration is Corrupt; incompatible unit/calibration is Unsupported.
General contribution compatibility is retained; `ValidLighting` is not relaxed.

`SampleConditionalField(ConditionalFieldQuery)` requires `optIn=true`, an explicit
field handle and the matching configuration. Missing selection of an exclusive
configuration is Ambiguous; a different configuration is NotFound. The result
contains a shared immutable descriptor, RGB value, declared quantity/domain/unit,
colour/transfer knowledge, calibration/profile/configuration, full membership,
spatial coverage, geometry uncertainty, representation discrepancy, support IDs
and weights, field ID and publication generation. Lower bound zero is physical
nonnegativity; upper bound is unknown. The recorded numerical error is a tested
discrepancy, **not a calibrated confidence interval or global error guarantee**.

Examples:

- Authored red-bounce field: physical E in Lux through `SampleIndirect`.
- Known source-linear relative configuration: Relative through the opt-in query;
  physical queries unavailable.
- Approximate original encoded appearance: `LegacyAppearance`, explicitly unknown
  primaries, unknown source roots/reflection orders, no additive physical contract.
- No admissible probes, missing sparse values, unknown sky or blocked interpolation
  support: Unavailable, never successful black.
- A computed zero behind a known wall is a valid explicit zero.
- Negative SH reconstruction: Unsupported; no silent clamp/energy bias correction.

## Spatial support and quality

P02 geometry certificates are reused for query-to-probe support. Region IDs alone
neither prove visibility nor create an opaque wall. A certified local link can work
with an incomplete region graph. Known wall crossings are rejected. Conditional
static geometry requires explicit query permission and never activates strict
physical capabilities. Original baked samples stay immutable; field coefficients
are evaluated at the **final** generated-probe positions.

Queries perform bounded topology lookup/interpolation/basis evaluation, with fixed
support arrays and immutable preloaded values. They trace no new rays, read no disk,
run no fitting, allocate no GPU objects, populate no lazy cache and allocate no
per-query dynamic coefficient arrays. Interpolation never normalizes zero admissible
support into brightness. Missing values at any required support probe return
Unavailable; spatial support is distinct from lighting-data availability.

Probe-level admission compares projection against a higher-resolution source/sky
integral and independent receiver directions. Reports include relative RGB RMSE,
maximum absolute error, luminance RMSE and signed integrated energy bias. Finite
sources use refined quadrature; area-edge reference is also tested against an
independent 64x64 integral. These measured errors are scoped, not formal quadrature
bounds. Geometry volume and lighting-supported volume are separate. Independent
position/normal error is reported separately from probe angular admission.

## Persistence and resources

Canonical asset schema is **8**, invalidating older schema-7 caches explicitly.
No historical observation format changes. Added appearance quantity/component enum
values are appended; unknown old readers must not reinterpret them. The source-only
generation includes photometry/calibration/configuration and geometry parameters;
changing those invalidates prepared fields. P02 map/geometry/topology keys remain
separate. Settings and source support identities are recorded in provenance.

Geometry and topology reuse P02 chunk codecs. `org.openjk.spatial-field/1` adds a
descriptor record and sparse ordered value records, at most 64 values per chunk.
Each value stores stable probe ID/final position, P03 Float32 angular payload and
measured quality data. Checksums, dataset hashes, versions, sorted IDs, count,
quantity, canonical membership and generation are validated before preparation.
Absent sparse entries mean unknown. No dense source-by-luxel matrix is required.

Default caps: 20,000 probes; 256 selected roots; 65,536 direct quadrature nodes;
200,000 passive nodes; 128 MiB value/build budget; 4M rays; 128M deterministic work
units; 300s wall safety. Work counts source integration, coefficient projection,
reference integration and geometry traversal. Cancellation and budget errors discard
the incomplete artifact. The value-memory estimate is not OS peak RSS: source asset,
shared topology, allocator overhead and temporary buffers are separately bounded by
input/codec caps and measured process RSS. Canonical metadata is size-checked with
the existing bounded record writer before copying (at most one quarter of the
requested budget, capped at 32 MiB serialized metadata). This is a conservative
copy guard, not an exact heap-size accounting of every standard-library object.
This first implementation prepares the
selected field in memory; it is not a scene-wide out-of-core build controller.

## Reproduction and measured witness

Run from the repository with GCC toolchain on PATH:

```powershell
$env:PATH='C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;'+$env:PATH
cmake --build build/s06-gcc -j 2
& C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe -X utf8 tools/rend2/field_builder/run.py --out tools/rend2/field_builder/results/v2
ctest --test-dir build/s06-gcc --output-on-failure -j 2
$env:CL='/MP2'
cmake --build build/s13-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 2
```

The runner refuses to overwrite an existing versioned result directory. It freezes
commit plus dirty-source hashes, compiler, native executable, settings and CPU/OS.
Two semantic witnesses must match; time/RSS are outside deterministic comparison.
No game, licensed assets, Python numerical dependencies or additional third-party
library is introduced. New code follows the repository GPL-2.0-or-later licence.

Frozen witness: [protocol](../tools/rend2/field_builder/results/v2/protocol.json),
[results](../tools/rend2/field_builder/results/v2/summary.json),
[CSV](../tools/rend2/field_builder/results/v2/quality.csv),
[manifest](../tools/rend2/field_builder/results/v2/manifest.json).
Both runs pass **12,082 assertions**, with identical numerical semantics and
identical complete serialized example bytes. The owned example is
`results/v2/first.jklr`, SHA256
`e85e63f8e45c4d6d386cbc2f90180cd68ee45086892566c45c47d2b4ea84f74a`.
It is a controlled physical point-light reference asset, not stock reconstruction.

v1 is preserved as the initial witness. v2 adds conservative source-uncertainty
propagation and metadata-copy budget regressions; these do not change the reference
light equations, angular tolerances or the poor coarse-grid controls.

| Controlled measurement | Result | Scope/denominator |
|---|---:|---|
| Point / spot E relative error | 1.520% / 1.520% | One independent analytic facing-receiver reference per model, SH4 |
| Rectangle / tube E relative error | 1.323% / 2.034% | Independent 64-division area reference at one receiver per model |
| Constant sky E absolute error | 8.742e-8 Lux | Expected pi Lux from L=1 Nit; 64/64 probes admitted |
| Constant sky L absolute error | 0 Nits | Expected 1 Nit; 64/64 probes admitted |
| Partially occluded area E relative error | 4.225% | Visible-energy fraction 40.610%; independent 64x64 ray integral |
| Red bounce R/G | 16.0 | Authored rho=(.8,.05,.05), equal root illumination |
| Coarse passive-bounce spatial RMSE | **39.628%** | 30/30 independent positions, upward normal |
| Finer passive-bounce spatial RMSE | **6.343%** | Same 30/30 positions and source/quadrature/error budgets; .5-WU spacing, 512/512 probes |
| Coarse point spatial/angular RMSE | **26.742%** | Conditional on 720/1800 supported position-normal queries |
| Coarse point unsupported queries | **1080/1800 (60%)** | SH ringing rejected; never silently counted as black or removed from coverage denominator |
| SH2 direct projection | 8/8 probes rejected | 8.58–8.89% angular RMSE exceeds unchanged 8% gate |
| SH4 point maximum probe angular RMSE | 4.472% | All independent reference normals at admitted probes |
| SH4 point maximum integrated-energy bias | 0.1705% | Probe angular integral, separate from spatial interpolation error |
| Known opaque-wall false brightness | 0 | 32 supported blocked receivers; separate appearance-wall leak also zero |
| Legitimate opening positive receivers | 12 | Actual direct-source visibility; not an emitter at the doorway |

The coarse direct example supports 64 world-unit-cubed of geometry and lighting
values, but that **does not** guarantee every receiving normal is representable.
Its missing queries are predominantly signed low-order back-face ringing. These
diagnostic direct-grid scores fail a claim of an accurate all-direction field;
they support the default explicit-direct/indirect-field policy. No threshold was
changed to conceal them.

The finer bounce comparison uses a uniform geometry-only refinement, without
changing sources/materials or sampling target values. It improves the independent
error by about 6.25x, at 32,768 source visibility rays versus 2,304 for the coarse
case. This is evidence for resolution/error controls, not a universal .5-WU map
spacing recommendation. The E0 fixture is an authored fixed patch-average root,
not a rebake or proof that a stock source produces that E0.

The entire native controlled suite takes 0.73–1.04s wall, 0.703s process CPU, and
11.80–12.21 MiB OS peak working set in this concurrent environment. The eight-probe
point build takes 2.03–2.09ms, 16 visibility rays, 4,856 integration work units,
7,560 estimated resident value/diagnostic bytes and a 15,968-byte complete canonical
example archive. That small fixture has no BVH primitive traversal. Sky E/L tests
use 26,624/45,056 rays. These are scoped CPU measurements, not large-map build costs.
Ten thousand repeated **off-probe** interpolated queries average 4.14–4.15us/query,
including test-loop status checks; no disk/rays/fitting occur during those queries.
Timing differences are deliberately excluded from semantic comparison.

Failure tests cover no roots, rejected/exclusive source, missing configuration,
wrong profile, wrong calibration, missing sky, unknown material, stale passive
roots, changed source generation, quantity mismatch, missing/corrupt/versioned
chunks, altered membership, missing sparse values, cancellation, work/memory
exhaustion, metadata-copy limits, source uncertainty and nonfinite settings. The same serialized archive is decoded through
the actual canonical Disk container and explicitly prepared before querying.

Repository-wide build/test verification is retained separately in `checks.json`
and its logs; the focused witness alone does not establish that every unrelated
test or historical cache lifecycle is passing.

The first broad CTest run passes 40/42: new conditional capability registration
invalidates two old `Supports` call-count assertions, and schema 8 invalidates the
old schema-7 empty-format golden hash. These expectations were updated for the
explicit contract changes; schema 6/7 rejection checks were added. Both API and disk
tests then pass. The next broad run passes **41/42** (250.27s): the existing S13
producer repeated-store test fails iteration 26 with Win32 **1175**, `atomic cache
write/flush/replace failed`. The producer test had passed in the first broad run.
This intermittent cache failure is already documented in X15/P02; no cache writer,
retry policy or assertion was changed by P01. The remaining point-refinement test
and every numerical/geometry test pass. Final P01 uncertainty/budget guards are
covered by the freshly rebuilt **10/10** focused CPU regression and native witness.
SP/MP Release builds pass on those final sources. The usual x64 `/arch:SSE2`
warning remains unrelated. Full all-green repository CPU status is **not claimed**.

One declared follow-up producer run with a fresh isolated cache directory passes
**549/549** checks in 62.91s, without changes to cache logic or assertions. It does
not erase the repeated-store Win32 1175 failure or prove that its cause is fixed.
The earlier full runs and this isolated result are all preserved in the check
witnesses. No further cache retries were used to obtain a claimed green suite.

The initially attempted combined Python unittest invocation omitted the historical
research modules' import paths and failed four imports. Corrected explicit
`PYTHONPATH` passes **38/38** P02/P03/DDGI/RBDOOM tests. Both command/log witnesses
are retained; this was a harness invocation issue, not a numerical change.

```powershell
$env:PYTHONPATH=(Get-Location).Path+';'+(Join-Path (Get-Location) 'tools/rend2/field_representation')+';'+(Join-Path (Get-Location) 'tools/rend2/ddgi_research')+';'+(Join-Path (Get-Location) 'tools/rend2/rbdoom_research')
& C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe -X utf8 -m unittest tools.rend2.probes.test_reports tools.rend2.field_representation.test_benchmark tools.rend2.field_representation.test_octahedral tools.rend2.ddgi_research.test_probe_weights tools.rend2.rbdoom_research.test_field_basis
ctest --test-dir build/s06-gcc --output-on-failure -R '^lightrecon_(api|disk|field_builder|field_basis|contributions|probes|lifecycle|receiver_support|bake_forward|photometry)$' -j 2
build/s06-gcc/lightrecon_producer_test.exe C:/Users/Keyten/Desktop/projects/OpenJK-rend2/build/p01-isolated-producer-cache
```

Build-measure wrapper RSS refers to the CMake parent process, not aggregate
compiler subprocess memory. Native benchmark RSS is measured on the actual test
process. No game was launched.

## Remaining scientific limits

No historical forward recipe is inferred here. No accepted original-stock source
is created. No full directional GI inversion, later bounce, SG spatial preparation,
finite solar disk, general source-assembly radiance, or certified universal error
bound is claimed. Unknown optics/dynamic states stay conditional. The appearance
route is an explicit scalar approximation with independent error admission, not a
general angular reconstruction of original lightmaps. Consumer GPU resources and
visual lighting improvements are outside P01.
