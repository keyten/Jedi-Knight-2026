# X18 — independently evaluated original-BSP appearance fields

## Result

The existing P01 observation route can produce useful **local, conditional RGB8
appearance proxies**, but the measured coverage does not justify a general stock
character-lighting replacement. Four of fifteen preselected geometry ROIs admit
a sparse field: two on `vjun2`, two on `mp/duel4`. Three pass the predeclared
common-domain improvement gate. The other eleven remain unsupported, including
all selected ROIs on `hoth3`, `hoth2` and `t1_danger`.

There are **zero invented physical emitters, zero calibrated irradiance/radiance
fields, zero inferred GI components and zero enabled consumers**. This work does
not change X17's absence of an accepted original-stock physical source model.
The physical source-conditioned P01 route and its strict API guards remain intact.

Exact per-ROI metrics, exclusions, original BSP/archive hashes, folded geometry
manifests, source hashes, native executable identity and environment are in
[`tools/rend2/appearance/results/v1`](../tools/rend2/appearance/results/v1).
The numerical source baseline is commit `ad6a7395a`; the protocol records exact
working-tree hashes for the X18 implementation. Original V02, X12, X17 and
57-map v4 results were not overwritten.

## Quantity and information content

The target is **original static-style-0 lightmap RGB8 code appearance**. It retains
unknown original transfer, primaries, exposure, bake normalization, reflectance
and compiler history. It is neither lux nor nits, neither incoming radiance nor
physical normal-dependent irradiance, and has no direct/indirect attribution.
RGB interpolation occurs in this declared encoded proxy domain. It is not a
linear-light energy calculation. A target code of 255 is an observed appearance
code, not an estimate of the original pre-clipped illumination; saturated and
unsaturated evaluation groups are reported separately.

The fitted quantity is L0: one nonnegative RGB value per generated probe. The
existing angular codec's constant coefficient is only a storage/evaluation
mechanism. The canonical descriptor is `LegacyAppearance`, not an irradiance
claim. No normal-dependent SH L2/L4 coefficients, sharp beams, sun, lamps or
directional radiance are inferred. Higher angular order would not manufacture
missing independent measurements at the same world position.

Lightmaps constrain colour at particular original receivers with known normals,
subject to material/bake ambiguity. Different surfaces are not repeat measurements
of every normal at one free-space point. The immutable lightgrid supplies a
different historical ambient/directed/direction proxy. Its directions and scales
are not independently verified physical angular measurements. Neither dataset
establishes a unique free-space radiance distribution or a physical GI field.

The test query is **16 WU along the original oriented geometric normal**, with
the original surface RGB as its surface-anchored proxy target. Original sample
positions/RGB are never moved or relabelled. This is an explicit approximation
between a surface observation and nearby free space, not ground truth for an
entity at that location. Historical lightgrid samples are read at their original
positions by interpolation and are never relocated with probes.

## Implementation and support

Reused: P01 `Fields::Build/Prepare/Encode/Decode`, P02 generated topology and
convex free-space interpolation certificates, P03 constant SH codec, original
offline sample IDs/footprints, `StaticVisibility`, P06 membership rules, V01
`Disk::Persistent::Store/Load`, immutable conditional CPU queries and V02 runners.
No second lighting asset, collision system, field API or cache format was added.

For probe p, choose up to 32 nearest training observations within 192 WU using
distance then stable observation ID, independently of their RGB. For supported
static geometric paths, compute

\[
 a(p)=\frac{\sum_{i\in S(p)} w_i c_i/(1+\|p-x_i\|^2)}
              {\sum_{i\in S(p)} w_i/(1+\|p-x_i\|^2)}.
\]

This is empirical interpolation of observations, not transport of energy.
Samples are geometry-deduplicated before fitting; `w_i=1` represents one selected
spatial/normal correlation group. It is not reflector area or a map-wide
statistical expansion weight. Actual selected chart area is reported separately.

Original surface endpoint IDs permit the existing ray API's attested endpoint
contact rule. They do not exempt unrelated occluders or introduce a source-side
clearance offset. Opaque walls reject the connection. Unknown optical, patch or
missing geometry rejects that particular support connection, retaining separate
Unknown counts. An unrelated unknown connection no longer discards all otherwise
supported appearance evidence at a probe. No valid evidence gives no probe value.

P02 queries use certified support volumes and immutable fixed neighbour stencils.
Every required neighbour must have a value. No surviving-value renormalization
fills sparse holes; zero admissible support returns `Unavailable`. Region IDs
alone neither prove transmission nor prohibit a legitimate opening. Static
geometry certification remains separate from unknown moving-door/runtime state.
Stock queries explicitly opt into conditional static support; they are not
certified for every door state or missing alpha mask.

