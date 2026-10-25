# Real q3map2 baked-observation validation

This follow-up closes the previously unperformed compiler/map-load check for
the [offline observation workflow](rend2-offline-observation-datasets.md).
It compiles self-authored brush maps, loads their unmodified BSPs with actual
SP Rend2, exports original CPU observations and validates them without GL.
It does not recover source lights or measure inverse-solver accuracy.

## Local compiler installation

Installed the portable Windows amd64 package from the
[official NetRadiant download page](https://netradiant.gitlab.io/page/download/):

- [NetRadiant 1.5.0-20220628 ZIP](https://dl.illwieckz.net/share/netradiant/release/netradiant_1.5.0-20220628-windows-amd64.zip),
  59,858,083 bytes.
- [Published SHA512 manifest](https://dl.illwieckz.net/share/netradiant/release/netradiant_1.5.0-20220628.sha512sum).
- Verified SHA512 before extracting/executing:
  `2c8e7979b77ed2b63b6483c77d5ff4c0d11e0050b72143b151640bd1748417a3622fc265e3d0e713d735c449323135b41320d50491cafd5b26213da24f00422d`.
- Executable: `build/toolchain/netradiant-20220628/portable/netradiant_1.5.0-20220628-windows-amd64/q3map2.exe`.
- Reported version: `2.5.17n-git-d9338309e`; NetRadiant v1.5.0,
  Jun 28 2022 13:57:12. Dependencies remain next to the executable.

No system installation, registry change or persistent PATH modification was
needed. ZIP, checksum manifest, extracted binaries and help output stay under
ignored `build/toolchain/`; they are not redistributed in this repository.
To reinstall, download the two official files, compare `Get-FileHash -Algorithm
SHA512` with the manifest, then extract the ZIP with its directory layout intact.

## Reproduction commands

Run from repository root in PowerShell. `python` means Python 3; the runtime
image comparator also needs Pillow, as does the existing renderer
validation tooling. The bake/independent geometry scripts use only the standard
library. Use new output directories: scripts refuse to overwrite witnesses.

```powershell
# Existing MSVC x64 configuration; both production renderer targets.
cmake --build build/lightrecon-core-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 lightrecon_dataset_tool --parallel 4
ctest --test-dir build/lightrecon-core-renderers -C Release --output-on-failure

# Existing GCC standalone configuration, no GL or engine startup.
cmake --build build/lightrecon-disk-standalone --parallel 4
ctest --test-dir build/lightrecon-disk-standalone --output-on-failure
python tools/rend2/datasets/test_q3map2_recipe.py -v

$q3 = Resolve-Path build/toolchain/netradiant-20220628/portable/netradiant_1.5.0-20220628-windows-amd64/q3map2.exe
$tool = Resolve-Path build/lightrecon-disk-standalone/lightrecon_dataset_tool.exe
python tools/rend2/datasets/run_pipeline.py --tool $tool --output build/q3map2-bake-verified --q3map2 $q3

# Requires the user's licensed local JA assets, executable, and game DLL.
# --private-archives is an existing local folder containing assets0..3.pk3.
# They are hardlinked into the private runtime, never copied into solver inputs.
python tools/rend2/datasets/test_q3map2_runtime.py --installation 'C:\Program Files (x86)\Steam\steamapps\common\Jedi Academy\GameData' --binaries build/ssr-msvc/RelWithDebInfo --renderer build/lightrecon-core-renderers/Release/rdsp-rend2_x86_64.dll --tool $tool --suite build/q3map2-bake-verified --output build/q3map2-runtime-verified --private-archives build/lightrecon-evidence-runtime/final-source/stock-assets/base
python tools/rend2/datasets/verify_q3map2.py --suite build/q3map2-bake-verified --runtime build/q3map2-runtime-verified --tool $tool

# No game/GL needed for subsequent inspection.
& $tool inspect build/q3map2-runtime-verified/inputs/door_occlusion.jkobs
& $tool validate build/q3map2-runtime-verified/inputs/door_occlusion.jkobs
& $tool audit-bsp build/q3map2-runtime-verified/inputs/door_occlusion.jkobs build/q3map2-bake-verified/authoring/base/maps/door_occlusion.bsp
& $tool samples build/q3map2-runtime-verified/inputs/door_occlusion.jkobs heldout
& $tool trace build/q3map2-runtime-verified/inputs/door_occlusion.jkobs -80 0 80 80 0 80
```

A fresh generic standalone build is documented in
`tools/rend2/datasets/README.md`; substitute its executable path above. Initial
renderer/game build setup follows the repository build instructions. The exact
local Python executable was
`C:\Users\Keyten\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe`;
local CMake/GCC came from `../OpenJK/build/toolchain/w64devkit/bin` on process PATH.
The strict tool compile also ran `g++ -std=c++11 -Wall -Wextra -Werror -pedantic
-Ishared/rd-rend2 -Itools/lightrecon_test -fsyntax-only
tools/lightrecon_test/lightrecon_dataset_tool.cpp`.

## Scenes and original answers

Twelve actual BSPs: `point_room`, `two_colour`, `overlap_whites`, `spot`, `panel`,
`tube`, `sun_sky`, `skylight`, `door_occlusion`, `bounce_colour`, `wall_leak`,
and `bounce_control` (identical geometry/lights to `bounce_colour`, zero bounces).
Each underwent BSP, VIS and LIGHT twice: 72 compiler stage invocations.

The generator's original untested recipe needed corrections: classic .map plane
winding, a sealed skylight cap, separate shader-stage directives, and an occupancy
anchor in the otherwise dark sealed room. These fixes are in `run_pipeline.py`.
The thin tube's old surface-light value 100 quantized to all-zero floor RGB8;
the accepted recipe uses 2000 and records that authored change. Panel remains 100.
No post-bake pixels are repaired or normalized.

Each private `answers/<scene>.q3map-recipe.json` records input hashes, exact argv,
compiler hash/version, source entity fields, source geometry and original bake
answers. The .map and answer are evaluator-only. q3map2 source values differ from
the independent CPU reference baker: point/spot `light=300`, panel surface light
100, tube 2000, sun 120 and sky 25 are **legacy tool units, not cd/nits/lux**.
Panel/tube are emitting six-face rectangular prisms; no native analytic tube
support is claimed. The spot uses a target 128 units away and radius 64; including
q3map2's additional 16 gives an effective half angle `atan(80/128)` (~32.005°),
not the CPU reference fixture's 25°. Sun azimuth 14°/elevation 68° and sky sampling 3
are recorded separately. Coordinates remain JA world units, Z up.

Commands specify JA RBSPv1, one thread, `-meta -leaktest -noflares -samplesize16`,
VIS, then `-light -deluxe -deluxemode0 -gamma1 -compensate1 -exposure0 -bounce0`
(bounce1 only for `bounce_colour`; actual argv have separated options/arguments).
Compiler logs confirm linear lightmap/texture/colour policy, exposure 0 and
compensation 1. Source decoding is therefore `RGB8/255`; no second sRGB decode.
Rec.709 primaries remain an explicit assumption. Absolute photometric scale and
the common lightgrid/lightmap relative calibration remain unknown.

## Export, blind admission and checks

`test_q3map2_runtime.py` copies compiled own-content BSPs/materials into a private
SP game directory, never source .map files or answers. Build3 captures source
observations independently of use: master 0, consumer requests 1, physical PBR,
LTC, Forward+, volumetric fog and dynamic lights off. Each map calls
`r_lightReconExport <scene>_a/b 0 0 0 128 224` twice: all raw images and all mapped
pages, with a bounded region ROI. Texture uploads are not used as observations.

The standalone tool adds two commands:

- `audit-bsp <dataset> <original.bsp>` independently compares SHA/size, every
  original embedded colour/deluxe pixel, raw grid/indirection bytes, source
  vertices, UVs, shading normals, style slots and material names/IDs. Available
  model/world-space deluxe vectors are checked against `normalize(RGB/255-.5)`.
  This compact-fixture audit has a 32 MiB BSP input limit.
- `benchmark <source> <destination> <compiler-version> <PUBLIC-profile>` admits
  these controlled fixtures with a known linear transfer, preserves raw source
  arrays, regenerates derived pages and uses existing blind evidence filtering.
  Public profiles record compiler/settings/hashes without source entity answers;
  original light entities, sun directions and authored intensities are removed
  from solver inputs. Visible emissive geometry/colour remains legitimate evidence.
  This is a trusted evaluator operation for this exact bake profile, not an
  automatic calibration recognizer for arbitrary BSPs. Profile limit 1 MiB.

The solver sees only `runtime/inputs/*.jkobs`; private compiler logs, raw exports,
answers, .maps and original BSPs are outside that mount. Existing versioned,
checked, bounded .jkobs sections are reused without a schema change. Raw source
data and derived scene-linear candidates stay distinct. Full archives contain
held-out samples; the evaluator must still withhold held-out RGB during fitting.

`verify_q3map2.py` independently parses original triangle vertices/UVs and checks
every admitted sample against barycentric mapping at `(x+.5,y+.5)/128`, material
IDs, normals, area and source RGB. Position tolerance .002 world units, unit-normal
alignment >.9999, linear RGB error <1e-6. Spatial 128-unit blocks cannot cross
train/validation/held-out partitions. Area-weighted raw RGB8 floor tests check
colour separation, overlap, spot cone, surface emission, bounce and wall/door
occlusion. Six exported-geometry rays test wall/door, closed ceiling, sky and
skylight aperture. No-hit is not accepted as evidence of sky.

Runtime shots use a fixed camera, exposure 0, gamma 1, disabled motion/temporal
effects and a settled frozen game. A/B cameras and images must agree exactly
(zero changed pixels); images are visual inspection evidence, not decoder input.
The floor/UV/ray tests provide numeric coverage beyond the one camera view.

## Reproducibility limits

Identical **BSP input** must produce byte-identical CPU exports and admitted
datasets. Whole-file BSP identity across two external compiler builds is a
separate measurement, recorded honestly in each recipe. This portable compiler
can vary the ancillary RBSP brush-side `surfaceNum` field; its cause has not been
established. Geometry planes/material fields and all lightmap bytes must agree,
or the script fails. BSP bytes and source SHA are never normalized to conceal it.

A prior repeated wall-leak bake also differed in 12 logical lightgrid cells at
the thin partition, including lit-versus-black records. Those two BSPs remain in
`build/q3map2-bake-accepted/compiler-logs/`; the final witness records this failed
compiler repeat separately. NetRadiant's
[lightgrid code](https://github.com/xonotic/netradiant/blob/master/tools/quake3/q3map2/light.c)
nudges solid grid points using random offsets. This is consistent with the
affected positions, but does not prove the root cause for this exact binary.
Neither style nor nonzero RGB certifies free space. The recipe comparison reports
grid differences as **not reproducible**, including changed logical indices;
it never changes captured samples. Do not claim stable whole-BSP cache hashes
across rebuilds or calibrated lightgrid ground truth at near-solid positions.

See the versioned [validation witness](../tools/rend2/datasets/q3map2-validation.json)
and generated `result.json`/`verification.json` for counts, metrics and hashes.
Runtime source buffers and snapshots use the existing bounded capture path;
the validation tool may load a complete compact fixture. Large stock-map peak
memory has not been re-benchmarked by this follow-up.

## Results on 2026-10-25

Both SP/MP Release builds passed. GCC and MSVC each passed all 15 CPU test
executables (665,064 checks, including 17,500 offline checks); GCC strict syntax
checking passed. Six Python bake-report guards passed. Five actual CLI rejection
tests passed: wrong BSP identity, private answer profile, answer-contaminated
public profile, unsupported transfer and damaged archive checksum. Failed
admission published no output.

All 12 maps loaded, exported twice and passed raw/metadata/source audits. Every
same-BSP export pair and blind-admission pair was byte-identical. In the final
two compiler passes, lightmaps, geometry and resolved/raw grids agreed for all
12; eight BSPs still differed in the ancillary brush-side field (four full files
agreed). The earlier lightgrid repeat failure remains a separate known limitation.

The independent checker tested **25,731 mapped luxels**, with maximum world-space
position discrepancy **3.18e-14 world units** for these planar fixtures. No claim
of this error bound is made for arbitrary patches or stock geometry.

| Area-weighted floor measurement (raw RGB8 scale) | Result |
| --- | ---: |
| Sealed-wall light side / dark side, red | 51.8493 / **0** |
| Doorway lit strip / occluded strip, red | 17.8889 / **0** |
| Two-colour left / right R:B ratio | 3.0722 / 0.2870 |
| One point / overlapping white points, red mean | 35.1875 / 75.9694 |
| Spot inside 20° / outside 45°, red mean | 23.3575 / **0** |
| Panel / tube, red mean | 1.5599 / 5.8021 |
| Bounce 1 minus bounce 0, RGB mean | +26.8060 / +20.7272 / +20.0775 |

All six controlled rays had the expected result with no uncertainty flags: wall
blocked, doorway visible, closed ceiling blocked, sun/sky reached, skylight
centre reached, off-aperture ceiling blocked. Sky checks explicitly use
`trace <file> sx sy sz ex ey ez sky`; ordinary segment queries intentionally
cannot certify sky. The initial verifier used the wrong query purpose and was
corrected in the standalone CLI, with no production visibility change.

All 12 screenshot pairs at 960x540 had **zero changed pixels**, zero maximum
channel difference and identical recorded camera poses (reported integer pose
`-150 -96 76`, angles `0 29 0`, FOV80). The requested `setviewpos` Z64 is not
assumed to equal the player eye height. Original TGAs are under
`build/q3map2-runtime-verified/run/home/OpenJK/screenshots/`; the locally inspected
contact sheet is `build/q3map2-contact.png`. It shows the expected coloured light
separation, spot footprint, door occlusion and increased bounce illumination.
Panel/tube and skylight views are dim with these fixed settings; source floor
measurements and geometry rays, rather than screenshot brightness, certify those
checks. One fixed camera does not cover every surface or the top of every fixture.

The 12 admitted datasets total **127,121,616 bytes**. Each stores both original
128x128 RGB8 colour/deluxe images and all 16,384 base-page records, including
padding flags; only covered records have world-space samples. Source-component
and tile-size estimates are in the witness/inspector, not process RSS. No game
assets, BSPs or screenshots were committed.

The raw/runtime audit used the pre-sky-CLI tool binary; the final independent
checker used its rebuilt binary with optional sky purpose. Both hashes are in
the witness; dataset format/production libraries were unchanged. Earlier
unsuccessful attempts are retained under ignored `build/offline-q3map2-*` and
`build/q3map2-bake-accepted`: winding/sky leaks, unstable camera controls, weak
tube quantization and external-compiler differences were corrected or explicitly
reported, never counted as passing results.

## Scope and remaining coverage

Only tools and documentation changed. Production shaders, map-loading/rendering,
collision and reconstruction consumers remain unchanged. SP and MP Release
targets and both CPU test configurations were rebuilt/tested. This follow-up
executes actual SP; it does not claim MP gameplay validation, compiled curved
patch fixtures, animated-lightstyle bake coverage or external HDR bake coverage.
Those CPU-format paths remain covered by the earlier standalone tests. Stock
maps are not redistributed; previous stock hoth2/hoth3 export evidence stays in
the original report. No inverse solver or recovered-light accuracy is claimed.
