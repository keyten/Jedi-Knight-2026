# C04 entity and Ghoul2 irradiance

Set `r_lightReconBuild 1`, `r_lightReconEntities 1` and `r_lightReconUse 1`,
then restart the renderer and load the map. The first switch uses the existing
cache-only policy. The entity switch defaults to zero and is latched for the
model shader and the world-load selection. `r_lightReconUse 0` is the live
A/B switch. `r_lightReconInfo` shows the prepared capability and the last
view's query count, cache hits, root and body samples, fallbacks and CPU time.

## Existing renderer paths

| Receiver | Existing static and dynamic inputs |
| --- | --- |
| SP player and NPCs | `code/cgame/cg_players.cpp` submits model entities, including attached models, through the renderer scene API. `RF_LIGHTING_ORIGIN` can align lighting on multipart models. |
| MP player and NPCs | `codemp/cgame/cg_players.c` submits the player/NPC and attachments to the same renderer. SP and MP share `R_AddGhoulSurfaces` and the lightall shader. |
| Ghoul2 surface | The renderer builds the posed skeleton and uploads the existing Bones block. `R_SetupEntityLighting` supplies the ambient RGB, directed RGB and light direction. GPU skinning transforms the surface normal before the model-to-world transform. |
| Ordinary model | Uses the same origin-based light setup. Diffuse model materials can use lightall, including vertex-interpolated MD3 frames. Generic diffuse stages keep their vertex lighting. Fullbright and unlit materials keep their own shader behavior. |
| World and brush model | BSP materials use lightmaps, vertex lighting or their explicit shader stages. `R_AddBrushModelSurfaces` submits the brush geometry and marks its dynamic lights. C04 excludes these models. |

The legacy CPU sampler in `tr_light.cpp` interpolates the original styled
BSP grid and direction samples. It also handles HDR data, ambient and directed
scales, the LDR minimum-light flags and the legacy RGB transfer setting. The
backend places these values in the Entity block. Fast lightall applies
ambient plus the cosine-weighted directed term at vertices. Per-pixel
lightall evaluates the directed lobe with material diffuse/specular and adds
the ambient term. The generic and shadow paths also use the original
model-space light direction and legacy light values.

`r_entityLightGrid 1` supplies three legacy height samples.
`R_BuildLightGridTexture` and `R_UpdateEntityLightGridTextures` handle mode 2
and light styles. `R_BuildEntityLightProbes` builds the optional L1 volumes
from original BSP directions. That path recovers a dominant direction to
retain legacy specular. Grid debug and compare modes keep those original inputs.

`r_entitySunLight` can split the grid's directed lobe and shade its estimated
sun separately. Sunlight mode 1 modulates historical baked lighting with
shadows. Mode 2 has a separate primary light. Dynamic point, spot, saber and
projectile lighting are evaluated by the existing light-list or Forward+
passes. Shadow casting and character self-shadowing stay with those passes.

## Admission and energy accounting

| Input | C04 behavior |
| --- | --- |
| Physically authored, calibrated and uniquely selected reflected field | Accepts linear Rec.709 D65 normal irradiance in lux, at most L2 SH, with complete reflected-path membership and certified static receiver support. |
| Resolved reconstructed hypothesis | C04 has no evidence-based admission route for reconstructed sources. A resolved state or an API success supplies insufficient evidence on its own. |
| Conditional relative or scene-equivalent field | Uses the legacy model input. Relative amplitudes and legacy calibration mappings stay in their original domain. |
| Approximate legacy-appearance field | Uses the legacy model input. Encoded RGB stays in the appearance domain. |
| Unavailable or ambiguous | Uses the existing model path, including the selected legacy entity grid mode. |

The world-load cache adapter selects at most one qualifying immutable
`org.openjk.spatial-field` payload after the usual dependency fingerprint
check. It validates the bound BSP, geometry and source generation. C04 admits
the `physical-emission/1` profile with the primary authored configuration
(`configuration=0`). The source, calibration and roots must be authored, with
resolved source records and no alternatives or unresolved parameters. P06
membership must name the roots and positive reflection orders and explicitly
exclude zero-order local, sun, sky, legacy ambient and unresolved energy.
Reflected sky and sun roots are allowed. Direct sky, incoming radiance,
hybrid SG, specular reconstruction and exact local emitter evaluation are
outside this adapter.