New production controls are bounded neighbour selection and explicit independent
validation coverage. `appearanceMinimumValidationFraction` defaults to **1**,
preserving the original complete-validation policy; this benchmark explicitly
requests .25. Missing validation predictions remain in the denominator. The
builder rejects a model below the declared coverage/error gate. Failure clears
asset/descriptor/values while retaining actual work and diagnostic receipts.
Failed preparation is never advertised as an installed field.

## Frozen experiment

The five-map cohort and settings were frozen before its final evaluations:
`hoth3`, `hoth2`, `vjun2`, `mp/duel4`, `t1_danger`. Each map has three 512-WU
cubes centred at ordinal quartiles of original static planar surface geometry.
These are geometry-defined computational ROIs, not three certified rooms. ROIs
can overlap; their sums are not unique whole-map coverage.

- Planar, mapped, static-style-0, supported-material original 128x128 RGB8 pages.
  Ambiguous/duplicate/mixed footprints, moving/non-planar surfaces and unsupported
  material/style/encoding classes are excluded with counts.
- Geometry-hashed 128-WU blocks: 3/5 training, 1/5 validation, 1/5 final held-out.
  Gap is 8 WU **plus the maximum distance to the original clipped footprint
  vertices**, not a radius inferred from area. Historical filter support beyond
  this declared guard remains unknown.
- One lowest stable sample ID per 32-WU spatial/normal group, at most 256 rows
  per fold/ROI. Selection uses geometry and stable IDs, never final RGB.
- Generated probes: 64-WU coarse / 16-WU minimum spacing; 1-WU clearance;
  maximum 10,000 probes, 50,000 nodes, 80,000 links and 64M topology work per ROI.
- Field: 192-WU radius, 32 neighbours, 200,000 rays, 100M work, 180 s per build.
  At least 8 training, 6 validation and 6 held-out selected rows to attempt fitting.
  Admission requires >=25% validation predictions and relative RGB RMSE <=.6.
- Final RGB is not passed to `Fields::Request`. Validation only admits/rejects
  the fixed field; it does not change its values, density, radius or weights.
  Final metrics are measured after coefficients/admission freeze.
- Empirical usefulness gate: >=6 final predictions, >=25% final rows, no blocked
  supported rays and >=5% common-domain RMSE improvement over both constant and
  original-grid proxies. A zero-error baseline cannot establish an improvement.
  This gate is not a physical confidence or general consumer suitability test.

Initial `build/x18-stock-v1` used an insufficient area-derived footprint guard;
its results are preserved as **preliminary**, not final evidence. V2 corrected
that guard without tuning numerical parameters. V3 only retains rejected-build
work receipts. V2/V3 sample manifests, admission, predictions and accepted
dataset bytes are identical. No original frozen stock run was replaced.

## Independent quality

Errors below are RGB8-code RMSE on the **same supported held-out rows** for all
four methods, not physical irradiance error. Full frozen denominators, baseline
errors on their own larger support, normal/brightness/saturation/space groups
and every failed ROI are in `tables.md` and `stock.json`.

| Map / ROI | Final rows | Selected chart-area coverage | Constant | Original-grid proxy | Nearest surface | Geometry L0 |
|---|---:|---:|---:|---:|---:|---:|
| vjun2 / 0 | 40/128 | 3.6% | 57.97 | 152.62 | 50.98 | **29.82** |
| vjun2 / 1 | 6/19 | 48.1% | 66.94 | 65.99 | **23.43** | 41.00 |
| mp/duel4 / 0 | 14/30 | 50.8% | 59.62 | **28.62** | 35.92 | 29.58 |
| mp/duel4 / 1 | 42/88 | 51.8% | 70.28 | 102.72 | 52.97 | **50.04** |

`vjun2/0` improves versus grid by 80.5% and versus constant by 48.6%, but its
area coverage is extremely narrow. `vjun2/1` passes the grid/constant gate while
nearest-surface prediction is substantially better. `mp/duel4/0` does not beat
the original grid. `mp/duel4/1` improves versus grid by 51.3%, versus constant by
28.8%, and only modestly versus nearest. These differences do not establish
entity appearance truth or recover original Raven light sources.

| Map | Eligible mapped chart area before folds | Admitted ROIs | Remaining failures |
|---|---:|---:|---|
| hoth3 | 71.25% | 0/3 | ROI0 3/16 validation support; ROI1 only 5 held-out rows; ROI2 validation relative error .807 |
| hoth2 | 62.06% | 0/3 | ROI0 no validation predictions; ROI1/2 lack sufficient independent folds |
| vjun2 | 92.78% | 2/3 | ROI2 no supported validation predictions |
| mp/duel4 | 69.48% | 2/3 | ROI2 no supported validation predictions |
| t1_danger | 42.48% | 0/3 | ROI0 >10,000 probe budget; ROI1 validation error .656; ROI2 only 10/59 support despite .231 conditional error |

