# AR-35 — blocked before multi-fixture integration

**2026-11-10 (Australia/Sydney). Task status: Blocked. Outcome:
Blocked-MissingDecision/SourceSupport. Aesthetic gate: Unknown.**

Inspected revision `25bd45803c8918b5059a3add56f123ac1f81f4c6` on
`rend2-unified-wip`, origin `git@github.com:keyten/BetterJK.git`. The supplied
checkout is the authority for the historically named Jedi-Knight-2026 project.
The starting worktree and index were clean. No applicable repository or ancestor
`AGENTS.md` was found. Concurrent untracked `tools/rend2/stock_lights/` appeared
during verification and was left untouched. No files were staged or committed.

## Required decision is absent

The current [AR-34 decision](viability-decision.md) and unchanged
[frozen policy](../../tools/rend2/aesthetic_relighting/policies/viability-v1.json)
require an AR-41 causal receipt that explicitly authorizes this bounded
integration. The policy remains `FrozenBlocked`, with
`integration_authorized: false`. The plan's named prerequisite,
`docs/rend2-relighting/contact-plane-diagnosis.md`, does not exist; no AR-41-named
artifact was found in the inspected docs/relighting trees. This task does not
invent that decision or perform the separate diagnosis.

The documented dummy-zero probe was repeated through the unchanged native
fitting/admission path. Both singleton configurations and their joint
configuration remain **unavailable** with
`Named mounting surface absent at declared contact plane`. A nuisance-only
dummy baseline has no meaning as an original-lighting quality score. The
previous float-normal explanation remains an unproved hypothesis.

## Operation map and plan result

| Required operation | Existing path to reuse | Remaining work and result of this task |
|---|---|---|
| 1. Inspect/reuse current work | ASR24/25 manifests, accepted and failed witnesses; `stock_aesthetic.run.check_frozen` | Completed inspection/integrity checks. Existing one-source successes are credited once; they do not complete this pilot. |
| 2. Evaluate independently supplied fixed fixtures together | `run.fixture_lines` supports per-fixture positions and mounting surfaces; native CLI accepts 1–4 fixed fixtures and enumerates all nonempty subsets | No new fitter is needed. A pilot adapter must consume the exact frozen housing inventory and receiver IDs, retaining individual component masks, families/approximations and fixed poses. Blocked before implementation. |
| 3. Same native response and support assumptions | CLI coverage uses `MakePlacement`/`WithOccupancy`, `Shape::Prepare` and `Block::Evaluate`; `aesthetic_fit.cpp::Prepare` additionally calls `SourceSupport::Check` | Fresh geometry replay succeeds, while native source admission refuses. Coverage cannot stand in for admission. No tolerance/guard change, free-point substitution or source relocation was made. |
| 4. Frozen receivers/folds and joint RGB fit | Original geometry-only AR-34 membership; `Aesthetic::FitAndValidate` already fits RGB and one bounded background, selects on validation and audits disjoint held-out data | Four-role IDs/blocks and positive-source counts verified. Existing `prepare` searches a single receiver/material design and must be bypassed only by the frozen explicit multi-chart membership. `rows_text` recomputes the old split, so the adapter must preserve the frozen block IDs explicitly. No pilot target fit was run. |
| 5. Save/reload parameters and removals | Native result exposes actual source parameters/support diagnostics; `run.evaluate` writes `hypothesis.json`; `stock_aesthetic.audit.audit` reloads parameters, replays every selected source with native coverage/encoding and removes all sources without refitting | Extend this existing research packet with complete multi-fixture provenance, all-fold per-receiver predictions and individual removals/own-influence diagnostics. Geometry-only replay is conditional on already admitted, frozen sources. No selected pilot parameters exist to replay. |
| 6. Spatial/visual packet | `stock_aesthetic.report.report` already draws all supplied footprints/points and exports OBJ; `stock_audit.packet.build` provides original XYZ context; `stock_audit.response.build` provides registered original/on/all-off chart samples | Extend these views for frozen three-chart extents, normals, individual-off variants, residuals and missing support at fixed presentation. The current response helper handles accepted old regions and only on/all-off variants. No pilot appearance plate or separate review was fabricated. |
| 7. Regression, replay and budget | Existing native tests and stock protocol counterexamples; immutable AR-34 dummy and coverage inputs | Eight native tests and ten Python tests passed; four stock native probes match AR-34 stdout byte for byte. No original-target cycle, repair or production integration started. Full saved-parameter/removal/review gates remain unavailable. |

