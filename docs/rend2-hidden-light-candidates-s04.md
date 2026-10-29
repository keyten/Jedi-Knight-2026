# S04: hidden point/spot candidate proposals

This provider proposes **geometry hypotheses**, not recovered original lights.
`Hidden::Generate` is a CPU-only complementary input to later fitting. It neither
installs physical emitters in a `ReconstructedLightingAsset` nor enables any
renderer capability. Original BSP RGB, intensity units and legacy rendering are
unchanged. Stock JA remains `UnknownProfile`.

## Boundary and inputs

`shared/rd-rend2/lightrecon/hidden_candidates.h/.cpp` is part of the shared CPU
library linked by SP, MP and standalone tools. Its input is a bounded immutable
training-sample lease, explicit profile hypotheses, map/geometry/classifier
identities, analysis/search bounds, static visibility and optional region graph.
No filesystem, renderer allocator, cvar, frame state, GPU or thread is needed.

`FromObservation` copies original colour lightmap RGB8 from the canonical decoder.
It rejects missing, HDR and direction-only data. A caller cannot make unknown
transfer/primaries verified through this adapter. An explicit conditional-encoding
option permits anonymous encoded-channel comparison under supplied candidate bake
profiles; it does **not** produce verified source-linear RGB or physical units.
Current PBR albedo is never divided into observations. Animated/moving/unsupported
receivers are excluded by the existing surface classification at the adapter.

`Result.evidence` and `Result.candidates` use the existing `EmitterEvidence` and
`EmitterCandidate` records. New `EvidenceOrigin::SurfaceObservation` distinguishes
inferred geometry from authored shader intensity or emissive geometry. All emitted
intensities/colours/chromaticities remain unknown, with `EvidenceUnit::Unknown`.
`staticEligible` means a static-source **proposal**, not a free-space certificate
or verified existence; inspect the separate feasibility/visibility records.
The parallel `Hypothesis` contains profile, geometric classification, contributing
sample IDs/surfaces, per-channel scores, visibility, uncertainty and search bounds.
Outputs own all records. No input RGB/geometry pointer survives the call.

## Local spatial hierarchy

Explicit `SearchArea` records support a later controller requesting more candidates
in unexplained areas without exposing withheld RGB to the provider. Without them,
deterministic world-space bins define receiver neighbourhoods and expanded source
search bounds. Defaults are 512-world-unit bins and 256-unit reach, clipped to
explicit analysis bounds. These are neighbourhoods, **not invented semantic rooms**.

For each source position:

1. Use a certified region assignment when available. Solid or physically outside
   positions are rejected. Being outside a region-analysis ROI is distinct from
   being physically outside.
2. Otherwise query the supplied BSP/domain classifier. Preserve leaf/cluster/area
   context separately. A known solid is rejected; PVS/cluster IDs never prove LOS.
3. A known-free point plus a locally complete visual-geometry overlap query can
   certify a locally feasible location even if global scene metadata is incomplete.
4. Unknown/boundary positions remain explicit unverified alternatives. Relevant
   unknown rays are never silently transparent. Moving-boundary uncertainty from
   the graph prevents falsely exact placement claims.

Region membership is optional, not a prerequisite for scoring a valid luxel.
`LightingRegionGraph::VisibilityKey()` checks that an optional graph was certified
against the supplied visual scene. A stale same-map graph is rejected. A known
solid from the current domain still overrides a free-region statement.
Candidate-to-receiver rays are queried independently; equal region IDs do not
certify visibility and disconnected graph components do not certify blockage.
An aperture/reflective hint marks transport evidence, never an artificial portal
emitter. Unidentified direct/sky/bounce/material mixtures default to ambiguous
source-versus-transport. Only an independently attested direct-only experiment
can receive the stronger **physical-emitter-candidate** category, still without
existence probability or resolved-light status.

## Localization mathematics

Each local search samples a bounded coarse lattice and refines several distinct
best basins with eight child cells. It never starts from lightgrid extrema or a
map-global brightness list. Receiver selection is geometry-only, round robin
over source surfaces, with area balancing within each surface. Every area has
its own candidate budget, so a bright distant room cannot consume a dim room's
entire allocation.

