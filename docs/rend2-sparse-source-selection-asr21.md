# ASR21 — choose a sparse aesthetic source set

`Aesthetic::SelectSparse` in the **tool-only** aesthetic library chooses fixed
source hypotheses from a bounded candidate catalog. It reuses ASR20's modern
`Shape::Prepare` responses, source-support admission, positive bounded RGB
fitting, balanced interval/Huber objective, and frozen third-partition audit.
The historical solver, Joint, canonical asset/storage contracts and all
renderer consumers are unchanged. Every returned emitter remains a conditional
**Candidate**; exact historical lamp identity and absolute scale are unavailable.

## Inputs and assumptions

Supply an ASR20 `Request` with **empty configurations** and a `SparsePolicy`.
There is one explicit `FixtureEvidence` entry per input candidate, including
duplicate provider records. Visible evidence needs its own provenance supplied
before fitting. Residual brightness, fitted power and a supported emitting
primitive do not establish the physical existence of a visible fixture.

The candidate catalog can come from existing geometry/fixture discovery or a
bounded Joint/SceneSchedule region. The caller supplies fixed local emitter
geometry and its support, for example `Joint::Candidate::shape.initial`, and
keeps provider evidence and alternative-family scoping explicit. This route
does not invoke Joint's historical objective, move sources, generate residual
lamps, or use receiver influence rays as an admission/pruning mask. Environment
and GI transport fitting remain with their existing solvers.

Unknown source placement or forward coverage makes an affected configuration
unavailable. It does not become an empty scene or a black prediction. The same
target contract as [ASR20](rend2-aesthetic-fit-asr20.md) applies: explicitly
attested relative-linear data or fixed forward-encoded RGB8 appearance.
Encoded bytes are never calibrated linear irradiance. A shared, bounded,
training-fitted constant background is nuisance, not recovered bounce.

## Deterministic search and selection

1. Sort candidates by persistent source ID. Exact repeated IDs with matching
   forward geometry, support, bounds, fitting provenance and independent
   evidence become one amplitude block. Conflicting repeated IDs fail explicitly.
   Distinct IDs with similar responses remain separate competing hypotheses.
2. Prepare each admitted source/variant's forward column once per partition.
   Starting at the fitted no-local-light baseline, evaluate all single additions
   to the current subset. Each unique subset fits every RGB amplitude and the
   same background from training only; repeated subsets reuse that frozen fit.
3. Minimize `training.robust + trainingPenaltyPerFixture * sourceCount` along
   the greedy path. An addition must improve this objective by more than the
   declared minimum gain, prune slack and fitting tolerance. After each accepted
   addition, try every single-source deletion and fully refit; retain a deletion
   when its penalized objective stays within `pruneSlack`. Repeat deletions until
   none qualifies. Previously expanded subsets stop cycles.
4. Fit terminal one-for-one replacements as additional alternatives. This
   preserves a near-equivalent rival even when an earlier greedy choice excluded
   its branch. All proposal generation and parameter fitting freeze before any
   validation forward evaluation.
5. Compare frozen models using
   `validation.robust + validationPenaltyPerFixture * sourceCount`. The best
   improvement must beat the **same fitted nuisance baseline** by more than
   `validationEquivalentSlack + tolerance`. Select the fewest sources within
   the best score's equivalence slack, then the fewest sources lacking independent
   visible evidence, then lexicographic source IDs. Thus a hidden source wins
   when its stronger validation evidence lies outside the equivalence band.
6. Retain compact, equal-cardinality equivalent configurations as alternatives.
   Larger equivalent fits remain in `Fits()` with their raw scores, and redundant
   supersets get explicit removal receipts. Alternatives are never averaged or
   summed. A fit with a negligible training contribution from any source cannot
   be selected: `columnRms * max(relativeRGB) <= minimumContribution`. This
   threshold is invariant to rescaling the fixed relative gauge.

The third, disjoint `AuditHeldout` partition can neither refit nor select. A
training-only improvement is insufficient to introduce a lamp. Selection uses
validation, so its improvement is a model-selection result; the independent
held-out audit is reported separately.

