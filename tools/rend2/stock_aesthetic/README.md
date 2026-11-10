# ASR24 original stock aesthetic source study

[Measured results and limits](../../../docs/rend2-stock-aesthetic-asr24.md) |
[public receipts](results/v1/summary.json) |
[all exploratory attempts](results/v1/attempts.json).

For the uniform 57-map inventory, current capture availability, independent ROI
coverage and explicit per-map blockers, see the [ASR25 audit](../stock_audit/README.md)
and [measured corpus report](../../../docs/rend2-stock-discovery-asr25.md).

This controller connects existing providers: ASR09 visible BSP fixtures, ASR14
lightmap descriptors, ASR15 associations, ASR12/13 grid/geometry, ASR07 solar,
ASR20 fitting and optional ASR21 sparse selection. It introduces no new fitter,
ray tracer, canonical asset writer or renderer consumer. Python 3.8+ and the
existing Pillow dependency are sufficient.

Licensed original assets0–3 and original observation captures are required.
Missing stock assets are **Blocked**, not a successful synthetic substitute.
Full geometry, source images, XYZ and original RGB8 stay under ignored `build/`.
Public receipts contain counts, losses, limitations and hashes.

## Reproduce the positive path

Run from the repository root, with the existing native toolchain on `PATH`.
Use fresh output directories. The default original assets are under
`build/lightrecon-evidence-runtime/final-source/stock-assets/base`; flags can
override paths to equivalent licensed local captures and exports.

```powershell
cmake --build build/s06-gcc --target lightrecon_aesthetic_tool lightrecon_grid_geometry_tool lightrecon_solar_audit lightrecon_bake_forward_tool lightrecon_dataset_quality -j 2

python -X utf8 tools/rend2/bsp_fixtures/catalogue.py --base build/lightrecon-evidence-runtime/final-source/stock-assets/base --map maps/mp/duel10.bsp --mask-axis 32 --max-work 1000000 --output build/asr24-original-catalogues/mp_duel10
$taskCapture = "$env:TEMP/jk-lightrecon-all-stock-20261031/capture/mp_duel10/home/rend2/lightrecon/v1/observations/mp_duel10_all.jkobs"
build/s06-gcc/lightrecon_dataset_quality.exe scan $taskCapture build/asr24-original-catalogues/mp_duel10

python -X utf8 -m tools.rend2.stock_aesthetic.discover --catalogue build/asr24-original-catalogues/mp_duel10/catalogue.json --capture $taskCapture --out build/asr24-fresh-discovery.json
python -X utf8 -m tools.rend2.stock_aesthetic.run prepare --work build/asr24-reproduce --discovery build/asr24-fresh-discovery.json --local --common-normals --receiver-path
python -X utf8 -m tools.rend2.stock_aesthetic.run evaluate --work build/asr24-reproduce --discovery build/asr24-fresh-discovery.json --local --common-normals --receiver-path

python -X utf8 -m unittest tools.rend2.stock_aesthetic.test_protocol -v
```

The existing full captures remain local. `--stock-private` overrides the captured
MP map root. `--base`, `--catalogues`, `--rows`, `--captures` and native-tool flags
override other paths. Never replace original archives with controlled fixtures.

`discover` reads original visible-fixture and grid/geometry evidence, **not luxel
RGB**, and ranks a bounded census of static luminous components. It retains all
candidates, original IDs, support/rejection checks and hashes. At most 256
eligible components with at least 500 WU² luminous support are considered; an
exceeded budget fails rather than silently truncating. The numerical follow-up
freezes the top three ranked components. This is not map-wide lamp recall.

`--receiver-path` traces the fixture's original outward direction through the
existing native provider and uses a known geometric opaque hit to define the
receiver. The maximum trace is 768 WU; bounds include source and hit, with 192 WU
tangential and 32 WU axial margins. Only that original receiver surface is fitted.
Unknown or mover-dependent hits remain unavailable. A farther outside-world ray
endpoint does not certify empty space beyond the known blocker.

`--local` uses 128 WU blocks and a separate split seed. `--common-normals` selects
same-material/signed-normal support by known-positive geometric response in every
fold. The finite design compares 64 geometry-only block origins. Three times a
planar per-axis original footprint bound, plus 1 WU, separates folds. Stable
original IDs cap each fold at 48 samples. Actual blocks and represented areas
are reported. Original bake filtering remains uncertain.