For a profile m, position p and point/spot geometry g, production
`Bake::CpuForwardEvaluator::Prepare/Evaluate` supplies encoded predictions
`f_i(m,p,g,q)` for two **declared probe amplitudes** q (128 and 512 by default).
These are source-response probes, not fitted/final intensities. Factor reuse is
limited to the same source geometry/profile/receivers; the production evaluator
recomputes amplitude-dependent cutoffs and output encoding. Source motion gets
new factors. The historical observation operator is not runtime PBR attenuation.

For material group M and encoded channel c, rank spatial response shape with

```
shape_distance = 1 - sum_M(w_i y_ic f_ic)
                       / sqrt(sum_M(w_i y_ic^2) sum_M(w_i f_ic^2))
```

Groups with fewer than four positive samples or no meaningful within-material
contrast are not localization evidence. Material groups are normalized separately;
a material-colour step alone cannot become an illumination gradient. Surface-area
weights give each receiver surface equal influence. Scores are averaged over
admitted groups, and the lower score over the same fixed probe-amplitude budget
is retained. This is a scale-insensitive **proposal statistic**, not a likelihood,
calibrated confidence, final RGB fit or proof of physical source existence.

Channels are searched separately to preserve differently coloured source basins.
Identical geometry across channels is deduplicated with an evidence-channel mask;
profile and point/spot alternatives remain distinct. Point proposals have a
separate retention quota from spot proposals. Spot axes initially point toward
the locally illuminated receiver centroid, with two tested cone geometries.
That axis/cone search is incomplete; later fitting must test/refine alternatives.

Back-facing, visible, blocked, unknown and assumed relations are distinct. Known
blocked predictions are zero in their correct observation model. Unknown
predictions are excluded from numerical comparison, never inserted as black.
Positions with no possible positively illuminated receiver under known geometry
are not proposed. An all-unknown case can retain an unresolved geometric cell,
with unavailable score and no physical-source classification.

The method handles normal-response changes through the forward model rather than
interpreting every lightmap maximum as a lamp. It is not a full multi-light
decomposition: overlapping sources, reflections or an incorrect bake profile can
still bias local single-source shape rankings. Preserve those hypotheses for
independent fitting/validation, rather than promoting them by residual energy.

## Directional support and uncertainty

`GridConstraint` and `DeluxeConstraint` preserve original receiver-to-incoming-light
directions, quantization bounds and provenance. A **positive additional mixture
uncertainty** is required: quantization alone is not an accurate lamp-direction
likelihood. Grid ambient/directed RGB is never reinterpreted as GI/direct light.

Two nonparallel directional constraints may seed the midpoint of their closest
forward-ray approach. Nearly parallel rays do not get an unstable finite
intersection. Every seed still needs surface evidence, domain checks and visibility.
Directions only break equivalent surface-score ties; they cannot generate a source
without enough surface observations. Missing or noisy grid evidence therefore
cannot resurrect the obsolete lightgrid proxy algorithm.

Each retained hypothesis records independent surfaces/materials, receiver baseline,
normal diversity, known/unknown visibility and profile dependency. Single-plane,
missing-visibility and weak-support cases are explicitly underconstrained.
`searchedCell` is search resolution, **not an XYZ confidence interval**. Returned
cells/positions are finite tested alternatives; truncation is reported and their
union is not a complete feasible set. Existence confidence and final intensity
remain unknown; scores are not arbitrary products of confidence factors.
Count, distance/intensity, angular shape, source colour and transport attribution
can remain non-identifiable even with a promising candidate position.

## Limits and reproducibility

Defaults bound 512 input samples, 64 selected receivers/area, 32 areas, four
profiles, 768 tested positions/area, two refinement rounds, 2M rays, 64M traversal
units, 100K forward calls and 64 MiB accounted estimator memory. Selection and
output truncation are explicit. Too many areas require deterministic caller-side
shards; no pages/rooms are silently dropped to satisfy a global source budget.
Cancellation, provider failures, invalid inputs and work/memory exhaustion clear
all unpublished candidate output while preserving diagnostics.

