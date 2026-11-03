# P02 generated spatial probe support

`Probes::Build` generates geometry only. `Topology::Lookup` has no lighting values,
allocation or ray tracing. `Probes::Prepare` binds separately evaluated P03 angular
values and canonical field membership to the actual final probe positions.
Original baked observations are not relocated or used as calibrated field values.

```powershell
$env:PATH = 'C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;' + $env:PATH
cmake -S tools/lightrecon_test -B build/s06-gcc
cmake --build build/s06-gcc --target lightrecon_probes_test lightrecon_probes_tool -j 2
build/s06-gcc/lightrecon_probes_test.exe build/p02-native.json
python -X utf8 tools/rend2/probes/run.py --private "$env:TEMP/jk-lightrecon-all-stock-20261031" --out tools/rend2/probes/results/v3
python -X utf8 -m unittest tools.rend2.probes.test_reports
```

Use a new output directory for reruns. `v1` retains the failed mixed-cell codec
development attempt; `v2` validates the repair; final `v3` also checks non-axis
float directions, directional colour gradients and incomplete chunk rejection.
Protocol, source/executable/BSP/dataset hashes and failed work remain recorded.
The three named stock maps have three predeclared geometry-only ROIs each.
They have **no lighting coefficients or photometric ground truth**. Query support
through a doorway is not illumination accuracy. Unknown dynamic motion remains
conditional even when the static support certificate is valid.

The CPU chunks contain generated geometry/topology and bind their geometry
generation; checksums detect corruption, not authenticity of arbitrary hostile
certificates. Use the trusted producer and the canonical container's expected
chunk/content identities. Shared source geometry and original images are never
published in the versioned benchmark results.

See [design and results](../../../docs/rend2-adaptive-probes-p02.md).