Default search caps are 128 input candidates, 8 selected sources and 512 unique
models; hard caps are 128, 16 and 2048. ASR20's row, memory, arithmetic, ray,
variant, sweep and cancellation limits also apply, with additional catalog memory
preflight. A model/work/memory budget failure returns an error and an empty
experiment, never a silently truncated successful selection. The maximum source
count defines the search space. This bounded greedy neighborhood is not an
exhaustive search or a global-optimum certificate. Correlated geometry, unknown
materials, or spatially coherent GI may remain ambiguous despite a passing
held-out comparison; no test certifies that all such ghosts can be rejected.

`Experiment::Sparse()` records policy, assumptions, counts, training-path model
indices, accepted additions/deletion refits, exact duplicate removals, unsupported
sources, validation-redundant removals and final selected/alternative/excluded
decisions. Full training/validation losses and source diagnostics remain in
`Fits()`; `AuditHeldout` returns scores in the same order.

## CLI and reproduction

The existing tool accepts a separate protocol without changing ASR20 inputs:

```text
JKASR21-SPARSE-ENCODED-ASSUMED-MATERIAL-1
gain
backgroundUpperR backgroundUpperG backgroundUpperB
maxSources maxModels trainingPenalty minimumGain pruneSlack minimumContribution validationPenalty validationSlack policyReference
fixtureCount
point id x y z upperR upperG upperB visible|hidden evidenceReference
surface id originalSurfaceOrdinal upperR upperG upperB visible|hidden evidenceReference
... training fold, then validation fold ...
```

Use one fixture record per candidate; both point and surface forms are shown
above as alternatives. Provenance references are nonempty whitespace-free
tokens. Zero candidates is valid. Fold rows retain ASR20's
`id block surface x y z nx ny nz area rawR rawG rawB` format. The separate held-out
file opens only after selection freezes. JSON uses `org.openjk.sparse-fit.v1`
and contains the policy/evidence/decision receipt plus all model scores.

```powershell
$env:PATH = 'C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;' + $env:PATH
cmake -S tools/lightrecon_test -B build/s06-gcc
cmake --build build/s06-gcc --target lightrecon_sparse_test lightrecon_aesthetic_tool lightrecon_aesthetic_test -j 2
ctest --test-dir build/s06-gcc --output-on-failure -j 2 -E '^lightrecon_point_refine$'
python -X utf8 tools/rend2/sparse_selection/run.py --out build/asr21-reproduce
```

Use a fresh output directory. The unchanged, long-running historical position
refinement test is excluded from the broad check; this route never moves XYZ.
The runner freezes source/executable/input hashes before evaluation, verifies
every historical core and canonical type file against ASR20, and repeats owned
and pinned compiled reports byte for byte. Checked-in receipts are under
[`tools/rend2/sparse_selection/results/asr21-v1`](../tools/rend2/sparse_selection/results/asr21-v1).

Owned witnesses cover two adjacent mounted panels against one hypothetical large
uniform ceiling emitter, several lights on one surface, one light on three
surface-normal groups, a central proxy removed after separated roots enter,
duplicate geometry-derived IDs, near-equivalent replacement alternatives,
visible preference, stronger hidden-source evidence, permutation/noise stability,
48-candidate constant negatives, empty catalogs, unknown geometry and budgets.
The GI-ghost witness is a **controlled residual proxy**: training resembles a
local source while the other folds contain only a diffuse constant background.
It tests rejection of training-only structure, not general GI identification.

The final owned suite passes **486 assertions**, the preserved ASR20 suite passes
**365**, and the CPU regression run passes **56/56 tests** with the historical
position-refinement exclusion stated above. All four affected C++ translation
units also pass C++11 `-Wall -Wextra -Werror -pedantic` syntax checks.

The pinned compiled appearance witnesses select 1, 2 and 1 sources respectively;
each input also includes a geometric rival and an exact duplicate ID:

| Scene | Selected count | Held-out baseline interval MSE | Selected interval MSE |
|---|---:|---:|---:|
| `point_room` | 1 | 1764.33333 | 9.60507e-9 |
| `two_colour` | 2 | 773.65741 | 0.1125254 |
| `door_occlusion` | 1 | 2001.46816 | 0 |

These are conditional encoded appearance errors, not physical irradiance errors
or proof of the historical lamps. Original world occluders stay in force.
