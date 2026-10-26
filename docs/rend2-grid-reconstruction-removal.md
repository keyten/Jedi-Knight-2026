# Removal of the obsolete lightgrid source reconstruction

Investigation base: `380ee0f7e`, 2026-10-25, `rend2-unified-wip`.
Validation completed 2026-10-26 (Australia/Sydney).
This replaces the unsuccessful grid-gradient reconstruction, not original JA
lighting. The [18 SP / 2 MP investigation](rend2-static-lighting-validation.md)
and [obsolete design](rend2-static-lighting-obsolete.md) remain historical evidence.
No inverse solver or new rendering consumer is implemented.

## Dependency inventory and ownership decisions

Search covered shared Rend2, both SP/MP registration/build lists, GLSL,
standalone tests, runtime tools, generated settings menus and documentation.
The [machine-readable inventory](../tools/rend2/lightrecon_baseline/grid-removal-inventory.json)
records the original file hashes, function/declaration search inventory, private
state members and cross-file references. Its signature scan is not an AST call
graph; the ownership decisions below also use inspected bodies and all callers.
The production call graph at the investigation base was:

```text
RE_LoadWorldMap
  R_LoadBSP -> R_BuildLightGridTexture (ordinary fog and entity volumes)
  R_LoadAreaLights (authored JSON / auto emissive LTC)
  R_BuildStaticLighting
    R_PBRCalibrateLegacyMap                     PRESERVE independently
    sun-alignment + centre/corner collision rays PRESERVE ordinary fog B/S
    R_CollectStaticAreaSources                  DELETE fitter-only adapter
    VR_Reconstruct                             DELETE
      gradients -> seeds -> source fitting -> MatchSource attribution
      entity attributed directions / L1 moments
      runtime attenuation fitting -> point/spot classification -> promotion
      constriction/aperture fitting -> portal promotion
    R_BuildEntityLightProbes(dirMix)             PRESERVE original BSP mode
    R_CalibrateAreaLight / Finish                DELETE grid fitter calibration
    R_UploadVolumetricStaticLighting             REPLACE by ordinary B/S upload
  R_CalibrateEntitySun                          PRESERVE
  canonical reconstructedWorld.Finish          PRESERVE unchanged

R_VolumetricBuildLightLists -> R_StaticLights / R_StaticLightPortals DELETE
RE_RenderScene -> StaticLights/StaticLightPortalsBeginScene         DELETE
R_EntitySunProbe -> R_SunVisibleFromPoint                          PRESERVE
```

| Old module | Owned functions/data | Disposition |
| --- | --- | --- |
| `tr_volrecon.h/.cpp` | `vrInput`, `vrAreaSource`, `vrProxy`, `vrStaticLight`, `vrLightPortal`, light/portal flags and kind enums, `vrOutput`, `vrStats`; private `Ctx`, `Seed`, `Source`, accumulators, gradient/eigen/solve/profile/flood scratch; `ComputeGradients`, `FindSeeds`, `FitSeed`, `BuildAreaAnchor`, `MatchSource`, `Attribute`, `EntityDirections`, `ResolveMoments`, `FitRuntime`, `ClassifyPoint`, `PromoteLights`, `FindConstriction`, `FitAperture`, `BuildPortals`, `Reconstruct`, `VR_Reconstruct`, `VR_PackHalf` | Entire files deleted; no source/proxy/portal compatibility wrappers. General finite half packing retained separately with exhaustive tests. |
| `tr_staticlighting.h/.cpp` | `staticLight_t`, `staticLightPortal_t`, upload stats, `s_sl` vectors/stats/timers; build/clear/list/PVS/debug/stats accessors; collision trace callbacks; `R_StaticSunRays` | Entire files deleted. PBR call moved to world boundary; sun trace to `tr_light.cpp`; edge-refined ordinary fog rays to `tr_bakedlighting.cpp`. |
| `tr_volumetric_reconstruct.cpp` | sampler capability cache; `R_VolumetricStaticDirectional`, clear/upload functions; B/S/M texture packing; particle mean reference and sun percentile | Deleted. B/S, particle reference and sun percentile preserved in compact `R_BuildBakedFogLighting`; no M or P fields. |
| `tr_arealights.cpp` | `R_CollectStaticAreaSources`, `R_CalibrateAreaLight`, `R_FinishAreaLightCalibration`, calibration count/threshold | Removed only fitter adapters; authored JSON, authored units/modes, auto-candidate scanner, shared image-mask grouping, line/tube and dynamic area lights remain. |
| `tr_bsp.cpp` | optional probe `dirMix` input, deferred probe construction | Removed attributed input. Probe construction now directly belongs to `R_BuildLightGridTexture`, with the original BSP direction, no canonical dependency. |
| `tr_volumetric.cpp` | static/portal list append, PVS scoring, fake shaft records, exact-only debug scaling, recovered/portal statistics and extra 64-record capacity | Removed. Ordinary dynamic point/spot/saber lists, cookies, compute clustering, shadow maps and fog media remain. |

