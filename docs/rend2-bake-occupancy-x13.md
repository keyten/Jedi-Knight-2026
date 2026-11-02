# X13: bake occupancy is not runtime swept motion

The offline placement API now separates declared bake-scene occupancy from
runtime motion guards. Unknown mover travel is **not** a proof of collision with
every source. An explicit `ConditionalBake` request can admit a source-support
hypothesis after static-world checks, carrying bounded spatial assumptions and
unknown bake state. It cannot establish an original lamp position, historical
door state or an exact-light certificate.

This changes CPU fitting/admission only. No runtime lighting, doors, collision
gameplay, renderer shadows or consumers are enabled. Other renderer changes
already present in the shared worktree are outside X13.

## Original evidence and the error in the old interpretation

`Offline::CaptureBspSpace` reads the **original RBSP**, not live gameplay state:
model lump 7 has bounds, surface ranges and brush ranges. Model 0 identifies the
world. Every non-world record was retained as a `DynamicBoundary`, even when the
associated entity was a trigger, weather volume or nominally static brush.
The record name did not establish physical motion or optical participation.

The captured bounds are original **model-lump geometry bounds**, not measured
current game bounds, not an entity transform, and not a complete runtime sweep.
The archive has immutable draw-surface model membership, raw lightmap/style data
and material facts. It does not have a production compiler recipe or authoritative
bake state. Stored submodel lightmaps do not prove that the submodel participated
in world shadowing at any selected pose.

Source semantics checked in `code/codemp/qcommon/qfiles.h`,
`R_LoadSubmodels` and the SP/MP mover spawn code:

- Entity `model="*N"` identifies a brush model. `origin`, `angle/angles`, `lip`,
  `height`, `speed`, target chains and spawnflags can constrain gameplay setup.
- Door spawn code derives `pos1/pos2` using direction, size and lip; train motion
  depends on target entities. Rotation, scripts, team state and other model
  classes require different rules.
- Neither the model lump nor those gameplay routines identify Raven's actual
  bake-time choices. No generic endpoint/swept envelope is inferred here.
- `StaticVisibility` excludes `BrushModel`/deformed surfaces from its static BVH.
  Their omission is not proof of transparency. Known static solids and visual
  blockers remain independently queried.

Original licensed BSPs were read locally with exact inventory hash checks; only
aggregate facts are retained in
[source-semantics.json](../tools/rend2/occupancy/results/v1/source-semantics.json):

| Map | Non-world models / entity references | Non-world surfaces | With stored lightmaps | func_door | Trigger entities |
|---|---:|---:|---:|---:|---:|
| t1_danger | 86 / 86 | 65 | 60 | 9 | 60 |
| mp/siege_desert | 94 / 94 | 735 | 731 | 33 | 32 |
| hoth3 | 88 / 88 | 94 | 94 | 13 | 68 |
| hoth2 | 124 / 124 | 229 | 153 | 15 | 90 |

No model has multiple entity references in these four inputs. Several original
`origin/angle/lip/speed/target/spawnflags` keys exist; no retained `pos1/pos2`
keys were found. `hoth2` additionally has ten `misc_weather_zone` models. This
does not authorize automatically excluding all non-rendered volumes or treating
`func_static` as verified static bake participation. The conditional interpretation
preserves the missing optical/bake evidence instead.

The former placement loop returned `Dynamic` upon the first unknown swept extent,
before examining local geometry. X12 reported 86/94/88/124 such non-world records
on t1_danger/siege_desert/hoth3/hoth2. This was an unknown-data gate, not a measured
map-wide physical obstruction. The new code checks known static geometry first,
so a real wall is not hidden behind an unrelated unknown-motion diagnosis.

## Typed evidence and scope

`PointRefine::OccupancyEvidence` is a versioned CPU interpretation alongside the
unchanged archive `DynamicBoundary`. It includes original model identity, map/
geometry generation, captured bounds, separately asserted runtime swept bounds,
possible `BakeState` records, one selected state, provenance and an optional
local conditional-clear support region. It is not constructed from observation
RGB or dataset-fold assignment.

