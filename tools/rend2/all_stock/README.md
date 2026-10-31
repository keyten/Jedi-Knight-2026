# Stock-map solver audit

This runs the real CPU `Producer::Run`, not the Python lighting formula. It is an
audit of the current solver and its admission failures, **not** a promise of
successful map reconstruction. A completed OS process is not a completed fit.

The controller scans all derived lightmap pages, selects geometry-only spatial
strata, constructs disjoint folds with footprint gaps, sanitizes vertex RGB from
the geometry and invokes the existing hidden/source, solar, consolidation,
joint-fitting, uncertainty and canonical-publication pipeline. If a stage rejects
the request, subsequent stages are not reported as executed. Fixed source XYZ
and hidden ground-truth answers are not supplied. The two numerical profiles are
explicit conditional NetRadiant hypotheses; stock historical identity stays
`UnknownProfile`. Legacy compiler Q is never candela.

Default limits: 512/256/256 rows in training/validation/held-out; one representative
per 512-WU position/normal stratum; 256-WU partition blocks; three footprint
radii plus one WU exclusion at block boundaries; split seed 20261031. The
full-map **census** is complete, while the attempted numerical problem uses a
bounded subset. The spatial stratification is a coverage proxy, not Fisher
information. No all-source-by-all-luxel matrix is allocated.

The outer runner limits each CPU process to 600 s / 1 GiB peak working set; the
joint stage has its own 180 s per-profile safety limit and deterministic work
limits. Process timing limits are safety limits, not deterministic convergence.
Peak RSS comes from Windows `GetProcessMemoryInfo`, not accounted vector sizes.
Each map is a resumable job; all attempted and rejected jobs remain in JSON.

Licensed PK3s, `.jkobs`, OBJ/payloads and coordinate-level debug TSVs remain under
the chosen **private** folder. Only aggregate counts/hashes/results are published.
The current capture uses SP shader interpretation even for MP BSPs; its optics
must not be presented as independently verified MP runtime material semantics.

Example PowerShell workflow, from the repository root (replace `python` with the
bundled runtime if needed):

```powershell
cmake -S tools/lightrecon_test -B build/s06-gcc
cmake --build build/s06-gcc --target lightrecon_map_solver lightrecon_dataset_quality -j 2
$private = "$env:TEMP/jk-lightrecon-all-stock-20261031"
python -X utf8 tools/rend2/all_stock/inventory.py --base build/lightrecon-evidence-runtime/final-source/stock-assets/base --output "$private/inventory.json" --commit 27119e46e
python -X utf8 tools/rend2/all_stock/capture.py --private $private --renderer build/s13-renderers/Release/rdsp-rend2_x86_64.dll
python -X utf8 tools/rend2/all_stock/run.py --private $private --revision fresh-run
python -X utf8 tools/rend2/all_stock/collect.py --private $private --revision fresh-run --out build/all-stock-fresh-results
python -X utf8 -m unittest discover -s tools/rend2/all_stock -p 'test_*.py' -v
```

The game capture requires the existing test runtime in `build/x02/runtime/run`
and licensed archives. It uses one sequential owned child at 320×240, picmip 4,
30 FPS, no sound, PBR, Forward+, LTC, fog, shadows, SSR/SSGI or AA. Source capture
uses immutable BSP bytes, never screenshots/GPU texture readback. Existing
captures are reused only with independent BSP-SHA validation by the CPU adapter;
prefix-only archives are re-derived for **all** pages with the existing exporter.

Private debug outputs:

- `census.json`: every-page mapping categories, source-area denominators,
  exclusion reasons, source hints, raw-domain/censoring and region counts.
- `folds.json`: positive signal, black interval-MSE baseline, distinct receiver
  surfaces/blocks and coarse normal diversity for every fold.
- `selected-geometry.tsv`: stable sample IDs, original page/texel/surface,
  positions, actual patch area and spatial-stratum weight; no observation RGB.
- `geometry.json`, `fixture-rays.tsv`, `ray-summary.json`: geometry completeness,
  bounded region coverage and geometry-only fixture-centre ray witnesses.
  These are not fitted-source visibility or independent proof of true emitters.
- `isq.json`, `linear.json`: producer state/reason, actually reached stages,
  joint work, model attempts, conditional sources, final assessment availability.
- Native/capture logs and `*-resources.json`: time, actual peak process RSS,
  budgets, errors and exact private commands.

The archive loader and independent validation/final callbacks belong to the
trusted controller. Numerical fitting receives training targets only. This is
an API boundary for the trusted C++ implementation, not an OS sandbox protecting
against arbitrary malicious native code in the same process. Existing separate
Python solver/evaluator view negative tests are also run.

The exploratory first adapter had float-footprint area/collapsed-triangle
errors and did not filter unsupported varying normals. Those errors are not
attributed to the production optimizer. The corrected frozen `v2` uses areas of
the actual float vertices, discards collapsed polygon slivers and explicitly
counts unsupported transport normals. Earlier exploratory logs stay private.
Do not change the native executable during a frozen run. Use a new revision
after changes; `--force` is an explicit replacement, not a determinism test.

The separate `--local --revision local-v3 --maps ...` protocol chooses one 512-WU
cell using geometry-only normal diversity/area, then 64-WU strata. It intentionally
does not widen the ROI or relax folds after inspecting validation/held-out values.
It can fail because the selected cell has too few independent folds. Its local
outcomes cannot stand in for full-map reconstruction. Use `collect.py
--attempted-only` for such a selected-map cohort.
