# S05: emitter-evidence consolidation and bounded candidate work

This stage is CPU-only. It assembles **candidate optimisation jobs**, not a
reconstructed-lighting result. It never fits intensity, invents physical units,
calibrates source-existence probabilities, resolves a historical bake profile or
publishes renderer lights. Original evidence and canonical Solar photometry remain
immutable. No renderer/fog/shadow/cvar/shader path is changed.

## Boundary and ownership

`shared/rd-rend2/lightrecon/candidate_pool.h/.cpp` defines
`Rend2LightRecon::Core::Candidates` (version 1):

```text
EmitterEvidenceSet ─ FromEvidence ─┐
Hidden::Result ──── FromHidden ────┼─ immutable Provider leases
Solar::Result ───── FromSolar ─────┘       │
explicit source/fragment/context annotations
                                        ↓
                         Consolidate → Catalog
                         evidence DAG / input aliases / model alternatives
                                        ↓
                  Budget + caller-owned Cursor + optional Demand
                                        ↓
                            Select → bounded work tickets
                                        ↓
                       future solver (not implemented here)
```

Public operations are `FromEvidence`, `FromHidden`, `FromSolar`, `Consolidate`,
`Select`, `Evidence`, `InputAnnotation`, `Summarize`, `ClassName`, `DeferredName`.
The first three adapt existing providers. `Consolidate` and `Select` accept explicit
`ReadContext` cancellation/memory limits and publish no partial output on failure.
`Select` updates its cursor only on success. Outputs distinguish invalid input,
stale context, cancellation, work/memory limits and deferred unsupported records.

Each `Chunk` has a strong immutable backing lease; a catalog can outlive all
upstream handles. Original strings, JSON, entity/surface ordinals, candidate
`sourceIds`, observations, material uncertainty and photometry are reachable through
those leases. The catalog owns small references and consolidated metadata. Solar
adapters additionally retain an aliasing lease to the original canonical asset and
its environment payload owner; relative intensity, calibration, provenance,
uncertainty, `SunCoverage` and unresolved transport are not recast into invented
`EmitterEvidence` intensity values. Callers must freeze inputs and the published
catalog for their lease lifetime. There is no world pointer, engine allocator,
background task, global cursor or pair cache.

## Conservative consolidation

There are three distinct operations:

1. **Evidence union:** explicit nonzero `Annotation::sourceIdentity` attests the
   same upstream source. The digest must identify an authored record or one proven
   connected emitter, not a material, colour, room or general fixture containing
   several bulbs. Different models/photometry of that source remain alternatives.
2. **Exact duplicate:** the same original primary evidence ID, origin, source
   locator, original surface/entity anchors and exact geometry/photometry can union
   across providers. Distinct authored aliases prohibit this automatic union.
   Without a same-source attestation, multiplicity remains explicitly unknown.
   Ordering, proximity and brightness cannot supply the attestation.
3. **Possible association:** a spatial hash proposes bounded nearby links using
   distance, compatible direction/known colour, shared observation support, region
   membership and optional CPU ray results. These links never trigger transitive
   unions. A→B→C proximity cannot collapse three adjacent lamps. A blocked link
   stays blocked, and unknown optics do not become transparent. Region/PVS labels
   are not a visibility test. Missing colour transfer disables colour comparison.

Geometry/photometry are never averaged, summed or replaced by an inferred panel.
Point, spot, area/surface, tube, sun, sky, transport and unknown classes are retained.
Different shapes, profile conditions, scenarios or source parameters produce model
alternatives. Conflicting authored models are marked, not silently overridden.
An `AmbiguousSourceOrTransport` record remains ambiguous; a transport record is not
promoted into a physical emitter. No stored evidence confidence is changed.

An explicitly attested `fragmentSet` can describe an **exact union of original
parts of one source**. It also requires `sourceIdentity`. The model preserves part
references, per-part geometry, masks and photometry; it does not replace their union
by a bounding rectangle. Duplicate parts with identical original ID and geometry
are represented once, while all input/evidence aliases survive. Unknown disconnected
fragments cannot use this shortcut. Existing emissive extraction/grouping is reused
through `FromEvidence`; no new image-mask or fixture extraction is introduced.

Availability combines redundant copies with OR, then required unique parts with
AND: one usable duplicate remains admissible despite an unavailable copy from
another provider. A genuinely missing unique component of an exact union still
prevents claiming complete geometry. Both cases have explicit negative/positive
regressions. The original unavailable evidence is retained in either case.

