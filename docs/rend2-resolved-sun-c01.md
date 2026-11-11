# C01 resolved directional sun consumer

C01 consumes immutable physical sun metadata through an independent renderer
gate. Validation uses a controlled, physically authored source. The stock
study accepted zero nonempty physical models across eight original maps,
prepared zero original-stock physical fields and failed the blind medium
source discovery benchmark.

## Activation and selection

Both `r_lightReconUse 1` and `r_lightReconSun 1` are required. Both still default
to 0. Building/loading remains the independent map-load `r_lightReconBuild`
policy. No query triggers fitting, tracing, loading or cache generation.

The adapter requires `r_pbrPhysical 1`, nonzero `r_sunlightMode`,
`r_depthPrepass 1` and initialized CSM resources. Configure
latched renderer controls before map load/`vid_restart`. C01 adds no shader
permutation or GPU resource layout. Its two consumption controls remain live.
With froxel fog enabled, a missing original baseline texture selects the legacy
sun path. `r_cubeMapping` and `r_diffuseIBL` must be 0 because captured probes
currently lack renderer-bound sun exclusion proof.

Precedence follows the historical override order in `RE_BeginScene`.

1. Authoritative `RDF_EXTRA | RDF_SUNLIGHT` refdef values, copied last by JA.
2. Any nonzero `r_forceSun` (including animated mode 2). A positive explicit
   `r_pbrSunIlluminance` also retains the existing authored-brightness path.
3. A unique, physically usable canonical sun, when the independent gates pass.
4. Shader-authored sun.
5. The existing renderer fallback. Its arbitrary default direction leaves the
   sun marked unavailable.

The legacy code runs first, unchanged. Failure/override leaves its sun arrays,
map colour scale and shadow-floor values intact. C01 never writes `tr.sunLight`,
`tr.sunDirection`, `tr.sunParsed`, a cvar or the canonical asset.

## Admission and quantities

`lightrecon/resolved_sun.h/.cpp` is the CPU renderer policy, with no GL/world
owner. `State` is a trivially copyable, detached value in `trRefdef_t`. The
renderer caches admission by publication generation and evaluates scene policy
once per scene. Main-world begin/unload and renderer reset clear cached values.

The state records policy, availability, failure reason, physical versus legacy
classification, rejected-candidate classification, source/provenance/calibration
IDs, confidence, optional angular radius, both direction signs and RGB normal
illuminance. Exposure and tone mapping are absent.

* Canonical directional `frame.forward` is photon travel. `Sun().directionToSun`
  is its negative. The scene state copies both values.
* Emitter RGB is linear Rec.709/D65, Y in lux at normal incidence. The adapter
  uses `Photometric::Emission`, requires `DirectionalIlluminance`, and requires
  `PhysicalSI`. `Rend2PBR::PhysicalToScene` applies the current fixed scene scale
  once. At scale 0.0001, 100000 lux gives scene illuminance 10.
* Relative values, calibrated legacy scene equivalents, approximate appearance
  fields and unknown colour or intensity select the legacy path with a typed
  diagnostic. Historical Q and encoded RGB remain in their original domains.
* Authored/external physical sources require explicit exact-candidate suitability,
  complete selected configuration/profile/geometry identity, supported photometry,
  no observation issues and global applicability.
* An unresolved alternative outside the selected hypothesis group prevents
  selection. A resolved source settles candidates in its mutually exclusive
  group. Candidate-only and appearance-only assets have separate diagnostics.
* Reconstructed sources additionally require a validated per-source ExactEmitter
  admission, complete supported uncertainty factors, known absolute scale and a
  selected-configuration witness. A Conditional aggregate sun assessment blocks
  use. Admission requires the source evidence in addition to the aggregate
  canonical assessment.
  Consumer-scoped empirical event `rend2/directional-sun:0.25deg-5pct` must have
  calibrated lower bin probability at least 0.95. Direction spread must be known
  and at most 0.25 degrees and intensity bounds must lie within +/-5 percent of
  the central value. A generic confidence score leaves the source ineligible.

The CPU suite tests the decision boundary with controlled metadata witnesses.

## Audited sources and consumers

