# P06: conditional source attribution and conservative replacement metadata

This stage extends S13's existing contribution data. It does not subtract baked
lighting, enable a rendering consumer or turn a fitted stock candidate into a
physically resolved source. The producer remains CPU-only and ends at the
canonical asset plus versioned chunks.

## Reused data and genuinely new capabilities

S13 already emits stable sample/patch identities, canonical source IDs,
configuration/profile provenance, pre-encoding source-root values, first-bounce
records and signed observation-domain discrepancy. P06 reuses the same
`ContributionReference`, `TransportMembership`, `SampleRecord`,
`ContributionRecord`, `ObservationRecord`, chunk/hash chain and disk container.
It does not add another physical-light model or a separate calibration system.

The audit found two concrete gaps: `Producer::Publish` did not install any
`CpuEvaluator`, and the stored source domain was the transport-patch domain.
The existing `Snapshot::SampleContribution` consequently had no production
prepared implementation. P06 adds:

- `Attribution::Prepare`: validation/decoding and immutable source/sample indexes
  before publication. Const queries perform no I/O, fitting, tracing, deferred
  decoding, cache filling or GL work.
- A prepared evaluator installed by explicit offline `Producer::Publish` and
  its offline cache/build backend. Normal renderer loading does not call this
  producer or gain an active light consumer.
- Separate original-observation sample-domain records and per-source surface
  observation replay, in addition to retained transport-patch contributions.
  Transport area is never copied from statistical sample weights.
- An attribution ledger, algebraic root/path composition checks and conservative
  per-receiver replacement admission. No scalar confidence threshold grants use.

## Mathematical domains and ledger

For a specified configuration `h`, pre-encoding compatible incident components
are

\[
T_h=D_{local,h}+D_{sun,h}+S_{sky,h}+B_h+A_h+U_h.
\]

`B` is passive source-linked reflected transport with declared order; `A` is an
identified legacy ambient boundary; `U` is explicitly estimated unresolved
illumination, **not** a positive residual invented to close the equation.
The total observation prediction is

\[
\hat y_h=O_h(T_h;\rho,\mathrm{filter},\mathrm{style},\mathrm{encoding}).
\]

`O` is the existing observation-specific operator. Compiler shared-peak RGB
normalization, transfer, clipping and quantization make encoded components
non-additive: in general `O(D+B) != O(D)+O(B)`. Per-source stored direct values
are evaluated before this encoding; no consumer may blindly subtract their
encoded display colours from RGB8 lightmaps.

The observed target is an interval `[l,u]`, including the selected encoding's
censoring rule. The ledger reports the signed nearest-interval error

\[
e_c=\hat y_c-\operatorname{clamp}(\hat y_c,l_c,u_c).
\]

S13's stored `signedIntervalDiscrepancy` has the opposite sign (target minus
prediction); the prepared ledger labels the sign explicitly. It preserves
observed lower/upper endpoints, infinite-upper masks, total predicted observation,
direct/sun/sky/ambient/indirect/unresolved pre-encoding components, units,
calibrations, issues and attribution completeness. Direction/grid diagnostics
are separate observation operators. Unsupported per-source packed-grid reduction
does not become a surface irradiance decomposition.

Rec.709 luminance uses the project's canonical coefficients only when the total
observation domain is explicitly linear Rec.709 irradiance/radiance. Encoded
stock RGB and compiler-relative channels receive no physical luminance claim.
For exact positive linear observations, `modelObservedRatio` is an **unclamped**
per-channel diagnostic; 1.5 remains 1.5 and exposes overshoot. It is not emitter
recall, a physical explained-energy fraction or proof of conservation. Quantized,
censored, missing or unknown-domain observations do not get that exact ratio.

`predictionAvailable`, `discrepancyAvailable`, `componentTotalsAvailable`, `attributionComplete` and
`luminanceAvailable` are independent. A known total need not imply identifiable
per-source allocation. `physicalConservationTested` remains false: passing source
sum checks establishes numerical consistency only. Actual passive energy and
reciprocity checks remain in the existing transport stage.
Zero-valued component slots refer to the declared conditional model; they do not
certify absence of real sunlight, GI or unresolved illumination outside that
model. Provider/coverage/uncertainty assessments retain those missing components.

## Production source replay and geometry

Each accepted frozen source is evaluated through production `Joint`/`Shape`/
`Bake` routines with its original fitted RGB and geometry, declared profile,
material hypothesis and static visibility. No Python formula or runtime PBR
shader supplies these values. The source is isolated for attribution, not refit
against targets. Fixed identified boundary roots are replayed separately.
Source-root first bounce follows the same passive operator. Source motion cannot
reuse an unrelated geometry generation.