Every input has `Mapping` with its bundle and action: standalone, evidence merged,
alternative model or fragment part. Provenance is map → scoped original evidence →
bundle. Canonical Solar provenance remains additionally accessible in its original
asset namespace through the provider lease.

## Identity and conditional contexts

IDs hash map content, geometry/region scope, version, canonical provider generation
and sorted original aliases; model IDs additionally include class, conditional
profile/scenario and model geometry. Vector indices, iteration order, addresses and
GPU coordinates never enter hashes. References retain their original ordinals for
lookup. Hash collisions fail explicitly. Adding evidence can change a bundle ID;
input aliases and `Mapping` provide cross-revision correspondence. IDs are not a
promise to recognise a physically unchanged lamp across arbitrary provider revisions.

The catalog key includes evidence content, annotation content, geometry/material
generation, region certification, bin scale and proposal policy. Changing geometry
invalidates the key/cursor. Providers must provide complete content generations;
immutable custom providers must not reuse a generation after changing hidden data.
Budgets/expansion requests do not alter model IDs. No existing region/disk schema or
cache entry is reinterpreted by this new version-1 stage.

Solar hypotheses are **joint alternative explanations**, not independent sun/sky
sources that can all coexist. Each provider has a scenario digest. If multiple
profiles/scenarios are present, their jobs remain `conditional-context` until a
caller explicitly selects the context. One context may schedule both its sun and
its sky; it cannot schedule a competing scenario. A single profile/scenario can be
used without a redundant selection. The selection itself proves no historical
compiler identity. Generic fixture candidates can participate in a selected context.

## Ranking, local homes and adaptive budgets

A bundle has one deterministic scheduling home to avoid charging the same source
once per visible room. A valid supplied `LightingRegionGraph` assigns the primary
source position to a certified region. Its map, visibility and certification keys
must match. Otherwise a 512-world-unit spatial bin is an **analysis bin**, never a
fabricated room. Sun/sky have a separate global-environment home; missing position
is unlocalized. All upstream region memberships and receiver supports remain, so a
source can support multiple regions. Membership without a graph remains an upstream
hint, not a new certificate. No disconnected graph rejects a physically valid ray.

Default per-home class reservations are point 4, spot 4, area 4, tube 2, sun 2,
sky 2, transport 2, unknown 2, with a 24-ticket home ceiling. Within a class, order is:
protected authored evidence → upstream evidence tier → number of distinct
certified-visible receiver surfaces (or observation IDs if absent) → stable ID.
These are ordinal scheduling criteria, **not probabilities**. Initial intensity is
not a ranking input. Unknown or weak visible support does not delete dim hypotheses.

Up to two additional home jobs can exceed class reservations when they cover new
certified-visible receiver support in that wave. This bounded coverage bonus does
not use hidden RGB or reward thousands of correlated luxels on one surface. It is
an information proxy, not a Fisher-information estimate or source-recall guarantee.

`Demand` requests extra class quota for a named home/region with an explicit reason.
A request can also reach a source's other recorded region memberships, while the
source is still charged once at its primary home. It can be driven by a future
solver's unexplained **training-side** error; this module sees no validation or
held-out colours. Expansion is still subject to explicit work/memory ceilings.

A wave admits at most one alternative per bundle. Default global wave ceilings are
256 tickets, 4,096 upstream-defined geometry-work units, 64 exact parts per ticket
and 32 MiB conservative scratch accounting. These are work limits, not a permanent
source cap. Home visitation rotates after exhaustion of a global wave limit.
Caller-owned progress eventually exposes every eligible model/part under adequate
nonzero quotas. With very small budgets a class/alternative may need later waves;
first-wave admission is not total source recall. Changing quotas permits continued
progress; changing conditional context/catalog requires an explicit cursor reset.

Protected authored records overflowing an active quota become `protected-overflow`,
not deleted. Other reasons are local quota, wave budget, alternative, partial exact
geometry, unsupported geometry, disabled class, oversized indivisible part and
conditional context. A partial exact-union ticket has `completesModel=false`; it
must not be rendered/evaluated as the complete source. `exhausted` means no eligible
work remains in this context, not that the lighting reconstruction succeeded.
`Summarize` reports home × class × provider × reason, selected work and protected
counts. A consolidated model may appear in several provider rows; those rows must
not be summed as independent physical sources.

## Bounds, cost and limitations

