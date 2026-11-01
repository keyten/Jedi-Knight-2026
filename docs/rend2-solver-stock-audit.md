# Inverse-lighting status and all-stock solver audit — 2026-10-31

Branch: `rend2-unified-wip`; input commit:
`27119e46e1a155a5629d243a4f5c2f1c15895719`. This report includes the working-tree
audit adapter and small capture/diagnostic fixes listed below. No runtime lighting
consumer or source-discovery algorithm was added.

**Follow-up:** [optional-provider orchestration](rend2-provider-orchestration.md)
retests `t1_danger` after this frozen audit. Its unavailable solar provider no
longer prevents reaching joint fitting. The original 57-map audit below remains
unchanged historical evidence; proceeding to fitting is not stock recovery.

**X09 follow-up:** [independent transport topology](rend2-transport-topology-x09.md)
reproduces the original `mp/siege_desert`, `t2_dpred` and `t3_bounty` singularities.
The preparation path now preserves opposite sides and local uncertainty. Bounded
transfer is incomplete; dpred's original precise mappings remain unsupported.
This does not revise the historical audit into a stock-recovery success.

**The project has working controlled CPU fitting components and a canonical
producer, but has not demonstrated autonomous recovery of original lights from
stock JA maps.** Invoking the complete producer on every installed stock BSP did
not yield an accepted fit. In particular, the joint optimizer never started in
these whole-map attempts. This is an integration/admission result, not a measured
failure of a converged inverse solution and not proof that those maps lack useful
physical-light evidence.

The requested exhaustive *optimization* cannot currently be completed by this
pipeline: its regional scheduling and optional-provider failure handling are
incomplete. The audit invokes the existing stages with bounded, honest inputs and
records where they stop. It does not disable the failing stages, turn unknown
visibility into clear rays, fabricate predictions or substitute a Python solver.

Machine-readable results and per-map tables are under
[`tools/rend2/all_stock/results/`](../tools/rend2/all_stock/results/).
`v2` is the first corrected frozen 57-map run; `local-v3` is a separately labelled
six-map dense local diagnostic. **`v4` is the final 57-map repeat** after retaining
failed-provider work. All 57 censuses/fold diagnostics and all stop-stage outcomes
agree between the full runs. Licensed payloads remain outside the repository.

## What already works, and what those results actually establish

| Layer | Evidence | Limit of the claim |
|---|---|---|
| Immutable observations | Original lightmap/grid bytes, source surfaces/UVs, material/style facts, source hints, BSP domain and visibility are captured; all original colour pages can be derived offline | Capture completeness is not colour calibration, observability or successful reconstruction |
| Historical forward evaluation | Production C++ kernels compared with pinned controlled q3map2 fixtures; scoped compiler shadow replay matches all 1,225 tested floor samples | Tested compiler/features only; not the Raven stock recipe, universal bounce replay or all optical geometry |
| Blind fixed-geometry amplitudes | `point_room`, `two_colour`, `door_occlusion` fit through production C++ with withheld true amplitudes | Candidate XYZ was supplied; this is not source discovery |
| Geometry/type/shadow/solar/GI components | CPU tests cover bounded point refinement, spot/panel/tube alternatives, automatic versus oracle shadows, positive/negative sun/sky and passive source-linked first bounce | Compact supported scenes; numerous source positions/frames are supplied priors |
| Joint/S12/S13 integration | Publication suite: 27 records, 351 assertions; 21 supported positive cases publish conditional canonical artifacts | A structurally valid artifact is not proof of original physical configuration or calibrated existence |
| All-stock scene-wide fitting | 57 map attempts and full-page census in this audit | No joint solve, final held-out prediction or stock physical-source verification completed |
| Runtime consumers | Independent gates exist; feature support remains unavailable | No recovered source changes world, NPC, fog, Forward+, LTC, CSM or PBR lighting |

The earlier actual compiled fitting witness, retained in
[`rend2-producer-s13.md`](rend2-producer-s13.md), reports:

| Compiled fixture | Train interval MSE | Validation interval MSE | Held-out interval MSE | Black held-out MSE | Relative compiler-Q error |
|---|---:|---:|---:|---:|---|
| `point_room` | 0 | 0 | 0 | 2,495.889 | 0.0484% |
| `two_colour` | 0.21817 | 2.17680 | 0.22287 | 1,170.602 | 5.16%, 5.86% |
| `door_occlusion` | 0 | 0 | 0 | 2,495.889 | 0.0484% |

These are encoded-domain, quantization/censoring-aware metrics, not lux errors.
They use 18 training and 15+15 independent samples from a selected floor subset.
The production C++ operator generates fitting predictions. The approximately
zero position error in a separate controlled refinement test starts only 0.3
world units away under declared local bounds. It is not whole-map localization.
The excellent matching scores of fixtures with supplied geometry must not be
advertised as autonomous emitter recall.