An accepted model surface uses the field in place of the full legacy diffuse
grid term. Its synthetic grid specular lobe and legacy diffuse cubemap
modulation are bypassed. The cubemap/SSR specular path retains its original
lighting inputs. Explicit runtime lights continue to supply direct
illumination, including any source whose reflection is in the field. World
lightmaps and their historical direct energy are untouched. This replacement
is an authored entity look with a different energy source from the original
grid. It makes no claim of equivalence to the historical bake.

Existing independent analytic sun and grid-sun split terms keep their
visibility and shader behavior. A parsed sun with only a merged or
modulation-only legacy grid uses legacy entity lighting unless a split sun
term is available. If the canonical sun adapter selected a sun and blocked
its entity diffuse through P06, the entity also uses its legacy baseline.
No static visibility fraction is applied to a dynamic penumbra.

## Sampling, shader and limits

- The anchor is `lightingOrigin` when `RF_LIGHTING_ORIGIN` is set and the
  model origin otherwise. The prepared adaptive topology looks up a bounded
  stencil using convex free-space certificates. Unknown regions, conditional
  dynamic boundaries and missing probe values return fallback. There is no
  per-frame trace or prepared-cache I/O.
- Ghoul2 L0/L1 can sample below and above the anchor using an offset based on
  the renderer radius and scale, clamped to 4–20 world units. The two
  endpoints must share the anchor's certified cell and region. In that case
  fragments interpolate vertically between the three samples. Otherwise the
  anchor is used. L2 uses one anchor. Body parts that cross a wall or extend
  beyond the anchor stencil need finer per-part receiver placement. The
  prepared interpolation can change stencil or lose coverage at cell edges.
- The renderer packs either nine RGB coefficients or three sets of RGB L1
  coefficients into the existing nine Entity vec4s. Both fast model lighting
  and the normal-mapped path evaluate the signed world-space real basis
  independently for each channel. The shader clamps negative SH
  undershoot at evaluation. The animated shading normal supplies the
  direction. Nonuniform entity transforms use the inverse transpose on the
  opted-in draw.
- PBR uses `E / pi * r_pbrPhysicalToScene` with the material's indirect diffuse
  reflectance and existing material occlusion. Legacy uses a fixed
  `E / pi * 0.0001` in the diffuse multiplier. This is a display compatibility
  conversion, independent of the stock grid transfer. The SH is already
  cosine-convolved. It is not reused as radiance or as a directional BRDF
  light.
- The 256-entry cache occupies 45,056 bytes on the tested build. Entries key
  on the world publication generation, entity slot, exact anchor and
  sample spacing. Crossing a region or moving invalidates the reuse.
  Each miss performs one or at most three bounded stencil queries. C04 adds
  zero UBO bytes, texture units, uploads, emitter slots or draw calls. No
  consumer budget changes the canonical scene.

| Configuration | Behavior |
| --- | --- |
| SP and MP, PBR off/on | Shared shader and adapter. Model fast, per-pixel, vertex-animation and skeletal variants. |
| Forward+ and fog off/on | No dependency on either. Existing dynamic light evaluation stays active. |
| Grid modes 0, 1 and 2 | The original paths run with C04 off or on fallback. Grid debug views use their original data. |
| OpenGL model shaders | Reuses the existing std140 layout and requires no compute or 3D sampling. Unsupported field encodings use the configured legacy path. |
| Fullbright, goggles, worldless views, no-depth/volumetric effects | Legacy handling. |

## Validation

