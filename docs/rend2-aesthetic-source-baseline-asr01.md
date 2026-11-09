# ASR01: aesthetic source-discovery baseline

2026-11-09. Input revision `f83dc0604` on `rend2-unified-wip`.
**The small blind point-source proposal baseline succeeds; scene-aesthetic
readiness fails and historical-source certification remains NotCertified.**
All three controlled local roots have a proposal within 32 WU, the compiled
no-local-light sky control produces none, and stock data are available and tested.
No accepted nonempty discovery configuration is established. A nearby proposal,
an empty converged model and a completed process are different outcomes.

The workspace is `OpenJK-rend2`; its configured origin is
`git@github.com:keyten/BetterJK.git`, although the task names
`keyten/Jedi-Knight-2026`. This report identifies the actual checkout and revision;
no repository remote or branch was rewritten. The pre-existing modification to
`rend2-shadow-evidence-s09.md` is outside this change.

## Frozen evidence and reproduction

[Commands and input preparation](../tools/rend2/aesthetic_baseline/README.md),
[pre-run protocol](../tools/rend2/aesthetic_baseline/results/v1/protocol.json),
[measured results](../tools/rend2/aesthetic_baseline/results/v1/results.json),
[stock stage audit](../tools/rend2/aesthetic_baseline/results/v1/stock.json), and
[artifact hashes](../tools/rend2/aesthetic_baseline/results/v1/manifest.json)
are the reproducible baseline. The
[policy](../tools/rend2/aesthetic_baseline/policy-v1.json) was written before the
measurements. It adds a report-only aesthetic assessment; production gates and
historical assertions retain their previous meanings and thresholds.

The positive/negative set is `point_room`, `two_colour`, `skylight`.
Before replay, freeze the sanitized geometry, original compiled BSP, training
request, training/validation/final partition files and manifests, owned answer
hashes, native binaries, code and prior frozen-manifest hashes. Evaluation root IDs
are `BSP_SHA256/local-light/original_ordinal`; production proposal IDs remain separate
and are exported without renumbering. These controller identities do not change
canonical source IDs or storage. Stock truth and historical lamp IDs are unknown.

| Input | Original BSP SHA-256 |
|---|---|
| point_room | `890a15b75b0ae24ddad2725ba24327f7e1949bf17d0e7fd6baef100a59feb2af` |
| two_colour | `0969f5a631e5a9bac95224f1831df1587bad62f883761c89b92231119eef6ec1` |
| skylight | `b6bdb70e3f5de43f13bab50774a36aff9b024fc0731e71f1bf9235446981c15e` |
| hoth3 ROI | `821e6413f10e1e4aca7b6a5fd1bc57a6731bce5d9075cb0175208915cee5cb32` |
| mp/duel4 all-pages | `6575914ae34291ae645889a14650184157774a83f094e3fc43feed2a88ff65f0` |

Owned full folds are 352/176/176 train/validation/final, seed 20261032,
256-WU blocks, 1-WU gap plus three footprint radii. The hoth3 captured-view folds
are 125944/11364/37896, seed 20261026, 1024-WU blocks, 8-WU gap plus footprint
margin. These are distinct older captures; neither is substituted for the later
stock scene sampling. mp/duel4 uses the unchanged production geometry-selected
113/93/72 schedule and geometry-only common support of 95/78/59. Full files and
selected 64-row discovery IDs are pinned, including rows excluded by the native
provider. No evaluation RGB chooses receivers, proposals, profiles or tolerances.
The controller hashes evaluation files at freeze; it does not feed their values
to Hidden. Owned answers open only after proposal outputs freeze.

The baseline explicitly reuses the S04 default `JKHIDDEN1` policy: two separate
conditional falloff hypotheses, 384-node cap, 4 coarse divisions, 2 refinement
rounds, point and spot trials, default four refinement tickets. It does not enable
the opt-in X16 geometry-axis/balanced-eight policy. Fixtures use geometry-selected
receiver bounds expanded by 128 WU; stock uses the pre-existing hoth3 centre
(3320,700,123), radius 512 WU. These bounds are declared search support, not proof
that all plausible sources lie inside them. Candidate budgets are offline costs.