IDs hash original map content, geometry/classifier/region identities, unit policy,
sample geometry/raw RGB/material eligibility, profiles and search settings, then
area/position/type/orientation. Training sample ordering is canonicalized. Profile
and explicit search-order configuration is part of the experiment. Producer
`hidden-v1` is independent of the existing evidence/asset schema. Appending the
new evidence-origin enum changes no binary record layout or existing enum values;
new readers accept old archives, old strict readers reject the new origin.
The codec rejects inferred records claiming authored/physical intensity. Blind
dataset filtering still removes inferred-answer records from solver priors.

No result is automatically merged into the manager, cache or a canonical resolved
emitter array. Canonical candidate records need an explicit trusted publication
step if persisted. Source/certification rule changes must change producer versions
and relevant content keys.

## Tools and reproduction

The standalone tool accepts **sanitized blind geometry** plus a bounded training
request. It rejects full image/grid archives and hidden vertex RGB. The Python
controller uses the existing `TrainingView`, never opens validation/held-out files,
and reads known-source answer files only after the C++ proposals are frozen.
No Python lighting predictor or NumPy/SciPy dependency is introduced.

```powershell
cmake -S tools/lightrecon_test -B build/lightrecon-disk-standalone
cmake --build build/lightrecon-disk-standalone --parallel 6
ctest --test-dir build/lightrecon-disk-standalone --output-on-failure
build/lightrecon-disk-standalone/lightrecon_hidden_test.exe build/s04-cpu.json
python -X utf8 -m unittest discover -s tools/rend2/hidden_candidates -p 'test_*.py' -v
python -X utf8 tools/rend2/hidden_candidates/run.py --tool build/lightrecon-disk-standalone/lightrecon_hidden_tool.exe --output build/s04-datasets
cmake --build build/lightrecon-core-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 lightrecon_hidden_test lightrecon_hidden_tool --parallel 6
```

Use a fresh output directory. `--fixtures`, `--answers` and `--stock` select local
existing controlled/stock TrainingViews; `--skip-stock` explicitly omits stock work.
The default stock experiment uses the predeclared hoth3 ROI centre `(3320,700,123)`,
radius 512. Licensed geometry, RGB and debug OBJ stay under ignored `build/`.
The small versioned report stores counts/hashes/limitations, not proprietary maps.

Existing q3map2 fixture preparation/compiler hashes are documented in
[the offline dataset workflow](rend2-offline-observation-datasets.md) and
[q3map2 validation](rend2-q3map2-validation.md). Reusing their frozen compiled
observations tests real baked data; it is not a new compiler invocation and
cannot identify the historical Raven compiler.

## Executed validation and results

The versioned [numerical witness](../tools/rend2/hidden_candidates/results/validation.json)
and [process measurements](../tools/rend2/hidden_candidates/results/measurements.json)
contain LF-normalized source hashes, executable hash, BSP/partition/answer hashes,
scope, per-profile/type/feasibility breakdowns and explicit incomplete-search flags.
The two production-C++ dataset sweeps have identical semantic output:
`7771d1cbd2b869ad790ec3a1e3e201edc6ac01f710387d3b6b94986b9e258b0a`.
Time/RSS and concurrent unrelated Git history are not numerical comparisons.

| Frozen compiled fixture | Candidate records | Nearest point proposal to each hidden answer | Coverage at declared finest-cell radius | Evidence |
|---|---:|---|---|---|
| point_room | 16 | 14.33 world units | 1/1 at 26.06 units | Three receiver surfaces; conditional profile/trace; unknown rays retained |
| two_colour | 38 | 14.33 / 8.79 units | 2/2 at 26.06 units | Three receiver surfaces; distinct RGB channels; both profile families retained |

The 26.06-unit radius comes from the **input** search-box diagonal divided by
`2 * coarseDivisions * 2^refinementRounds`; it is a resolution reference, not a
confidence interval or a threshold selected from source answers. Point-only
proposals already achieve this coverage. Sixteen/38 alternatives are not sixteen/38
recovered emitters: source existence/count/intensity remain unknown. No held-out
RGB is read, and no held-out lighting accuracy is claimed in this candidate task.

