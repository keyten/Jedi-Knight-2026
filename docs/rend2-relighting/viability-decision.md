# AR-34 — freeze the proof; block on named source support

**2026-11-10 (Australia/Sydney). Investigation Done; experiment Blocked-SourceSupport; aesthetic gate Unknown.** The next action is AR-41, a size-S contact-plane diagnosis. Do not start a real-target fit or the general roadmap. A geometry response is insufficient to admit a mounted source.

Inspected HEAD `dff8ea77acc1fc578239af69b17253e3a12b7c9f`, branch `rend2-unified-wip`, origin `git@github.com:keyten/BetterJK.git`. ASR24 is committed at `bddf5b376`; ASR25 is committed at HEAD. No changes were staged. The supplied checkout is the project authority despite its historical `Jedi-Knight-2026` name. No applicable `AGENTS.md` was found in repository/ancestor directories. Existing edits to the plan and the untracked launch notes were preserved.

During this investigation a concurrent commit, `a8cfaa215192dbf7693d1060ef8e8900329a3710` (`new plan`), committed only those plan/launch documents. Its committed plan text matches the pre-AR-34 snapshot after Git line-ending normalization, and launch-note bytes are unchanged. Completion HEAD is `a8cfaa215`; the frozen implementation and experiment inputs still match the inspected ASR25 hashes. This work did not create that commit or stage files.

The frozen [protocol v1](../../tools/rend2/aesthetic_relighting/policies/viability-v1.json) and [verification receipt](../../tools/rend2/aesthetic_relighting/results/ar34-v1/evidence.json) carry full input/output hashes. Licensed geometry, source images, receiver positions and diagnostic files stay in ignored `build/ar34-v1/`. This is a complete negative preflight decision, not a completed multi-light reconstruction.

## Existing evidence credited once

| Configuration | Actual numeric support | Spatial and visual limit |
|---|---|---|
| ASR24 `mp/duel10`, surface 942 | Held-out interval MSE 174.230938 → 95.075591; robust loss 36.172113 → 24.154715; 93 distinct known-static, mover-independent grid records; frozen-background removal MSE 7837.108618; 17/17 rows change by at least one code | One receiver chart (155), 20/20/17 rows in 3/2/1 blocks. Useful conditional mounted point, not a room. |
| ASR25 `mp/duel7`, surface 1657 | Held-out MSE 1196.505292 → 1000.383821; robust improvement 11.82%; 48 held-out rows in three blocks; removal MSE 3923.111111; 48/48 rows change | Maximum residual 177.592888 codes; three agreeing grid records are minimally sufficient under that study; a bright patch remains unexplained. |

ASR25's duel10 native report is byte-identical to ASR24's. It is **one preserved result**, not a third success. Full report SHA-256 values are respectively `c72d343d52d7c8b893f7232fd262f921e421c7a1bb23f2cb2ddd21759c70af70` and `b1f475579816fceb291fdd555ec0d0c0a91dd467829f81d1bfae1d6f63dd54d0`. Removal reports, folds, hypotheses and plates are also pinned in the receipt.

Fresh checks matched all four ASR24 and six ASR25 public manifest entries, all 209 ASR24 private manifest entries, ASR25's 150 implementation files, 113 historical/core/canonical files and six native tools. These are new integrity checks, not a rerun of the reported CPU tests or 57-map audit. The original response plates were inspected: duel10 shows a small held-out fragment; duel7 leaves the large bright residual visible. Neither plate accepts a complete room.

The corpus baseline remains 57 BSPs, 37 available captures, 45 completed catalogues and 210 observation ROIs on 35 maps. The two accepted configurations support 201 unique original fitting rows including 65 held-out, within their own maps. No new map, accepted area, historical lamp count or completion probability is established here. The 53 blocked-map counts are not physical reconstruction failures; their blocker classes overlap.

## Smallest selected scope

Select **one** new original-stock scope, `duel10-oblique-housings-v1`: two separate `textures/h_evil/light1` housings on original world surfaces **987 and 988**. Each has two independently catalogued luminous components; the fixture inventory in the protocol lists all four IDs. They are two housing hypotheses, not four independently recovered historical lamps. Original shader and glow-image identities, source masks and a three-projection geometry plate were inspected before any original target fit.