## Current path inspection and previous evidence

The [V02 report](rend2-reusable-lighting-validation-v02.md),
[X12 stock fits](rend2-stock-joint-fits-x12.md), their pinned manifests and the
[X16 discovery report](rend2-blind-discovery-x16.md) establish the older stop points.
V02 medium-bridge recall was 2/3; X16's opt-in policy reached 3/3 full-pool recall
but still lacked an accepted complete configuration. The narrow spotlight has
zero positive validation observations in that frozen bridge. Those larger
experiments are reference evidence, **not rerun or promoted by this small task**.

Current production boundaries inspected:

| Path | Responsibility and remaining boundary |
|---|---|
| `lightrecon/hidden_candidates.*` | Surface-response geometry hypotheses, explicit profile/encoding assumptions, finite local search; unknown rays excluded from shape comparison, never declared empty |
| `lightrecon/evidence.*`, `emitter_support.*`, `candidate_pool.*` | Authored shader/entity/emissive support and inferred observations retain provenance; illuminated receivers do not become mounting fixtures |
| `lightrecon/joint.*`, `joint_support.cpp` | Placement/clearance, mandatory comparable coverage, bounded structural and amplitude fitting, independent validation; no asset publication |
| `lightrecon/producer.*` | Provider coordination, uncertainty/final receipt and canonical publication; `Completed` alone is no discovery certificate |
| `lightrecon/sun_fit.*` (`Solar`) | Separate sun/sky hypotheses, sky visibility and validation; `UnknownSun`/unresolved environment remain explicit |
| `lightrecon/offline*`, `decode.*`, `surfaces.*`, `receiver_support.*` | Original evidence identity, mapped receiver eligibility and conditional forward capability; admission does not guarantee a supported source-dependent path |

All paths are under `shared/rd-rend2`. Encoded RGB8 is anonymous encoded-channel
evidence under conditional compiler recipes, not calibrated linear irradiance.
No current PBR albedo is divided out to invent a light. Stock transfer, material
response, absolute units and original compiler settings remain unresolved.

## Fresh measurements

| Case | Training requested/used | Proposals | Nearest local-root distances, WU | Recall at 32 / 64 WU | Semantic repeat |
|---|---:|---:|---|---|---|
| point_room | 64/64 | 16 | 14.32655 | 1/1 / 1/1 | equal |
| two_colour | 64/64 | 38 | 14.32655, 8.78920 | 2/2 / 2/2 | equal |
| skylight, no local lamps | 64/64 | 0 | no local roots | N/A / N/A | equal |
| hoth3 declared ROI | 64/58 | 16 | stock truth unavailable | Unavailable | equal |

Both positive fixtures pass the predeclared 80% proposal-neighbourhood recall
gate. The denominator is authored local point roots, not all exploratory
hypotheses. Nearest-pool distance does not measure final source count, type,
photometry or accepted-emitter precision. All 70 nonempty proposals across these
cases are `AmbiguousSourceOrTransport`; none has a calibrated existence estimate
or accepted fitted intensity. Accepted source IDs are empty.

The compiled `skylight` fixture has sky/sun illumination and zero local light
entities. Native Hidden returns `Unavailable` with explicit **no within-material
positive surface contrast**, zero nodes and zero forward calls. This passes the
limited no-local-proposal control, not a claim that a sky model was fitted. It is
a low-contrast negative; it does not establish rejection of every reflected
hotspot. The unchanged analytical controls separately retain six transport-only
proposals for `bright_doorway_transport`, six ambiguous proposals for unlabelled
bright transport, and `UnknownSun` for `unmodelled_reflection`. These are analytic
controls, not additional compiled reflected-light source-recovery successes.

| Search case | Nodes | Known-solid / outside rejections | Unknown-region / unknown-relation counters | Forward calls / rays |
|---|---:|---|---|---|
| point_room | 144 | 4 / 52 | 144 / 1056 | 1584 / 33792 |
| two_colour | 256 | 4 / 52 | 256 / 2400 | 3600 / 76800 |
| skylight | 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| hoth3 ROI | 128 | 24 / 16 | 128 / 4182 | 1584 / 16182 |