The previous six-stock-map S13 controller used only two glow candidates and
24 observations per fold. Its diagnostics were not a full-map solver benchmark.
This audit makes that distinction explicit rather than retroactively changing
the historical evidence.

## Architectural boundary and quality priority

The requested boundary is correct and substantially present:

`immutable inputs → CPU reconstruction → ReconstructedLightingAsset + immutable payloads`

`Producer::Run` seals/validates data. `Producer::Publish` hands that data to the
immutable manager; cache serialization does not allocate renderer resources.
Numerical modules have no GL/engine library dependency and do not read
Forward+ shadow capacity, froxel texture formats or `r_pbrPhysical`. The automated
dependency audit covers 14 numerical modules and the standalone core CMake target.

`decode.h` deliberately also describes a **renderer-compatible presentation**
stage, whose snapshot contains `physicalPbr`. This is not the source observation
domain used by fitting. A new numerical test varies physical PBR, linear lighting,
HDR and tone mapping through 16 configurations and checks that source RGB and its
quantization/censoring bounds remain identical. Source decoding, scene calibration
and presentation must remain separately named.

| Requested property | Current verdict |
|---|---|
| Solver ends at canonical asset, not renderer allocation | Satisfied at the CPU API boundary |
| NPC/fog/sun/Forward+ consumers are independent | Independent `EvaluateConsumer` gates; no consumer implementation is enabled |
| Physically invalid/nonfinite/negative solutions cannot win on RGB | Hard feasibility checks and controlled negative tests exist |
| Low residual proves original physical configuration | Explicitly false; physical existence is not automatically certified |
| Physical correctness precedes visual residual | Intended contract, only partly measurable today; stock truth is absent and the full optimizer was not reached |
| Uncertainty is quantitatively established on arbitrary stock maps | Not established; profile/material/visibility/absolute-scale uncertainty remains |
| Offline quality is independent of game performance limits | No renderer resource limits in numerical code; CPU work limits return partial/budget states |

`Joint::Ranking` actually uses validation objective plus complexity and
geometry/material priors, after the required-block gate. It is not a mathematical
oracle of physical truth, nor a complete lexicographic implementation of the four
priorities. Physical feasibility and honest uncertainty are prerequisites; low
held-out RGB alone cannot certify source existence. Keep the stated priority as
the acceptance contract and test it with false-light/source-removal/ambiguous
controls. Do not optimize output source count for a future renderer's shadow slots.

There is a distinct **provider orchestration** problem: an unavailable optional
sun search currently aborts local-light fitting. This is not a consumer dependency,
but must be repaired before general stock-map reconstruction. Unknown sun should
remain unknown while independent local hypotheses can be assessed; malformed
inputs, cancellation and exhausted budgets must still propagate explicitly.

## Inventory, source identity and protocol

Official local `assets0.pk3` through `assets3.pk3` contain **57 unique BSPs: 34 SP,
23 MP**. Later archives override earlier members; four BSPs are overridden by
`assets3.pk3`. Enumeration and SHA-256 are checked independently of map names.
Re-running the inventory reproduces exactly the frozen archive/member/BSP list.
No mod maps are substituted. Every CPU invocation checks the captured BSP hash
against that inventory.

All 57 source archives are available; 48 captures were generated/re-generated and
nine prior original captures reused. Prefix-only derived archives were expanded
with the existing streaming exporter, `mappedImages=0`. Every native attempt
requires `derivedComplete`; no two-page prefix is counted as full-map coverage.

Capture uses one sequential owned SP process: 320×240, picmip 4, 30 FPS, no sound,
PBR, Forward+, LTC, fog, shadows, SSR/SSGI or AA. MP BSPs were captured under the
**SP shader/material context**. That is an explicit qualification of MP optical
metadata, not verified equivalence with the MP renderer. BSP lightmap bytes are
captured before display transforms and never read from screenshots/GPU textures.

The native controller:

1. Loads the immutable snapshot and scans every derived colour tile.
2. Preserves original page/texel/surface/style identity; excludes ambiguous,
   moving, unsupported and non-planar fitting receivers explicitly.
3. Clips original source UV triangles to the original 128×128 JA luxel polygon;
   checks its world area. It does not use GPU atlas coordinates or nominal square
   footprints. Constant-normal transport support is required separately.
4. Selects stable minimum-ID representatives in 512-WU spatial/normal strata.
   Limits are 512/256/256 train/validation/held-out rows.
5. Partitions 256-WU physical blocks using seed 20261031 and origin `(17,31,47)`;
   rejects footprints closer than three radii plus one WU to a block boundary.
6. Gives fitting training RGB only, sanitized geometry with vertex RGB removed,
   and legitimate fixture/grid/deluxe seeds. Validation/final targets remain in
   trusted callbacks. No source answers are supplied.