Each housing uses one fixed point at its area-weighted luminous-union centroid plus 4 WU along the original outward normal, named to its original mounting surface. The dark interval between luminous components remains in the inventory; the point is a housing proxy, not a claim that the interval emits. No XYZ, orientation, optical shape or position was optimized. Exact private bounds/poses and input identities are in `build/ar34-v1/pilot-scope.json`. The ROI includes both housings, their native outward floor hits and surrounding original receiver surfaces. An initial lower bound of 192 WU missed the floor at zero; this geometry-only correction is retained, not a second searched fixture scope.

The diagnostic fit/evaluation material is **only** `textures/wedge/w_windowtrim`, on original surfaces **705, 706 and 707**, with both signed Y normals. This exercises separated fixtures, surrounding vertical receivers, shadow separation and a shared bounded background beyond a repeat of chart 155. It deliberately tests the existing same-material model. The floor and ledge have different materials: their overlapping transport is useful geometry evidence, but their appearance cannot be accepted using an invented cross-material response.

Fresh native unit-response preflight, with dummy zero target RGB:

| Measure | Result |
|---|---:|
| Eligible mapped rows in the geometric ROI | 30,869 |
| Stratified geometry sample across original surfaces | 941 |
| Both positive / first only / second only / both known zero / either unavailable | 18 / 18 / 19 / 604 / 282 |
| Finer same-material trim sample | 394 |
| Trim: first only / second only / both zero / either unavailable | 57 / 64 / 199 / 74 |
| Joint response on the frozen four-role subset | 141/141 available; maximum absolute difference from sum of individual coefficients **0** |

Known zero here means no direct response under this model, not observed darkness. Mesh extraction reports complete coverage for the requested box, while optical queries still report unavailable rows. Complete mesh extraction never clears unavailable visibility. The two trim response columns are distinct and shadow-separated; there are **no both-positive trim rows** in this sample. The minimal interaction test is whether a shared two-source solution explains both banks without filling the known-zero controls, and whether individual removals have their expected spatial effects. This scope cannot prove overlapping mixed-material appearance or full-room readiness. No second ROI or fallback search is authorized.

## Independent evaluation frozen before original-target fitting

All 394 trim rows were selected by original geometry, eligibility and stable IDs, with one row per 16 WU surface cell. Target RGB was discarded on parsing and never used for selection. Twenty accessible prior duel10 fold packets contain 439 unique fitting IDs; none intersects this trim sample. Previously exposed stock outputs are development/regression evidence. This is a fresh receiver scene on an already known map, **not independent map transfer or a blind human trial**.

The retained geometry-only fold design tried the old-style 128 WU hash design, then a 64 WU hash design; neither supplied both fixtures in all four roles. Before target exposure, a 64 WU influence-stratified block design was frozen: origin `[41,9,9]`, fixed seed and exact IDs in `frozen-folds-geometry.json`. Blocks have one role each, exclude all prior fitting blocks and retain the three-times planar footprint margin plus 1 WU. Stable cell hashes order each geometry-only source-influence stratum, then assign train/validation/final/reserve cyclically. Sixty-four origins were compared by minimum positive rows per fixture/role, with lexical tie breaking. All unsuccessful designs remain private. Original bake filtering is unknown: the gap is a bounded precaution, not proven statistical independence.

| Role | Rows | Blocks | Positive rows for housing 987 / 988 | Known-zero controls |
|---|---:|---:|---:|---:|
| Train | 40 | 9 | 6 / 6 | 28 |
| Validation | 41 | 8 | 5 / 7 | 29 |
| Final | 34 | 8 | 5 / 8 | 21 |
| Repair reserve | 26 | 6 | 5 / 4 | 17 |

Train, validation and final each cover all three receiver charts; reserve covers 706/707. Primary final has two disjoint same-chart lit/control pairs per housing within 128 WU. Reserve has two pairs for housing 987 and **none for 988**: it is not a complete repair acceptance set. This smaller spatial design changes no ASR24/25 result or frozen criterion. The new protocol explicitly bounds its weaker bake-independence claim. No original final or reserve appearance was viewed, fitted or scored. The dummy admission probe contains literal zeros, not substituted observations or unavailable predictions.

Freeze the existing `DiffuseIntegral` / `RelativeIncident` evaluator, isotropic mounted-point approximation, native occupancy, conditional NetRadiant forward encoding, gain 1, nonnegative relative RGB capped at 100000 and a **single shared constant nuisance RGB** capped at 255. Use 200 sweeps, tolerance `1e-4`, Huber 4 and existing area/block/normal weights. RGB8 quantization intervals and saturation censoring remain encoded appearance targets; they are not calibrated linear irradiance. No finite-radius implementation, full fields, MD3, full-map export, new optimizer or learned material/background capacity is required.