Unknown-region counts describe unavailable region assignment, not a count of
known-empty or known-solid cells. Relation counters include tested hypotheses;
they are not unique failed receivers. Owned geometry reports global capture
completeness, but that does not certify every ray's optics. hoth3 additionally
reports incomplete geometry. Its six saturated training samples are censored
and not used by this provider. Every search reports `search_complete=false`;
finite support, sample/refinement limits and missing contrast remain visible.

## Stock stage blockers

The licensed stock gate was **executed**, not skipped. mp/duel4's all-pages
dataset SHA-256 is
`8173ddaa017c87dd4b341d240f95f6a99599f894d4164d0ab7902bc70b2ac3c5`.
The same two profiles and strict common-coverage rules as the current production
adapter run independently, with conditional bake occupancy and direct-only fitting.

| Stage, per profile | Outcome |
|---|---|
| Local scheduling | 20 calls, 4 proposal jobs, 16 `NoSupportedEvidence`; incomplete local/candidate coverage |
| Candidate capability/admission | 85 unsupported alternatives; 1849 deferred input records, not 1849 missed physical lamps |
| Joint | 4 calls, 12 models, 18 fitting evaluations |
| Source placement | 8 checks: 2 proven, 6 collision/clearance failures |
| Source-bearing forward coverage | isq 55/64; linear 60/64; both unavailable and rejected |
| Model outcomes | 4 no-source baselines converge; 6 source attempts fail clearance; 2 fail mandatory coverage |
| Validation / uncertainty / final | 4 validation callbacks, 0 uncertainty attempts, 0 final callbacks |
| Canonical nonempty publication | 0; `canonical_asset_available=false` |

The actual missing-ray issue is `PatchApproximation`: nine missing receivers for
each of two isq attempts and four for each of two linear attempts. The diagnostic
factor-receipt census records 55 available/18 unavailable and 60/8 across repeated
attempts; do not confuse those repeated receipts with 64 unique observations.
The 85 capability failures explicitly retain unsupported shape/transport/fragment
unions. Clearance failures do not prove that authored boundary-mounted lamps are
impossible; the current source-side support contract does not admit those trials.

Receiver census: 19842 mapped records, 9477 supported, 6524 nonplanar,
616 geometry unavailable, 863 origin-solid and 2362 origin-boundary. Supported
chart area is about 62.74% of mapped area. The requested geometry schedule loses
46/278 records to the fixed common support set; the subsequent source-dependent
unknown paths still reject candidates. These exclusions are neither successful
dark predictions nor physical transport area.

Passive transport preparation retains 1616 patches and 159 links, 22 known
positive links, with 140 uncertain links and 270126 unknown rays. Its pair search
hits the local work budget; patch set/pair coverage are incomplete. These categories
can overlap and must not be summed as disjoint outcomes. It does not supply a
complete GI explanation. Solar remains unresolved; no stock source-existence,
wall-leak, final lightmap-error or SI accuracy measurement is available on an
accepted model. A no-source baseline cannot turn the 94 positive common training
rows into evidence that the scene has no lamps.

## Aesthetic acceptance policy

Apply the frozen policy to each declared evaluation scope. Report corpus execution
coverage and fixture-source coverage separately. A small control suite passing
does not certify a stock scene; required-stage completeness includes the declared
stock scope. Any required unmeasured quantity is **Blocked**; a measured violation
is **Failed**. Overall failure does not erase individual blocked gates. Missing
stock data must leave `stock_scene_evaluated` Blocked, even if owned controls pass.