These eligible-area fractions concern the appearance observation domain. They
are **not** X17 historical forward-operator support, disjoint transport area,
lighting-covered map area or evidence of physical source correctness.
`hoth2` includes genuinely dim selected regions: on ROI0 the constant's full
held-out RMSE is 1.41 versus the grid proxy's 21.18. Darkness alone does not prove
the new field works; that field remains unavailable there.

The original-grid baseline follows the current `tr_light.cpp` static style-0 LDR
entity trilerp: ambient + directed times clamped dot with blended legacy direction,
unit ambient/direct gains, no minimum ambient/dynamic additions/display transform.
Animated-style grid records are unavailable. This is a reproducible historical
entity-response proxy, not an assertion that grid ambient equals surface RGB.
The full original grid is independent public input, not held-out lightmap answers.
Large cross-domain errors can reflect different encoding/bake semantics as well
as spatial approximation. MP material interpretation remains the original frozen
SP capture context, not independently verified MP optics.

## Geometry, directionality and controlled tests

All **696** tested successful held-out query-to-probe support segments agree with
the static visual ray API: 0 blocked and 0 unknown. Blocked and unknown
probe-to-observation connections are separately retained/excluded. This certifies
the tested static support, not absent alpha masks, runtime doors or whole-map
coverage. The controlled opaque-wall bright/dark appearance fixture remains
leak-free, including an explicit zero-valued observed room. A room with missing
lighting support returns `Unavailable`, not successful black.

A doorway narrower than the certified probe resolution remains unsupported.
A separate resolution-supported opening passes the appearance interpolation
control. It is not promoted into a doorway lamp. Opposite receiver normals return
the same L0 appearance value; normal-binned held-out errors expose this limitation.
Unknown alpha/patch/material paths and incomplete topology remain uncertain.
There is no stock fog phase-function, specular or incoming-radiance accuracy claim.

The existing independently sampled authored physical route remains separate:
point/spot error 1.52%, rectangle 1.32%, tube 2.03% in its controlled point tests;
fine red-bounce spatial RMSE 6.34% on 30 independent positions. Its coarse point
field has 26.74% relative spatial/normal RMSE and 1080/1800 unavailable queries.
These are supplied-source physical controls, **not** scores for stock appearance,
blind source discovery or universal P01 accuracy. They demonstrate why neither a
smooth field nor a successful codec is sufficient quality evidence.

## CPU API, membership and persistence

The existing opt-in `SampleConditionalField` returns a `LegacyAppearance` value
with `Relative` units, encoded-source colour, unknown transfer/primaries,
calibration/profile/configuration, geometry/topology identity, publication
generation, support IDs/weights and partial/uncertainty metadata. This route has
unknown source roots/reflection orders. Both addition and baked-direct replacement
are forbidden. A field is never promoted to `Resolved` by good RGB fit.

`SampleIndirect`, `SampleIncomingRadiance`, physical photometry and exact-source
eligibility are unchanged. The prepared appearance evaluator advertises neither
physical capability. Unknown or absent sparse entries cannot become zero values.
Queries use resident immutable coefficients/topology; no disk, rays, fitting,
lazy mutation, GPU resources or query-time reconstruction are involved.

`Fields::kVersion` is now **3**, invalidating older derived spatial-field payloads
explicitly. Canonical asset schema remains **8**. Observation IDs, original
datasets and contribution codecs are reused. Training/validation content, source
surface IDs and appearance settings are canonically hashed into provenance.
Neighbour selection is deterministic under evidence-order changes.

Every admitted stock ROI was stored/reloaded through the existing atomic `.jklr`
cache and selective `Disk::Persistent` preparation. Repeated conditional queries
match the live field; physical capabilities remain unavailable after reload.
Corrupt field chunks are rejected. The offline fingerprint binds the complete
frozen observation archive/captured material context, original BSP identity,
selection manifest, units and algorithm/settings. It does **not** assert complete
original Raven/SP/MP production dependencies or game map-load cache readiness.
Prepared files remain in ignored `build/x18-stock-v3/*-cache`; no copyrighted map
geometry or field payload is committed.

## Resources and repeatability

Windows 10.0.19045, Intel family 6/model 165, 12 logical CPUs; GCC 16.2 C++11
Release (`-O3 -DNDEBUG`). SP/MP DLLs use MSVC 2019 x64. Runs overlapped CPU tests
and compilation, so timings are observed resource costs, not isolated speedups.