The protocol specifies admission, two active sources, convergence/bounds, validation-only subset selection, the unchanged 10% MSE / 5% robust held-out improvement thresholds, comparison with frozen singletons, individual and all-source removal, geometry-selected shadow controls, per-chart residuals, brightness ordering, color error and independent visual review. Original grid corroboration is retained as diagnostic evidence for this visibly inventoried A scope, explicitly **not a universal A eligibility rule**; the old three-record gate remains part of the unchanged ASR24/25 studies. New acceptance cannot retroactively promote their failures.

Registered original/on/off receiver appearance plates at fixed presentation settings and a blinded separate review are required. A wireframe, low MSE or successful process alone cannot pass. No shading of unmeasured world surfaces is authorized. Grey/unavailable support remains visible, and the accepted denominator is the declared measured receiver subset, never the room's area.

Reuse is concrete: `stock_aesthetic.run.fixture_lines` already accepts multiple fixed positions/surfaces; the native CLI already accepts up to four fixtures and enumerates all nonempty subsets; `Aesthetic::FitAndValidate` supplies the shared fit and validation boundary. The existing `prepare` controller's single receiver-surface/material winner must be replaced only for this pilot by the **already frozen explicit IDs**, not by another search. `stock_aesthetic.audit.audit` already replays every saved source through native coverage and encoding and removes all sources without refitting. The remaining wiring is individual-removal diagnostics, multi-fixture provenance and the registered multi-chart review packet using existing `report.py`, `stock_audit.packet` and `response` helpers. Native fit/geometry work caps remain 300,000,000 / 16,000,000 units respectively, with their own memory/ray gates unchanged; unlike work counters must not be summed. None of this authorizes a new fitter, material model or evaluator.

## Blocking result and bounded next action

`Shape::Prepare` geometry-only coverage does **not** run `SourceSupport::Check`. A stronger probe through the existing native aesthetic CLI, using only zero dummy targets, reaches that check and makes all three source configurations unavailable with:

> Named mounting surface absent at declared contact plane

This is not a nuisance-only success or a failed original-data fit. The original values have not been opened. The native path is already implemented; its named oblique support contract currently prevents this experiment. The earlier geometry-only metadata incorrectly labelled coverage as admission; the original receipt/script are retained with `before-admission-correction` names, and the final receipt corrects that claim. A separate first dummy probe failed because the scratch block-ID encoding collided; its inputs/error are retained, and collision-free SHA-256 cell IDs produced the substantive refusal above. No production code changed for either correction.

Code inspection finds a plausible numerical cause: `FromSurfaces` stores the outward normal as float, whereas `Check` tests the resulting contact plane with a double-roundoff-scale error. The surfaces are oblique. **This is a hypothesis, not a proved diagnosis.** Do not enlarge a tolerance, remove the mount test, use a free point, or assume the fixture/world is empty to force admission.

AR-41 must distinguish missing named geometry, a numerical representation defect and genuine unsupported/contact geometry using these exact two fixtures and existing providers. It must deliver a measured causal receipt or a precise negative decision within S. It may authorize AR-35 only if a bounded modern-path proof/adapter change can preserve historical behavior and existing storage meanings. Otherwise take the blocked receipt directly to AR-36 for a verdict; no speculative replacement evaluator follows.

The total next coding allowance remains **one M integration (at most eight engineering hours), plus at most one M repair (at most four hours)** after a written causal decision. AR-41 is a read-only decision, not an extra implementation allowance. This v1 authorizes at most **one original-target fit and identical repeat**, after admission and any authorized repair have completed. The incomplete reserve controls mean that no repair fit after final exposure is authorized by v1. After any final has informed a change, it becomes development evidence; a new explicit decision with fresh adequate final evidence is required before further coding or fitting. No source relocation, second ROI or broadened model is permitted by this freeze. Each native process is capped at 300 s; the real-target cycle at 30 minutes of subprocess wall time. Budget exhaustion, unavailable review or unmeasurable controls produces a named non-Go outcome.

AR-34 adds **no accepted A/B/C/D product coverage**. The two housings are visibly motivated A candidates with unresolved admission; their output remains D/original-data fallback. No B lamp or spatial C field was authored, and the nuisance term is not promoted into a C asset. The decision leaves conditional one-source feasibility intact, leaves coherent-region viability unknown, and authorizes only the targeted diagnosis before further investment.