| Outcome | Definition and acceptance requirement |
|---|---|
| Fixture execution coverage | Executed required scenes / frozen scenes, including both positives and the negative; all three mandatory |
| Local proposal recall | Authored local roots with a frozen proposal within 32 WU / required roots, per positive scene; >=80%; also report 64 WU sensitivity without changing selection |
| Accepted fixture coverage | One-to-one matching of accepted local sources to independently known positive roots or independently inventoried visible fixtures; same source kind and <=32 WU for these point controls, >=80% coverage where truth exists; never infer stock lamp recall from RGB |
| Source-to-fixture consistency | Fraction of accepted sources satisfying independently supplied free-space/fixture-side support, extent, orientation and attachment rules; 100%; bare point controls use known free space and do not invent a visible luminaire |
| False emitters | Unmatched accepted sources in owned truth scenes, and every accepted local source on no-local-light controls; zero; exploratory far-away/transport proposals counted separately |
| Wall leaks | False-clear source/receiver paths across independently known opaque walls: zero, on a nonempty evaluated path set; unknown paths remain unresolved, not clear or black |
| Spatial lightmap agreement | Fixed geometry-only spatial blocks, area/correlation-aware weighting, exact same mandatory receivers and profile for every alternative; final interval MSE <=1 code squared, <=2% of black MSE, worst-block RMSE <=4 codes |
| Positive evidence | At least two independent positive blocks, black-reference interval RMSE >=4 codes, at least one accepted local source, and accepted nonempty models in both mandatory positive scenes |
| Completeness | Every required source/receiver/configuration stage measured; zero missing mandatory predictions and zero unresolved required geometry; report searched support, omitted evidence, budgets, unsupported area and deferred alternatives |
| Reproducibility | Frozen IDs, hashes, profiles, folds and requests; identical proposal parameters/support/IDs/counters on repeat; timings/RSS excluded from semantic equality and retained separately |

The spatial thresholds retain V02/X16's absolute and black-relative final error
requirements and add an explicit 4-code worst-block aesthetic cap for future
measurement. They do not weaken historical gates. Compute interval errors in the
declared encoded observation domain, including saturation/shared-peak rules;
do not add or subtract encoded RGB as physical energy. A global pixel average
cannot conceal a failed room, wall side or normal group. Source count, type,
source-removal and sky/passive-transport alternatives must preserve the same
evaluation support; source-removal diagnostics freeze before final RGB access.

The report-only policy is intentionally non-vacuous: perfect error on an empty
scene cannot replace positive cases; zero accepted sources fails; a black predictor
on a positive scene has black-relative MSE 1 and fails the <=0.02 requirement.
Unavailable errors remain null. A successful policy witness yields **aesthetic
readiness only**. Historical certification separately requires independently
verified source identity, compiler/profile, material/transfer and photometric
evidence. No value of this aesthetic score grants that certificate.

## Verification and baseline disposition

The unchanged native Hidden, Solar, Joint and Producer suites pass with
1992/17631/251/566 assertions respectively and zero failures. These include
geometry/unknown optics, sky/reflection ambiguity, physical rejection and
publication controls. They are not source-discovery successes on stock maps.
Six new acceptance counterexample tests pass. All four discovery requests repeat
semantically; process commands/resources and all native suite records are retained.
The [verification receipt](../tools/rend2/aesthetic_baseline/results/v1/checks.json)
also checks dependency/artifact hashes and an independent mp/duel4 confirmation:
graph keys, model attempts, receiver failures, controller states and work counts
match. Raw wall time/RSS are measured on a shared host and are not hard limits or
an isolated performance comparison.

Disposition: **proposal coverage Passed on 2/2 blind positive fixtures (3/3 roots),
limited no-local-proposal control Passed on 1/1 compiled control; accepted
discovery configurations 0; scene-aesthetic readiness Failed; historical
certification NotCertified.** Accepted-source/fixture consistency, false-emitter
rate, wall leaks and final spatial RGB agreement are unmeasured/Blocked rather
than silently zero. Stock capture/execution is available; stock source-model
acceptance is blocked by the measured support failures above.

This change adds only offline evaluation tools, a policy and evidence documents.
It does not modify Hidden, Joint, Producer, Solar, historical fitting, existing
strict tests, canonical asset/storage schemas, renderer/world shading, GPU/runtime
consumers or lighting composition. It makes no double-lighting change.