Each bake state declares Included/Excluded/Unknown participation, an explicit
generation/provenance, an optional complete **outer** support and existing
`RegionDomain`/`StaticVisibility` leases. Outer AABBs are never filled solids.
Actual occupied volume and visual intersections are queried through the existing
classifiers/BVH. `verified` is a caller attestation for that declared scene,
appropriate to independently controlled geometry; it is never set merely from a
stock classname or a good RGB fit.

State alternatives are not summed. A request selects at most one state of each
source model; duplicate selected records for one model are invalid. Open and
closed states have different keys and must be evaluated as independent scene
requests. No original stock state is selected or recovered in this task.

Two policies are explicit:

| Policy | Meaning |
|---|---|
| `RuntimeConservative` | Retains the old complete-runtime-envelope gate for callers requesting that scope. An unbounded envelope is Unknown, not a physical collision proof. |
| `ConditionalBake` | Requires explicit provenance. Static/bake contradictions still reject; unspecified bounded model states may support a local conditional source hypothesis. Runtime travel does not define bake occupancy. |

`PlacementResult` distinguishes `Free`, `Conditional`, known collision constraints,
unknown geometry/path/dynamic occupancy and invalid geometry/search bounds.
`Feasibility` reports Proven/Collision/Conditional/Unknown/Invalid separately.
The `occupancyKey` binds the policy and evidence scope. `bakeOccupancyProven` is
separate: even clearance from a known runtime sweep does not establish historical
bake participation. `Admissible` accepts only scoped Free/Conditional results;
hard errors, cancellation and resource exhaustion are never admissibility.

For an enclosing source-support box A, a conditional request asserts a local
counterfactual `A intersect opaque_geometry_at_bake = empty` only after supported
static-world tests. It does **not** prove an actual mover trajectory/state that
satisfies the assertion. Witnesses retain model/state IDs, cause, runtime-extent
uncertainty and the union of queried source-displacement/extent support. Repeated
queries merge stable sorted witnesses; observation partitions never create area.

Known captured bounds alone, including overlap, do not prove a filled collider.
Their overlap is explicitly labelled `CapturedBoundsOverlap`. A model with no
spatially certified bounds remains Unknown unless an explicit local exclusion
assumption is supplied. Missing/unbounded static geometry is not bypassed by
either policy.

## Independent visibility and fitting

`PlacementProvider` also supports the independent existing ray contract.
`WithOccupancy` narrows a supplied visibility provider; it never clears a primary
Unknown or Blocked result. Selected bake-state geometry is queried by the same
CPU BVH, and known opaque geometry remains blocked. Additional model tests use
segment semantics: only the base scene can establish an actual sky hit.

- A conditional source centre does not imply clear receiver rays.
- A ray crossing unspecified captured model support remains Unknown.
- Away from bounded captured support, conditional static-scene visibility is
  explicitly Assumed with `PolicyAssumption`; unknown runtime/bake states are not
  asserted transparent physical truth.
- An entirely unbounded model remains Unknown for rays, even if a separate local
  source-placement exclusion is requested.
- Physical-integral shape evaluation retains its strict rejection of assumed
  visibility. Supported historical recipes can produce explicitly conditional
  numeric predictions. No tolerance/normal/encoding rule is weakened.
- Passive transfer and automatic shadow uncertainty retain their independent
  policies. Selecting a placement state does not rewrite an already prepared
  transfer matrix or make unresolved optical paths available.

Joint accepts the conditional placement predicate, recomputes finite source
extent/rim/path checks, and repeats final placement before acceptance. It retains
per-source placement receipts, model assumptions and conditional status. Source
geometry changes still invalidate trace factors through the existing generation/
observation-policy keys. Moving-emitter attachments without an independently fixed
bake pose remain explicitly unsupported; there is no inferred animated transform.

Conditional models remain `Candidate`, never Resolved through convergence.
Uncertainty disqualifies exact-light suitability if any retained explanation has
unverified bake occupancy, even with physical intensity calibration. Producer
provenance, BuildAssessment limitations and diagnostics retain X13 assumptions.
Contribution membership gains `AttributionVisibilityUnknown`; prediction issues
retain `MovingPath`. Signed model discrepancy is not converted into physical GI.

## Identity, memory and lifecycle