| Boundary / file | Existing meaning | C01 behaviour |
| --- | --- | --- |
| `tr_shader.cpp` directives `sun`, `q3map_sun`, `q3gl2_sun` | Normalized authored RGB times legacy strength. Yaw and elevation give the direction toward the sun. `q3gl2_sun` can enable shadows. | Shader-authored fallback |
| `tr_bsp.cpp` main load | Defaults, shader state, baked calibration, fog split and entity calibration | Uses existing baked preparation. A default direction leaves the sun unavailable. |
| `tr_scene.cpp` | Legacy `sunCol` with overbright and gamma, mode 1 modulation, mode 2 direct light, animated and refdef overrides | Resolves once after all explicit overrides and before scene consumers and CSM. |
| `tr_main.cpp` CSM | Camera forward is negative `refdef.sunDir`, the photon travel direction. | Uses resolved scene direction and enables the existing CSM path for the canonical sun. |
| CSM static cache and disk pages | Exact MVP, light axes, bounds, texel scale, caster settings and world generation | Reuses existing keys for shadow geometry. |
| `tr_backend.cpp`, `lightall.glsl` | Primary sun colour from legacy calibration or explicit lux. Mode 1 shadows baked diffuse. Mode 2 adds direct sun. | Supplies scene illuminance. Mode 2 replaces the primary sun lobe. Mode 1 modulates. Additive diffuse requires P06 proof. |
| `tr_light.cpp` entities | Original grid, optional heuristic sun subtraction and bounded local sky probes | Uses the full grid and canonical scene direction in the physical sun lobe. Additive diffuse has its own P06 gate. |
| `R_CalibrateEntitySun`, `tr_bakedlighting.cpp` | Map-load authored-direction statistics and sun-grid split in legacy units | Original scene units and map-load calibration apply on the legacy path. |
| `tr_atmosphere.cpp` | Legacy white-Lambert proxy and ground versus TOA conversion. The shader multiplies by pi. | Uses scene E/pi, direction and known angular extent. The procedural disk requires a positive angular radius. |
| `tr_clouds.cpp` | Atmospheric ground sun proxy, sun direction and cloud attenuation | Uses the atmosphere adapter. Direction changes refresh tracked shadow maps and temporal keys. |
| `tr_volumetric.cpp`, `volumetric_inject.glsl` | Split original grid and empirical sun radiance. CSM replaces the baked sun proxy. HG uses 4*pi normalization. | Canonical selection uses the full original grid. Direct scattering requires P06 proof and current CSM coverage. E/(4*pi) matches phase normalization. |
| `tr_sky.cpp`, `tr_postprocess.cpp` | Sprite and ray visibility positioned from the global authored direction | Uses the canonical scene direction for the existing sprite and ray mask. |
| `tr_pom.cpp` | Scene sun direction for POM self-shadow | Reads the shared `sunDir`. |
| `tr_ao.cpp` | screen-space contact shadow ray direction | Uses the resolved scene direction and existing sunlight visibility flag |
| `watersurface.glsl` | directional glint, submerged scattering and foam | Uses scene irradiance directly for glint and normalized scattering, and E/pi for Lambert foam |
| `tr_pbr_contract.cpp` | physical PBR calibration diagnostics | Prints canonical lux and scene Y without applying legacy calibration |
| `tr_weather.cpp`, `tr_rainlens.cpp` | Legacy grid and weather appearance, scene sun proxy | Shares scene sun direction and colour. Rain lens recognizes canonical availability. |
| Cubemap and environment captures | Scene setup, saved view state and diffuse and specular probe transport | Copies the detached scene state. Active probe composition selects the legacy path. |

Atmosphere and clouds retain their existing optional artistic intensity,
ground/TOA and scattering controls. The immutable state always contains the
source illuminance before those controls. CSM/PCF remain the existing directional
shadow approximation. Finite-source penumbrae and sun imagery painted into
textured skyboxes are outside the supported lighting composition.

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
The baseline must exclude direct sun. Admission also requires a separate
fully supported, uncontradicted `ReplacementAllProofs` receipt must bind scope to
that domain and runtime operator `rend2/original-baked-linear/v1`, with known zero
absolute error and leakage risk. Unknown or multiple descriptors deny the channel.

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
shader-authored map without legacy CSM because sunlight mode 2 can read the old
shadow block when its current scene generates no CSM. Retaining canonical matrices
there caused a strict A/B/A failure (about 850 pixels, maximum 16 display codes)
in initial testing. The final adapter restores those historical values, without
clearing the immutable asset or invalidating an unchanged canonical cache.

The existing CSM classifier compares exact projection matrices. Direction changes
therefore invalidate full depth or scrolling eligibility. Identical shadow
geometry reuses the cache across brightness, source and publication changes.
Disk layout keys already include the
light axes, bounds, dimensions, texel sizes, world and caster settings. Mode 1
continues to bypass a continuously moving sun and admits a settled projection
after four observations. Existing map/caster generations protect reused world
storage. No canonical source or shadow slot is added to the asset.

Cloud shadow interval reuse checks the resolved direction during canonical
selection and refreshes on transition back to legacy. Forced and unavailable
scenes use the original reuse policy. Cloud/froxel temporal keys include canonical direction and colour
only while selected, so a live switch cannot retain the previous illumination.
The all-disabled cloud interval/history keys retain their old rules.

Worldless scenes use the legacy renderer policy. Scene refdef copies own the
detached value through skybox, mirrors, recursive views and queued backend work.
New scenes acquire the current world generation. SP and MP share this code.
`r_lightReconInfo` prints actual scene policy,
source, rejected-candidate class, failure reason and independent P06 channel
coverage in addition to the existing consumer capability gate.

## Validation and limits

### Reproduction tools