Direct replay is separately evaluated without requiring unrelated bounce-link
completeness. The physical-bounce replay still retains its own availability and
limitations. Values equal to their stored endpoints are **fixed-parameter
numerical values**, not confidence intervals for source location/intensity,
historical profile, reflectance or missing geometry. Those remain explicit
membership/uncertainty causes, and block exact replacement.

Original luxel IDs, positions, geometric/shading normals, material/surface/style
and physical footprint area survive in the observation domain. No GPU atlas
index or array offset is a sample identity. Grid ambient/directed/direction and
deluxe observations retain distinct stable sample kinds; source-specific packed
grid attribution is currently unsupported. Their total observation diagnostic
can be available while their source contribution query is unavailable.

## Versioned membership and composition

Canonical `TransportMembership` v2 extends the existing descriptor. Parent
contribution/field records retain quantity, units, calibration and provenance.

| Metadata | Meaning |
|---|---|
| `colour`, `domain` | Linear Rec.709, compiler-relative channels, encoded source or unknown; never inferred from a plausible RGB value |
| `roots`, `rootsComplete` | Stable local emitter/sun/sky/identified ambient IDs; unknown root remains explicit |
| `reflectionOrders` | Sorted exact union of orders, with matching min/max; empty means unknown |
| Include/exclude/unknown flags | Direct local/sun/sky, ambient, reflected and unresolved membership |
| Configuration / alternative group | Exclusive full hypotheses; never concatenate profile branches |
| Generation / geometry / profile | Immutable dependency identities |
| Style / parent sample domain | Scoped spatial and observation coverage; missing entries remain unknown |
| Uncertainty / numerical error | Separate causes and independently bounded numerical error; no product of heuristic probabilities |
| Addition / replacement rules | Permitted, forbidden or unknown; permissions never override missing evidence |

`CanAdd` checks canonical validity, exact compatible domains/units/calibration/
colour/style/generation, independent configuration scopes, complete root/path
membership and overlapping root/order sets. It does not compare RGB similarity.
Two components with one shared root and order are rejected as double counting.
The same root at order 0 and order 1 can compose. Different known roots can
compose. Unknown-root aggregates, encoded components, mismatched units, different
sample domains without mapping and mutually exclusive source/configuration
alternatives cannot silently compose. Fields are checked using their canonical
descriptor without consulting GPU state. The result is conditional algebraic
compatibility, not certification of the model's historical truth. A missing field
value still prevents numerical use even when its metadata are composable.

## Replacement eligibility

`ReplacementMetadata` stores provenance, explicit scope/operator, separate
supported/contradicted proof masks and bounded expected error/leakage. Required
proofs cover source identity, localization, amplitude/allocation, visibility,
profile, calibration, receiver support, GI separation and runtime-operator
comparability. Unknown is neither zero nor an affirmative proof.

`MayReplace` currently permits a scoped attempt only when **all** of these hold:

1. Prepared metadata match the exact immutable canonical asset; the sample exists
   and its source contribution is available.
2. Direct zero-bounce irradiance is physical Lux, linear Rec.709, with a matching
   physically authored calibration. Compiler Q, relative source brightness and
   scene-equivalent scale alone fail.
3. The selected configuration is explicit; the source is resolved, has no
   exclusive source group, has actual calibrated photometry/colour and known
   geometry. Reconstructed exact-light suitability requires the existing typed
   calibrated assessment; independent authored reference truth can supply the
   authored proof path without inventing an empirical posterior.
4. The source uncertainty receipt is complete, absolute scale is known, every
   cause is explicitly Supported, and multiple configuration allocations do not
   survive. A high existence-confidence scalar cannot substitute for this set.
5. All replacement proofs have scoped provenance, none is contradicted, and
   numerical/model/geometry/root uncertainty remains absent for the proposed use.
6. The receiver has complete source attribution and comparable non-censored
   linear observations with matching unit/calibration, static style 0 and full
   channel support.
7. The caller independently declares acceptable absolute error/leakage limits;
   the stored bounded estimates satisfy them. Missing bounds fail. Independently
   comparable observation error must be available, its issues must be absent and
   its measured discrepancy cannot exceed the declared replacement error bound.
8. The actual supported mathematical replacement contract matches the source:
   ideal isotropic point irradiance or parallel directional sun irradiance.
   Finite radii/disks, spot, rectangle/tube and runtime LTC/GGX equivalence do not
   gain exact eligibility from this implementation.