Policy hashing includes map/domain/geometry identities, captured/runtime bounds,
policy/provenance, state sets, selection, support and state-generation/geometry
keys. Record/state ordering does not change identity. Stale geometry, absent state
selection, duplicate model states and malformed metadata are rejected.

Limits: 4,096 combined model records, 32 states per explicit record, 4,096-byte
individual provenance, 16 MiB interpretation metadata plus the caller's smaller
budget, existing per-check work/segment caps and global fitting work/ray limits.
Retained placement receipts are budgeted in Joint memory preflight. Sorted witness
lookup avoids repeated quadratic duplicate search. No second collision engine,
per-luxel dense mover matrix or runtime-dependent cache is introduced.

Algorithm keys advance: PointRefine 2, Shape 2, Joint 5, Uncertainty 2, Producer 5.
Joint's old cooperative checkpoints are invalidated. Producer settings hash the
complete occupancy interpretation, including attachments. Canonical schema stays
7; existing uncertainty/diagnostic/provenance mechanisms carry the limitations.
The immutable `.jkobs` 1.0 record layout and prior frozen X12/v4 results are unchanged.

## Executed evidence

The [native test](../tools/lightrecon_test/lightrecon_occupancy_test.cpp) reuses the
owned S10/S11 geometry/forward/trusted validation infrastructure. It covers distant
known/unknown runtime bounds, overlapping captured AABBs versus known occupied
bake geometry, verified and assumed open/closed states, a real static wall,
unbounded model support, independent unknown rays, incomplete region graphs,
attachment rejection, stale generations, deterministic ordering, cancellation,
corruption and memory/work exhaustion.

The positive controlled historical-profile lamp executes actual Joint fitting and
strict production forward evaluation. Its amplitudes and held-out targets are
not used as fitting initialization. The result is conditional and retains
`MovingPath`; held-out improvement over black is required. The original negative
distant-unknown-motion witness remains reproducible under `RuntimeConservative`.

Versioned numerical results, stock before/after tables, checks and exact commands
are linked from [the tool README](../tools/rend2/occupancy/README.md). No stock XYZ,
original source count, physical SI error or historical mover-state accuracy can
be measured without independent truth. Missing final predictions remain unavailable.

### Stock progression and the next actual blockers

The new frozen `x13-a` run uses all four requested maps and two independent assumed
NetRadiant profiles. Original all-page archive/BSP hashes, X12 fold/gap policy,
four Joint calls/profile, 12 models/call and local/shard/solver budgets are
retained. No geometry, source or work policy is tuned using final held-out RGB.
This is a new code revision following intervening research/producer work, not a
claim that X12 and X13 catalog identities are identical. The original v4/X12 files
and executable witnesses remain unchanged.

| Map | Profile | Source proposals attempted | Old X12 placement stop | New conditional placement queries | New collision queries | New source forward calls | New stop |
|---|---|---:|---|---:|---:|---:|---|
| t1_danger | isq / linear | 8 / 8 | Dynamic, boundary 1 unknown sweep | 0 / 0 | 8 / 8 | 0 / 0 | Proposed centre classified Solid by world domain |
| mp/siege_desert | isq / linear | 8 / 8 | Dynamic, boundary 1 unknown sweep | 0 / 0 | 8 / 8 | 0 / 0 | Centre/support intersects retained visual geometry (Clearance) |
| hoth3 | isq / linear | 8 / 8 | Dynamic, boundary 1 unknown sweep | 0 / 2 | 8 / 6 | 0 / 2 | Clearance; linear conditional evaluations lack 12/64 comparable rows |
| hoth2 | isq / linear | 8 / 8 | Dynamic, boundary 1 unknown sweep | 8 / 6 | 0 / 2 | 8 / 6 | Conditional evaluations lack 16–17/64 comparable rows; one linear candidate Solid |

Counts are attempted checks/proposals including repeated waves, **not unique
lamps, recall or accepted source counts**. No source is marked bake-proven in
the stock experiment. Each profile has four real Joint calls; across the eight
map/profile branches this is 32 calls, 64 nonempty source proposals, 16 actual
source forward calls and 608 fitting rays. The no-source/boundary baselines also
finish and are independently validated, but cannot pass positive source recovery.
All eight branches have zero final held-out calls, zero accepted nonempty source
fits, zero uncertainty/publication stages for a stock winning model and no
canonical stock asset. Their final prediction error is Unavailable, not zero.

