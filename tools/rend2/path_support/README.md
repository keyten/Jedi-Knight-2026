# X17 source-to-receiver support audit

This drives the existing production map adapter, Hidden/S05/SceneSchedule/Joint,
ReceiverSupport and static geometry APIs. It does not implement light equations,
invent source answers, enable a renderer or change the frozen acceptance gate.

Keep a copy of the pre-change native `lightrecon_map_solver.exe`. Use fresh output
and private work directories for each invocation. The eight-map list is declared
in `run.py` before results are read. Original BSP/observation hashes are checked;
SP material interpretation of the captured MP maps remains conditional.

```powershell
$py = 'C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$private = "$env:TEMP/jk-lightrecon-all-stock-20261031"
& $py -X utf8 tools/rend2/path_support/run.py --private $private --work "$env:TEMP/jk-lightrecon-x17-before-v2" --out build/x17-before-v2 --tool build/x17-baseline/lightrecon_map_solver.exe --label before-d3ef7c59a
& $py -X utf8 tools/rend2/path_support/run.py --private $private --work "$env:TEMP/jk-lightrecon-x17-after-v4" --out build/x17-after-v4 --tool build/s06-gcc/lightrecon_map_solver.exe --label after-final-scoped-ray-support-v4
& $py -X utf8 tools/rend2/path_support/run.py --private $private --work "$env:TEMP/jk-lightrecon-x17-repeat-v3" --out build/x17-repeat-v3 --tool build/s06-gcc/lightrecon_map_solver.exe --label repeat-final-scoped-ray-support --maps hoth3 mp/duel4
& $py -X utf8 tools/rend2/path_support/controlled.py --out build/x17-controlled-done
& $py -X utf8 tools/rend2/producer/benchmark.py --tool build/s06-gcc/lightrecon_joint_tool.exe --out build/x17-compiled-done
& $py -X utf8 tools/rend2/path_support/report.py --before build/x17-before-v2 --after build/x17-after-v4 --repeat build/x17-repeat-v3 --controlled build/x17-controlled-done --controls build/x17-compiled-done --out tools/rend2/path_support/results/v1
```

`report.py` reads numerical receipts after fitting. It checks the paired input,
source-independent receiver denominator and full runtime coverage gate. Failed
source attempts retain all bounded missing IDs/typed ray causes. Missing final
prediction metrics stay null; completed no-source baselines are not nonempty fits.
Raw coordinate/debug/BSP payloads remain private. Public reports contain aggregate
coverage, work, topology statistics and stable witness IDs, never licensed meshes.

Production direct rays use the declared PointRefine bake scene. Passive Transfer
links and its runtime/motion QueryPath policy remain independent. Conditional
paths are tagged Assumed, not Geometric. Unknown optical geometry and a required
missing receiver still prevent model acceptance. No stock source is certified by
the improved number of available factors.

See [the technical report](../../../docs/rend2-source-path-support-x17.md) for
version keys, limitations and the actual executed test/build commands.
