# X13 scoped bake occupancy

[Semantics and implementation](../../../docs/rend2-bake-occupancy-x13.md).
No renderer or game launch is required. Python standard library only; all source
fitting/placement/visibility uses the actual C++ core.

Use a fresh protocol/private revision/output. Existing X12 and v4 results must not
be overwritten. Stock `.jkobs` and geometric TSV witnesses remain under the
existing private temporary root; public results contain counts, stable IDs and
hashes, never proprietary geometry or target RGB.

```powershell
$py='C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$env:PATH='C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;'+$env:PATH
$private=Join-Path $env:TEMP 'jk-lightrecon-all-stock-20261031'
cmake -S tools/lightrecon_test -B build/s06-gcc -DCMAKE_BUILD_TYPE=Release
cmake --build build/s06-gcc --parallel 2
& build/s06-gcc/lightrecon_occupancy_test.exe build/x13-native-sealed.json
& $py -X utf8 tools/rend2/occupancy/run.py --private $private --tool build/s06-gcc/lightrecon_map_solver.exe --test build/s06-gcc/lightrecon_occupancy_test.exe --protocol build/x13-protocol.json --revision x13-a --freeze
& $py -X utf8 tools/rend2/occupancy/run.py --private $private --tool build/s06-gcc/lightrecon_map_solver.exe --test build/s06-gcc/lightrecon_occupancy_test.exe --protocol build/x13-protocol.json --out build/x13-run
ctest --test-dir build/s06-gcc --output-on-failure -j2 --output-log "${PWD}/build/x13-ctest.log"
$env:CL='/MP2'
cmake --build build/s13-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 2 *> build/x13-renderers.log
```

The final build log used for sealing is `build/x13-renderers-final.log`.
The final measured CPU build is `build/x13-measured-build.log`; the complete CPU
regression build is `build/x13-cpu-regression-build.log`.

Additional checks and public sealing actually executed:

```powershell
& $py -X utf8 tools/rend2/producer/benchmark.py --tool build/s06-gcc/lightrecon_joint_tool.exe --out build/x13-compiled-controls
& $py -X utf8 tools/rend2/all_stock/architecture.py > build/x13-architecture.json
& $py -X utf8 -m unittest discover -s tools/rend2/datasets -p test_blind_views.py -v *> build/x13-blind-tests.log
& $py -X utf8 -m unittest discover -s tools/rend2/producer -p 'test_*.py' -v *> build/x13-controller-tests.log
& $py -X utf8 -m unittest discover -s tools/rend2/occupancy -p 'test_*.py' -v *> build/x13-occupancy-tests.log
& $py -X utf8 tools/rend2/occupancy/finalize.py --run build/x13-run --out tools/rend2/occupancy/results/v1
& $py -X utf8 tools/rend2/occupancy/finalize.py --verify tools/rend2/occupancy/results/v1
```

The preserved original negative control remains available:

```powershell
$env:S11_CASE='dynamic-scope'
& build/s06-gcc/lightrecon_joint_test.exe build/x13-old-dynamic-scope.json
Remove-Item Env:S11_CASE
```

The new test is complementary: it does not edit the historical X12 witness or
weaken the default runtime-envelope policy to make that old control pass.

The new explicit map-tool mode is `scene-bake-conditional`; existing `scene` keeps
the conservative runtime-envelope placement policy. Both retain independent
visibility, material/profile, GI and held-out gates. Runs are sequential with
600-second/1-GiB outer safety caps and unchanged bounded X12 scheduler budgets.
The frozen source-copy executable and per-map diagnostic files are private.

`source-semantics.json` counts original entity/model ownership and authored key
availability. No trajectory, bake participation or historical pose is inferred
from those keys. `stock.json` records actual fitting calls/work and explicit
outcomes; a process completion is not a successful inverse fit.