7. Calls production `Producer::Run` with hidden and solar providers, S05
   consolidation, joint fitting, source-linked passive transport, uncertainty and
   final canonical publication available. Records stages actually reached.

This is a **full-map input census and bounded whole-map pipeline attempt**.
Selecting 28,250 rows is not optimizing all 17.7 million mapped luxels. Nor does
spatial-stratum weight mean that all represented area was fitted or predicted.
The trusted native controller and numerical library share one process; the API
withholds colours, but is not an OS sandbox for malicious native code. Existing
separate solver/evaluator-view negative tests are run as well.

The two admitted numerical alternatives are explicit NetRadiant inverse-square
and linear hypotheses, with conditional gamma-1/primaries assumptions. Stock
identity stays `UnknownProfile`. No attenuation/gamma, absolute SI photometry or
current-PBR albedo is inferred from this choice. Lightgrid/deluxe streams seed
proposals only: unknown historical grid reduction is not a surface irradiance
operator. Passive first bounce uses explicit bounded material hypotheses rather
than inventing an emitting patch residual.

"Eligible planar" below is the adapter's **receiver preselection**, not a proof
that every profile/source/trace combination is implemented. Receiver placement
is conditional on the proposed recipe; the original compiler's push/filtering
is not certified. The probe uses constant-normal receiver hypotheses. A future
general adapter must preserve two-sided/unknown-cull semantics and use canonical
receiver capability preflight throughout. No accepted stock fit was produced
under these assumptions.

For the tested truncating RGB8 operator, interval error is distance from the
prequantization prediction to `[b,b+1]` (black lower edge zero); encoded 255 is a
censored lower bound. World-area/spatial weights enter the aggregate MSE, with
different observation operators kept separate. Shared-peak normalization remains
inside the production evaluator, not independent post-hoc RGB addition. These
conditional encoded-code errors are not irradiance, outgoing radiance or SI
photometric errors. Missing predictions do not enter the metric as black.

## Whole-map readiness census

These totals cover all 57 maps. Percent denominators are shown explicitly.

| Quantity | Count / denominator | Interpretation |
|---|---:|---|
| Original colour pages | 1,779 | All relevant pages derived |
| Original colour texels | 29,147,136 | Raw colour-page denominator |
| Mapped world-space luxels | 17,718,741 / 29,147,136 = 60.79% | Mapping, not source observability |
| Footprint-only | 799,566 / 29,147,136 | Separate approximate-position category |
| Padding/uncovered | 9,327,023 / 29,147,136 | Absent surface coverage, not black physical observations |
| Ambiguous overlap | 1,060,300 / 29,147,136 | Not silently assigned to one receiver |
| Unsupported | 241,506 / 29,147,136 | Different from padding/absence |
| Eligible planar receivers under this fitting/transport scope | 11,249,028 / 17,718,741 = 63.49% | Style/material/normal/geometry restrictions remain |
| Excluded differing shading/geometric normals | 2,894,477 | Unsupported quadrature scope, not absent raw data |
| RGB channels classified censored/255 | 2,653,471 / 87,441,408 = 3.03% | Raw encoded-channel denominator; profile coupling still matters |
| Logical lightgrid indices | 20,927,044 | Not independent physical irradiance samples |
| Original source-evidence candidates | 187,100 | Many glow fragments/alternative shapes; **not recovered lights** |
| Selected rows, train / validation / held-out | 16,255 / 6,019 / 5,976 | Geometry selection before numerical admission |
| Positive selected rows with detailed fold diagnostics | 14,858 / 5,490 / 5,436 | ≥8 encoded codes; excludes the map rejected before fold diagnostics |
| Rows actually used by joint fitting | 0 | No accepted prediction coverage |

Unknown source transfer and primaries remain recorded. A stored RGB value is not
automatically source-linear, scene-calibrated or physically authored Rec.709.
The sum of mapped receiver areas is approximately 7.479×10⁹ WU²; eligible area is
4.596×10⁹ WU². Actual selected patch area in the available fold diagnostics is
1.258×10⁷ WU²; represented stratum weight is 2.624×10⁹ WU². These are distinct
quantities; none is claimed as successfully reconstructed area.

Static geometry builds and detailed fold diagnostics are available for 56 maps.
27 of those builds report globally complete retained geometry. This does not mean
all rays have known optics. Captured region graphs cover bounded export ROIs,
not the whole maps: their aggregate 809 region records are not a census of rooms.
Unknown region membership is not used as a blanket rejection of direct rays.

There are 20,120 deterministic **static-geometry-only fixture-centre** witnesses:
242 Visible, 5,644 Blocked, 14,234 Unknown. Some maps have zero admitted static
fixture seeds and therefore zero such queries. These witnesses exclude dynamic
models from the static scene and are not independently verified source positions,
full moving-door visibility, fitted-source coverage or proof of no wall leaks.
Known/unknown optical distinctions are retained. Most arbitrary fixture/receiver
pairs are distant or obstructed; this sampling is not an upper bound on useful
local direct-light support.