All helpers private to the removed fitter (including its local math/flood/
fitting utilities) had no independent production caller. The new CPU core already
has its own geometry, decoding, validation and visibility math and does not link
to them. `tools/volrecon_test/volrecon_test.cpp` and the include-based
`static_lighting/vrdiag.cpp` tested only the deleted algorithm and are removed.

The old `PROXY_MAX=256` area-first source budget, gradient seeds, isotonic profile
fitting, attribution, physical-confidence/spot classification, finite runtime
attenuation fit, baked-energy subtraction/promotion, fake portal shafts and
reconstruction-only first moments no longer exist in production.

## Independent rendering and map boundary

`RE_LoadWorldMap`, after successful BSP/world publication and authored LTC load,
calls `R_PBRCalibrateLegacyMap(world)` unconditionally, then
`R_BuildBakedFogLighting(world)`, then `R_CalibrateEntitySun(world)`. Canonical
world completion still follows legacy/weather/cubemap initialization. None of
these calls consults `r_lightReconBuild`, master-use or consumer flags.

`R_SunVisibleFromPoint` moved into `tr_light.cpp`, keeping the prior SP `SV_Trace`
versus MP `CM_BoxTrace` contract (including legacy unobstructed-ray acceptance).
This is a runtime sun helper, not the canonical optical visibility oracle. The
entity sun cache now includes a map calibration generation because world storage
can be reused across map loads. Its original units/calibration and analytic sun
shadow path remain otherwise unchanged.

Fog mode 0 performs no new fog preparation; mode 1 keeps the original merged
`R_BuildLightGridColorTexture`; mode 2 keeps the same merged volume and independent
B/S split. LDR baseline is `max(A,D)`, HDR `A+D`. With authored sun metadata,
`f=alignment*legacy visibility`, `S=min(f*D,legacy)`, `B=legacy-S`. Same centre/corner
rays, alignment thresholds, sun 90th percentile, half rounding and formats remain.
Without a usable split, injection binds the ordinary merged grid; the existing
no-grid placeholder behaviour is retained. There is no canonical
fog-field consumer or recovered point-light injection.

The style-availability mask in legacy presentation is retained for compatibility,
and is explicitly **not** free-space classification. New original observation
datasets keep every raw logical cell and their existing geometry-aware flags.
No observation, region graph, source evidence, codec, cache policy, asset/API,
builder or lifecycle implementation was removed or substituted.

`r_entityLightGrid` modes 0/1/2 and `r_entityLightProbes` remain. Optional L1 probes
project original `A+D max(N.u,0)` to `C0=A+D/4`, `C1=D*u/2` with de-ringing; they
make no claim to recover separate source directions. Existing style-refresh and
legacy validity conventions remain unchanged. Authored LTC values are never
overridden by a grid fit.

## Deleted controls and GPU state

Removed consistently from SP/MP globals, externs and registration:

```text
r_staticLightReconstruction       r_recoveredVolumetricLights
r_recoveredVolumetricMaxLights    r_recoveredPhysicalConfidence
r_recoveredSpotConfidence         r_staticLightDebug
r_lightPortals                    r_lightPortalDebug
r_ltcAreaCalibration              r_ltcAreaCalibrationScale
r_volumetricFogStaticDirectional
r_vfogStaticStats (console command)
```

There are no aliases to canonical `r_lightRecon*` controls. Persisted user config
lines for removed names have no renderer effect. The generated render-settings
catalog and presentation labels also drop the removed fog control.

Deleted `world_t` fields: moment images R/G/B, reconstructed source/area counts,
directional cell/energy statistics. B/S images, sun colour/availability, particle
reference, PBR calibration and independent entity volumes remain. Deleted shader
state: `USE_FROXEL_STATIC_RECONSTRUCTION`, three moment sampler uniforms and their
uniform enum/table entries, `TB_VOLUMETRICMOMENTB`, moment bindings and fragment/
compute capability checks, fake portal record branches, L1 fog/debug branches.
Retired debug slots 21/22/57/58 are not reassigned; ordinary B/S views20/23/24/25
remain. Existing GL3.2 baseline injection needs 14 samplers (15 with RGB
extinction), with no extra three-moment requirement.

