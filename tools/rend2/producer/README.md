# S13 canonical offline producer

The CPU entry point is `Core::Producer::Run` in `shared/rd-rend2/lightrecon/producer.h`.
It composes the existing stages; it does not install an in-game solver or consumer.
Read [the technical report](../../../docs/rend2-producer-s13.md) before interpreting a result.
`Completed` describes conditional software execution, not physical-source certainty.
`DiagnosticOnly`, unknown metrics and an empty resolved list are intentional outcomes.

Build and test (GCC/MinGW or the existing Visual Studio generator):

```powershell
cmake -S tools/lightrecon_test -B build/s13-tools -G "Visual Studio 16 2019" -A x64
cmake --build build/s13-tools --config Release --parallel 2
ctest --test-dir build/s13-tools -C Release --output-on-failure
python -X utf8 tools/rend2/joint/benchmark.py --tool build/s13-tools/Release/lightrecon_producer_suite.exe --out build/s13-published-suite
python -X utf8 tools/rend2/producer/benchmark.py --tool build/s13-tools/Release/lightrecon_joint_tool.exe --views build/x07-verified --bakes build/q3map2-bake-verified --out build/s13-compiled
```

The native adapter receives only sanitized geometry, allowed candidate XYZ priors,
training colours and independent receiver geometry. The trusted Python controller
keeps validation/held-out RGB and source amplitudes outside the native inputs.
It returns aggregate receipts; final colours never tune the frozen model. This is
an API/process data boundary, not an OS sandbox against malicious native code.
The old three-argument joint adapter remains supported. A fourth argument enables
canonical publication and requires `JKJOINT13` partition hashes/coverage counters.

The compiled benchmark writes `.jklr`, a report, an OBJ and a bounded text preview.
A second identical run must produce byte-identical archives. Every payload is read
and validated on reload. These are explicit offline caches: normal renderer
fingerprint/preflight remains `NotCacheable` and normal map loading does not fit.

Licensed stock captures are local inputs only:

```powershell
python -X utf8 tools/rend2/producer/stock.py --tool build/s13-tools/Release/lightrecon_joint_tool.exe --rays build/s13-tools/Release/lightrecon_profile_rays.exe --private C:/local/X06-captures --out C:/local/S13-stock
python -X utf8 tools/rend2/producer/runtime_preview.py --renderer build/s13-renderers/Release/rdsp-rend2_x86_64.dll --rays build/s13-tools/Release/lightrecon_profile_rays.exe --out C:/local/S13-preview
```

Stock selection uses TRAIN plus geometry only. A predeclared local ROI falls back
to full-map geometry strata if independent partitions are absent. This is a small
bounded attempt, not exhaustive map reconstruction. Both numerical assumed
inverse-square/linear profiles and a separate nonnumeric UnknownProfile diagnostic
are recorded. Unknown visibility is never changed to clear to admit a source.

`r_lightReconPreview <safe-name>|off` explicitly reads
`lightrecon-preview/<safe-name>.txt` after source capture (build policy 2/3).
A mismatched original BSP hash, invalid frame, over 256 records or over 128 KiB is
rejected. Orange markers are conditional fitted hypotheses; magenta markers are
unfitted priors. Full IDs/type/unknown confidence are printed to console. These
are depth-tested data markers and never lighting. State clears on map load,
aborted map load and renderer shutdown. Default empty state registers no shader.
A source with unknown absolute scale is never labelled candela/lux/nits.

`compare_revision.py` compares actual disabled shader inputs with a named source
revision and retains the separate historical frozen-baseline result. It never
rewrites the project's old regression manifest.

The checked witness is [results/v1/manifest.json](results/v1/manifest.json). It keeps
software, stock physics, cache and consumer readiness separate.
