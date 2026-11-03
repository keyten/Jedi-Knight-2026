# X15 frozen receiver support

`ReceiverSupport` consumes geometry-only streams. It reuses `Bake::Capability`
for historical operators and distinguishes their verified planar scope from the
separate physical finite-source integral. Its immutable manifest records IDs,
roles, receiver areas, required groups/channels, operator/profile identities,
reasons and uncertainty. It contains no RGB targets or emitter positions.

Joint freezes training support before model proposals. A supplied manifest may
span independent validation/final folds and multiple profile branches. Common
support is their predeclared intersection; per-profile coverage remains reported
separately. Missing supported predictions fail coverage, including explicit
55/64 controls. A known blocked zero is not an absent prediction.

```powershell
$env:PATH = 'C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;' + $env:PATH
cmake -S tools/lightrecon_test -B build/s06-gcc
cmake --build build/s06-gcc --target lightrecon_receiver_support_test lightrecon_map_solver -j 2
build/s06-gcc/lightrecon_receiver_support_test.exe build/x15-native.json
python -X utf8 tools/rend2/receiver_support/run.py --private "$env:TEMP/jk-lightrecon-all-stock-20261031" --out tools/rend2/receiver_support/results/v4
python -X utf8 -m unittest tools.rend2.receiver_support.test_reports
```

Use a fresh output revision for a reproduction. The runner freezes executable,
source, dataset and original BSP hashes before evaluating `academy1`, `mp/duel4`,
`hoth3`, `hoth2`. The first two maps exercise the production scene scheduler and
Joint; the other two have full captured-map receiver censuses. Licensed data and
world-coordinate witnesses stay in the private dataset root. Published selected
manifests contain IDs and geometry hashes, not lightmaps or target RGB.

`v1` is preserved as a development attempt, explicitly tagged with its scope
error. Full-map numerical support under the stock adapter's *conditional UV
mapping/placement hypothesis* must not be confused with strict raw-BSP-basis
admission. Basis-mismatch flags are reported independently. Normals are never
replaced, and candidate-dependent unknown visibility is never excluded to improve
fit. No stock historical profile, absolute calibration or emitter truth is known.

See `docs/rend2-receiver-support-x15.md` for results, coverage semantics and limits.
