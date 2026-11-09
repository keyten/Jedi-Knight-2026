# ASR15 fixture / illumination associations

Tool-only Python 3.8+ diagnostic. No runtime or canonical lighting consumer.
Run from the repository root:

```powershell
python -X utf8 -m unittest tools.rend2.fixture_association.test_associate -v
python -X utf8 -m tools.rend2.fixture_association.evaluate --out build/asr15/validation.json
```

To reproduce the frozen repeat check, run `evaluate` twice with fresh output
paths, then `python -X utf8 -m tools.rend2.fixture_association.verify A.json B.json
--out build/asr15/verification.json`. Optional `--panel-a` and `--panel-b` check
two copies of the compiled-panel command below, including its frozen unknown/
blocked-ray accounting. `results/v1` contains the checked analytical report and
verification/input-hash manifest.

The API is `associate(fixtures, observations, geometry=None, policy=None)` in
`associate.py`. It returns every fixture/observation pair with ranked compatibility,
per-support reasons, unknown terms, selection state and uncertainty. `selected`
indicates a positive penalized coverage gain. `selected_conditional`, `ambiguous`
and `rejected` describe direct-source hypotheses; none certifies a lamp.

Use original ASR09 BSP fixtures and/or ASR11 world-placed MD3 emission catalogues,
ASR14 descriptors made by its `run` command, and the existing ASR13 geometry/grid
bridge. The CLI checks matching map hashes, pins all input hashes and optionally
checks the original receiver-export hash before loading dark controls:

```powershell
python -X utf8 -m tools.rend2.lightmap_patches.run `
  --rows build/x02/compiled-fixtures-frozen/panel.rows.jsonl `
  --source build/x02/compiled-fixtures-frozen/panel.source.json `
  --out build/asr15/panel.patches.json

python -X utf8 -m tools.rend2.fixture_association.run `
  --fixtures build/asr09/panel/catalogue.json `
  --patches build/asr15/panel.patches.json `
  --rows build/x02/compiled-fixtures-frozen/panel.rows.jsonl `
  --geometry-tool build/lightrecon-standalone/Release/lightrecon_grid_geometry_tool.exe `
  --capture build/x02/compiled-fixtures-frozen/panel.jkobs `
  --grid-roi -128 -128 16 128 128 224 `
  --out build/asr15/panel.associations.json
```

`--grid-roi` selects original cell centres by geometry and includes every supported
record in that ROI. The bridge refuses more than 4096 cells. The default direction
interpretation is `consumer256_unknown`; `--grid-interpretation writer255_merge4`
is an explicit alternative assumption, not historical compiler identification.
Weak, missing, nonfree, dynamic, malformed-style and multiple-style direction
records are rejected with counts. Shared records share a correlation group.
Fixture and grid evidence are matched directly; no source-position answers from
ASR13 triangulation enter the association.

`--fixture-roi X0 Y0 Z0 X1 Y1 Z1` explicitly selects catalogue fixture centroids.
Use ASR14's geometry-only `--surface` / `--roi` when generating a smaller patch
report. There is no target-dependent top-K selection or silent downsampling.
Without geometry the CLI retains ranked ambiguous links and selects no source.
Surface-origin rays and fixture-centroid rays keep the existing bridge's unknown
or blocked result. No arbitrary endpoint displacement turns them into clear rays.

For controlled or other already-normalized evidence, `--evidence evidence.json`
accepts `org.openjk.fixture-association-evidence.v1`:

```json
{
  "schema": "org.openjk.fixture-association-evidence.v1",
  "map_sha256": "owned-controlled-scene-v1",
  "fixtures": [{
    "id": "fixture-1", "position": [0, 0, 128], "outward": [0, 0, -1],
    "radius_world": 4, "colour": [1, 0.6, 0.25], "static_eligible": true
  }],
  "observations": [{
    "id": "region-1",
    "samples": [{
      "id": "original-grid-record-1", "position": [0, 0, 64],
      "incoming": [0, 0, 1], "colour": [160, 96, 40],
      "correlation_key": "original-grid-record-1"
    }]
  }]
}
```

This example has no geometry and therefore remains ambiguous. Optional support
fields are `normal` (geometric), `gradient` (world tangent code gradient),
`incoming` (aggregate incoming grid direction), relative `colour`, original
`rgb8`, `material` and `style`. Missing vectors are omitted or null. Observations
may add `dark_samples`, `projection: {foot, normal, radius_world}`, and uncertainty
strings. Fixtures may add uncertainty strings. Unknown size/colour/orientation
must be null, never a fabricated zero measurement. Dark controls require original
uncensored codes and matching material/style/normal; they cannot be arbitrary
black points in unknown geometry. Caller-supplied normalized evidence remains a
declared hypothesis with its own provenance, not automatically original evidence.

Default limits: 1024 fixtures, 256 observations, 4096 bright supports, 4096 dark
supports, 16384 fixture/observation pairs, 131072 fixture/bright-support pairs,
four million counted scoring/assignment operations, 16 catalogue files, 64 MiB
per JSON input and 64 MiB serialized output. Fixture catalogue scans cap at
100000 entries; original row loading keeps ASR14's input bounds. Dark-control
selection separately caps the patch-by-receiver scan at four million operations.
The native bridge has its existing traversal/work limits. Failure publishes no
partial assignment; existing output files are not removed. These are data/work
bounds, not a measured process-RSS guarantee. Use fresh output names for studies.

See [the method and validation record](../../../docs/rend2-fixture-association-asr15.md).