## Actual pipeline outcomes

Each admitted map attempts both profile hypotheses; identical preflight failure
does not rank the profiles.

| Stop stage | Maps | Profile attempts | What it means |
|---|---:|---:|---|
| Hidden search requests >32 local areas | 49 | 98 | Producer has no scene-wide shard scheduler; do not increase one global cap and claim a solution |
| No supported multi-observation hidden search | 3 | 6 | `mp/duel4`, `t1_inter`, `t2_rancor`; sparse sampling can remove local within-material gradients |
| Coincident/overlapping transport nodes | 3 | 6 | `mp/siege_desert`, `t2_dpred`, `t3_bounty`; independent original patches need explicit side/overlap interpretation |
| Sun stage unavailable after hidden proposals | 1 | 2 | `t1_danger`; optional solar failure aborts before S05/joint selection |
| Insufficient disjoint fold geometry | 1 | 0 | `mp/siege_korriban`; no numerical request admitted |
| Joint source/intensity/type optimization | 0 | 0 | Not executed |
| Final held-out prediction / uncertainty / canonical artifact | 0 | 0 | No physical or conditional fit published |

The final failed-provider records quantify the sampling limitation: `mp/duel4`
has 16 search areas with at most four selected receivers each; `t1_inter` eight
areas with at most three; `t2_rancor` 30 with at most four. The provider requires
at least six. Their whole-map sparse selections never exercised its numerical
source search. These outcomes cannot falsify localization on a properly sampled
local problem.

`t1_danger` did execute hidden search: 26 areas, 1,041 forward calls and 1,077
rays per profile, with partial search coverage. This is candidate search, not
joint amplitude/XYZ fitting. The subsequent inability to infer sun is not
evidence that its local physical lights do not exist.

Therefore stock prediction MSE, XYZ accuracy, emitter precision/recall, source
allocation, SI intensity error and calibrated existence coverage are **Unavailable**,
not zero. Source-level errors additionally lack authoritative stock truth even if
a conditional fit were to complete. The controller's black-predictor metrics are
available readiness baselines; they are not substituted for missing predictions.

For example, `academy1` has 113/49/39 selected rows, all positive; six major normal
direction bins and 86/37/34 spatial blocks. Its black interval MSE is approximately
9,088.4 / 11,194.9 / 9,632.08. The pipeline stops on area scheduling, so those
positive observations never produce a fitted-map error. The negative outcome
cannot honestly be attributed simply to absence of useful baked signal.

## Dense local falsification attempt

The global sample rule can starve a hidden provider that needs at least six local
receivers and four positive same-material samples with contrast. A separate
follow-up changes sampling, not the physical model. It selects one 512-WU cell by
normal diversity, then receiver area, then lexicographic coordinates, **without
using observed RGB**, and samples 64-WU strata inside it. Source priors are bounded
to that ROI plus 256 WU; distant alternatives remain untested.

| Map | Train / validation / held-out | Result |
|---|---:|---|
| `academy1` | 86 / 46 / 2 | Insufficient held-out fold; no fit |
| `hoth2` | 43 / 0 / 6 | Insufficient validation/held-out geometry |
| `hoth3` | 85 / 5 / 44 | Insufficient validation fold |
| `vjun2` | 115 / 0 / 76 | Insufficient validation fold |
| `mp/duel4` | 13 / 17 / 11 | Hidden search operates; optional solar Unavailable aborts |
| `mp/ffa3` | 188 / 49 / 11 | Transfer/visibility work limit before fitting |

For `mp/duel4`, each profile uses 861 hidden forward calls, 3,654 rays, 128 nodes,
and retains 16 proposals; 759 source/receiver relations remain uncertain. Search
is explicitly partial. The same producer then fails on unavailable positive
solar training support. No SunSource is fabricated, but local-source fitting also
does not run. This is a concrete orchestration counterexample.

The four missing-fold cases expose a sampling/admission limitation of a single
small ROI. They are not mathematical evidence of non-identifiability of the whole
map. A future scheduler must assemble independent blocks with enough local
gradients and wider spatial validation, rather than relaxing separation or peeking
at held-out colours. This audit does not tune a new ROI until a favourable fit appears.

## Fixes made during the audit

- `tr_lightrecon.cpp`: diagnostic offline export permits bounded 32-MiB chunks
  and 8-Mtexel derivation. `mp/ctf1` has a roughly 14.2-MiB source-evidence section
  and previously failed the 8-MiB default chunk limit. This changes explicit
  export budgets, not ordinary world rendering or lightmaps.