The independent liquid procedural baked-light approximation remains mode2;
direction-dependent mode1 remains unavailable without direction evidence.
No water, shadow, Forward+, cookie, LTC or dynamic-light shader feature is
force-enabled by reconstruction policy.

## Memory and map-load work

No proxy/source vectors, attribution/gradient/position arrays, promotion buffers,
aperture fitting, reconstruction PVS lists or stats/debug cache survive. No
reconstruction solver runs at map load. The preserved fog split is linear in
logical grid cells: 16 bytes/cell half B/S staging, 1 byte/cell style mask,
8 bytes/cell sun alignment/visibility when authored sun exists, temporary centre
mask during tracing and a sun-luminance vector. Vectors die synchronously on
return; image ownership follows the renderer image registry and shutdown.
GPU fog B/S still costs 12 bytes/cell; the optional 18 bytes/cell RGB16F moment
volumes are gone. Froxel light/range storage loses 64 obsolete records per frame;
normal dynamic capacity is unchanged. Canonical buffers keep their prior bounded
ownership and disabled/cache-only behaviour.

## Validation

Local generated evidence is under `build/remove-grid-recon/`; no stock assets or
screenshots enter Git. The compact, versioned
[validation witness](../tools/rend2/lightrecon_baseline/grid-removal-validation.json)
records source/binary/frame hashes, test results, settings, cameras and limits.

### Builds, CPU and source contracts

| Check | Final result |
| --- | --- |
| SP and MP Rend2, MSVC 2019 x64 Release | Both built successfully; existing x64 `/arch:SSE2` warning remains. |
| Standalone GCC Release CPU suite | 16/16 tests, 1,777,137 checks, 21.11 s. |
| MSVC Release CPU suite | 16/16 tests, same 1,777,137 checks, 5.62 s. |
| New independent baked-lighting test | 1,112,073 checks: B+S conservation, RGB8/HDR bounds, half boundaries/ties/saturation and finite PBR calibration. |
| Reconstruction dependency/registration mutation tests | 15/15. Includes deliberate reintroduction of retired controls and source dependencies. |
| Render-menu tests | 7/7; release ZIP changed only its menu PK3, other entries byte-identical. |
| Final disabled-path source contract | 27 programs / 51 stages, zero differences, retirement and ownership audits passed. |

The existing CPU suite still exercises asset/API/controls/disk/lifecycle, raw
lightmaps, decoder, lightgrid, luxel mapping, classification, BVH, regions,
emitter evidence and offline datasets. Canonical core/adapter production sources
have no changes in this removal. No new q3map2 bake was needed here; the extra
actor scene uses the already validated, self-authored `point_room.bsp` fixture.

Executed build/test commands (PowerShell from the repository root):

```powershell
$env:PATH='C:\Users\Keyten\Desktop\projects\OpenJK\build\toolchain\w64devkit\bin;'+$env:PATH
$py='C:\Users\Keyten\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
cmake --build build/lightrecon-core-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 4
cmake --build build/lightrecon-core-renderers --config Release --parallel 4
ctest --test-dir build/lightrecon-core-renderers -C Release --output-on-failure
cmake --build build/lightrecon-disk-standalone --parallel 4
ctest --test-dir build/lightrecon-disk-standalone --output-on-failure
& $py tools/rend2/test_lightrecon_contract.py
& $py -m unittest discover -s tools/render_menu -p 'test*.py'
git diff --check
```

The renderer build directory is configured with Visual Studio 16 2019, x64,
`BuildSPRend2=ON`, `BuildMPRend2=ON`, `BuildLightReconTests=ON`, engine/game/UI/
vanilla-renderer targets off, and bundled dependencies. The standalone directory
uses `cmake -S tools/lightrecon_test -B build/lightrecon-disk-standalone
-G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release`. Existing configured directories
were reused; paths to compilers/dependencies are machine-specific.

### GLSL review and GPU execution

The pre-removal baseline is frozen in
[`obsolete-grid-v1/`](../tools/rend2/lightrecon_baseline/obsolete-grid-v1/README.md),
including the exact compressed diff and explicit removal review. Of 27 programs,
22 are unchanged; five injection stages lost dead portal, moment initialization
and retired debug code. All active driver-reflected sampler, UBO and SSBO
interfaces are identical. `lightall.glsl` and `volumetric_common.glsl` are unchanged.
The reviewed current baseline now has zero differences; a future old-symbol or
binding reintroduction fails the retirement audit, rather than silently passing
because its shader branch happens to be disabled.