Six CPU suites passed, covering the canonical reflected field API, persistence,
controls, adaptive probes, a first-bounce Lambertian reference and the C04
fixture. The fixture preserved the constant 1,900 lux channel and measured
12,800 versus 3,200 lux on the two opposite red-facing normals, with a separate
12,800 lux blue-facing response. The solid wall sample fell back and no
opposite-room colour contributed beside the wall. All 17 doorway positions
were supported. The 500 lux/unit blue gradient had 281.15 lux RMS error and a
1,000 lux maximum step, so doorway coverage does not imply continuous
interpolation across cells. Three anchors in one certified cell passed.
Missing values and region coverage triggered fallback. The isolated moving
position test averaged 0.805 µs per uncached query.

MSVC Release SP and MP renderer builds passed. The Intel UHD OpenGL driver
compiled and linked eight C04 model permutations and their eight controls
with identical reflected GPU layouts. The 27 disabled shader programs and
binding contracts match the input revision `80301f662`. The older frozen
shader fixture also compiles, with historical Forward+ differences from its
recorded source. Its historical baseline was left in place.

The runtime stimulus is an independently specified reflected radiance
boundary condition with a known cosine integral, exported as a private
authored cache over the actual BSP geometry. The existing world is visible
in each accepted capture. All reported SP A/A, B/B and restore image
controls were pixel-identical at 640×360.

| Runtime map | Configuration | Pixels changed / max 8-bit channel change | CPU cost, warm sample cache | Main-view GPU median off → on |
| --- | --- | ---: | ---: | ---: |
| `lrq3_two_colour` | Coloured indoor, legacy fast | 6,855 / 63 | 0.40 µs | Unmeasured |
| `lrq3_door_occlusion` | Doorway, physical PBR and Forward+ | 7,154 / 119 | 1.30 µs | 3.619 → 3.631 ms |
| `lrq3_sun_sky` | Sunlit outdoor, PBR and separate primary sun | 6,859 / 103 | 0.50 µs | 5.806 → 5.800 ms |
| MP `mp/duel4` | Stock geometry with private authored field, PBR and Forward+ | 5,711 / 61 | 0.80–1.60 µs | 26.090 → 25.639 ms |

The SP views used two cached root samples each. MP used one. Forward
batches matched across the controls (40, 38, 40 and 97 respectively).
Prepared payload sizes were 14,232,752, 5,017,760, 605,312 and 5,354,936 bytes
respectively, excluding cache metadata and decoder scratch memory. Four
Forward+ test lights changed both legacy and C04 captures. With the lights
present the field changed 7,047 pixels at the SP doorway and 5,665 pixels in MP.
The GPU numbers are total main-view times with 17–18 observations each
and include ordinary timing noise. C04 has no separate draw timer.

The legacy SP and MP cache reload and unload tests passed. The SP stock
`hoth3` empty-cache control produced pixel-identical A/B images with C04
requested and zero field queries. Modern SP reloaded
the field after `vid_restart`, but the first automated disconnect runs stalled
after leaving gameplay. The separate SP capture runs completed. The first MP
capture attempts had unstable saber FX. The accepted MP run used a pistol
loadout with a clear third-person camera. It passed pixel-identical A/A, B/B
and restore controls with the actual Ghoul2 player and visible world geometry.

Captures, console logs and numeric comparisons are in the local build
directories `c04-sp-colour-v2`, `c04-sp-door-v3`, `c04-sp-sun-v3` and
`c04-mp-duel4-v4`. Each has an `a.png` legacy image, `b.png` authored image
and `result.json`. The additional `c04-sp-colour-turn` run checked a
different model orientation, with 7,166 changed pixels and exact A/A controls.
The SP doorway motion sequence shows a spawned stormtrooper settling at the
doorway. Four observed moving frames performed three queries plus one cache
hit each and spent 19.8–37.1 µs on the adapter. A full NPC traverse across
the doorway and a visual three-anchor comparison remain unverified.

This validation exercises an authored diffuse field. The existing stock
validation found zero accepted nonempty stock physical models and no prepared
original-stock fields among the eight maps tested. The MP stock BSP is a
geometry input for the private authored stimulus. Stock Q, historical RGB and
uncalibrated candidates do not acquire physical units or eligibility through C04.
