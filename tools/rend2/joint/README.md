# S11 coordinated CPU optimization

The production implementation is `shared/rd-rend2/lightrecon/joint.{h,cpp}`.
Python runs trusted evaluation and reproducibility checks; it contains no forward
formula or replacement optimizer. No external packages or runtime dependencies.

```powershell
cmake -S tools/lightrecon_test -B build/s11-tools -G "Visual Studio 16 2019" -A x64
$env:CL='/MP2'
cmake --build build/s11-tools --config Release --parallel 2
python -X utf8 tools/rend2/joint/benchmark.py --tool build/s11-tools/Release/lightrecon_joint_test.exe --out build/s11-repeat
python -X utf8 -m unittest discover -s tools/rend2/joint -p 'test_*.py' -v
```

Use a fresh output directory. Single-configuration builds omit `Release/`.
`numerical.json` records semantic results and source hashes; `resources.json`
contains variable wall time/actual Windows peak process RSS. RSS is unavailable
on other systems rather than silently recorded as zero.

The compiled native validation additionally requires the original hash-pinned
`build/q3map2-bake-verified/` recipes/BSPs and already isolated X07 views. Reproduce
those using `tools/rend2/fixed_geometry/README.md`; do not copy secret amplitudes
or evaluator files into the solver directory.

```powershell
python -X utf8 tools/rend2/joint/compiled.py --tool build/s11-tools/Release/lightrecon_joint_tool.exe --views build/x07-verified --out build/s11-compiled-repeat
```

This native child receives the sanitized geometry archive, allowed fixed XYZ
priors, training RGB, and validation/held-out geometry. Only aggregate validation
metrics return through stdin. The controller privately loads the source answers
and final held-out RGB. The adapter refuses source images/grid/vertex lighting
or retained emitter evidence in its geometry archive. It is a trusted prototype,
not an OS sandbox for malicious native code.

See [the S11 report](../../../docs/rend2-joint-optimization-s11.md) for objective,
profile/operator limits, conditional source-count semantics, passive GI, coarse
and strict evaluations, known rejected overfits and checkpoint scope. Neither a
small residual nor a populated Candidate list certifies physical recovery.