Intel UHD Graphics, driver `3.2.0 - Build 27.20.100.8280`, compiled and linked all
27 programs / 51 stages. The full GPU/contract suite initially passed 14/15:
`test_volumetric_compute.py` failed because its pre-existing UBO fixture left
`u_FroxelDebugParams.w` (baked PBR scale) at zero, making baked input black.
The fixture now sets the production value 1. No tolerance was relaxed and no
production shader was changed to satisfy that assertion. The focused rerun
passed 1/1, compiling all 22 scalar/RGB raster/compute/media permutations and
checking integration, tail/particle lighting, local media, height, history,
jitter and reprojection. Thus every selected test has a passing final result;
there was no second complete 15-test run.

```powershell
& $py tools/rend2/lightrecon_regression.py --output build/remove-grid-recon/pre-shaders
# After deletion, comparison to the historical baseline intentionally reports the reviewed diff.
& $py tools/rend2/lightrecon_regression.py --output build/remove-grid-recon/post-shaders-review
& $py tools/rend2/lightrecon_regression.py --write-baseline 'Remove obsolete grid reconstruction only; preserve independent baked B/S, entity and sun paths' --output build/remove-grid-recon/post-shaders
& $py tools/rend2/lightrecon_regression.py --no-gpu --output build/remove-grid-recon/source-final
& $py tools/rend2/run_gpu_tests.py
& $py tools/rend2/run_gpu_tests.py --only test_volumetric_compute.py
```

The baseline-writing command documents the deliberate update; it is not a
routine test instruction. Current users run the comparison without
`--write-baseline`. The baseline revision field identifies the investigation
base; source hashes and the review describe its working-tree removal changes.

### Actual SP map and screenshot checks

Licensed stock assets and a runnable SP executable were available. Isolated
runtime directories loaded `hoth3` and `hoth2`; no installation assets were
modified. Four legacy/modern map configurations were run before and after the
change with the existing A/B/A consumer-toggle protocol. All within-build image,
dynamic-light/FX positive stimuli and permutation checks passed. A naive
cross-process comparison of those first captures did **not** pass: camera
smoothing/third-person placement made the poses differ. Those images are not
accepted as a before/after lighting oracle.

`test_bakedlighting_removal_runtime.py` instead disables camera smoothing,
exposure adaptation, fog noise/history, weather and dynamic temporal effects,
freezes static captures, hides actors for the exact comparison and records both
camera poses. Nine configurations (18 launches) passed repeat and before/after
comparison at 960x540, FOV 80. Every comparison was exactly zero differing pixels,
zero maximum channel delta and zero mean RGB delta. The permitted cross-build
budget is at most five pixels, channel delta 1, mean RGB delta 1e-5; no budget was
used. Recorded cameras are `(3464,700,97), yaw90` for `hoth3` and
`(1728,-576,1052), yaw135` for `hoth2`, pitch/roll zero.

| Configuration | Map | Build policy | Independent path covered |
| --- | --- | --- | --- |
| legacy | hoth3 | 0 | PBR/fog/Forward+/LTC off, legacy entity grid |
| physical-fog | hoth3 | 3 | Physical PBR, froxel fog, Forward+ and LTC |
| fog1-multipoint | hoth3 | 0 | Legacy fog mode1, entity grid mode1 |
| cacheonly-gpu-grid | hoth3 | 1 | Missing cache, physical PBR, fog2, entity grid2 without probes |
| independent-probes | hoth3 | 0 | Raw BSP L1 entity probes with fog2 and Forward+ |
| sun-raster | hoth2 | 0 | GL3.2 raster fog, sun shadows, entity sun split |
| sun-physical-ltc | hoth2 | 2 | Physical PBR, modern sun shadows, fog, Forward+/LTC, probes |
| ltc-request-only | hoth3 | 0 | LTC requested, Forward+ off; existing LTC prerequisite remains |
| forward-only | hoth2 | 0 | Forward+ on, LTC off |

Master reconstruction use is zero, with consumer requests set to one: none
becomes effective. Policies 1/2/3 do not start a solver or force another renderer
technology. No GL error or NaN/Inf diagnostic appeared. No new black fog volume,
sun direction change, double sunlight or static-surface difference appears in
these controlled views. This is comparison to the prior non-reconstruction
Rend2 configuration, not a claim that physical PBR/fog/LTC matches vanilla JA.

