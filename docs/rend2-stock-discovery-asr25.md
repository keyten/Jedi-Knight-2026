# ASR25 — aesthetic discovery across the original 57-map inventory

**The corpus audit is complete, with blockers; generalization remains limited.**
All 34 SP and 23 MP maps have reproducible results or explicit current input/tool
blockers. Two distinct maps demonstrate substantive nonempty conditional source
discovery: `mp/duel10` and the yellow-lit `mp/duel7`. No SP map passes this frozen
path. This is not a claim that 57 maps, or either accepted map in full, are relit.

Run date: **2026-11-10**. Starting revision: `bddf5b376b6f9a909b5f36b363b2100fe5cdcb72`
on `rend2-unified-wip`. The local checkout's origin is `keyten/BetterJK`; repository
identity for this experiment is pinned by the revision and original BSP hashes.

* [Every map: readable diagnostics](../tools/rend2/stock_audit/results/v1/maps.md)
* [Every map: JSON](../tools/rend2/stock_audit/results/v1/maps.json),
  [CSV](../tools/rend2/stock_audit/results/v1/maps.csv)
* [Measured summary](../tools/rend2/stock_audit/results/v1/summary.json),
  [frozen public protocol](../tools/rend2/stock_audit/results/v1/protocol.json),
  [checks](../tools/rend2/stock_audit/results/v1/checks.json),
  [artifact hashes](../tools/rend2/stock_audit/results/v1/manifest.json)
* [Commands, policies and interpretation](../tools/rend2/stock_audit/README.md)
* [Private visual/geometric inspection index](../build/asr25-v1/inspection/index.html)

## Inventory and distinct coverage measures

All 57 original BSPs are locally available in the licensed assets0–3 view and
their identities match the preserved stock inventory. The old capture manifest
records 57 captures, but **only 37 dataset files still exist**. Those historical
receipts are not fresh input availability checks. The current run neither
downloads assets nor replaces missing original evidence with synthetic data.

| Measurement | Actual result |
|---|---:|
| Original BSP inventory | 57: 34 SP / 23 MP |
| Currently available original capture files | 37 |
| Completed original fixture catalogues | 45 |
| Catalogue fixture components, across completed catalogues only | 54,570 |
| Fresh independent observation coverage | 210 ROIs on 35 maps |
| Maps with at least one known upward sky reach in sampled ROIs | 24 |
| Completed grid-fixture discovery censuses | 4 maps |
| Eligible components in those censuses | 95 |
| Components with at least three distinct agreeing static grid records | 43 |
| Geometry-selected follow-up regions | 9 |
| Numerically fitted regions, each repeated identically | 5 |
| Accepted bounded one-source configurations | 2, on two maps |
| Original samples supporting accepted configurations | 201, including 65 held-out |

The independent six-ROI-per-map design uses original world planar geometry and
stable spatial sampling, not fixtures, RGB or fit outcomes. These statistics count
**captured observations**, including material uncertainty and known/blocked/unknown
ray states. ROI overlaps are not added as unique coverage. Reused/partial capture
completeness is not silently upgraded.

Accepted-source coverage is a separate per-map count of unique original fitting
IDs in accepted bounded regions: **57 / 17 held-out** on `mp/duel10`, and **144 / 48
held-out** on `mp/duel7`. There is no accepted coverage claim elsewhere on either
map. Whole-map source recall, spatial acceptance fraction and historical lamp
count are unavailable. The archived v4 observation census remains labelled
historical; its 17,718,741 mapped samples never become accepted-source coverage.

The per-map outcomes are **2 AestheticCandidate, 1 NoAcceptedCandidate, 1
NoSupportedCandidate, and 53 Blocked**. The last group's accepted counts and
coverage remain null. `academy1` has no component eligible under this particular
search policy; this is not an empty-light solution. `mp/duel4` has supported
proposals but none of its three chosen regions has sufficient separated receiver
folds. It is not a successfully relit map with zero lamps.

## Substantive configurations and their limits

Both configurations use the original luminous footprint, fixed 4 WU mounted-point
proxy, native geometric admission, independently agreeing original grid records,
validation selection, held-out improvement, and frozen source-removal replay.
The source and folds freeze before fitting; no held-out score chooses a new lamp
position. Original RGB8 is compared through conditional forward encoding, never
decoded as calibrated linear irradiance.

| Accepted region | `duel10-grid-2` | `duel7-grid-3` |
|---|---:|---:|
| Original BSP surface | 942 | 1657 |
| Original fixture material | `textures/mp/s_flatwhite` | `textures/desert/light_yellow` |
| Train / validation / held-out rows | 20 / 20 / 17 | 48 / 48 / 48 |
| Held-out geometric blocks | 1 | 3 |
| Known-static / mover-independent grid records | 93 / 93 | 3 / 3 |
| Refitted nuisance-only baseline held-out interval MSE | 174.230938 | 1196.505292 |
| Source held-out interval MSE | 95.075591 | 1000.383821 |
| Held-out MSE improvement | 45.43% | 16.39% |
| Held-out robust-loss improvement | 33.22% | 11.82% |
| Source held-out maximum residual, codes | 24.749123 | 177.592888 |
| Frozen-background source-removal MSE | 7837.108618 | 3923.111111 |
| Held-out rows changing by at least one code on removal | 17 / 17 | 48 / 48 |

The nuisance-only baseline and frozen-background removal are different checks:
the former refits its background; the latter removes only the frozen source.
The large `duel7` residual and barely sufficient three-record grid support limit
the strength of the new demonstration. Its yellow luminous image and warm
original receiver observations provide the required distinct color-lit scene;
the point response leaves a bright patch unexplained. This supports a local
aesthetic hypothesis, not scene readiness or a unique optical/source explanation.

