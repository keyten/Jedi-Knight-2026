# ASR14 lightmap patches

Offline feature discovery from the existing `lightrecon_dataset_quality scan`
export. Python 3.8+, standard library; optional Pillow for diagnostic PNGs.
[Contract, measurements and limitations](../../../docs/rend2-lightmap-patches-asr14.md).

From the repository root:

```powershell
python -X utf8 -m unittest tools.rend2.lightmap_patches.test_patches -v
python -X utf8 -m tools.rend2.lightmap_patches.run --rows build/x02/compiled-fixtures-frozen/point_room.rows.jsonl --source build/x02/compiled-fixtures-frozen/point_room.source.json --out build/asr14/point.json
python -X utf8 -m tools.rend2.lightmap_patches.inspect --rows build/x02/compiled-fixtures-frozen/point_room.rows.jsonl --report build/asr14/point.json --out build/asr14/point.png --index 2
```

`--surface 3 4 5` or `--roi X0 Y0 Z0 X1 Y1 Z1` explicitly selects receiver
geometry. Cropped support remains clipped/partial. Input can be `.jsonl.gz`.
No target-dependent top-K crop is silently made to satisfy a budget. Limits
fail the request without publishing a partial descriptor file. Use fresh output
names when retaining a research run.

Optional `--geometry-tool build/lightrecon-standalone/Release/lightrecon_grid_geometry_tool.exe
--capture build/x02/compiled-fixtures-frozen/point_room.jkobs` reuses ASR13's
read-only geometry bridge. Build its existing CMake target first. The geometry
capture's map hash must match the receiver source manifest. Unavailable geometry
is explicit; the tool also works without this optional executable.

Frozen owned-compiler study, retaining full descriptors outside git:

```powershell
python -X utf8 -m tools.rend2.lightmap_patches.evaluate --out build/asr14/check-a --geometry-tool build/lightrecon-standalone/Release/lightrecon_grid_geometry_tool.exe
python -X utf8 -m tools.rend2.lightmap_patches.evaluate --out build/asr14/check-b --geometry-tool build/lightrecon-standalone/Release/lightrecon_grid_geometry_tool.exe
python -X utf8 -m tools.rend2.lightmap_patches.verify build/asr14/check-a build/asr14/check-b --out build/asr14/checks.json
```

Override `--fixtures` and `--suite` for the existing frozen export folder and
owned q3map2 suite. Missing inputs fail explicitly. `evaluate` writes every
compiler descriptor before opening source answers; it pins original rows,
source manifests, original BSPs, answers and implementation. The experiment is
a descriptive census, with no held-out fit score, no claim of OS isolation and
no blind-human claim. Existing benchmark partitions are neither altered nor
used as new independent fitting validation.

`results/v1` contains aggregate measurements, hashes and repeat verification.
Stock raw rows, geometry, full descriptors and diagnostic images stay in the
private build directory. For the measured stock chart:

```powershell
python -X utf8 -m tools.rend2.lightmap_patches.run --rows build/x02/mp_ffa3.rows.jsonl.gz --source build/x02/mp_ffa3.source.json --surface 3628 --out build/asr14/stock.json
```

No diagnostic is a canonical lighting asset. Radial projections, source-position
alternatives, major footprint axes and apparent boundaries remain qualified
features. They do not establish a primary lamp, emitter orientation, historical
identity, physical depth from intensity, calibrated irradiance or clear space.