The production fitter does **not** manufacture these proofs. Its conditional
datasets explicitly forbid replacement, preserve unknown parameter/profile
uncertainty and leave expected replacement error/leakage unknown. Thus a stock
candidate with excellent encoded RGB fit, UnknownProfile and unknown scale is
still unsafe. The positive eligibility control is an independently authored
linear physical receiver with explicit proof and error budgets, not a recovered
stock lamp. No actual subtraction or dynamic-light publication is performed.

## Storage, lifecycle and bounds

Canonical schema advances **6 -> 7**, producer algorithm **3 -> 4**. Disk format
1.0 remains the container; its schema/key gate rejects old canonical caches.
Original `.jkobs` datasets and source observations remain unchanged. The existing
source-contribution encoding stays **v1**. Sample-domain and observation-diagnostic
encodings advance to **v2**, with explicit readers for the old v1 rows inside a
valid current canonical container. Metadata membership v1 remains readable, but
lacks proof of safe composition.

All records use the existing little-endian bounded chunk headers and SHA-256
chain. Source records are 65 bytes before chunk overhead; sample v2 records are
100 bytes; observation v2 records are 189 bytes. Explicit zeros are stored
records. Sparse omitted records are `Unavailable`, never zero; a sample absent
from its geometry domain is `NotFound`. Missing chunks/checksums/invalid intervals,
IDs, roots, path orders or metadata fail preparation rather than being treated
as sparse omission. Sparse coverage requires `completeSampleCoverage=false`.

Prepared storage indexes source/domain/component/quantity and stable sample IDs;
queries select explicit dataset/configuration when alternatives would otherwise
be ambiguous. The existing physical `LightingSample` adapter remains Rec.709.
Opt-in compiler-channel queries use separate typed `value/lower/upper/domain`
fields and leave `lightingSampleAvailable=false`; they cannot silently become
physical RGB. Read-only snapshots retain the evaluator lease after manager reset,
and publication rejects an evaluator bound to changed metadata.

There is no mandatory global source-by-all-luxel matrix. The producer retains its
bounded patch domain and selected observations, adds a default two-million
source/receiver attribution-pair cap, and keeps the existing source, chunk,
payload, per-evaluation ray/work and cancellation budgets. Each source is replayed
and serialized before its temporary prediction buffers are released. Prepared
decoding is bounded by the read context; it retains the **stored sparse records**,
not a zero-filled dense map. Input artifact bytes, metadata/index overhead and OS
peak RSS remain distinct from accounted record storage. Actual measured resource
results and reproduction commands accompany the versioned witness.

## Tests and limits

The P06 suite covers analytical point irradiance, differently coloured sources,
same-colour allocation ambiguity, point/spot wall and doorway visibility, separate
sun/sky/reflected-sun receipts, missing/zero/unknown sparse records, source-bound
round trips, corruption, cancellation/budget rejection, saturated observations,
material/profile/position/style ambiguity, total-field double counting and
exclusive alternatives. A real S13 two-colour fit verifies canonical source/root
mapping against separately recomputed frozen roots and observation-domain queries.
Existing producer/joint suites supply the wider physical and compiler controls,
including coloured passive bounce and extended geometry. Exact renderer-model
compatibility, arbitrary lightstyles, complete stock attribution and source-level
grid packing remain unsupported rather than silently approximated.

See [P06 commands and numerical results](../tools/rend2/contributions/README.md).

## Executed numerical validation

The actual compiled pipeline uses fixed supplied XYZ, unknown RGB amplitudes,
the selected known NetRadiant profile, native `Joint::Solve` and frozen independent
validation/held-out evaluation. Each fixture uses 18 training, 15 validation and
15 held-out observations. This bounded sample set is not complete-map coverage.

| Compiler fixture | Train / validation / held-out interval MSE | Held-out black MSE | Prepared queries after disk reload | Explicit zero records |
|---|---|---|---|---|
| `point_room` | 0 / 0 / 0 | 2495.888889 | 66 | 0 |
| `two_colour` | 0.218170 / 2.176802 / 0.222869 | 1170.601852 | 132 | 0 |
| `door_occlusion` | 0 / 0 / 0 | 2495.888889 | 66 | 18 |

All 264 available source records survive codec/cache/prepared-Snapshot round trip
with exactly zero value difference. Values are compiler-relative pre-encoding
channels, not measured lux. Wrong-profile controls are `DiagnosticOnly` with no
accepted source dataset; missing held-out predictions are not scored as darkness.
The repeated two-colour run produces byte-identical archives. The successful
archives are 26,328, 35,624 and 26,352 bytes respectively, including both retained
patch-domain and original-observation-domain attribution.

