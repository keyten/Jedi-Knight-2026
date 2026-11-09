# ASR07 offline sun and sky discovery

Reuses `Core::Solar`, its compiled forward evaluator, original BSP/UV mapping and
spatially separated train/validation/final-test folds. No historical solver,
renderer, canonical schema, storage or runtime consumer is changed.

Read the [audit and measured limitations](../../../docs/rend2-dominant-sun-sky-asr07.md).
[`results.json`](results.json) is the compact deterministic witness. Raw own-content
bakes, source answers and full diagnostics stay in ignored `build/`; stock assets
are never copied into the checked report. Python 3.8+ standard library only.

`policy.py` interprets the already validated native result. It preserves three
conditional alternatives: `SunPresent`, `SunAbsent`, `SunUncertain`. Absence means
that a diffuse-only model explains the observations; it is never a zero upper
bound on actual sunlight. A fitted directional field without independent used-sky
sun evidence remains compatible with a distant lamp. Even `SunPresent` remains a
conditional aesthetic hypothesis with `finite_local_alternative=NotExcluded`.

The controlled shader reader handles only the generated `asr07sky` block. Native
captured-evidence parsing handles stock declarations. Unused helper shaders cannot
provide sun provenance. Search sees no authored direction or amplitude prior.

With the existing standalone CMake build, compiler and Python on PATH, from root:

```powershell
cmake -S tools/lightrecon_test -B build/lightrecon-disk-standalone
cmake --build build/lightrecon-disk-standalone --target lightrecon_solar_audit lightrecon_sun_test lightrecon_bake_forward_tool --parallel 6
python -X utf8 -m unittest discover -s tools/rend2/solar_discovery -p 'test_*.py' -v
python -X utf8 tools/rend2/solar_discovery/run.py --output build/asr07-bakes
python -X utf8 tools/rend2/solar_discovery/run.py --reuse build/asr07-bakes --output build/asr07-repeat
build/lightrecon-disk-standalone/lightrecon_sun_test.exe build/asr07-cpu-new.json
python -X utf8 tools/rend2/solar_discovery/report.py --first build/asr07-bakes/results.json --repeat build/asr07-repeat/results.json --cpu build/asr07-cpu-new.json --output build/asr07-report.json
```

On a clean machine configure a suitable CMake generator first. For a Visual Studio
build add `--config Release` and use `--tool` and `--audit-tool` to point to its
`Release/` executables. The default pinned q3map2 path and binary hash follow the
[S02 prerequisites](../sun_fit/README.md). The compiler identity is checked even
when reusing bakes. `--reuse` verifies BSP, map, shader and recipe compiler hashes;
it runs inference again without claiming another compiler invocation. Both run
directories and the report output must be new paths.

Eight freshly compiled scenes yield nine fits: indoor dark/local, outdoor sun/local,
diffuse sky aperture, a sun shadow, mixed sun/local with and without a declared
fixed local contribution, and a distant finite lamp. The paired fixed contribution
is evaluated in the production pre-encoding domain. It is an explicit oracle
ablation, not local-light discovery. The incomplete mixed model remains uncertain.
No subtraction of encoded RGB8 is performed.

The optional local stock inputs default to the existing Hoth2 capture and immutable
training export under `build/x02/`. Override `--stock-inputs` and `--stock-archive`
to use another original archive/export with matching BSP hashes. The controller
checks the training file hash and uses geometry alone to select an outdoor ROI.
The native audit finds an actual sky-visible grid origin near training geometry,
then queries selected receiver positions, diffuse directions, authored directions
and original aggregate grid directions. It never attests stock encoding or
receiver placement. Unsupported/approximate geometry remains unknown. Missing
licensed inputs are recorded as unavailable, not passed as a stock test.

The native audit has a 64-million traversal-work budget, at most 16,384 outdoor
search rays, 256 surface receivers, 48 queried directions and 4,096 grid rays.
Grid-baseline pairs use at most 256 confirmed positions; omitted rays are reported.
Grid coherence is unweighted directional agreement, not an irradiance reduction or
solar probability. No existing asset is published, written or promoted.