| Map | Complete cold benchmark seconds | Peak process MiB |
|---|---:|---:|
| hoth3 | 11.89 | 93.55 |
| hoth2 | 5.94 | 60.86 |
| vjun2 | 18.70 | 137.41 |
| mp/duel4 | 4.75 | 74.32 |
| t1_danger | 24.98 | 43.49 |

Cold work includes archive fingerprint/read, full-map eligibility streaming,
static BVH, three bounded topology/appearance attempts and admitted cache round
trips. Failed builds retain their actual rays/work; no failure is zero-cost.
The native tool does not run an inverse solver.

Admitted `.jklr` files are 4.91–6.90 MB each, primarily conservative spatial
topology rather than one RGB coefficient. Their explicit warm load validates and
prepares resident CPU data without source fitting, probe construction or ray
tracing. Actual per-ROI cold/warm times, bytes/read counts and reserved decoded
budgets are in `stock.json`. Query p50/p95 are measured separately per ROI; the
small mixed-success/unavailable samples and coarse clock granularity are not a
latency guarantee or status-conditioned microbenchmark.

`mp/duel4` repeat has identical folded manifests, statuses, work, support, quality
metrics and dataset content keys. Two controlled P01 builds have identical semantic
results and canonical bytes. Timing/RSS are intentionally not compared for equality.

## Tests and commands

Native controlled assertions cover explicit black versus absent data, opaque
wall separation, supported doorway, missing support, sparse validation denominators,
evidence-order determinism, codec reload, corrupt chunks, physical capability
rejection and unchanged source-conditioned cases. Existing cache/lifecycle tests
cover stale snapshots, read-only preparation and resource ownership.
Final validation: **45/45 CPU tests** (221.40 s), **12,104 native field assertions**
twice with identical semantics/bytes, **19 Python tests**, and both SP/MP Release
DLL builds pass. The machine-readable validation receipt includes log and binary
hashes. The assertion count includes repeated read-only queries, not 12,104
independent physical scenes.

```powershell
$env:PATH='C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;'+$env:PATH
$py='C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
cmake -S tools/lightrecon_test -B build/s06-gcc
cmake --build build/s06-gcc --parallel 2
& $py -X utf8 tools/rend2/appearance/run.py --private "$env:TEMP/jk-lightrecon-all-stock-20261031" --out build/x18-stock-v3
& $py -X utf8 tools/rend2/field_builder/run.py --out build/x18-controlled-receipts
& $py -X utf8 tools/rend2/appearance/report.py --run build/x18-stock-v3 --preliminary build/x18-stock-v1 --out tools/rend2/appearance/results/v1
& $py -X utf8 -m unittest tools.rend2.appearance.test_report tools.rend2.path_support.test_reports tools.rend2.validation.test_report tools.rend2.producer.test_controller
ctest --test-dir build/s06-gcc --output-on-failure -j 2
$env:MSBUILDDISABLENODEREUSE='1'; $env:CL='/MP2'
cmake --build build/x16-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 1 -- /nr:false
& $py -X utf8 tools/rend2/lightrecon_regression.py --cpp C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin/gcc.exe --output build/x18-disabled-gpu
& $py -X utf8 tools/rend2/producer/compare_revision.py --revision ad6a7395a --gpu-report build/x18-disabled-gpu/report.json --out build/x18-disabled-delta
& $py -X utf8 tools/rend2/appearance/validate.py
```

Use **fresh output directories** when repeating; existing witnesses are not
overwritten. `--preliminary` is optional. The renderer build targets above are
DLLs, not game launches. All 27 disabled shader programs / 51 stages compile;
source/binding delta versus X17 is zero. The old `380ee0f7` shader baseline still
has its historical 7,278-line drift; it was not rewritten to force a pass.
No game was launched and no runtime visual improvement is claimed.

## Consumer eligibility and remaining work

- Experimental entity diffuse appearance: only an explicit compatible proxy
  policy, scoped support/coverage and a selected field; evaluate on independent
  entity positions/normals before deployment. `vjun2/0`'s tiny area coverage and
  `vjun2/1`'s superior nearest baseline prevent a general replacement claim.
- Fog incoming radiance, physical indirect GI, environment specular, exact local
  lights and baked-direct subtraction: **unsupported by these fields**.
- Total/direct/indirect composition: prohibited because roots/path membership is
  unknown. Do not add this proxy to explicit physical lights as extra energy.
- Whole-map appearance readiness: unsupported. Three fixed ROIs do not establish
  whole-map coverage, robust outdoor support, MP material equivalence or dynamic
  door correctness. `t1_danger` also demonstrates a bounded topology failure.

The next evidence needed is independent entity-position appearance/normal
reference, better spatial support coverage, uncertainty/error calibration over
new regions and explicit dynamic/material policies. Source-conditioned physical
fields remain a separate higher-quality route when their inputs are supported.
