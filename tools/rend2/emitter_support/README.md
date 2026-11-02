# X14 source support

Native implementation: `SourceSupport::{Validate,FromSurfaces,Check}` and
`RegionDomain::ClassifySupport`, used by production Shape and Joint feasibility.
No shader or runtime light publication. `mounting-only` is a geometry-only
stock audit; it does **not** claim a joint fit or source recovery.

```powershell
$env:PATH = 'C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;' + $env:PATH
cmake -S tools/lightrecon_test -B build/s06-gcc
cmake --build build/s06-gcc --target lightrecon_emitter_support_test lightrecon_map_solver -j 2
build/s06-gcc/lightrecon_emitter_support_test.exe build/x14-native.json
python -X utf8 tools/rend2/emitter_support/run.py `
  --private "$env:TEMP/jk-lightrecon-all-stock-20261031" `
  --out tools/rend2/emitter_support/results/v3
```

Use a new output revision for a changed executable/protocol. The runner freezes
hashes before evaluation, preserves original BSP/dataset hashes, repeats the
native witness and `mp/duel4`, and keeps geometric TSVs in the private temp root.
No proprietary BSP, vertex list, lightmap or source image is copied here.

The admitted geometry is a conditional hypothesis, not a certified original
lamp. Stock compiler identity, absolute scale, emission masks, bake model states
and independent receiver visibility remain separate limitations. Source-support
keys enter Shape/Joint/Producer cache identities. Existing S08 legacy vacuum
checks remain available for unchanged callers and frozen before/after controls.

See `docs/rend2-emitter-mounting-x14.md` for contract scope and results.

Version 1 preserves the initial exact-support audit. Version 2 adds the explicit
solid-leaf boundary-contact regression; version 3 also tests colour-sanitized
geometry leases. None of these changes admitted a stock lamp. The exact X12
rectangle/tube IDs are linked by `compare_witnesses.py`, with coordinates retained
only in the private TSVs. Native `rays`/`work` measure the standalone reference
value checks, not every fitting/Joint operation; whole-process time/RSS include
the fit and Joint smoke test.
