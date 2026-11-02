# X11 CPU scene scheduling

Production implementation: `shared/rd-rend2/lightrecon/scene_schedule.{h,cpp}`.
Inputs contain training observations, geometry and independently scoped
one-profile producer requests. Local jobs call production Hidden, candidate
waves use S05, and combined fitting uses production Joint. No Python light
formula or renderer dependency is introduced. Python uses the standard library.

```powershell
$env:PATH='C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;'+$env:PATH
cmake -S tools/lightrecon_test -B build/s06-gcc -DCMAKE_BUILD_TYPE=Release
cmake --build build/s06-gcc -j2
New-Item -ItemType Directory -Force build/x11 | Out-Null
build/s06-gcc/lightrecon_scene_schedule_test.exe build/x11/controlled.json
build/s06-gcc/lightrecon_scene_schedule_test.exe build/x11/controlled-repeat.json
ctest --test-dir build/s06-gcc --output-on-failure -j2
python -X utf8 -m unittest discover -s tools/rend2/scene_schedule -p 'test_*.py' -v
```

The controlled positive protocol supplies fixed XYZ priors, not true RGB or hidden
targets. It is not an autonomous source-localization benchmark. Cancellation,
source-free/indirect controls, source identity and independent evaluation are
tested separately.

Existing private stock archives:

```powershell
$private="$env:TEMP/jk-lightrecon-all-stock-20261031"
python -X utf8 tools/rend2/scene_schedule/run.py --private $private --revision x11-final-c
python -X utf8 tools/rend2/scene_schedule/run.py --private $private --revision x11-final-d
```

Use fresh revision names. The runner preserves attempts, copies and verifies a
frozen native executable, and enforces 600 seconds / 1 GiB actual RSS per owned
child. The native mode is:

```text
lightrecon_map_solver archive.jkobs private-output-directory BSP-SHA scene
```

`scene` reuses the complete source census and frozen X10 evaluation selections,
retains the dense training-only pool for all geometry-driven local jobs, and
executes inverse-square/linear conditional trials separately. It does not infer
their historical identity. Local calls are cooperatively resumed in groups of
32 with an in-process immutable checkpoint; every individual Hidden call uses
one area. JSON progress is diagnostic, not a serialized numerical checkpoint.

Private outputs are `scheduler-{isq,linear}.json`, progress JSON and unit TSVs,
plus the existing producer/map diagnostics. The public collector excludes
licensed geometry, observed RGB, local asset payloads and private paths.

After creating an independent build/test record (see checked-in `checks.json`):

```powershell
python -X utf8 tools/rend2/scene_schedule/collect.py --private $private --revision x11-final-c --repeat x11-final-d --controlled build/x11/controlled.json --controlled-repeat build/x11/controlled-repeat.json --checks build/x11/checks.json
python -X utf8 tools/rend2/scene_schedule/verify.py
```

SP/MP Release builds use the existing `build/s13-renderers` configuration:

```powershell
$env:CL='/MP2'
cmake --build build/s13-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 2
```

The full [technical report](../../../docs/rend2-scene-scheduler-x11.md) distinguishes
searched/partial/unsupported units, Joint calls, accepted models and final-held-out
status. No candidate count is a source-existence probability. No game is launched.
