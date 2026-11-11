# C01: resolved directional sun consumer

C01 is an independently opt-in renderer adapter. It consumes immutable physical
sun metadata; it does not recover stock lighting. The scientific stock result
remains unchanged: no accepted nonempty original-stock physical model among the
eight tested maps, no prepared stock physical fields, and failed blind medium
source discovery. Operating this adapter is not evidence of source recovery.

## Activation and selection

Both `r_lightReconUse 1` and `r_lightReconSun 1` are required. Both still default
to 0. Building/loading remains the independent map-load `r_lightReconBuild`
policy. No query triggers fitting, tracing, loading or cache generation.

The installed adapter requires the existing physical PBR/sun/depth variants:
`r_pbrPhysical 1`, nonzero `r_sunlightMode`, and `r_depthPrepass 1`. Configure
latched renderer controls before map load/`vid_restart`. C01 adds no shader
permutation or GPU resource layout. Its two consumption controls remain live.
When froxel fog is enabled, its full original baseline texture must exist;
otherwise the complete sun adapter falls back rather than binding an invented
black baseline. `r_cubeMapping` and `r_diffuseIBL` must be 0: current captured probes have no
renderer-bound proof that they exclude this sun. This is an explicit unsupported
composition, not permission to change those cvars automatically.

Precedence preserves the order in the actual historical `RE_BeginScene`:

1. Authoritative `RDF_EXTRA | RDF_SUNLIGHT` refdef values, copied last by JA.
2. Any nonzero `r_forceSun` (including animated mode 2). A positive explicit
   `r_pbrSunIlluminance` also retains the existing authored-brightness path.
3. A unique, physically usable canonical sun, when the independent gates pass.
4. Shader-authored sun.
5. The existing renderer fallback. Its arbitrary default direction is not sun
   evidence; `sunParsed` remains false and C01 never promotes it to a source.

The legacy code runs first, unchanged. Failure/override leaves its sun arrays,
map colour scale and shadow-floor values intact. C01 never writes `tr.sunLight`,
`tr.sunDirection`, `tr.sunParsed`, a cvar or the canonical asset.

## Admission and quantities

`lightrecon/resolved_sun.h/.cpp` is the CPU renderer policy, with no GL/world
owner. `State` is a trivially copyable, detached value in `trRefdef_t`; it owns
no pointer, source handle or Snapshot lease. The renderer caches admission by
publication generation, not per subsystem or per pixel. Scene policy is cheap
and separate. Main-world begin/unload and renderer reset clear cached values.

The state records policy, availability, failure reason, physical versus legacy
classification, rejected-candidate classification, source/provenance/calibration
IDs, confidence, optional angular radius, both direction signs and RGB normal
illuminance. Exposure and tone mapping are absent.

* Canonical directional `frame.forward` is photon travel. `Sun().directionToSun`
  is its negative; both are copied rather than inferred from engine defaults.
* Emitter RGB is linear Rec.709/D65, Y in lux at normal incidence. The adapter
  uses `Photometric::Emission`, requires `DirectionalIlluminance`, and requires
  `PhysicalSI`. `Rend2PBR::PhysicalToScene` applies the current fixed scene scale
  once: 100000 lux gives scene illuminance 10 at scale 0.0001.
* Relative values, calibrated legacy scene equivalents, approximate appearance
  fields and unknown colour/intensity are not exact physical sunlight. Their
  explicit result classification/reason leads to the legacy path. No inverse
  of historical Q or encoded RGB is attempted.
* Authored/external physical sources require explicit exact-candidate suitability,
  complete selected configuration/profile/geometry identity, supported photometry,
  no observation issues and global rather than region-limited applicability.