- `offline_bsp.cpp`: planar RBSP model bounds are retained with a conservative
  ±1/1024-WU guard and outward float rounding. Stock `t2_dpred` model 229 has
  equal Z bounds at −4. Previously it invalidated the entire region/export build.
  It remains **moving-geometry uncertainty**, not invented free/solid volume or
  optical transparency. Inverted bounds still fail. New archive/certification
  content changes the relevant keys; old archives are not silently rewritten.
- `producer.cpp`: copy Hidden/Solar work reports before propagating provider
  errors. Failed search no longer loses its exclusion/work diagnostics. This
  changes diagnostics only; it does not swallow failures or promote candidates.
- Added the CPU map adapter, resumable capture/audit/report commands and source
  UV-footprint helper. Exploratory adapter float-area/sliver and varying-normal
  errors were corrected before the frozen 57-map run. Those adapter errors are
  not attributed to the optimizer.
- Added planar/inverted BSP-model regression, source-domain invariance under
  renderer settings and failed-provider report retention tests.

No source-discovery, physical confidence calibration, runtime shader or consumer
algorithm was changed to obtain better stock results.

## Performance, reproducibility and validation

The first corrected 57-map CPU run takes **664.79 s aggregate wall time**, median
10.5 s/map, maximum 29.875 s (`t1_danger`). Peak native process working set is
**116.64 MiB** (`t2_rancor`). These measure census/preparation/rejection work,
**not full optimization performance**. The snapshots used total 16.44 GiB;
largest is `t1_danger`, 896,223,176 bytes. Streaming avoids holding that archive
as a dense decoded source-by-observation matrix.

Successful new/re-generated capture measurements cover 48 maps: 1,545.39 s
aggregate and at most 650.51 MiB game process RSS. Nine captures are reused, whose
original capture cost is not falsely counted as zero. Initial failed capture
attempts and exploratory adapter logs are preserved privately. All current map
captures are present. The two native repetitions of `academy1`/`hoth3` reproduce
all eight census/fold/geometry/ray/profile/selected-ID files byte-for-byte; time/RSS
stay outside semantic witnesses.

Executed checks:

| Check | Result |
|---|---|
| Standalone CPU CTest | **31/31**, 310.42 s, including observations, visibility/regions, forward, amplitudes, geometry/types, shadows, sun, GI, joint, uncertainty, producer and new BSP bounds test |
| After diagnostic retention fix | **4/4** targeted producer/publication/BSP/decoder regressions, 19.25 s; producer 460 checks, publication suite 351 assertions |
| Decoder | 64,028 checks; source-domain invariance included |
| Python evaluation | **39 passed** |
| Blind solver/evaluator views | **6 passed**, including forbidden hidden reads |
| Positive/adversarial X05 gates | **16 passed** |
| New audit reporting/dependency checks | **4 passed** |
| SP and MP MSVC Release | Both renderer targets built successfully after changes |
| Disabled-path GL compilation | 27 programs / 51 stages compiled on Intel GL 3.2 |
| Input-revision shader comparison | **0 source/binding differences** versus `27119e46e` |
| Historical frozen shader baseline | **Fails with 3,870 pre-existing diff lines** against `380ee0f7…`; baseline not rewritten |
| Actual stock visual reconstruction | **Not executed**; source capture is not a visual lighting comparison |
| New q3map2 rebakes | Not performed in this audit; existing pinned compiled controls reused |

CPU tools use GCC 16.2.0; renderer builds use MSVC 2019 x64 Release; controller uses
Python 3.12.14. No numerical third-party runtime dependency is added. The core
remains independently usable without OpenGL/game startup. Existing shader drift
is reported separately from the successful input-commit comparison.
Process CPU-seconds and isolated-machine throughput were not measured. Wall time
is labelled as such; other tasks were running on the workstation.

The final `v4` repeat takes **559.30 s**, median 9.546 s, maximum 23.062 s;
peak process RSS is **116.68 MiB**. All 57 census and fold JSON objects match the
first run exactly. No joint model is evaluated in either run. Preserved provider
work totals 2,082 hidden forward calls, 2,154 hidden rays and 160 area-report
records across both profile hypotheses; these are not joint-optimizer iterations.
Timing differences must not be interpreted as an algorithmic speedup. The
additional eight-file byte-identical checks on `academy1`/`hoth3` use the retained
first-run binary and exclude time/RSS.

## What must change before claiming a general stock solver

1. **Scene-wide scheduling and assembly.** Build deterministic regional jobs with
   enough local surface gradients and separate validation blocks, preserve all
   deferred source hypotheses, then reconcile cross-region/direct-sun/transport
   alternatives. A 128-candidate/32-source fitting wave is an offline job budget,
   not a completed map solution. Do not simply raise all limits or use renderer
   shadow capacity to prune the physical answer.
2. **Optional provider isolation.** No positive/identifiable sun and no new hidden
   candidates must be representable without automatically aborting independently
   admissible local-light models. Preserve unsupported reasons and genuine hard
   failures. Add no-sun-but-positive-local-light end-to-end tests.