`Aesthetic::SelectSparse` is also already implemented: bounded training-only
proposal/prune/refit, then validation selection with held-out audit after
freezing. It is not needed to replace the existing exhaustive 1–4-fixture path.
The historical ASR24/25 grid gate and acceptance logic remain unchanged; any
eventual pilot adapter must use viability-v1's separate frozen criteria.

## Fresh verification

The [machine receipt](../../tools/rend2/aesthetic_relighting/results/ar35-v1/evidence.json)
records 1,038 assertions, including individual hash comparisons and preservation
rechecks. These are provenance/preflight assertions, not aesthetic gates.

| Check | Measured result |
|---|---|
| Public manifests | ASR24 4/4, ASR25 6/6, AR-34 5/5 entries match |
| Preserved private packets | ASR24 209/209, AR-34 42/42 selected artifacts, 12 accepted-witness references match |
| Implementation/tools | ASR25 150/150 files, including 113 historical/core/canonical files; six native tools match |
| Selected scope inputs | Catalogue, exact scope, input-path record, original row-file identity, implementation protocol and original capture hash match; pilot RGB values were not parsed |
| Frozen four-role folds | No duplicate/cross-role IDs or blocks; row/block/positive-source counts match policy |
| Dummy native admission | All three local configurations unavailable; 16,422 native fitter work units, not original-target fitting |
| Fresh 394-row individual geometry responses | 57 first-only, 64 second-only, 199 both known zero, 74 either unavailable, 0 both positive |
| Fresh joint geometry responses | 141/141 available; maximum difference from sum of individual coefficients **0** |
| Native regressions | 8/8: aesthetic, sparse, occupancy, bake-forward, shape, emitter-support, shape-selection, receiver-support; CTest total 16.47 s |
| Python regressions | 10/10: existing stock-aesthetic protocol and stock-audit counterexamples |

Reading the current old witnesses retains `duel10-grid-1` as Rejected: its
recorded held-out MSE changes from 101.812750 to 96.604654, below the unchanged
10% improvement requirement. `duel7-grid-1/2` remain Unresolved for unavailable
local support. The two accepted response plates were also inspected: duel10
colors a small chart fragment, while duel7 leaves its conspicuous bright patch
unexplained. These are inspections of existing development evidence, not new
fits, independent review or pilot acceptance. The original
[pilot geometry plate](../../build/ar34-v1/pilot-geometry-clean.png) remains a
geometry-only view with the admission block visibly labelled.

Raw stdout/stderr, the inspection script and detailed integrity inventory are in
ignored `build/ar35-v1/`, with hashes in the receipt. The initial receipt writer
failed after all subprocesses/checks completed because relative `__file__` was
formatted against an absolute root. Its script is retained; the receipt-only fix
reused the completed outputs without rerunning the native probes/tests. Transient
per-process timings were lost and are explicitly unavailable; no combined cycle
wall time is claimed. This is not a production repair or an AR-41 causal decision.

Recheck only this blocked preflight from repository root:

```powershell
python build/ar35-v1/inspect.py
```

This hashes the immutable inputs and reruns the four literal-zero native probes
and existing regression tests. `--reuse-outputs` verifies the preserved logs and
hashes without rerunning those commands. It cannot authorize original-target
fitting, produce accepted parameters or replace an AR-41 receipt.

## Coverage, limitations and next action

The two visible housings on original surfaces 987/988 remain motivated **A
candidates** with **D/original-data fallback** because admission is unavailable.
Receiver surfaces 705/706/707 and every original component/mask remain in the
unchanged frozen inventory. No new accepted map/receiver area, B source or C
asset is delivered. Known-zero direct response is not observed darkness, and
unavailable support is never a black prediction. Old ASR24/25 failures and the
reserve's missing housing-988 control pairs remain binding.

The model assumptions remain the existing DiffuseIntegral/RelativeIncident
point proxy, fixed 4 WU stand-off, scoped native occupancy, conditional
NetRadiant encoding, gain 1, source RGB in [0,100000] and one shared nuisance RGB
in [0,255]. Encoded RGB8 is not calibrated irradiance. No finite near-field
physics, material/background capacity, historical/canonical meaning, BSP,
lightmap, renderer or runtime consumer changed. Codex performed this inspection
and automated verification; no subagents, separate reviewer or new pilot target
appearance inspection were used.

**Next action:** complete the existing size-S AR-41 diagnosis on these exact
mounts and obtain its explicit bounded authorization, or send the blocked
receipt directly to AR-36. The one-M integration/eight-hour ceiling and at most
one causally authorized pre-final repair/four-hour ceiling are unchanged. The
one original-target fit/identical repeat, no second ROI and no post-final repair
fit restrictions remain in force. AR-35 implementation is not complete.