The two numerical `duel10` reports are byte-identical to the preserved ASR24
reports. Across all nine selected regions, two pass, one fails its held-out gate,
four lack sufficient separated receiver folds, and two retain unknown local
source support. Failed regions and their selections remain in the same packet.

Private accepted-region packets:

* [`mp/duel10` geometry, configuration and appearance](../build/asr25-v1/maps/mp_duel10/fit/duel10-grid-2/report.html)
* [`mp/duel7` geometry, yellow fixture, configuration and appearance](../build/asr25-v1/maps/mp_duel7/fit/duel7-grid-3/report.html)

The inspection index adds Hoth exterior/model-rich, Hoth interior, industrial,
color/material-diverse, yellow-lit, mixed-arena and large exterior scenes. Seven
original geometric plates show native mesh completeness, grid directions and
external model origins. Missing model geometry/emission remains unknown. These
are geometric diagnostics and frozen appearance-response plates, not rendered
remaster screenshots. Grey response-plate cells mean unmeasured support.

## Failure classes and targeted next work

Counts below overlap where a map has more than one blocker; they are not a
partition of the 53 blocked maps.

| Observed class | Maps / regions | Targeted action supported by this audit |
|---|---:|---|
| Missing original capture file | 20 maps | Restore or regenerate the exact original evidence through the existing capture-only workflow; recheck BSP/material identities and retain the old receipt. |
| ASR24 static-map guard | 23 maps | Investigate locally certifiable mover/extents and fixture support using the existing geometry providers. Unknown travel extents still block certification; do not remove the guard without equivalent local checks. |
| Catalogue work budget | 11 maps | Profile footprint/component connectivity and add bounded spatial indexing or explicit deterministic shards, recording total work and shard coverage. No silent truncation or partial-catalogue success. |
| Catalogue polygon budget | 1 map, `t1_rail` | Inspect the luminous footprint expansion and bounded representation; retain unsupported topology when it cannot be certified. |
| Native capture section refusal | 2 maps, `t1_rail` and `vjun3` | Diagnose section structure/limits or recapture with the stable exporter. The message alone does not prove corrupt assets. No ROI statistics are invented. |
| Inadequate receiver folds | 4 regions | Test a predeclared geometry-only receiver design on a fresh protocol; retain footprint gaps, original surface mapping and material support. |
| Incomplete local source support | 2 `duel7` regions | Preflight the existing named-mount admission before expensive fold preparation; investigate exact local fixture support. The native `ExtractBox` completeness refusal remains binding. |
| Held-out rejection | 1 `duel10` region | Retain rejection. Evaluate independently motivated neighboring fixtures only in a new frozen discovery study. |

External-model lamp discovery is a further scope gap, visible in Hoth: original
primary-model entity references number 383 on `hoth2` and 381 on `hoth3`. These
include dynamic/unsupported entities and are not lamp counts. A targeted next
study should bridge the existing ASR10/11 model-instance/emission evidence into
certifiable fixture hypotheses, preserving missing-asset, correspondence,
motion and optical uncertainty. Sparse neighboring-source hypotheses may address
the `duel7` unexplained patch only with independent source evidence and new frozen
validation. Neither proposal justifies weakening this run's acceptance criteria.

Twenty-four maps show sampled geometric upward sky reach; negative or unknown
samples do not prove an interior or exclude other sky directions. Per-map sky
counts and native fitting-ROI solar states are retained. Numeric sun strength and
exact historical solar direction remain unavailable; a sky hit is not a sun fit.

## Runtime, reproducibility and verification

The uniform two-worker corpus run took **1209.922 s (20 min 9.9 s)**, with
**2402.264 s** of summed per-map wall time. Catalogue subprocesses account for
1688.250 summed seconds. These are measured wall times including I/O/scheduling,
not deterministic CPU-time claims. Source scans and fitting occur only where
the earlier stages permit them; missing stages never acquire zero-valued scores.
Packet generation, post-run verification and the separate adapter preflight are
excluded from these corpus timings.

Recorded work is kept in separate units: **4,557,876** independent ROI ray
primitive tests; **95** logical source-point checks; **3,537** logical discovery
segment checks; **8,432,810** native fitter work units for one pass, or
**16,865,620** including identical repeats. Cached queries, unrecorded catalogue
work and source-removal work prevent a single honest combined work total.

Verification rehashed all **57 BSPs**, **37 surviving captures**, **45 catalogues**
and **193 original source images**. All **425 selected fitting rows** match the
original BSP RGB8 bytes and original face planes; sample IDs and geometric blocks
are disjoint within each region. All five native numerical reports repeat
identically. Ten Python checks and ten focused CPU regression tests pass. The
one-fixture cohort adapter also prepares and freezes a real original region
without opening a fitting score.

The historical core and canonical **113 source files** match the pre-run hashes.
No historical solver, canonical codec/storage contract, renderer/world shading,
GPU/runtime consumer or double-lighting behavior changed. Licensed raw data, XYZ,
textures and source configurations remain under ignored `build/asr25-v1/`.

Private protocol SHA-256:
`83698daacae11af605b0352f0147fbb1195124e91f8c401d45d22bf9593dbe7d`.
Frozen policy SHA-256:
`c8e9fcd3bd15786704defd8ab616c27310824c820eeb89563d419cebfa3fffd5`.

**Historical source-count truth, exact historical XYZ and absolute SI remain
Unknown; not-yet-supported shapes remain Unavailable.** MP retains qualified SP
material interpretation. Neither accepted hypothesis is an exact historical
lamp or a canonical `Resolved` lighting asset.
