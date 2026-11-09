# ASR16 adaptive hidden-source proposals

Tool-only local discovery using the existing native `Hidden::Generate` scorer
and geometry admission. The production strict generator and scene scheduler
are unchanged. See [method and measured failures](../../../docs/rend2-adaptive-hidden-asr16.md).

Build the standalone tools, using your CMake executable:

```powershell
cmake -S tools/lightrecon_test -B build/lightrecon-standalone
cmake --build build/lightrecon-standalone --config Release --target lightrecon_adaptive_tool lightrecon_adaptive_test
ctest --test-dir build/lightrecon-standalone -C Release -R "lightrecon_adaptive"
```

For one existing sanitized training request with explicit local areas:

```powershell
python -X utf8 -m tools.rend2.adaptive_hidden.propose `
  --tool build/lightrecon-standalone/Release/lightrecon_adaptive_tool.exe `
  --geometry build/x02/compiled-fixtures-frozen/point_room-views/inputs/geometry.jkobs `
  --request build/asr01-v2/point_room/training.request `
  --out build/asr16/local-proposals.json
```

Use fresh output names. Inputs must be the geometry-only blind archive and a
`JKHIDDEN1` training request, never the complete BSP observation archive or source
answers. The wrapper freezes a separate `.adaptive.request` and pins all input
hashes. Missing source support produces an empty diagnostic, not absence proof.

To use ASR15 visible-fixture screening, additionally pass `--associations` and
`--area-observations`. The latter is an explicit JSON object such as
`{"1": ["original-patch-id"]}`, mapping native area IDs to ASR15 observation IDs.
An area is skipped only when **every** listed observation has a selected,
non-ambiguous conditional link with score at least .8, known term mass at least
.3 and at least 75% eligible supports. Empty mappings, partial links, rejected,
ambiguous and unavailable evidence stay eligible for local search. The map hash
must match. This is a conditional discovery policy, never physical certification.

The C++ API in `tools/lightrecon_test/adaptive_hidden.h` accepts `Hidden::Request`,
separate adaptive options, and returns standard proposal records plus an audit.
Explicit search areas are required. `explainedAreas` implements caller-supplied
fixture screening. Supplied original-grid/deluxe `DirectionConstraint` values
can seed local ray volumes; their mixture angle widens the volume. Geometry and
surface evidence still determine admission and score. Native protocol
`JKADAPTIVE1` extends `JKHIDDEN1` with:

1. A policy line after the six search options: `strategy axes balanced refineSeeds
   maxRays maxForwardCalls maxWork`, where strategy 0 invokes strict search and
   strategy 1 invokes adaptive search.
2. After the profile selectors: explained-area count and IDs, then direction
   count and rows `id originXYZ toSourceXYZ mixtureHalfAngleRadians`.

`JKHIDDEN1`, `JKHIDDEN2` and the original executable retain their historical
behavior. The new tool also accepts those protocols for parity checking.

Reproduce the owned compiled GI counterexample using the pinned JA compiler:

```powershell
cmake --build build/lightrecon-standalone --config Release --target lightrecon_regions_inspect lightrecon_dataset_quality
python -X utf8 -m tools.rend2.adaptive_hidden.gi_control `
  --out build/asr16/gi-reproduction `
  --compiler build/toolchain/netradiant-20220628/portable/netradiant_1.5.0-20220628-windows-amd64/q3map2.exe `
  --native build/lightrecon-standalone/Release
```

Benchmark and repeat, with `--bridge` pointing to the preserved V02 output:

```powershell
python -X utf8 -m tools.rend2.adaptive_hidden.run `
  --tool build/lightrecon-standalone/Release/lightrecon_adaptive_tool.exe `
  --out build/asr16/replay-a --bridge C:/path/to/jk-lightrecon-v02-bridge-v2 `
  --extra build/asr16/gi-reproduction/specification.json
python -X utf8 -m tools.rend2.adaptive_hidden.evaluate `
  --input build/asr16/replay-a --bridge C:/path/to/jk-lightrecon-v02-bridge-v2 `
  --gi build/asr16/gi-reproduction --out build/asr16/replay-a/evaluation.json
```

Repeat `run` to a fresh `replay-b` directory, then:

```powershell
python -X utf8 -m tools.rend2.adaptive_hidden.verify build/asr16/replay-a build/asr16/replay-b --out build/asr16/repeat.json
```

The runner never opens source answers. The separate evaluator verifies every
frozen output before opening answers. Native children receive no held-out targets
or source parameters. The small point/spot/mixed scenes use a separately labelled
original full-floor discovery census, since their historical training folds have
no floor support. V02 uses the unchanged original public requests and bounds.
No held-out predictive-quality claim is made by this proposal study.

`policy-v1.json` freezes the common caps and thresholds; checked aggregate counts,
input/source hashes and repeat checks live under `results/v1`. Detailed candidate
and batch diagnostics remain local under `build/asr16`.
