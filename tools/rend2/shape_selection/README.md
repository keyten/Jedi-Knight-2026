# ASR19 modern shape selection

Offline implementation: `ShapeSelection::Rank`, `FitAndValidate` and
`AuditHeldout`. The stable output keeps actual fixture geometry, reason codes,
uncertainties, placement and photometric equivalence separate. It reuses the
existing source-support and Shape evaluators without changing historical
transport, canonical storage or runtime lighting.

```powershell
cmake --build build/s06-gcc --target lightrecon_shape_selection_test lightrecon_map_solver -j 2
build/s06-gcc/lightrecon_shape_selection_test.exe build/asr19-owned.json
python -X utf8 tools/rend2/shape_selection/run.py --private "$env:TEMP/jk-lightrecon-all-stock-20261031" --out tools/rend2/shape_selection/results/asr19-v2
```

Choose a fresh output directory. The runner freezes native/source/input hashes,
repeats owned witnesses and a stock map, and verifies unchanged ASR18 admission
states/reasons. Licensed stock geometry stays in private JSONL; public reports
contain no stock coordinates. Stock masks without exact clipped triangles remain
Unknown, with bounded whole-face priors explicitly labeled as approximations.

See [the ASR19 report](../../../docs/rend2-modern-shapes-asr19.md) for policy,
numeric fixtures, caveats and validation.
