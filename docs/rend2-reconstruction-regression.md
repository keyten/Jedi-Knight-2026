# Reconstructed-lighting compatibility regression protection

The controlled shader baseline is renderer revision
`60d1cc8c6f59e29e8d3d1ccc4b180a3369b7be2c`, immediately after the independent
[build/use controls](rend2-reconstruction-controls.md). These checks protect
original-style and current **non-reconstruction** rendering. They do not require
preserving unsuccessful experimental light fitting or promote its proxies.
No production C++, shader, feature default or rendering technology is redesigned
or changed by this validation task.

## Audit and scope

| Boundary | Existing behaviour and protection |
| --- | --- |
| Canonical `r_lightRecon*` controls | Read only in `tr_lightrecon.cpp`; build/load affects the CPU owner, all feature adapters currently unavailable. No shader define, sampler, UBO/SSBO or frame-light submission consumes them. The source dependency fence detects new raw-control/API/namespace dependencies outside the owner. |
| `lightall.glsl` entity lighting | Legacy CPU grid, explicit CPU/GPU grid interpolation and Ghoul2 permutations are covered. `USE_ENTITY_GRID_L1` is experimental, selected by the old probe controls; it is absent from this baseline. |
| `volumetric_inject.glsl` | `USE_FROXEL_STATIC_RECONSTRUCTION` and its three `u_VolumetricDirMoment*` samplers/fetches are absent. Existing isotropic static/sun/legacy-grid textures remain part of the ordinary froxel implementation. |
| `volumetric_common.glsl` | `USE_FROXEL_FOG` gates the library. Existing `VolumetricFog`, particle/media blocks and scalar/RGB lookup variants are captured, independently of canonical reconstruction. |
| PBR / fog / sun / LTC | Their existing controls generate their existing defines. No build/load or canonical master/consumer request writes those controls or activates their resources. |
| Forward+ | Current lightall uses runtime `u_FPlusParams2` selection, not an invented `USE_FORWARD_PLUS` define. Its existing TBO declarations/Camera fields can be present with Forward+ off. Existing GL4.3 cluster shaders/SSBOs are separate tested programs. |
| Runtime sampler assignments | The manifest records relevant `GLSL_SetUniformInt` expressions, texture unit values, block slots and CPU layouts from production files. It is an initialization inventory, not a per-draw GL trace. A setter for an absent uniform returns before a GL call; removed inactive moment setters are not frozen. |

`tr_glsl.cpp` still assembles headers/libraries, prunes unused defines for cache
keys, and compiles through its normal driver/helper infrastructure. The test
assembler uses an explicit representative subset of the same inputs: production
GLSL stages/libraries, production enum/constants, explicit optional feature
defines and the current `r_FBufScale` uniform convention. It uses GCC 16.2's C
preprocessor for macro/conditional expansion and token normalization, then the
**existing** SDL/GL compiler/linker from `test_watersurface_gl.py`. It does not
implement another GLSL compiler or claim to capture driver machine instructions.

The baseline stores expanded code as well as declared interfaces: an extra
instruction/function, fetch, define, sampler, buffer or changed layout produces
a readable unified diff, even if a particular driver would optimize it away.
Identical expanded code/interfaces, unchanged production control dependencies
and stable runtime permutations establish absence of additional canonical
fragment/froxel work. This is not an FPS/timing benchmark or a guarantee about
unrelated driver scheduling.

Active sampler types/counts, UBO sizes and SSBO bindings/sizes are also
reflected after driver linking. Active reflection is compared to the frozen
baseline only on an identical vendor/renderer/version key: drivers can optimize
unused fields/array tails differently. The NVIDIA run still compares all
driver-independent expanded source and declared layouts, checks forbidden
symbols and compiles/links every row; it explicitly reports the active-reflection
comparison as inapplicable to the Intel baseline.

## Matrix and baseline maintenance

The versioned [baseline directory](../tools/rend2/lightrecon_baseline/README.md)
contains **27 programs / 51 stages**:

| Coverage | Variants |
| --- | --- |
| Original-style world, entities, Ghoul2 | Fast lightmapped world, legacy CPU entity lighting, skeletal lighting |
| Dynamic lights and sabers | Classic local-light world path and generic saber/FX path; runtime fixed FX and legacy point-light submissions |
| PBR | Physical mode off/on; normal/specular material and skeletal variants |
| Entity interpolation | Explicit CPU interpolation and raw GPU grid direction, probes off |
| Sun shadows | Established comparison sampling and modern raw-depth sampling |
| LTC / Forward+ | Four independent off/on combinations, plus physical skeletal LTC |
| Fog | Off, scalar surface lookup, RGB lookup, raster injection, physical/sun injection, compute injection, RGB/sun compute and compute media |
| Clusters | Forward+ and froxel compute kernels with existing std430 SSBOs |

The two Forward+ off/on lightall rows intentionally have identical shader code;
their differing runtime settings remain explicit in the permutation manifest.
The CPU control tests additionally cover all four build policies, missing/invalid
assets/capabilities, master and independent consumers. This test's production
dependency audit prevents a canonical build/use flag from bypassing that boundary
to force another technology.

Comparison is scoped to the controlled baseline, not arbitrary commits with
unrelated changes. Whitespace, comments and `#line` noise are ignored; arithmetic,
identifiers, declarations and layouts are retained. Intended unrelated changes
must be reviewed in the per-stage diff and documented in an explicit baseline
update reason. Inactive experimental code can be removed without a source
contract change; a self-test removes its moment declarations and verifies this.
Never automatically regenerate goldens after a failure.

Nine detector self-tests cover the production dependency fence, normalization,
inactive branches/removal, define/sampler/UBO/SSBO/work mutations, permutation
diffs, preprocessor failures, missing runtime diagnostics and the exact/default versus bounded image policy.

## Actual SP visual protocol

The Windows harness `tools/rend2/test_lightrecon_legacy_runtime.py` launches an
actual SP engine/game with the current SP Rend2 DLL. It creates a private home,
copies executables there and uses only `assets0.pk3` through `assets3.pk3` in a
private stock-only base directory. Installed loose maps, configs and extra PK3s
are excluded. NTFS links are preferred; copying is used when installation ACLs
deny linking. No installed game file is modified and no commercial map/archive
is committed. Missing executables, maps, captures or a usable GL context are
failures, never a passing runtime result.

Requested cameras (`setviewpos x y z yaw`, world units; pitch/roll reset by the
game command):

| Map | Position / yaw | View |
| --- | --- | --- |
| `hoth3` | `3464 700 85 90` | Indoor base/laboratory: lightmapped surfaces, fixtures and Ghoul2 character |
| `hoth2` | `1728 -576 1040 135` | Outdoor snow terrain, sky, character, map sun and fog |

FOV is explicitly 80. JA can force a third-person camera for its current weapon;
the harness records **actual** position, angles and FOV using `writecam`, and
requires all three A/B/A records within each layer to match. The camera recording
and full per-case settings are retained with the captures; requested player
position is not misreported as the exact renderer camera.

Both profiles use 960x540, windowed, 60 FPS cap including unfocused/minimized,
MSAA off, HDR on, manual legacy exposure (`r_cameraMode 0`, camera EV 0,
auto-exposure off), tone-map mode 0, exposure compensation 0, gamma/intensity 1.
SSR, SSGI, SMAA, motion blur, AO, contact shadows, weather wetness, atmosphere,
clouds, cubemaps and modern water are off. Froxel temporal history/jitter is off.
HUD, view bob and view weapon drawing are off. Old reconstruction, promoted
lights, portals, entity probes/sun split and area calibration are explicitly off.

- `legacy`: normal/specular maps, physical PBR, Forward+, LTC, froxel fog and
  runtime sun shadows off. Baked map sun remains in original lightmaps.
- `modern`: normal/specular maps, physical PBR, Forward+, LTC, froxel fog and
  modern sun shadows on; cache off, shadow map size 1024. These are explicit
  test inputs, not technologies enabled by reconstruction.