* `lightrecon_resolved_sun_test` covers the Snapshot, photometry and P06 boundary, signs,
  lux conversion, forced/refdef precedence, physical mode, missing/ambiguous sun,
  relative/legacy rejection, scoped evidence, duplicated membership, unload and
  snapshot lifetime. Fixtures are controlled authored inputs.
* `test_resolved_sun_cache.py` extracts and compiles the production CSM
  classifier and disk-layout key, with renderer storage stubbed. 1039 checks
  cover stable reuse, direction/projection/settings/world changes and animated
  mode 1 bypass and settling.
* `test_resolved_sun_gl.py` compiles 37 lighting/fog and two water programs, physical sunlight modes
  1/2, lightmap/vector/vertex, entity split and skin permutations. Froxel
  readbacks verify missing P06 and missing CSM coverage preserve the baseline.
* `test_resolved_sun_runtime.py` tests private SP/MP authored caches, fixed camera
  and exposure, A/A/B/B/restore, forced modes, cache load, `vid_restart`, unload
  and a map transition. The offline `lightrecon_sun_fixture_tool` reads an explicit
  dependency manifest and writes the specified private 100000 lux authored cache.

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
positive world surface and leaf counts, and A/A pixels before accepting
comparisons. The MP fixture hides dynamic entities and disables clouds to
remove animation from the control. The UI patch is outside the production renderer.

### Measured results

SP `hoth2` used the 100000 lux authored fixture at 640 by 360 and fixed exposure.
With CSM cache mode 2, A/A differed at two pixels and A/restore at three pixels,
each by one display code. B/B was bit-exact. A/B changed 227045 pixels, with a
mean absolute channel delta of 6.449 and a maximum of 20. The frame shows the
world, player model and fog. Atmosphere, cloud shadow, cloud composite and
froxel passes ran. The screenshot measures the combined result and includes
fog over the character.

MP `mp/duel4` used cache mode 1 and the same controlled source. A/A, B/B and
A/restore were bit-exact. A/B changed 165703 of 230400 pixels, with a mean
absolute channel delta of 8.438 and a maximum of 212. The main visible change
is the procedural sky through the arena window. The MP comparison isolated the
static world after initial controls showed frame-dependent saber effects.

SP and MP each passed 45 CSM depth comparisons. Forced modes 1 and 2 took
precedence, `vid_restart` reloaded the authored cache, and unload cleared the
publication. The MP transition to `mp/duel1` rendered the new world using its
shader sun, with no canonical source. The three additive P06 channels stayed
off in both fixtures, preserving the original baked diffuse and grid light.

Each capture sampled 30 GPU frames per state. SP median main-view time was
12.172 ms off and 11.395 ms on. Enabled dynamic cascade overlay medians were
0.200, 0.175 and 0.223 ms. MP median main-view time was 22.496 ms off and
23.108 ms on, with roughly 0.001 ms per cascade overlay in the static scene.
Settled MP frames logged three static cascade hits and zero full rebuilds per
frame. Shadow view front-end and submission were 0.011 and 0.033 ms per frame
in that MP sample. The SP sample reported 0.211 and 0.380 ms respectively.
These timings are specific to the two scenes and their capture settings.

On SP `hoth3` with dynamic entities hidden, the original/current disabled
comparison was bit-exact over 230400 pixels. A/A, independent gate-off,
missing-asset and restore also matched bit for bit inside both executables.
The initial cross-execution run with dynamic entities froze different player
poses and animated surfaces and differed by 25112 pixels.

MP `mp/duel4` with physical PBR also matched the original renderer bit for bit
at all 230400 pixels. The disabled comparison sets `fixedtime 16` on the private
MP server so animated world materials begin at the same phase across launches.
Its A/A, independent gate-off, missing-asset and restore controls matched exactly.
With physical PBR off, the original MP DLL had a recurring one-pixel difference
of four display codes within the control run. The strict tolerance rejected
that comparison before testing the C01 DLL.

The older frozen preprocessed GLSL regression baseline already differs on the
original renderer because it predates shader comparison controls and CSM
changes. C01 leaves that historical baseline intact. The current driver tests
compile the live permutations and the gameplay comparison uses the actual
pre-C01 renderer DLL.

The Release SP and MP renderer builds passed. The seven relevant CPU CTest
targets, 15 shader/control contract tests, 1039 cache checks and 39 driver
shader programs passed. GPU readback returned visibility 0 through the wall,
1 through the doorway and 0 outside shadow coverage. Missing membership and
coverage preserved the original froxel baseline.

Each scene stores one detached state and uses the existing sun and three CSM
cascades, with zero additional GPU light slots, textures, samplers or permutations.
Admission scans and photometric validation run on publication change. Per-scene
selection and copy use fixed-size values. The renderer reports shadow gather and
submit time for sampled frames.

The legacy path covers unsupported stock photometry, conditional and relative
sources, spatially scoped suns and unproven probe composition. Additive diffuse
requires a scoped exclusion proof for the original bake. CSM uses the existing
directional PCF approximation. Textured skyboxes retain their authored imagery.