`scheduler-*-occupancy.json` additionally compares **the last wave's two catalog
centres** on identical geometry, before/after policy. For hoth2 isq both become
Conditional; hoth2 linear and hoth3 linear each have one Conditional/one Collision;
the remaining last-wave pairs collide. Full Joint counters in the table include
all waves and finite-support checks. The historical global guard would reject
every one of those last-wave centres solely due to unknown runtime motion. The
new conservative-runtime policy checks actual static geometry first and therefore
can report those collisions before the dynamic-uncertainty gate. Do not confuse
last-wave centre diagnostics with complete finite-emitter or map certification.

The now-visible remaining issue has a precise scope: Joint still wraps direct
queries through `Indirect::Transfer::QueryPath`. That independent provider returns
Unknown for non-geometrically-blocked rays when any supplied runtime swept extent
is unknown. The new occupancy layer **never clears an upstream Unknown**. Thus
conditional centre placement is no longer the global blocker, but positive source
rows may still be unavailable to that unchanged path operator. Some rows can
remain comparable zero under the declared influence cutoff; they do not provide
positive direct-light evidence. Full required-row admission rejects the model.

This task deliberately does not reinterpret an already prepared passive transfer
matrix as a different bake scene or delete its unknown-path flags. A future
independent bake-state visibility/transport branch must supply consistent geometry,
state provenance and regenerated dependency keys before those rows can be admitted.
It must not simply reuse historical captured bounds as complete runtime sweeps.
For the authored surface candidates rejected by Clearance, a verified mounting/
emitting-side/extent contract is needed; the result is not evidence that their
actual physical fixtures could not contain a lamp.

### Numerical controls and resource measurements

The dedicated X13 test passes **45 assertions** twice with identical numerical
receipts. Distant unknown motion under `RuntimeConservative` rejects the original
controlled witness; `ConditionalBake` reaches **461 production forward calls**
and two conditional placement queries. Strict held-out interval MSE is 0 on the
positive owned fixture and improves over its black baseline. This is fixed supplied
XYZ/source type and blind amplitude fitting, not source discovery. Verified closed
door geometry blocks its ray; the explicit open state permits it. An assumed open
state returns Assumed/PolicyAssumption; unspecified overlapping or unbounded ray
support remains Unknown. Known floor/solid collisions remain rejected.

Native repeat costs .610/.812 seconds and peak RSS 5,140,480/5,144,576 bytes.
All four stock processes complete within their frozen 600-second/1-GiB caps:

| Map | Local jobs/profile | Whole process seconds | Peak process RSS MiB |
|---|---:|---:|---:|
| t1_danger | 89 | 81.063 | 47.85 |
| mp/siege_desert | 58 | 33.281 | 58.72 |
| hoth3 | 853 | 46.578 | 101.95 |
| hoth2 | 811 | 19.156 | 66.30 |

Times include archive scans, geometry/transport preparation, local source search,
two profile branches and diagnostics. Other builds/tests ran on the same machine;
these are actual measured process costs, not isolated optimizer microbenchmarks.
Accounted scheduler memory and ray/work counters are separately retained in JSON.
Stock TSV/OBJ receiver/source coordinates and licensed archive payloads stay in
the private temporary directory; public reports retain their hashes.

The three existing real compiled controls are rerun through the production
producer/cache/query boundary. `point_room`, `two_colour`, `door_occlusion`
complete with held-out interval MSE **0 / .222869236135 / 0**, versus black
**2495.88888889 / 1170.60185185 / 2495.88888889**. Wrong-profile controls remain
DiagnosticOnly with unavailable final errors. All 264 available contribution
queries match their serialized values, including 18 explicit occluded zeros.
The repeated two-colour archive hash is identical. These use supplied geometry
and known compiler recipes, not stock historical source truth.

Full CPU suite: **38/38 pass**, 314.97 seconds. SP/MP MSVC Release builds pass.
Python tests: 7 blind-view, 4 producer-controller and 3 X13 protocol/parser tests
pass. No game or runtime visual test is needed or launched. Existing unrelated
renderer edits in the shared workspace were present at task start and are not
part of this patch; no shader baseline is rewritten.