Consolidation defaults: 100,000 candidate records, 200,000 evidence records,
2,000,000 support references, 128 MiB conservative resident/transient accounting,
32 MiB input text, 32 providers; 200,000 neighbour tests, 100,000 possible links,
16 neighbour attempts per bundle, 16,000,000 relation checks and 16,000,000 ray
traversal operations. Limits are checked before major allocation, with checked
arithmetic and allocation-failure handling. Count limits are safety ceilings; large
record shapes can hit the byte limit earlier. Such a build fails clearly and
preserves caller-owned upstream data; it never drops an area prefix and claims a
successful full catalog. Callers can explicitly raise limits or partition local
input providers, retaining shared identities across the higher-level controller.

Identity grouping is sorted/hash-key based, not all-pairs. Local spatial proposals
are capped, including support-intersection work. A limited proposal pass sets
`linksComplete=false` but retains every input/model. Source geometry is not silently
removed when its optional neighbour investigation is incomplete. Visibility uses
the existing CPU provider; assumptions/unknown results remain unknown. A proposal
ray cannot establish complete source-to-receiver transport. A custom visibility
provider must be an immutable implementation of the declared `geometryKey`; the
generic interface cannot independently discover or hash its backing scene.

Accounting bounds allocations owned by this stage and scratch; borrowed provider
payloads are separately reported. It is not an OS RSS limiter. RSS measurements below
cover the full test process. There is no mandatory source × luxel matrix. The stage
does not inspect maps or observations a second time and does not alter dataset
partition boundaries.

Remaining risks: bad upstream same-source attestations can incorrectly group
evidence; canonical distinct IDs/geometry and blocked-link diagnostics aid audit,
but no inverse algorithm is run to prove identity. Very dense scenes may retain
many deferred duplicates because conservative rules deliberately favour recall.
Link caps can miss useful associations. Source hints may contain false positives.
Missing source candidates cannot be recovered by a budget. Physical source recall,
stock-map recovery and calibrated uncertainty require later fitting/evaluation.

## Reproduction and validation

The self-contained `lightrecon_candidates_test` uses generated geometry/evidence,
not proprietary assets. Its fixtures cover nearby identical lamps, bridge chains,
exact duplicates across providers, conflicting authored intensities, competing
shapes/profiles/scenarios, static/alpha walls, a real two-region graph, multi-region
membership, visible-support quotas, exact connected parts, isolated spatial homes,
weak authored lamps, reorder/lifetime checks, bad references, geometry changes,
memory/work limits and cancellation with cursor rollback.

```powershell
# GCC standalone Release (PATH must contain this installation's GCC/make/CMake).
cmake -S tools/lightrecon_test -B build/lightrecon-disk-standalone -DCMAKE_BUILD_TYPE=Release
cmake --build build/lightrecon-disk-standalone --parallel 2
ctest --test-dir build/lightrecon-disk-standalone --output-on-failure
build/lightrecon-disk-standalone/lightrecon_candidates_test.exe build/s05-cpu-gcc.json

# Existing VS 2019 x64 SP/MP configuration, BuildLightReconTests=ON.
cmake --build build/lightrecon-core-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 lightrecon_candidates_test --parallel 2
python -X utf8 tools/rend2/candidate_pool/run.py --tool build/lightrecon-core-renderers/Release/lightrecon_candidates_test.exe --out tools/rend2/candidate_pool/results/msvc.json
python -X utf8 tools/rend2/candidate_pool/run.py --tool build/lightrecon-disk-standalone/lightrecon_candidates_test.exe --out tools/rend2/candidate_pool/results/gcc.json
```

`run.py` runs the real C++ stage twice, requires identical JSON/CSV semantic
witnesses, and keeps wall-clock/stage times and peak process working set outside
the deterministic witness. No numerical package or renderer runtime is required.
Machine-readable results and per-home/class/provider/reason CSVs live under
`tools/rend2/candidate_pool/results/`. Final measured results are recorded below.

## Measured results (2026-10-29)

The final GCC and VS 2019/MSVC executables each pass **21,342 assertions** with
zero failures. The final complete standalone CTest run passes **21/21 tests**
(134.19 seconds), and the final MSVC CTest selection passes **1/1** S05 test
(3.59 seconds). Repeated runs and both compilers have the same semantic witness
SHA-256 `1c1cf427f98bbbcb423a30b17ba20d51ba2aeeb8dd8ffa47fe8f95a4261caaed`
and CSV SHA-256
`77f39296816831e162597019f87d929db179c215892900253534b7ebf55393ec`.
Results record source-file and executable hashes; the shared checkout was at
`c86d5680c74a3bbbf09235dac472d7a7b5b2ac1f` with the S04/S05 sources uncommitted.