The own-content CPU program passes **1,791 checks**, with 18 named witnesses,
on GCC and MSVC. Checks include per-ray, ownership, invalid-input and independent
analytic room/wall-oracle assertions; they are not 1,791 independent scenes.

| CPU case | Proposals | Nearest truth distance / result |
|---|---:|---|
| Hidden point, no fixture hints | 5 | 9.03 units |
| Two coloured points | 15 | 7.18 / 7.18 units |
| Two nearby white points | 5 | 9.03 / 9.03; count remains ambiguous |
| Hidden spotlight | 6 | 19.01; coarse axis/cone remains a limitation |
| Known opaque wall | 5 | 9.38; no returned position inside the wall |
| Doorway transported illumination | 6 | Zero physical-emitter classifications |
| Same transport without the explicit label | 6 | All source-versus-transport ambiguous |
| Material-boundary-only observation | 0 | Unavailable; labelled material step is not a lamp |
| Saturated plateau | 0 | Unavailable; no intensity or location invented |
| Unknown optics, no regions | 1 | Unlocalized geometry alternative; numerical score unavailable |
| One receiver plane | 7 | Underconstrained despite a near-truth proposal |
| Sparse noisy grid seed | 5 | 9.03; surface evidence decides, grid-only input rejected |
| Two bake-profile hypotheses | 12 | Profile-dependent alternatives preserved |
| One unsupported receiver among usable faces | 5 | Receiver excluded locally; usable faces still yield 9.03 |
| Certified region supplied | 5 | Region context retained; stale visibility graph rejected |
| Small dark room + large bright courtyard | 18 | 9.03 / 18.55; both local budgets retain proposals |
| Definite opaque wall behind uncertain foreground | 0 | Blocked remains Blocked |
| Assumed alpha blockage | 1 | Unknown/ambiguous; not a hard optical rejection |

For the nominal small room the finest searched-cell radius is about 12.7 units.
The spot's 19.0-unit error exceeds that scale; the bounded centroid/cone initializer
is a proposal mechanism needing later geometry refinement, not precise spot recovery.
Nearby white sources illustrate that good candidate coverage does not resolve
source count. The `positionUnderconstrained` flag is a simple support warning;
false does not attest Jacobian rank or unique XYZ. Distance/intensity ridges and
profile disagreement are retained as alternatives plus unknown photometry.

The doorway fixture uses nine deterministic area samples of prescribed incoming
boundary radiance through a real 64×96 opening, with no physical source inside
the receiver room. It is transported-light evidence, not point-light-labelled
data and not a claimed replay of q3map bounce. The courtyard is four times the
linear room scale, has a sky ceiling/no sky receivers, and its source amplitude
is 600 times the small-room source; no global brightness ranking drops the dim room.

### Stock hoth3 is an honest localization limitation

The predeclared `(3320,700,123)`, radius-512 TrainingView ROI supplied 64 receivers
from 64 surfaces. Six saturated receivers were excluded; 58 remained. The bounded
search visited 128 positions, rejected 24 known-solid and 16 outside positions,
and retained 16 alternatives (8 point/8 spot, eight per profile). No region graph
was supplied and all region IDs remain unknown. Eight source positions have
locally certified static free space; eight retain unknown feasibility. Global
visual-scene completeness remains false.

Crucially, **none of the retained hypotheses has a certified visible receiver
relation**: their combined records include 60 unknown relations, with the other
relations blocked/back-facing. The shape convention assigns a worst cost to
zero-response comparisons; that is not positive localization evidence. The report
therefore labels this result `UnlocalizedNoKnownVisibleReceiverSupport`. All 16
are ambiguous source/transport, not physical recoveries or a demonstrated useful
stock source fit. There is no stock XYZ accuracy or source-recall claim. This is
the precise remaining evidence limitation, rather than a global zero-region gate.

### Cost, failure controls and commands actually used