The point/door recovered compiler RGB is `(299.854919,299.854919,299.854919)` versus
the withheld `(300,300,300)`, relative norm error 0.04836%. The two-colour source
errors are 5.162% and 5.864%, despite low held-out RGB residual. These are not exact
source allocation receipts. Native independent frozen-root recomputation on the
owned two-colour test agrees exactly (maximum error 0); its 60 sample-domain rows
and 120 contribution records account for 20,584 bytes of retained records/indexes,
and attribution replay records 4 forward calls and 336 rays. That agreement is
with the conditional model, not proof that its source parameters are true.

Positive wall/opening controls return 0 / 9.24146 in both point and spot cases.
A partially occluded spotlight footprint gives 3.94129 versus the unoccluded
8.12987. This is the declared physical quadrature test, not a generic compiler
shadow-boundary guarantee. Unknown alpha geometry produces unavailable support.
The independent point-plane physical reference is `100 cd / (4 m)^2 = 6.25 lux`.
The shared-peak counterexample stores pre-encoding `(510,255,0)` and total encoded
`(255,127.5,0)`; separately encoded sources would incorrectly sum green to 255.
No per-source encoded subtraction is admitted.

The 22-case forward regression retains 20 supported passes and two explicitly
unsupported full bounce replays. C++ versus original compiler samples are
byte-exact except the predeclared colour-clipping case (maximum one byte).
C++ versus the older Python analytic prediction also differs by up to 28 bytes
at the documented multi-sun boundary: that analytic calculation lacks the scoped
compiler trace-tree/acceptance/sky-continuation replay. All 1,225 C++ versus
compiler multi-sun floor samples remain byte-exact. The clipping comparison's
one-byte bound remains the existing tolerance; P06 does not claim a new exact
floating-point/packing replay there.
Wrong attenuation at the same angular settings has RMSE 17.720206; the separate
wrong-profile control has RMSE 14.477597. The historical stock compiler remains
UnknownProfile. Neither boundary replay nor source-level grid packing is widened
by this task.

Actual final compiled child runs used approximately 5.79–6.80 MiB peak RSS and
0.419–5.425 seconds; these are whole
producer/cache/query runs, not isolated query timings or optimized latency.
The dedicated two-run P06 test took 0.562 / 0.204 seconds and consumed
6.09 / 6.05 MiB peak RSS; final
variable measurements are preserved separately in the frozen witness. Record
accounting excludes the already-loaded artifact, canonical metadata and allocator
overhead. No global dense emitter-by-map-luxel matrix is required. Validation also
checks an aggregate scratch cap across sample domains and contribution identities.

Toolchain: GCC 16.2.0 CPU Release, MSVC 2019 x64 SP/MP Release, CMake 4.4.2,
Python 3.12.14 standard library, Windows 10.0.19045, Intel family 6 model 165,
12 visible logical processors. Build and CTest invocations used at most two
workers each. No renderer launch, new external dependency or licensed asset copying
was required.

The initial full regression exposed an intentionally stale schema-6 disk hash;
the schema-7 witness is explicitly frozen and old caches remain rejected. The
compiled adapter initially dropped the original UV-clipped footprint from its
receiver records while retaining it in patches. It now preserves the same
geometry in both domains without supplying withheld RGB. Neither failure was
fixed by loosening physical comparison tolerances. Full pass counts, final build
logs/hashes, repeat checks and exact commands are in the versioned P06 witness.

Final checks: **36/36 CTest** in 280.64 seconds; the final replacement-admission
and disk subset was repeated successfully after adding the missing-comparability
negative control. The dedicated native test has **497 assertions, zero failures**
and two identical semantic runs, including 14 distinct unsafe replacement cases.
Python regressions pass 39 evaluation, 7 blind-access, 16 non-vacuous/adversarial
and 4 trusted-controller tests. Both MSVC SP/MP Release modules build. The existing
redundant x64 `/arch:SSE2` option produces a D9002 warning, not a build error.

Disabled-path validation compiles all 27 GPU programs / 51 stages and finds zero
shader-source or binding changes versus input commit `e3c333650`. The historical
baseline at `380ee0f7` still reports 3,870 diff lines; that separate check remains
failed and is not relabelled passed or rewritten. The architecture dependency
audit passes, including the new prepared evaluator. No dynamic replacement,
world-diffuse change, fog/Ghoul2/shadow modification or runtime consumer activation
is introduced.

## X19 query ownership update

The [X19 runtime hardening report](rend2-runtime-hardening-x19.md) documents the
allocation-free prepared query. `ContributionSample` now retains a canonical
`metadata` lease; use `Domain()` or `metadata->domain` while retaining that lease.
The serialized attribution model, quantity/calibration and composition rules
remain unchanged. C++ consumers must be rebuilt for the API layout change.