Player/NPC and dynamic-light captures are separate from static goldens because
animation/FX randomness is not an image oracle. An additional actual-SP run
loads the self-authored q3map2 point room after `hoth3`, resumes gameplay, spawns
a Ghoul2 stormtrooper, equips and ignites the player's saber and adds test dynamic
lights. The saved `saber-room/legacy-after/.../baked-entities-saber.tga` visibly
shows the green blade and green light on the NPC; `saber-proof.png` is a lossless
preview. This verifies real saber/NPC lighting in that fixture, not exhaustive
stock-map combat or per-frame actor image identity. Early actor attempts with
incorrect menu unpause assertions were rejected. The harness now defers menu
closure one paint and verifies `cl_paused=0`; SP's stale `sv_paused=1` diagnostic
is explicitly not used as proof of active simulation.

Separate lifecycle runs passed `hoth3 -> hoth2 -> hoth3 -> vid_restart ->
disconnect`, with raw internal lightmap/lightgrid hashes, external RGB/HDR and
deluxe fixtures. Disabled/cache-only paths retained no observation buffers;
unloaded states retained no old map identity. Combining lightmap-HDR and
observation-only grid-HDR profiles initially failed incompatible fixture settings;
the CLI now makes those options mutually exclusive and both separate runs pass.
Runtime cache-hit/build-success is not claimed: the complete cache key and real
solver are still unavailable, with synthetic CPU coverage for those transitions.

```powershell
$install='C:\Program Files (x86)\Steam\steamapps\common\Jedi Academy\GameData'
$bins='build/ssr-msvc/RelWithDebInfo'
$before='build/remove-grid-recon/before-rdsp-rend2_x86_64.dll'
$after='build/lightrecon-core-renderers/Release/rdsp-rend2_x86_64.dll'
$private='build/lightrecon-evidence-runtime/final-source/stock-assets/base'
& $py tools/rend2/test_lightrecon_legacy_runtime.py --installation $install --binaries $bins --renderer $before --output build/remove-grid-recon/runtime --label before
& $py tools/rend2/test_lightrecon_legacy_runtime.py --installation $install --binaries $bins --renderer $after --output build/remove-grid-recon/runtime --label after
& $py tools/rend2/test_bakedlighting_removal_runtime.py --installation $install --binaries $bins --before-renderer $before --after-renderer $after --output build/remove-grid-recon/fixed-runtime --private-archives $private
& $py tools/rend2/test_lightrecon_lifecycle_runtime.py --installation $install --binaries $bins --renderer $after --output build/remove-grid-recon/runtime --label lightmaps-final --lightmap-fixtures
& $py tools/rend2/test_lightrecon_lifecycle_runtime.py --installation $install --binaries $bins --renderer $after --output build/remove-grid-recon/runtime --label lightgrid-final --lightgrid-fixture
& $py tools/rend2/test_bakedlighting_removal_runtime.py --installation $install --binaries $bins --before-renderer $before --after-renderer $after --output build/remove-grid-recon/saber-room --private-archives $private --actor-fixture-suite build/q3map2-bake-verified --only legacy
```

Use fresh output directories when rerunning; the fixed comparison deliberately
rejects an existing output directory. `$before` was built and saved before
editing, from the investigation base. Historical-only cvars are set to zero only
for that old DLL. Raw frames and the per-case copied DLL hashes distinguish
intermediate builds from the final DLL; final lifecycle and saber-room checks
use the final Release DLL. These commands require the local licensed archives,
SP engine/game dependencies and (for the optional actor room) the prior q3map2
fixture generation workflow in `rend2-offline-datasets.md`.

### File scope and remaining limitations

The validation witness lists all modified/deleted/new files. Production deletions
are the five obsolete source/header files above. Other removals are fitter-only
tests/diagnostic scripts and demo settings42-44. Modified production files are
the SP/MP build/registration lists, `tr_bsp`, `tr_light`, `tr_arealights`,
`tr_volumetric`, `tr_glsl`, `tr_local`, `tr_scene` and two volumetric shaders.
New production files are only `tr_bakedlighting.cpp/.h`. Tooling changes extend
the existing shader/CPU/runtime checks, remove the settings-menu entry and update
its two distributable archives. Current docs link to the canonical architecture;
the old reports remain clearly labelled historical.

Legacy sun collision traces, style-availability masks, sun-alignment thresholds
and fixed-at-map-load entity probe styles remain existing approximation debt,
not recovered optical/physical truth. Ordinary sun/fog preparation still traces
at map load: observed final `world static lighting` log values ranged from 5 to
866 ms in the nine cases, with no old reconstruction step. These runs were not
a timing benchmark; cache state and other GPU activity were uncontrolled.
There is no exhaustive material/driver/permutation or MP gameplay validation;
MP was compiled and shared CPU/shader contracts passed. No inverse solver,
canonical lighting consumer or runtime cache-hit integration was introduced.
