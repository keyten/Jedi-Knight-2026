# ASR08 offline local-light evidence

Tool-only C++ API and deterministic fixtures for the conditional cost
`distance(Encode_profile(global + nonnegative local), original code intervals)`.
Read the [domain, states, masks and limitations](../../../docs/rend2-local-light-evidence-asr08.md).
The [API](../../lightrecon_test/local_light_evidence.h) accepts explicit frozen
`Solar::Proposal` alternatives and finite local `Bake::SourceHypothesis` trials.

From the repository root with CMake and a C++11 toolchain on PATH:

```powershell
cmake -S tools/lightrecon_test -B build/lightrecon-disk-standalone
cmake --build build/lightrecon-disk-standalone --target lightrecon_local_evidence_test lightrecon_sun_test lightrecon_bake_forward_test lightrecon_bake_profile_test lightrecon_visibility_test lightrecon_decode_test lightrecon_lightgrid_test --parallel 4
ctest --test-dir build/lightrecon-disk-standalone -R '^(lightrecon_local_evidence|lightrecon_sun|lightrecon_bake_forward|lightrecon_bake_profile|lightrecon_visibility|lightrecon_decode|lightrecon_lightgrid)$' --output-on-failure
build/lightrecon-disk-standalone/lightrecon_local_evidence_test.exe build/asr08-first.json
build/lightrecon-disk-standalone/lightrecon_local_evidence_test.exe build/asr08-repeat.json
Get-FileHash build/asr08-first.json,build/asr08-repeat.json -Algorithm SHA256
```

For Visual Studio add `--config Release`, CTest `-C Release` and use executables
under `Release/`. On a clean machine select a suitable CMake generator first.
No licensed assets, renderer, GL context, Python packages or compiler downloads
are needed. `results.json` is the checked, pretty-printed native witness; native
output is compact JSON. Formatting is the only difference.

The targets are independently encoded analytic controlled signals with native
static visibility; they do not claim another q3map2 compilation. Five required
families cover mixed sun/local, sun only, no sun, saturation and color bleeding.
Additional checks cover nonlinear recipes, competing/unknown profiles, unavailable
directions/geometry, immutable input, paired masks and bounded failures.

`Valid` applies only to code intervals, every decomposition is `Conditional`, and
unknown input stays `Unavailable`. Row masks use the enum in the API header.
Scores and improvements are code values; supplied forward predictions are in
`CompilerLinearRelative255`. Unavailable scores are JSON null. The witness has no
lamp existence probabilities or exact historical-source certification.

Use separate requests on independently held-out receivers for actual search
validation; the diagnostic does not create folds, optimize source positions or
approve hypotheses. Retain global alternatives, include zero-local and nuisance
explanations, and inspect matching coverage before comparing scores. Color bleed
can be compatible with a local source: `AttributionUnresolved` is always retained.
