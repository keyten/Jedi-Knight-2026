# S01 numerical witness, 2026-10-28

See [the implementation/support report](../../../../../docs/rend2-bake-forward-evaluator.md).
`summary.json` compares production C++, Python kernels and the original 22
source-locked BSPs. Two bounce cases remain explicitly unsupported.
`cross-map.json` adds eight independent compiler fixtures; `thin_door` is partial
because 25 original UV positions require compiler nudge. `x05-score.json` records
all required frozen positive/negative/status/encoding contracts.

`physical-comparison.json` uses SI reference fixtures in a separate domain.
`support.json` is the feature/profile/source/output matrix. `disabled-path.json`
records the actual GL compile/link and baseline comparison. `manifest.json`
pins code and reports initial failures as well as supported results.

Timings and OS-measured RSS vary between runs. Source signatures have both exact
local-byte and LF-normalized hashes; numerical identities are reproducible.
No BSP, source image, stock asset, hidden answer file or renderer capture is
stored here. All compiler fixtures referenced by hashes are the existing
redistributable own-content tests. Reproduction commands are in the report.