`protocol.json` records original hashes, source hypotheses, boundaries, bounds,
policy, implementation and executable hashes before scoring. `freeze.json`
additionally pins every chosen material, fold, spatial layout and input payload.
Implementation/native snapshots remain alongside each run. Evaluation checks
the freeze before/after fitting and repeats native output byte for byte. The
native tool opens held-out targets only after fitting/validation have frozen.
This is a trusted-controller/API boundary, not an adversarial OS sandbox.

## Original multi-map cohort

The default `prepare` / `evaluate` cohort retains five Hoth lamp/blue-patch ROIs,
three `mp/duel4` ROIs and a geometry-selected Hoth exterior. Generate original
catalogues for `hoth2`, `hoth3` and `mp/duel4` using the same command above. The
Hoth original rows/captures default to `build/x02`; scan the original `mp/duel4`
capture into `build/asr24-original-catalogues/mp_duel4` with `dataset_quality`.
Then omit `--discovery --local --common-normals --receiver-path` to reproduce the
original frozen cohort. Missing/disjoint receiver coverage and source-admission
failures are retained. See `results/v1/attempts.json` for later exploratory
geometry, shape and sparse studies; they are not independent replicates.

## Native adapters and evidence

`mounted-point id originalSurfaceOrdinal x y z upperR upperG upperB` adds a named
mount to the existing CLI. `SourceSupport::FromSurfaces` / `Check` verify original
contact, source side, occupied support and BSP world solids. Maximum stand-off is
8 WU; this study fixes 4 WU before fitting. Zero model area describes a point
approximation, not a measured zero optical fixture area.

`JKASR24-GEOMETRY-ONLY-1` / `--coverage` calls existing `Shape::Prepare` and
`Block::Evaluate` with fixed unit amplitudes and zero dummy target RGB. It uses
the same scoped occupancy as the fit. Unknown optical coverage is counted and
excluded; known blocked responses stay known zero. Poisoning those dummy RGB
values changes no geometry result.

The bridge's new `segment` query classifies the actual convex segment through
existing BSP `ClassifySupport` and existing optical visibility. The older `ray`
operation keeps its enclosing-box rule. Additive opaque-hit fields expose the
existing native query result; no geometry provider is duplicated. ASR15 retains
its stricter unknown mover/surface-endpoint behavior. Budget refusals are explicit
and publish no partial associations; they do not become empty configurations or
stop an independent admitted-fixture/grid/fit path.

The fit compares modern relative incident predictions through a fixed conditional
NetRadiant encoding with original RGB8 quantization/saturation intervals. Gain
is fixed at 1, RGB powers bounded at 100000, and constant nuisance RGB bounded at
255. The existing objective uses a 200-sweep budget and declared `1e-4` relative
robust-loss stopping tolerance. RGB8 is never calibrated linear irradiance;
source power, color, transfer, material response and indirect transport remain
conditional. Historical `Resolved` and canonical/runtime storage are unchanged.

An accepted aesthetic candidate requires positive converged fitting, known source
admission/side, no upper-bound hit, validation selection, at least 10% held-out MSE
and 5% robust-loss improvement, and at least three distinct agreeing original
grid records with known-static rays. Grid directions use the explicit
`consumer256_unknown` interpretation; mover-independent counts are separate.
`audit.py` also requires a real frozen source contribution: at least one held-out
receiver changes by a full code and source removal at the **same** background
worsens interval MSE. Native encoding supplies the replay, with score agreement
checked. Audits do not refit or select alternatives.

The optional `--area-groups`, `--point-groups` and `--sparse-neighbours` flags
preserve investigated configurations using existing original-face/component and
ASR21 providers. They have no passing claim in the published exploratory runs.
No hypothetical shape or source is certified as an exact historical lamp.

Each numerically attempted ROI contains original fixture evidence, mapped folds,
geometry/grid rays, training patches, association outcome, solar audit, native
result/repeat, source-removal audit, `hypothesis.json`, `scene.obj`, `geometry.png`
and `report.html`. Every selected ROI has an assessment in `summary.json`;
unmeasurable regions retain `selection.json` with the reason. Licensed raw data
and coordinate-level diagnostics are not redistributed in Git.