3. **Receiver/transport topology.** Resolve coincident observations, distinct
   surface sides and finite patch overlap explicitly. Do not erase a real thin
   wall, merge different receivers by position alone or normalize invalid transfer.
   This probe's transport patches are only the selected receiver footprints with
   `completePatchSet=false`; they are not a complete map reflector discretization.
   Build geometry-based reflecting patches independently of target-colour folds
   before claiming map-scale GI coverage.
4. **Observation sampling as an information problem.** Global normal/spatial
   coverage alone is insufficient for within-material gradients and shadow edges.
   Preserve local chart neighborhoods and independent normals/distances while
   retaining strict spatial/footprint gaps and bounded memory.
5. **Conditional stock forward models.** Maintain profile/transfer/material
   alternatives and model discrepancy. NetRadiant agreement does not recover the
   historical Raven recipe. No stock candela/lux claims without calibration.
6. **Measure actual source discovery.** Run unknown-XYZ known-source compiled
   scenes through the same map adapter/scheduler, not only supplied-candidate
   fixtures. Then measure proposal recall, position/type/count error, source
   removal, held-out prediction, coverage and calibrated alternatives.
7. **Only then evaluate expensive final quality.** Stock held-out fidelity and
   plausibility can be measured; original physical accuracy still needs external
   truth. Keep indirect/environment/unresolved discrepancy distinct and all
   renderer consumers outside the solver.

The immediate blockers are scheduling/admission/integration, not a demonstrated
need to replace every numerical optimizer or accelerate gameplay rendering.
Controlled successes remain valuable, but do not establish the original project
goal on real maps.

## Reproduction and valuable debug data

See [`all_stock/README.md`](../tools/rend2/all_stock/README.md) for capture/setup,
data-access semantics, bounds and all private debug files. Executed commands use
the repository root and the bundled Python interpreter; MinGW tools were added
to PATH. The local private root is `$env:TEMP/jk-lightrecon-all-stock-20261031`.

```powershell
python -X utf8 tools/rend2/all_stock/inventory.py --base build/lightrecon-evidence-runtime/final-source/stock-assets/base --output "$private/inventory.json" --commit 27119e46e
python -X utf8 tools/rend2/all_stock/capture.py --private $private --renderer build/s13-renderers/Release/rdsp-rend2_x86_64.dll
python -X utf8 tools/rend2/all_stock/run.py --private $private --revision v2
python -X utf8 tools/rend2/all_stock/run.py --private $private --revision local-v3 --local --maps academy1 hoth3 hoth2 vjun2 mp/ffa3 mp/duel4
python -X utf8 tools/rend2/all_stock/run.py --private $private --revision v4
python -X utf8 tools/rend2/all_stock/collect.py --private $private --out tools/rend2/all_stock/results/v2 --revision v2
python -X utf8 tools/rend2/all_stock/collect.py --private $private --out tools/rend2/all_stock/results/v4 --revision v4
python -X utf8 tools/rend2/all_stock/collect.py --private $private --out tools/rend2/all_stock/results/local-v3 --revision local-v3 --attempted-only
ctest --test-dir build/s06-gcc --output-on-failure -j 2
ctest --test-dir build/s06-gcc --output-on-failure -R 'lightrecon_(producer|producer_suite|bsp_capture|decode)$' -j 2
python -X utf8 -m unittest discover -s tools/rend2/all_stock -p 'test_*.py' -v
python -X utf8 tools/rend2/test_lightrecon_legacy_gl.py --output build/all-stock-disabled-path
python -X utf8 tools/rend2/producer/compare_revision.py --revision 27119e46e --gpu-report build/all-stock-disabled-path/report.json --out build/all-stock-disabled-comparison
cmake --build build/s13-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 2
```

Frozen revisions reject changed executable hashes; use a new revision when
rebuilding. The exact `v2` executable remains in the private `binaries` directory.
Do not rerun old policy names with a different executable and call it identical.
JSON/CSV retain all maps, failed states, missing metrics, page categories,
area/count denominators, partition signal, static/region uncertainty, fixture-ray
counts and OS resource measurements. Coordinate-level TSV/OBJ/BSP/JKOBS stay
private. The following per-map table is generated from the frozen census; its
row selection is not successful prediction coverage.