* Reconstructed sources additionally require a validated per-source ExactEmitter
  admission, complete supported uncertainty factors, known absolute scale and a
  selected-configuration witness. A Conditional aggregate sun assessment blocks
  use. The aggregate canonical assessment deliberately cannot certify a sun;
  admission uses separate source evidence rather than changing that restriction.
  Consumer-scoped empirical event `rend2/directional-sun:0.25deg-5pct` must have
  calibrated lower bin probability at least 0.95. Direction spread must be known
  and at most 0.25 degrees and intensity bounds must lie within +/-5 percent of
  the central value. Generic confidence scores do not meet this contract.

These are consumer admission requirements, not a claim that suitable recovered
stock evidence currently exists. CPU tests supply controlled metadata witnesses;
they do not perform or certify an empirical confidence calibration.

## Audited sources and consumers

| Boundary / file | Existing meaning | C01 behaviour |
| --- | --- | --- |
| `tr_shader.cpp`: `sun`, `q3map_sun`, `q3gl2_sun` | normalized authored RGB times legacy strength; yaw/elevation produces direction toward sun; q3gl2 can enable shadows | Original parsing/state retained |
| `tr_bsp.cpp` main load | default colour/direction; shader state; baked calibration, fog split and entity calibration | No default promotion; all original baked preparation retained |
| `tr_scene.cpp` | overbright/gamma legacy sunCol; mode 1 modulation; mode 2 direct light; animated and refdef overrides | One scene resolution after all explicit overrides, before scene consumers/CSM |
| `tr_main.cpp` CSM | camera forward is negative `refdef.sunDir`, photon travel | Uses resolved scene direction; canonical sun can enable the existing CSM path |
| CSM static cache/disk pages | exact MVP, light axes, bounds, texel scale, caster settings/world generation | Existing keys already cover direction and relevant shadow changes; no generation-based per-frame flush |
| `tr_backend.cpp`, `lightall.glsl` | primary sun colour migrated by legacy calibration or explicit lux; mode 1 shadows baked diffuse; mode 2 adds direct sun | Canonical scene illuminance bypasses legacy/lux re-conversion; mode 2 replaces the existing primary sun lobe; mode 1 remains modulation; additive diffuse needs P06 proof |
| `tr_light.cpp` entities | original grid plus optional heuristic sun subtraction and bounded local sky probes | Preserve grid diffuse; bypass heuristic subtraction for canonical sources; use scene direction and primary physical sun lobe, with separately gated additive diffuse |
| `R_CalibrateEntitySun`, `tr_bakedlighting.cpp` | map-load authored-direction statistics/sun-grid split in legacy units | Not reinterpreted as measured lux or recalibrated each frame; disabling C01 restores the original split |
| `tr_atmosphere.cpp` | legacy white-Lambert proxy, ground versus TOA conversion; shader multiplies by pi | Supplies scene E/pi to the existing proxy boundary, uses scene direction and known angular extent; unknown/zero extent produces no invented finite solar disk |
| `tr_clouds.cpp` | atmospheric/ground sun proxy, sun direction, cloud attenuation | Same resolved sun via atmosphere adapter; correct frontend/backend scene selection; direction changes refresh tracked cloud shadow maps and temporal keys |
| `tr_volumetric.cpp`, `volumetric_inject.glsl` | split original grid; empirical sun radiance; 4*pi HG phases; CSM replaces baked sun proxy | Restore full original grid when canonical is selected; direct canonical scattering requires P06 proof and current CSM coverage; E/(4*pi) matches phase normalization |
| `tr_sky.cpp`, `tr_postprocess.cpp` | sprite and ray visibility positioned from global authored direction | Canonical views use their scene direction; no additional sprite is submitted |
| `tr_pom.cpp` | scene sun direction for POM self-shadow | Already reads scene sunDir; no second selection |
| `tr_weather.cpp`, `tr_rainlens.cpp` | legacy grid/weather appearance and scene sun proxy | Scene sun is shared; lens key direction recognizes canonical sun; legacy weather/grid appearance is not called physical radiance |
| Cubemap/environment captures | scene setup, saved view state and diffuse/specular probe transport | Scene-local value survives captures/portals; composition with existing probes is explicitly unavailable |