**Do not use `timescale 0` to freeze this SP build.** `Com_ModifyMsec` scales only
nonzero timescale, and legacy saber glow/radius consumes per-frame randomness.
The harness uses a tiny blank non-fullscreen UI fixture to apply normal SP pause,
verifies `cl_paused=sv_paused=1`, disables FX clock progression and captures:

1. A: build policy already applied at map load, master off, all consumer requests on.
2. B: master on, every consumer request off.
3. A repeat: original disabled-master requests restored, exposing unstable controls.

This is done first for the base scene and then for a dynamic stimulus. The latter
uses a newly authored fixed sprite/light FX (stock saber-flare material, constant
size/RGB/lifetime, FX physicalization off) and four existing deterministic renderer
test lights. FX scheduling receives 30 wait ticks to resume and 30 to instantiate
the effect before re-pausing; these are command wait ticks, not a timing benchmark. Each
layer then gets its own stable camera/animation A/B/A. Layers/profiles are not
compared as equivalent lighting configurations. A positive FX-only capture and
the dynamic-light capture must visibly differ from base, preventing a test that
silently omits its stimulus. Random original saber-blade glow is **not** certified
as an exact image test; its generic/surface shader paths remain covered by the
code/compile contracts.

For every A/B/A, the harness checks consumer effective states, typed build-policy
status/callback counts, technology cvar values, camera stability, shader-family
usage/permutation counts, screenshots and GL errors. Flat/blank frames fail.
Source revision, renderer hash, archive sizes, console logs, camera records,
configs and amplified difference PNGs are retained under ignored `build/`.

Image tolerance is deliberately small: at most **one 8-bit code step**, at most
**five pixels out of 518400** (less than 0.001%), mean RGB absolute error at most
**1e-5 code steps**. The repeat control must meet the same bounds. This permits
sparse FP16/display quantization, not visible lighting or random saber flicker.
The shared comparison CLI keeps its original exact default; only this runtime
protocol passes the explicit rounding budget. Shader contracts have no such
numerical allowance: normalized code/layouts must match.

## Commands and results

Run from the repository root with workspace Python (NumPy/Pillow) and the bundled
SDL2. For the local toolchain used in this validation:

```powershell
$python = 'C:\Users\Keyten\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$cmake = 'C:\Users\Keyten\Desktop\projects\OpenJK\build\toolchain\w64devkit\bin\cmake.exe'
$ctest = 'C:\Users\Keyten\Desktop\projects\OpenJK\build\toolchain\w64devkit\bin\ctest.exe'
$env:REND2_CPP = 'C:\Users\Keyten\Desktop\projects\OpenJK\build\toolchain\w64devkit\bin\gcc.exe'

& $python tools/rend2/run_gpu_tests.py --only '*lightrecon*'
& $python tools/rend2/run_gpu_tests.py --only '*lightrecon*' --nvidia
& $python tools/rend2/test_lightrecon_contract.py
& $python tools/rend2/lightrecon_regression.py --no-gpu --output build/lightrecon-contract-current
& $python tools/rend2/lightrecon_regression.py --export-baseline build/lightrecon-regression/baseline-readable
& $python -m py_compile tools/rend2/lightrecon_regression.py tools/rend2/test_lightrecon_contract.py `
  tools/rend2/test_lightrecon_legacy_gl.py tools/rend2/test_lightrecon_legacy_runtime.py `
  tools/rend2/gl43_validation/compare_images.py

& $cmake --build build/lightrecon-core-renderers --config Release --parallel 4 `
  --target rdsp-rend2_x86_64 rd-rend2_x86_64 lightrecon_test lightrecon_api_test lightrecon_core_test lightrecon_control_test
& $ctest --test-dir build/lightrecon-core-renderers -C Release --output-on-failure

& $python tools/rend2/test_lightrecon_legacy_runtime.py `
  --installation 'C:\Program Files (x86)\Steam\steamapps\common\Jedi Academy\GameData' `
  --binaries build/ssr-msvc/RelWithDebInfo `
  --renderer build/lightrecon-core-renderers/Release/rdsp-rend2_x86_64.dll `
  --output build/lightrecon-visual-20261022 --label accepted
