# ASR22 — bounded discovery after fitting plausible sources

`Discovery::Discover` in `tools/lightrecon_test/iterative_discovery.{h,cpp}` is
an opt-in **tool-only** controller. It reuses the existing modern
`Joint::Refit`, `AssessHypothesis`, `EvaluateFrozen` and source-linked
`Indirect` evaluator. It does not change Hidden, Joint's historical residual
search, the ASR20/21 objective, canonical assets/storage, production scheduling,
world shading or runtime consumers. All added local lights remain conditional
**Candidates**, with no physical-existence or exact historical-source certificate.

## Domain and attribution contract

Supply one modern `DiffuseIntegral` hypothesis, fixed geometry and material
parameters, a training reader, an initial plausible model, disjoint validation
rows, and independently sourced region evidence. The target must be explicitly
supported relative incident data or forward-encoded appearance under one fixed
transfer and comparable receiver response. The controller does not calibrate
stock RGB8, infer unknown albedo, select a historical bake recipe, or invert
encoding. `comparisonSupported` and `profileSeparated` default to false. If
either is false, there are **zero evaluator calls and no numerical residual**.
These flags require independent evidence/explicit experimental assumptions;
lower fit error cannot establish them.

Current source RGB amplitudes are jointly refit from training. The reused
evaluator recomputes each source's primary field and its passive bounce with
that amplitude, includes identified sun/sky/boundary roots, sums contributions,
and only then applies any declared encoding. A positive interval discrepancy in
encoded mode is an **appearance discrepancy**, never an additive lamp power.
It ranks full training refits rather than being inverted into source RGB.

Training receivers are grouped by their supplied region and fixed world-space
cell. Each receipt records the observations, spatial baseline, block support,
classification, evidence provenance and reason:

| Classification | Gate |
|---|---|
| `UnsupportedGeometry` | Incomplete unbounded capture, unavailable/conditional paths or receiver prediction |
| `Unexplained` | Unsupported comparison/profile, censoring, insufficient coherence, or absent independent local cue |
| `SunAmbiguous` | Solar alternatives have not been independently separated |
| `IndirectPossible` | Unseparated transport, omitted/unknown links or materials, or reflecting materials without passive propagation |
| `CandidateLocalDirect` | Uncensored supported positive structure across at least three rows/two blocks and a spatial baseline, plus independent local direct evidence |

There is no automatic promotion of unknown energy. A fixture aperture, bounded
independent shadow/direction evidence, or another independent cue may authorize a
conditional search. Bright residuals alone cannot set `localDirectCue`.
`indirectSeparated` must account for plausible omitted transport; successful
one-bounce fitting is not proof that higher bounces are absent. A separately
attested direct-only experiment may bypass passive requirements; this is not
inferred from low observed bounce. Unknown optical paths still fail admission.
Saturated/censored structures cannot initialize sources, although their intervals
remain in the full fit and validation objective.

## Deterministic proposal/refit loop

The caller can supply geometry hypotheses adapted from Hidden/fixture discovery
or bounded `SearchVolume`s. Volumes contain a geometry-only point/spot prototype,
an independently declared position box, and a fixed 2–4 division lattice. At
most eight volumes and 128 combined poses are allowed. Pose IDs are derived from
map identity, prototype ID, bounds and lattice coordinates. Poses of one volume
share an exclusive family, so two alternatives cannot become duplicate lamps.
Mounted surfaces are never translated by this lattice. All poses still undergo
the existing source-support, occupancy, path and forward-coverage checks.

1. Assess support and jointly refit the current model. Evaluate its frozen
   validation baseline. Stop on unsupported coverage; missing geometry is not a
   black prediction.
2. Evaluate the complete current prediction on training. Only
   `CandidateLocalDirect` cells authorize hypotheses associated with that region.
   Stable candidate-ID order bounds enumeration; candidate order permutations
   do not change the result.
3. Refit at most `fitsPerIteration` single-source additions jointly with all
   existing amplitudes. Geometry/material fitting and Joint's residual/split/
   merge searches remain disabled. Reject conditional source occupancy,
   negligible power, power-bound hits, and gains that do not pay the declared
   source cost and training margin.
4. Freeze the training-ranked batch. Independently evaluate at most
   `proposalsPerIteration` models against validation. Require complete matching
   receiver denominators, required-block checks, no block regression beyond the
   fixed slack, and improvement over the **incumbent**, including the explicit
   per-source cost. Keep at most one proposal per round.
