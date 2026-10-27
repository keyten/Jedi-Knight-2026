# X03 numerical witness

See [methodology, assumptions, results and exact commands](../../../../docs/rend2-bake-identifiability-x03.md).

* `witness.json`: scalar diagnostics, fitted parameters, profile/evaluation hashes,
  source-lock checks, fresh compiler checks and explicit stock trial availability.
* `metrics.csv`: all measured profile rows (validation/held-out interval and byte
  RMSE, parameters, encoding, budget and independent block counts).
* `tables.md`: generated first-row validation preference. **Ties use stable ID
  ordering**; a first row is not unique identification. Inspect the full CSV and
  regret/sensitivity analysis in the research document. Blocked-only stock trials
  have no ranking and therefore do not appear as numeric winners here.

Generated with `report_x03.py` from `build/x03/accepted/result.json` and the fresh
`build/x03/compiler-all22/result.json`. Two full independent runs are byte-identical,
SHA256 `541d7d207c3b0ab6fd4582fd0a2714dedde4e8d3bbb4196d00322fa244468d9a`.
These files contain no licensed BSP, texture, lightmap RGB or retained entity
payloads. Compiler fixtures remain self-authored; stock data stay private under
ignored `build/`. This witness is not a verified Raven/compiler identification,
physical-light reconstruction or calibrated photometry. `UnknownProfile` remains
the stock identity. GI, arbitrary visibility/filter replay and historical HDR
compiler output are unverified.
