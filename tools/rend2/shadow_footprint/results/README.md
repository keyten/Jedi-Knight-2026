# X04 reference witness

See [the technical report](../../../../docs/rend2-shadow-footprints-x04.md).

* `boundary.json` / `boundary.csv`: all35 original seam observations with original
  page/texel/world/footprint/normal identity, signed residuals, declared suns,
  original geometry and clipped trace hits. Own-content compiler fixture only.
* `boundary.svg`: spatial table across the original seam, compiler versus old
  point formula and restricted source-derived trace-tree replay.
* `benchmark.json`: value/gradient/finite-source/optical/localization results with
  source hashes and explicit uncertainty. No timing noise in this file.
* `methods.csv`, `derivatives.csv`, `finite_derivatives.csv`, `localization.csv`:
  complete measured numerical rows; see scope/units in the report.
* `witness.json`: fresh22-case compiler receipts, seven changed sampling recipes,
  binary hashes and generated report input SHA256 values.

`build/x04/verified/result.json` and `verified-repeat/result.json` are byte-identical,
SHA256 `e6c9755f79d0e0dd63e0fffca478f0d03cd654292ceefe55166374675454a895`.
Full logs/BSPs and separate Windows wall-clock/peak-working-set measurements stay
under ignored `build/x04/`. Source-file identities and original BSP reference are
checked, with no modification of historical goldens/tolerances.

Byte-exact boundary replay is verified only for this pinned opaque/sky room, not
all historical compilers. Gauss/adaptive errors are estimates. Finite-emitter
derivatives retain unresolved source-quadrature aliasing. Coplanar/unknown optics
do not become transparent. The SI analytical experiment does not calibrate JA
lightmap bytes or identify an authored physical lamp. No runtime lights/shadows,
complete inverse solver or historical Raven profile is introduced.