```

The build directory configuration is in the [core build instructions](rend2-reconstruction-core.md#build-and-run).
The SP engine/game binaries are existing local builds; renderer targets are
verified against current sources. The runtime script accepts explicit alternative
binary/install paths and unique labels; it refuses to overwrite prior cases.
For other build policies append `--build-policy 0`, `1` or `2`; restrict to
`--maps hoth3 --profiles legacy` for a small cold-start check.

Initial baseline creation used the explicit command below, with production GLSL
unchanged. It is a maintenance operation, not a routine test:

```powershell
& $python tools/rend2/test_lightrecon_legacy_gl.py --write-baseline `
  'Initial controlled disabled-consumption baseline at 60d1cc8c6; production GLSL unchanged; experimental reconstruction off; active binding inventory excludes absent old moment samplers'
```

Recorded results and capture hashes are in
[`validation.json`](../tools/rend2/lightrecon_baseline/validation.json). Shader
comparisons passed on Intel UHD (driver 27.20.100.8280) and NVIDIA RTX 2060
(616.92): 27 programs/51 stages compiled and linked, zero normalized source or
declared interface differences. SP and MP Release targets build, and 4/4 CPU
CTest programs pass (5856 checks). The final detector suite has nine passing tests.

Final SP image results (`accepted`, Intel, build policy 3):

| Map/profile | Base A/B and A/repeat | Dynamic lights/FX A/B and A/repeat | Shader usage |
| --- | --- | --- | --- |
| hoth3 legacy | 0 pixels differ | 0 pixels differ | Identical |
| hoth3 modern | 0 pixels differ | 0 pixels differ | Identical |
| hoth2 legacy | 0 pixels differ | 0 pixels differ | Identical |
| hoth2 modern | 0 pixels differ | 0 pixels differ | Identical |

That is eight A/B/A groups, sixteen image comparisons and 28 preserved captures
including FX positive controls. All final comparisons were exact, tighter than
the allowed rounding budget. Each FX positive control visibly changed the frame.
After strengthening the missing-diagnostic guard, all 24 preserved actual
diagnostic sections were replayed: 240 consumer records and all master states
matched. All four policies are covered by CPU tests; actual SP cold-start
captures use the strongest policy (force rebuild), with no backend installed.

Exploratory runtime labels `pilot`, `pilot2`, `pause-pilot`, `fixed`, `final`,
`rooms`, `verified`, `protected`, `regression` and `evidence` were used while establishing the
protocol. Initial blank/invalid views, `timescale 0` drift, random saber flicker,
one sparse 1-LSB pixel, and scheduling FX while already frozen were diagnosed;
failed/incomplete trials are not silently counted as final visual passes. Earlier
`rooms` captures also had exact zero differences for both layers on all four
map/profile pairs. The final recorded command uses actual SP pause, explicit
rounding bounds and a verified visible FX stimulus.

## Existing behaviour outside original JA

“Original-style” identifies a controlled Rend2 configuration, not pixel identity
with the proprietary original JA renderer. Current Rend2 already has its own
UBO architecture, common PBR scene parameters, Forward+ Camera fields/TBO
declarations, rewritten material lighting, HDR/tone mapping and optional modern
fog/sun/LTC paths. Froxel fog is itself beyond original JA and retains its
existing static/sun grid split and debug legacy grid. Experimental L1/moment
paths remain in production under their old controls; the new canonical master
does not redefine them or turn them off. Advanced configurations can still engage
that old subsystem through its old controls. This baseline explicitly disables
those controls; it does not freeze their algorithms or quality. Removal is separate work.

This suite certifies the controlled disabled-consumption paths and available
runtime views. It is not exhaustive material coverage, MP visual validation,
driver timing measurement, original-JA golden screenshots or reconstruction
implementation. A future canonical consumer must extend these cases before
relaxing the dependency fence; capability absence must continue to fail safely.