On this workstation/MSVC Release, point-room search took 2.69/3.25 s, two-colour
5.98/6.35 s, and stock search 3.52/2.81 s across repeated sweeps. Stock geometry
load/BVH preparation took 1.09/1.26 s. Actual C++ peak working set was about
5.2 MiB, 6.6 MiB and **53.9 MiB** respectively. The stock acceleration structure
accounts for 38,544,638 bytes; the estimator's conservative configured accounting
is 11,415,552 bytes, independently of the geometry lease. These are bounded small
ROIs, not full-map reconstruction throughput. Output quotas remain explicitly limited.

Standalone tests pass **20/20**; the final complete CTest run took 45.92 s.
Python protocol tests pass **5/5**. SP/MP Release builds pass with the same shared
core; no game was launched. Existing Git-tag/CPack and `/arch:SSE2` messages are
unrelated build warnings. Shader/image runtime regression was not executed for
this CPU-only change; no shader or active renderer call site was edited.

Executed final workflow (bundled Python 3, `-X utf8`; CMake/GCC on PATH):

```powershell
cmake --build build/lightrecon-disk-standalone --parallel 3
ctest --test-dir build/lightrecon-disk-standalone --output-on-failure
cmake --build build/lightrecon-disk-standalone --target lightrecon_hidden_test --parallel 2
build/lightrecon-disk-standalone/lightrecon_hidden_test.exe build/s04-cpu-final-gcc.json
cmake --build build/lightrecon-core-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 lightrecon_hidden_test lightrecon_hidden_tool --parallel 3
cmake --build build/lightrecon-core-renderers --config Release --target lightrecon_hidden_test --parallel 2
build/lightrecon-core-renderers/Release/lightrecon_hidden_test.exe build/s04-cpu-final-msvc.json
python -X utf8 tools/rend2/hidden_candidates/run.py --tool build/lightrecon-core-renderers/Release/lightrecon_hidden_tool.exe --output build/s04-published-a
python -X utf8 tools/rend2/hidden_candidates/run.py --tool build/lightrecon-core-renderers/Release/lightrecon_hidden_tool.exe --output build/s04-published-b
python -X utf8 -m unittest discover -s tools/rend2/hidden_candidates -p 'test_*.py' -v
python -X utf8 tools/rend2/hidden_candidates/report.py --run build/s04-published-a --repeat build/s04-published-b --cpu build/s04-cpu-final-msvc.json --other-cpu build/s04-cpu-final-gcc.json --tool build/lightrecon-core-renderers/Release/lightrecon_hidden_tool.exe --output tools/rend2/hidden_candidates/results
```

An earlier adapter incorrectly used training-receiver AABB as source bounds; the
partition retained only the southern room receivers and excluded one true source.
It produced an 89.31-unit nearest-proposal error. Expanding by a fixed 128-unit
neighbourhood reach before opening answers corrected the search domain; that failed
experiment is not counted as successful coverage. An initial two-room CPU fixture
also used nonexistent surface references and aborted after its unchecked test
assertion; it was replaced by actual second-room geometry/domain and guarded checks.
The final transport and courtyard fixtures use their actual stated geometry.
The bounds bug was diagnosed using development answers. Accordingly, these known
source-distance/recall numbers are development-fixture checks, not untouched
held-out-scene generalization. The C++ provider still receives no true source XYZ
or amplitudes; no validation/held-out observation colours are accessed.

Before full fitting: add condition/Jacobian diagnostics, broader spot-axis/cone
search, explicit multi-source/indirect attribution, stricter physical/bake boundary
validation and profile/calibration sensitivity. Missing material response, moving
states without supplied domain/region constraints and unknown stock trace recipes
remain limitations. HDR scoring is unsupported. Preserve the distinction between
candidate coverage, physical source correctness and unresolved transport throughout.

Changed implementation files: new hidden provider/API, shared-core CMake entry,
canonical evidence-origin/name and codec guard, read-only region visibility-key
accessor, standalone test/tool/CMake and the Python controller/report/tests/README.
Existing observation capture, runtime sun/fog/entity/LTC/Forward+ consumers and
canonical resolved-emitter publication are untouched.