5. Recompute residual after acceptance. Stop at no admissible residual, exhausted
   candidates, candidate-fit limit, no validated improvement, source limit or
   iteration limit. Each receipt explicitly says stop or continue.

Defaults are four rounds, 64 candidate poses, eight training addition fits and
two validation proposals per round, eight sources. Hard caps are eight rounds,
128 poses, 32 fits/proposals and 16 sources. Every round copies the fixed search
and evaluator options and records calls, evaluations, rays, work and model IDs.
The first round makes at most `4 + fits + proposals` evaluator calls; subsequent
rounds make at most `1 + fits + proposals`. Per-call Joint limits bound work,
rays, evaluations and memory; snapshot/model memory has a separate preflight.
Wall-clock stopping is disabled. Cancellation and exhausted evaluator budgets
return an error and an empty experiment, never a partially certified selection.

The validation fold is reused adaptively within the predeclared round cap. It is
model selection evidence, not an unbiased final score. `AuditHeldout` opens a
third partition only after freezing and cannot refit, propose, accept or rerank.
Sample IDs, spatial block IDs and nonzero correlation IDs must be disjoint across
all folds. The experiment snapshots training/validation rows and exposes const
diagnostics. A changed held-out target does not change the selected model.

## Validation and reproduction

```powershell
$env:PATH = 'C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;' + $env:PATH
cmake -S tools/lightrecon_test -B build/s06-gcc
cmake --build build/s06-gcc --target lightrecon_discovery_test -j 2
python -X utf8 tools/rend2/iterative_discovery/run.py --out build/asr22-reproduce
ctest --test-dir build/s06-gcc --output-on-failure -j 2 -E '^lightrecon_point_refine$'
```

Use a fresh output directory. The runner freezes source/binary hashes before
evaluation, verifies every historical core and canonical type file against the
ASR21 receipt, repeats the owned report byte for byte, and checks frozen inputs
again afterward. Checked-in results live under
`tools/rend2/iterative_discovery/results/asr22-v1`.

Owned witnesses cover a lamp behind a housing roof, a geometry-only housing
volume, a real occluder clipping a smooth spot, two differently colored lights
requiring successive rounds, and fixed forward-encoded gamma comparison.
Positive recall is audited against withheld synthetic answers and disjoint
spatial receivers. The spotlight declares black reflectors/direct-only transport
explicitly; it does not bypass source visibility.

Negative controls cover an actual source-linked red bounce, the same bounce
omitted from the model, a reflected doorway with captured jambs, a traced fixed
solar field, an unseparated solar field, profile ambiguity, saturation,
training-only structure, absent independent fixture evidence, and missing
geometry. Additional checks cover source complexity, round/candidate/evaluation
caps, duplicate IDs, cancellation, leakage, candidate permutation and a changed
held-out fold. The red-bounce decomposition retains one original primary root;
no bounce is converted into a synthetic emitter.

The final owned run passes **397 assertions** and repeats byte for byte.
Supported source recall increases from **0/4 to 4/4** across the hidden-housing,
occluded-spot and two-round scenes; **zero new lights** enter the ten negative
controls. The additional housing-volume and encoded experiments also recover
their supplied conditional source hypotheses. The selected CPU regression set
passes **57/57 tests**, excluding only the unchanged long-running historical
position-refinement test. Both new translation units pass C++11
`-Wall -Wextra -Werror -pedantic` syntax checks.

| Owned witness | Held-out initial interval MSE | Held-out selected interval MSE |
|---|---:|---:|
| Hidden housing | 0.0998790 | 1.31e-12 |
| Housing search volume | 0.0998790 | 1.33e-12 |
| Occluded spot | 0.00438209 | 2.74e-14 |
| Two successive discoveries | 0.830618 | 1.08e-8 |
| Fixed forward-encoded appearance | 1542.47 | 0 |

The last row measures conditional RGB8 appearance error. The other rows measure
the owned experiment's relative incident domain; these units are not comparable
across rows/domains and are not calibrated physical irradiance.

This is a bounded local heuristic and conservative attribution policy. It is
not an exhaustive spatial search, a stock-map source-recall result, or a general
proof against coherent GI ghosts. Accuracy depends on the supplied geometric
search coverage and independent profile, solar and transport evidence. Unknown
cases stay unexplained/ambiguous instead of being fitted away as lamps.