| Fixture | Input / consolidation | First work wave | Complete eligible traversal | Permanent drops |
|---|---|---|---|---|
| 24 spatial lamp clusters, 250 independent area fragments + point + spot each | 6,048 inputs → 6,048 bundles/models | 24 point, 24 spot, 96 area; 5,904 deferred | All 6,048 in 63 waves | 0 |
| Proven single connected emitter with 2,048 exact pieces | 2,048 inputs → 1 bundle/model with 2,048 retained parts | 32 exact parts; explicitly incomplete | All parts once in 64 waves | 0 |
| Three lamps copied by two providers | 6 inputs → 3 models, 6 original references | Each lamp admitted once | All evidence/aliases retained | 0 |
| Three same-colour neighbouring lamps with proximity bridge | 3 → 3 | No proximity union | No distinct fixture collapsed | 0 |
| Two coincident but independently authored lamps | 2 → 2 | Protected overflow deferred under quota 1 | Both exposed across two waves | 0 |
| Contradictory explicitly same-source authored records | 2 → 1 bundle, 2 parameter alternatives | One alternative per wave | Original intensities intact | 0 |
| Two joint Solar explanations | 2 sun + 2 sky models | None without context; selected scenario admits its 2 models | Competing context remains deferred | 0 |

The large fixture's supplied point/spot hypotheses have 100% **input retention** and
100% first-wave point/spot admission (24/24 each). This measures neither source
discovery nor recovered physical-light correctness. The 6,000 area records are
unverified fragments, not 6,000 certified lights. The stage correctly declines to
guess how many real emitters they represent. Optional neighbour testing stops at
the declared 1,000-pair cap, reports incomplete links and retains every record.

Only 144 jobs are admitted in the first large wave (below its 192-ticket safety
ceiling). The 5,904 deferred records have `local-quota` diagnostics; zero are
silently discarded. A separate one-ticket wave test reaches all 24 spatial homes
within 24 calls. A local demand raises area admission from 4 to 7 for the requested
home. The visible-support test raises a point reservation of 1 to 3 using the
two-job coverage bonus; disabling that bonus leaves 1.

| Measured scope | GCC Release, two runs | MSVC Release, two runs |
|---|---:|---:|
| Full test-process peak working set | 25.77–25.89 MiB | 25.98–26.53 MiB |
| 6,048-input consolidation | 6.28–10.68 s | 1.70–2.73 s |
| 63 production selection calls, excluding test validation loops | 2.62–3.10 s | 0.68–0.83 s |
| 2,048-part exact-union consolidation | 1.60–1.62 s | 0.78–0.89 s |
| 64 exact-union selection calls | 0.0068–0.0072 s | 0.0023–0.0031 s |
| Full test process, including all fixture construction/assertions | 12.88–20.61 s | 4.19–5.47 s |

The large catalog's conservative allocation accounting is 57,890,544 bytes
(55.21 MiB); that accounting is deliberately different from measured process RSS.
Timings are observations on a machine with other active agents/builds, not isolated
optimizer throughput claims or comparative compiler-speed conclusions. Normal game
map load performs no work from this unhooked module.

SP and MP Release renderer targets compile and link with the new shared core.
Existing build-environment warnings about missing Git tags, repeated CPack inclusion
and x64 `/arch:SSE2` are not S05 numerical/test failures. No game/GPU visual test was
run: the task changes offline CPU work scheduling, and adds no renderer call site.

Additional executed checks: `ctest --test-dir build/lightrecon-core-renderers -C
Release -R '^lightrecon_candidates$' --output-on-failure`, `python -X utf8 -m
py_compile tools/rend2/candidate_pool/run.py`, `git diff --check`, and a source scan
for renderer/GL/cvar dependencies in the new module (none). Full CPU tests include
source decoding/capture, surfaces, classification, regions, visibility, profiles,
forward prediction, sun, hidden candidates, lifecycle/disk and offline datasets.

The final regression includes the late redundant-copy availability fix: usable
geometry survives an unavailable duplicate, but a unique unavailable union piece
does not disappear. Final source hashes in the machine-readable reports refer to
the corrected implementation, not the earlier intermediate build.

Files added: core candidate-pool API/implementation, standalone test,
`tools/rend2/candidate_pool/run.py`, README/results and this document. Shared core
and standalone CMake lists include the new module once, reused by SP/MP. Earlier
S04 changes remain separate and are not part of this stage's renderer impact.