<!-- ALL_STOCK_TABLE_START -->
| Map | Pages | Mapped / raw | Eligible planar | Train / val / held-out | Fixture rays V / B / U | Native s / MiB |
|---|---:|---:|---:|---:|---:|---:|
| `academy1` | 24 | 235,581 / 393,216 | 224,303 | 113 / 49 / 39 | 0 / 0 / 0 | 5.7 / 15.2 |
| `academy2` | 22 | 211,130 / 360,448 | 201,380 | 80 / 39 / 32 | 0 / 0 / 0 | 5.6 / 19.9 |
| `academy3` | 22 | 228,917 / 360,448 | 218,314 | 110 / 44 / 35 | 0 / 0 / 0 | 6.5 / 20.1 |
| `academy4` | 22 | 211,130 / 360,448 | 201,661 | 88 / 34 / 29 | 0 / 0 / 0 | 6.9 / 18.6 |
| `academy5` | 22 | 211,130 / 360,448 | 201,380 | 82 / 40 / 29 | 0 / 0 / 0 | 6.4 / 19.9 |
| `academy6` | 34 | 352,907 / 557,056 | 335,872 | 245 / 74 / 79 | 12 / 226 / 274 | 10.7 / 16.4 |
| `hoth2` | 12 | 79,565 / 196,608 | 41,347 | 177 / 56 / 61 | 10 / 130 / 372 | 5.6 / 54.8 |
| `hoth3` | 38 | 403,084 / 622,592 | 202,558 | 512 / 181 / 147 | 0 / 97 / 415 | 15.2 / 87.9 |
| `kor1` | 24 | 235,843 / 393,216 | 180,494 | 512 / 188 / 211 | 0 / 0 / 0 | 9.5 / 67.1 |
| `kor2` | 80 | 717,542 / 1,310,720 | 121,243 | 360 / 161 / 114 | 0 / 172 / 340 | 18.3 / 53.7 |
| `mp/ctf1` | 18 | 159,777 / 294,912 | 113,115 | 374 / 73 / 106 | 6 / 138 / 368 | 7.2 / 80.6 |
| `mp/ctf2` | 24 | 260,065 / 393,216 | 77,138 | 211 / 86 / 94 | 17 / 205 / 290 | 7.1 / 35.0 |
| `mp/ctf3` | 20 | 199,362 / 327,680 | 165,588 | 323 / 122 / 135 | 7 / 185 / 320 | 7.0 / 35.6 |
| `mp/ctf4` | 60 | 663,613 / 983,040 | 492,909 | 512 / 256 / 256 | 0 / 0 / 0 | 21.2 / 49.9 |
| `mp/ctf5` | 20 | 198,998 / 327,680 | 163,181 | 413 / 148 / 117 | 0 / 110 / 402 | 12.4 / 72.1 |
| `mp/duel1` | 46 | 486,958 / 753,664 | 266,439 | 80 / 11 / 29 | 35 / 305 / 172 | 13.0 / 13.6 |
| `mp/duel10` | 36 | 439,569 / 589,824 | 255,569 | 106 / 19 / 38 | 14 / 219 / 279 | 9.4 / 24.1 |
| `mp/duel2` | 6 | 32,297 / 98,304 | 15,043 | 69 / 32 / 20 | 1 / 90 / 421 | 2.6 / 43.5 |
| `mp/duel3` | 22 | 237,522 / 360,448 | 154,680 | 130 / 33 / 43 | 25 / 154 / 333 | 6.1 / 20.6 |
| `mp/duel4` | 2 | 19,842 / 32,768 | 10,962 | 34 / 24 / 18 | 14 / 72 / 186 | 1.1 / 22.3 |
| `mp/duel5` | 76 | 833,796 / 1,245,184 | 659,954 | 245 / 85 / 73 | 0 / 85 / 427 | 20.2 / 19.7 |
| `mp/duel6` | 68 | 748,769 / 1,114,112 | 295,778 | 348 / 115 / 93 | 0 / 0 / 0 | 17.6 / 33.3 |
| `mp/duel7` | 27 | 247,563 / 442,368 | 227,545 | 146 / 31 / 50 | 28 / 244 / 240 | 7.3 / 17.6 |
| `mp/duel8` | 26 | 318,431 / 425,984 | 283,023 | 178 / 34 / 44 | 15 / 126 / 371 | 7.5 / 21.6 |
| `mp/duel9` | 40 | 444,221 / 655,360 | 85,704 | 115 / 30 / 34 | 8 / 192 / 312 | 9.7 / 18.0 |
| `mp/ffa1` | 22 | 226,573 / 360,448 | 175,571 | 368 / 88 / 123 | 7 / 144 / 361 | 7.2 / 32.2 |
| `mp/ffa2` | 32 | 381,767 / 524,288 | 331,532 | 478 / 191 / 164 | 6 / 189 / 317 | 10.6 / 36.0 |
| `mp/ffa3` | 26 | 301,473 / 425,984 | 177,997 | 376 / 124 / 127 | 4 / 131 / 377 | 8.9 / 52.3 |
| `mp/ffa4` | 42 | 522,042 / 688,128 | 484,957 | 512 / 223 / 241 | 10 / 127 / 375 | 12.6 / 40.8 |
| `mp/ffa5` | 46 | 539,814 / 753,664 | 484,085 | 512 / 229 / 245 | 1 / 160 / 351 | 14.5 / 34.4 |
| `mp/siege_desert` | 26 | 227,302 / 425,984 | 132,387 | 77 / 27 / 26 | 0 / 84 / 428 | 10.6 / 49.4 |
| `mp/siege_hoth` | 38 | 382,098 / 622,592 | 94,344 | 366 / 160 / 120 | 2 / 96 / 414 | 12.1 / 45.0 |
| `mp/siege_korriban` | 18 | 140,383 / 294,912 | 64,372 | 17 / 7 / 15 | — / — / — | 6.2 / 25.2 |
| `t1_danger` | 94 | 654,675 / 1,540,096 | 19,647 | 78 / 28 / 13 | 0 / 80 / 432 | 23.1 / 35.3 |
| `t1_fatal` | 28 | 293,094 / 458,752 | 249,008 | 512 / 210 / 169 | 0 / 162 / 350 | 11.5 / 74.6 |
| `t1_inter` | 12 | 107,085 / 196,608 | 72,334 | 13 / 9 / 13 | 0 / 0 / 0 | 3.1 / 14.2 |
| `t1_rail` | 28 | 75,186 / 458,752 | 54,198 | 201 / 64 / 84 | 0 / 0 / 0 | 9.8 / 95.1 |
| `t1_sour` | 12 | 94,812 / 196,608 | 48,719 | 104 / 37 / 36 | 0 / 0 / 0 | 4.5 / 46.5 |
| `t1_surprise` | 28 | 293,536 / 458,752 | 51,387 | 162 / 42 / 43 | 0 / 202 / 310 | 7.9 / 45.0 |
| `t2_dpred` | 32 | 312,913 / 524,288 | 253,735 | 512 / 200 / 210 | 4 / 142 / 366 | 11.4 / 79.0 |
| `t2_rancor` | 8 | 67,625 / 131,072 | 53,749 | 49 / 18 / 31 | 0 / 89 / 303 | 5.3 / 116.7 |
| `t2_rogue` | 26 | 299,268 / 425,984 | 246,790 | 512 / 180 / 165 | 0 / 0 / 0 | 9.8 / 45.1 |
| `t2_trip` | 14 | 101,098 / 229,376 | 91,420 | 312 / 91 / 117 | 0 / 0 / 0 | 5.3 / 58.0 |
| `t2_wedge` | 36 | 311,739 / 589,824 | 235,541 | 509 / 183 / 177 | 1 / 86 / 425 | 11.0 / 76.4 |
| `t3_bounty` | 32 | 284,029 / 524,288 | 164,244 | 325 / 92 / 110 | 0 / 114 / 398 | 12.0 / 104.0 |
| `t3_byss` | 28 | 325,784 / 458,752 | 213,307 | 361 / 101 / 95 | 0 / 0 / 0 | 8.2 / 35.9 |
| `t3_hevil` | 30 | 295,319 / 491,520 | 173,848 | 512 / 222 / 232 | 0 / 0 / 0 | 10.7 / 63.0 |
| `t3_rift` | 66 | 682,408 / 1,081,344 | 276,453 | 512 / 256 / 234 | 1 / 85 / 426 | 19.0 / 63.9 |
| `t3_stamp` | 24 | 226,561 / 393,216 | 165,601 | 486 / 170 / 141 | 3 / 203 / 306 | 8.4 / 60.4 |
| `taspir1` | 48 | 467,212 / 786,432 | 359,262 | 512 / 256 / 256 | 0 / 103 / 409 | 13.8 / 70.2 |
| `taspir2` | 30 | 317,209 / 491,520 | 258,651 | 512 / 193 / 216 | 4 / 198 / 310 | 10.5 / 103.7 |
| `vjun1` | 30 | 299,160 / 491,520 | 269,955 | 512 / 208 / 213 | 0 / 104 / 408 | 11.6 / 89.8 |
| `vjun2` | 42 | 448,811 / 688,128 | 409,070 | 418 / 149 / 154 | 1 / 147 / 364 | 12.9 / 84.0 |
| `vjun3` | 26 | 225,081 / 425,984 | 190,808 | 512 / 161 / 143 | 2 / 114 / 396 | 9.2 / 97.7 |
| `yavin1` | 14 | 144,622 / 229,376 | 112,274 | 117 / 38 / 42 | 0 / 37 / 475 | 4.3 / 31.8 |
| `yavin1b` | 10 | 92,167 / 163,840 | 73,815 | 74 / 36 / 31 | 4 / 97 / 411 | 3.5 / 33.1 |
| `yavin2` | 40 | 400,283 / 655,360 | 64,787 | 169 / 61 / 64 | 0 / 0 / 0 | 10.0 / 64.6 |

<!-- ALL_STOCK_TABLE_END -->