Atmosphere and clouds retain their existing optional artistic intensity,
ground/TOA and scattering controls. The immutable state always contains the
source illuminance before those controls. CSM/PCF remain the existing directional
shadow approximation; a finite angular radius does not turn them into exact
finite-source dynamic penumbrae. Textured skyboxes may contain authored imagery
which C01 cannot separate or reposition.

## Contributions and visibility

Direct sun, sky, indirect light and approximate appearance are distinct. C01
never samples SH irradiance as incoming radiance, imports a reconstructed field,
adds environment radiance, or treats a static source-visibility average as a
dynamic mask.

For additive world, entity and fog diffuse, the adapter requires two complete
P06 contribution descriptors in the exact renderer domain respectively:
`rend2/world-diffuse`, `rend2/entity-grid`, `rend2/fog-grid`. One describes this
explicit sun, the other the complete original baseline. Configuration, profile,
geometry, linear colour, quantity, units, calibration, source roots, reflection
orders and sample-domain coverage must satisfy the real P06 `CanAdd` algebra.
The baseline must exclude direct sun. Algebra alone is insufficient: a separate
fully supported, uncontradicted `ReplacementAllProofs` receipt must bind scope to
that domain and runtime operator `rend2/original-baked-linear/v1`, with known zero
absolute error and leakage risk. This is an admission receipt, not an instruction
to subtract any baked energy. Unknown/multiple descriptors deny the channel.

No such proof exists for the stock fixture. Its baked world/grid contribution
is preserved, world/entity additive sun diffuse is denied, and fog uses the full
original grid. Supported mode 2 primary specular/CSM/atmosphere/cloud consumers still share
one source. Direct fog, if admitted on controlled data, uses only the combined
current CSM visibility within valid coverage, without multiplying a prepared
area-average static fraction. Outside coverage it keeps the original baseline.
Missing probe composition disables canonical selection instead of adding a
second specular sun through IBL.

## Cache and lifecycle

Live deactivation restores the pre-canonical scene shadow matrices, splits,
blend widths, texel scales, depth spans and depth-top value. This matters on a
shader-authored map without legacy CSM: sunlight mode 2 can read the old shadow
block even when its current scene generates no CSM. Retaining canonical matrices
there caused a strict A/B/A failure (about 850 pixels, maximum 16 display codes)
in initial testing. The final adapter restores those historical values, without
clearing the immutable asset or invalidating an unchanged canonical cache.

The existing CSM classifier compares exact projection matrices. Direction changes
therefore invalidate full depth or scrolling eligibility; identical suns reuse
the cache. Brightness, source ID or a new publication with identical shadow
geometry do not force a depth rebuild. Disk layout keys already include the
light axes, bounds, dimensions, texel sizes, world and caster settings. Mode 1
continues to bypass a continuously moving sun and admits a settled projection
after four observations. Existing map/caster generations protect reused world
storage. No canonical source or shadow slot is added to the asset.

Cloud shadow interval reuse additionally checks the tracked scene sun direction,
including forced modes while the consumer is requested, and handles transition
back to legacy. Cloud/froxel temporal keys include canonical direction and colour
only while selected, so a live switch cannot retain the previous illumination.
The all-disabled cloud interval/history keys retain their old rules.

Worldless scenes never select a world asset. Scene refdef copies own the detached
value through skybox, mirrors, recursive views and queued backend work. Current
world generations are re-acquired for new scenes; no old emitter pointer is
retained. SP and MP share this code. `r_lightReconInfo` prints actual scene policy,
source, rejected-candidate class, failure reason and independent P06 channel
coverage in addition to the existing consumer capability gate. Gate availability
is not a scientific certificate or an assertion that an explicit override was
replaced.

## Validation and limits

Reproduction tools:

* `lightrecon_resolved_sun_test`: actual Snapshot/photometry/P06 boundary, signs,
  lux conversion, forced/refdef precedence, physical mode, missing/ambiguous sun,
  relative/legacy rejection, scoped evidence, duplicated membership, unload and
  snapshot lifetime. Fixtures are controlled authored inputs.
* `test_resolved_sun_cache.py`: extracts and compiles the actual production CSM
  classifier and disk-layout key, with renderer storage stubbed. 1039 checks
  cover stable reuse, direction/projection/settings/world changes and animated
  mode 1 bypass/settling. No parallel cache-policy implementation is tested.
* `test_resolved_sun_gl.py`: 37 actual shader programs, physical sunlight modes
  1/2, lightmap/vector/vertex, entity split and skin permutations; actual froxel
  readbacks verify missing P06 and missing CSM coverage preserve the baseline.
* `test_resolved_sun_runtime.py`: private SP/MP authored cache, fixed camera/exposure,
  A/A/B/B/restore, forced modes, cache load, vid_restart and unload. The offline
  `lightrecon_sun_fixture_tool` explicitly labels its 100000 lux sun as manually
  authored, not recovered stock data. It reads an explicit dependency manifest
  and writes only the specified private cache; the renderer hot path never does it.

* `test_resolved_sun_disabled_runtime.py` compares two renderer DLLs and
  verifies same-process A/A, independent gate-off, missing-asset fallback and
  restore controls. `--world-only` hides dynamic entities for separate-process
  comparisons.

For MP, build a private pause UI in a separate scratch worktree. Apply
`tools/rend2/c01-mp-pause-ui.patch` with `git apply --unidiff-zero`, configure
`BuildMPUI=ON`, build the Release
`uix86_64` target, and pass the resulting DLL as `--mp-test-ui`. The UI routes
`c01_test_pause` and `c01_test_resume` through the normal UI pause cvar and
keeps the world unobscured. Verify both `cl_paused` and `sv_paused`, the camera,
world draw counters and A/A pixels before accepting comparisons. This patch
is a test fixture and is outside the compiled production renderer.

### Measured results

On the controlled SP `hoth2` world at 640 by 360 with fixed exposure, cache
mode 2 and the authored 100000 lux source, A/A had one pixel differ by one
code value. B/B and A/restore were bit-exact. A/B changed 227095 pixels with
mean absolute channel delta 6.454 and maximum delta 20. Forty-five cascade
depth comparisons passed. Forced modes 1 and 2 took precedence, the authored
cache reloaded on `vid_restart`, and unload cleared the publication.

The same SP capture sampled 30 GPU frames per state. Median main-view time
was 13.090 ms off and 12.396 ms on. Enabled dynamic cascade overlay medians
were 0.191, 0.162 and 0.214 ms. These are measurements from that capture.
Cloud and fog timings varied, and one run does not establish a net speedup.
The final source also requires initialized CSM FBOs before selection.

The first SP `hoth3` original/current run had bit-exact A/A, gate-off,
missing-asset and restore images in each executable. The separate executions
froze different player poses and animated surfaces and differed by 25112
pixels. That cross-execution comparison did not pass.

Cost: one detached state in each scene, one existing sun and three existing CSM
cascades; zero extra GPU light slots/textures/samplers and zero new permutation
count. Admission scans/strict photometric validation occur on publication change;
per-scene selection/copy performs no allocation, fitting, disk I/O or global ray
tracing. CPU timings are synthetic microbenchmarks, not frame-time measurements.
A GPU frame-time improvement/regression is not inferred from shader compilation.

Unsupported: stock absolute photometry, conditional source promotion, spatially
scoped suns, relative exact sunlight, probe sun separation, original bake
subtraction without a scoped proof, exact area-source dynamic penumbrae, and
separating a sun already painted into a skybox texture. Missing data retains an
explicit original renderer channel, not an invented black or bright source.
