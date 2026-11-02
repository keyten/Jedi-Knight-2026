# Rain, wet surfaces and lens water

[All rendering features](../rendering-features.md)

Controls marked `vid_restart` need a renderer restart. Debug views use 0 to return to normal unless stated otherwise. Performance labels are rough relative costs, not benchmark guarantees.

## Weather visibility optimization

Avoids drawing distant or hidden weather chunks that cannot contribute to the view. The rain should look the same; this targets wasted rendering work.

**Main controls**

- `r_weatherCull 0 | 1` — draw every weather chunk / cull invisible chunks.

**Debug**

- `r_weatherDebugChunks 1 | 2` — visualize chunk bounds; mode 2 also prints detailed bounds.

Weather geometry is grouped into bounded chunks and rejected when those bounds are outside the visible region, rather than submitting all chunks each frame.

## Rain streaks

*Requires active rain, because this changes existing precipitation rather than creating weather.*

Makes rain streaks respond more naturally to view direction, distance and nearby lights. It improves the original bright-line rain without changing the map assets. Light to medium performance cost in rain.

**Main controls**

- `r_rainStreaks 0 | 1` — legacy / modern rain streaks.
- `r_rainStreakWidth <value>` — streak width.
- `r_rainStreakLength <value>` — length multiplier.
- `r_rainStreakOpacity <value>` — opacity.
- `r_rainStreakLighting 0 | 1` — fixed brightness / light-grid + sun lighting.
- `r_rainDynamicLighting 0 | 1 | 2` — 0 existing baked/sun light, 1 strongest nearby light, 2 limited nearby Forward+ lights. Also lights impact splashes.
- `r_rainDynamicMaxLights 1..8` — Accepted lights per drop/splash in mode 2, 1..8; default 4.

**Debug**

- `r_rainStreakDebug 1..5` — coverage, distance, lighting, contact and variation views.
- `r_rainDynamicDebug 1 | 2 | 3` — show the static/grid contribution, dynamic contribution, or normalized selected-light count.

The renderer shades stretched raindrop geometry with view-aware shape and lighting, using the weather-depth map to keep rain under roofs from showing.

## Distant rain

*Requires active rain haze, because distant streaks fade into the rain medium instead of disappearing.*

Keeps individual drops nearby while distant rain becomes a continuous haze. This reduces the wall of tiny lines and the work of drawing them far away.

**Main controls**

- `r_rainLOD 0 | 1` — master switch; default `1`.
- `r_rainLODNear <units>` — full-density radius; default `1200`.
- `r_rainLODMid <units>` — end of the streak/volume handoff; default `3600`.
- `r_rainLODDensityFloor 0.05..1` — lowest stochastic density in the middle band; default `0.25`.
- `r_rainLODFarVolumeCoupling 0..1` — `0` keeps the original all-distance medium, `1` fades the medium in across the LOD handoff.
- `r_speeds 100` — prints the current LOD/fallback state and the existing weather submission counts.

**Debug**

- `r_rainLODDebug 0 | 1` — show green near, yellow-to-blue middle and blue far bands. Debug deliberately restores far streak markers.

The near band keeps streaks; the middle band thins and fades them as the rain volume takes over. If that volume is unavailable, the renderer keeps the ordinary streaks.

## Wet surfaces

*Requires active rain, because the response is driven by rain exposure.*

Darkens exposed surfaces and makes suitable armor, metal and stone smoother and more reflective. Roofed areas stay drier; fabric mainly darkens instead of turning into a mirror. Light performance cost.

**Main controls**

- `r_weatherWetness 0 | 1` — disable / enable wet materials. Requires `vid_restart`.
- `r_weatherWetnessStrength 0..1` — overall wetness.
- `r_weatherWetnessRoughness <value>` — scale of class-specific smoothing.
- `r_weatherWetnessDarkening <value>` — scale of class-specific albedo darkening.
- `r_weatherWetnessNormal <value>` — scale of class-specific normal flattening.
- `r_weatherWetnessEntityFacing <value>` — wetness of vertical character/prop faces.
- `r_weatherWetnessBias <units>` — rain-occlusion depth bias.

**Debug**

- `r_weatherSurfaceDebug 1..4` — exposure, wetness, roughness and dry/wet split.
- `r_weatherSurfaceDebug 16` — detected wet material class.
- `r_weatherSurfaceDebug 21` — geometric normal.

A weather-depth map estimates exposure. Material class determines how much wetness changes color, roughness and fine surface normals.

## Wetness after rain

*Requires `r_weatherWetness 1` and restart, because this allocates a field that remembers surface water.*

Keeps surfaces wet after the rain stops and lets them dry gradually. The original immediate rain response cannot retain this history. Light additional performance cost.

**Main controls**

- `r_weatherWetnessPersistence` — Persist world wetness after rain stops (requires r_weatherWetness). Default `0`; `vid_restart`.
- `r_weatherWetnessAccumulationRate` — Persistent world water deposited per second at full rain. Default `0.20`.
- `r_weatherWetnessAcidAccumulationScale` — Acid-rain persistent wetness accumulation multiplier. Default `1`.
- `r_weatherWetnessAcidDryScale` — Acid-rain persistent wetness drying multiplier. Default `1`.
- `r_weatherWetnessDryRate` — Persistent world water removed per second (0.02 = about 50 seconds). Default `0.02`.
- `r_weatherWetnessResolution` — Persistent world wetness texture resolution (64-1024). Default `256`; `vid_restart`.
- `r_weatherWetnessSteepDryScale` — Drying multiplier for steep surfaces (selected from geometric normal). Default `2`.
- `r_weatherWetnessUpdateHz` — Persistent world wetness simulation updates per second. Default `10`.

**Debug**

- `r_weatherWetnessPersistenceDebug` — 1 shows persistent world water amount. Default `0`.

A bounded world-space texture accumulates water from rain and removes it over time. Steep surfaces and acid rain can use different rates. [Details](../rend2-weather-wetness-persistence.md).

## Acid rain

*Requires active acid-rain weather, because this changes its optical profile rather than creating rain or changing damage.*

Gives acid rain a consistent tint across streaks, haze, lens water and wet surfaces instead of several unrelated colors. Light additional performance cost.

**Main controls**

- `r_acidRainStrength` — Strength of the coherent acid-rain optical profile. Default `1`.
- `r_acidRainTint` — Linear RGB optical tint of acid rain. Default `0.78 1.00 0.62`.
- `r_acidRainVisuals` — Coherent optical acid-rain profile for rain, wet surfaces, puddles, runoff, volume and lens water. Default `0`.

**Debug**

- `r_precipitationInfo` — Print resolved precipitation type, intensity and consumer state.

Rain consumers read one resolved precipitation state. The optional acid profile applies a shared tint and strength to that state’s visual responses. [Details](../rend2-acid-rain-profile.md).

## [For mod authors] Weather response overrides

*Requires the corresponding wetness, puddle or runoff feature, because the material only scales an already enabled response.*

Lets a roof get wet without collecting puddles, a fabric stay free of standing water, or a special indoor shader stay dry. Stock materials keep their automatic response until an override is supplied.

**Main controls**

- No separate switch; use the feature or asset settings described here.

**Debug**

- `r_weatherSurfaceDebug 27..31` — enabled/excluded state, wetness/puddle/runoff scales and exclusion reason.
- `r_weatherMaterialList` — print drawn shaders with non-default response or automatic exclusion for one frame.

Use `weatherResponse <wetness> [<puddle> [<runoff>]]` in the shader or a stage:

~~~text
weatherResponse 1.3 1 0.3
~~~

Values range from 0 to 4; 1 keeps the normal response. A single `weatherResponse 0` disables all three channels.

The shader multiplies the automatic wetness, puddle and runoff masks by the authored scales. This changes shading, not rainfall placement, and adds no meaningful extra cost. [Details](../rend2-weather-material.md).

## Puddles

*Requires `r_weatherWetness 1` and rain, because standing water is placed on exposed wet surfaces.*

Adds glossy puddles to flat stock floors and ground without rebuilding the map. With a material height map, low cracks can fill before raised parts. Light additional performance cost.

**Main controls**

- `r_weatherPuddles 0 | 1` — disable / enable procedural puddles.
- `r_weatherPuddleUseHeightMap 0 | 1` — ignore / use material height data.
- `r_weatherPuddleCoverage 0..1` — approximate covered fraction.
- `r_weatherPuddleRoughness <value>` — water roughness.
- `r_weatherPuddleSlope "min max"` — geometric-normal range where puddles can form.
- `r_weatherPuddleScale <units>` — macro puddle pattern size.
- `r_weatherPuddleEdgeModel 0 | 1` — legacy response / continuous dry, damp fringe, thin film and standing-water response. Default `0` preserves the existing puddle shader.
- `r_weatherPuddleFringeDarkening 0..1` — additional albedo darkening at the damp fringe (default `0.18`).
- `r_weatherPuddleFilmRoughness 0.02..1` — target roughness of the thin film (default `0.18`); standing water continues to use `r_weatherPuddleRoughness`.
- `r_weatherPuddleHeightSoftness <value>` — water-line transition width.
- `r_weatherPuddleWaterLevelBias <value>` — bias the virtual water level.

**Debug**

- `r_weatherSurfaceDebug 5..10` — slope, noise, exposure, mask, roughness and eligibility.
- `r_weatherSurfaceDebug 13..15` — macro, micro and combined puddle masks.
- `r_weatherSurfaceDebug 33` — four continuous region weights: dry grey, damp brown, thin film cyan, standing water blue.
- `r_weatherSurfaceDebug 34..36` — final roughness, added puddle darkening and final normal for the layered edge model.
- `r_weatherSurfaceDebug 11..12` — material height and low/high regions.
- `r_weatherSurfaceDebug 15` — final combined puddle mask.

Surface slope, exposure and world-space noise choose puddle patches. The optional height test follows existing texture relief; layered edges blend dry ground, damp fringes, thin film and standing water.

## Rain rings in puddles

*Requires wet puddles and active rain, because the ripples perturb the normal of standing water.*

Adds spreading rings that move reflected lights and scenery. Mode 1 is a repeating procedural pattern; modes 2 and 3 can use actual rain impacts. Light to medium additional performance cost.

**Main controls**

- `r_weatherPuddleRipples 0 | 1 | 2 | 3` — off / procedural rings / real rain-impact field / real nearby impacts with procedural rings farther away.
- `r_weatherPuddleRippleStrength <value>` — ring normal strength.
- `r_weatherPuddleRippleScale <units>` — cell/ring scale.
- `r_weatherPuddleRippleRate <value>` — ring cycles per second.
- `r_weatherPuddleRippleDamping` — Real puddle ripple damping per second. Default `2.5`.
- `r_weatherPuddleRippleDistance` — Real ripple draw distance; hybrid fades to procedural beyond it. Default `1500`.
- `r_weatherPuddleRippleFieldScale` — World coverage of the real ripple field relative to the weather map. Default `1`.
- `r_weatherPuddleRippleImpulse` — Velocity impulse deposited by each real rain impact. Default `0.45`.
- `r_weatherPuddleRipplePropagation` — Real puddle ripple propagation in field texture cells per second. Default `10`.
- `r_weatherPuddleRippleResolution` — Real puddle ripple field resolution (64..1024; vid_restart). Default `256`; `vid_restart`.

**Debug**

- `r_weatherSurfaceDebug 17..20` — ripple height, normal offset, masking and final normal.
- `r_weatherPuddleRippleDebug` — Real ripple debug: 1 impulses, 2 height, 3 velocity, 4 final normal. Default `0`.

Real-impact modes feed a small height/velocity field. Hybrid mode uses that field nearby and procedural rings farther away; it does not simulate puddle drainage. [Details](../rend2-puddle-ripples.md).

## Water running down walls

*Requires `r_weatherWetness 1` and rain; retained trails also need persistent wetness, because they store where water has passed.*

Adds thin moving water streaks on exposed slopes and walls. Trails can remain dark and smooth briefly after the flow passes. Light performance cost.

**Main controls**

- `r_weatherRunoff 0 | 1` — disable / enable runoff.
- `r_weatherRunoffStrength <value>` — film/streak strength.
- `r_weatherRunoffSpeed <units/s>` — flow speed.
- `r_weatherRunoffScale <units>` — streak-cell size.
- `r_weatherRunoffProbe <value>` — small outward rain-exposure probe for steep walls; `0` disables it.
- `r_weatherRunoffEntities 0 | 1` — also apply runoff to characters, props and movers.
- `r_weatherRunoffTrailPersistence` — Seconds world runoff trails remain dark and smooth after water passes (0 off; needs persistent wetness). Default `0`.

**Debug**

- `r_weatherSurfaceDebug 22..26` — slope class, projected gravity, runoff mask, animated flow and final roughness.
- `r_weatherRunoffTrailDebug` — Persistent world runoff trail debug: 1 retained history, 2 final trail mask. Default `0`.

Gravity projected onto the surface chooses the flow direction. Animated film changes roughness and color; optional history retains its trail without a full fluid simulation. [Details](../rend2-weather-runoff.md).

## Rain splashes

*Requires active rain and the weather-depth map, because splashes begin where falling drops cross a surface.*

Adds small spray or rings where rain hits the world. Optional material response gives metal, soil, vegetation, puddles and real water different splash strengths. Light normally, medium in very heavy rain.

**Main controls**

- `r_rainSplashes 0 | 1` — disable / enable impact splashes.
- `r_rainSplashSize <units>` — splash radius.
- `r_rainSplashLifetime <ms>` — lifetime.
- `r_rainSplashOpacity <value>` — opacity.
- `r_rainSplashHardDryStrength` — Hard dry rain impact response scale. Default `1`.
- `r_rainSplashHardWetStrength` — Hard wet rain impact response scale. Default `1`.
- `r_rainSplashMetalStrength` — Metal rain impact response scale. Default `1`.
- `r_rainSplashPuddleStrength` — Puddle rain impact response scale. Default `1`.
- `r_rainSplashSoilStrength` — Soft soil rain impact response scale. Default `1`.
- `r_rainSplashSurfaceResponse` — Surface-dependent rain impacts: 0 exact legacy splash, 1 class response (vid_restart). Default `0`; `vid_restart`.
- `r_rainSplashVegetationStrength` — Vegetation rain impact response scale. Default `1`.
- `r_rainSplashWaterStrength` — Real water rain impact response scale. Default `1`.

**Debug**

- `r_rainSplashDebug 1..3` — impact points, accepted/rejected crossings and recent trajectories.
- `r_rainSplashSurfaceDebug` — Show the top-down R8 weather impact class map. Default `0`.

GPU rain trajectories are checked against weather depth instead of tracing every drop on the CPU. An impact-class map selects the response. [Details](../rend2-rain-splashes.md).

## Water on the camera lens

*Requires `r_hdr 1` and the world view, because droplets distort and blur the scene image; new rain needs an exposed camera.*

Adds beads, sliding drops and wet films to the view in rain or when leaving water. Under cover, remaining water keeps moving and dries instead of vanishing immediately. Light when wet, higher at the largest quality setting; a dry inactive pass costs nothing.

**Main controls**

- `r_rainLens 0 | 1` — disable / enable lens water. Requires `vid_restart`.
- `r_rainLensQuality 0..2` — field resolution, drop count and defocus samples. Requires `vid_restart`.
- `r_rainLensFilmModel` — 0 persistent beads and drops, 1 draining film with short-lived tiny drops. Default `0`.
- `r_rainLensDensity 0..2` — rain input multiplier (default 1).
- `r_rainLensRefraction 0..4` — refraction strength.
- `r_rainLensDropSize 0.25..4` — drop size (geometry only).
- `r_rainLensFilm 0..2` — thin film / trail visibility.
- `r_rainLensBlur 0..2` — drop defocus.
- `r_rainLensReflection 0..2` — reflection of the environment (nearest cubemap) with Fresnel, and glints of the sun and nearby lights; 0 = refraction only.
- `r_rainLensInertia 0..2` — water lags behind strong camera acceleration (default 0).
- `r_rainLensEmergeTime` — How long lens water drains after leaving a pool, 0.25..4. Default `1.0`.
- `r_rainLensEmergeWater` — Amount of lens water after leaving a pool, 0.25..3. Default `1.0`.
- `r_rainLensFilmMicro` — Fine distortion in film-first mode, 0..4. Default `1.0`.
- `r_rainLensImpactSplat` — Directional impact streaks/spray in film-first mode, 0..2; 0 disables them. Default `0`.
- `r_rainLensMist` — Peripheral mist reaching the lens in film-first mode, 0..1. Default `0.04`.

**Debug**

- `r_rainLensDebug 1..9` — optical weight / blur, normal, UV offset, scene/final split, agents, film/wetness, pinning, transient state and controller panel; forces the pass on.
- `rainlens_event splash|spray|emerge`, `rainlens_profile light|rain|heavy|acid|auto`, `rainlens_clear`, `rainlens_stats`.
- `r_rainLensFieldHeight`, `r_rainLensAgentLimit` — debug field/drop budgets (cheat).
- `r_rainLensPinning`, `r_rainLensMerge` — debug drop sticking and merging (cheat).
- `r_rainLensFilmDecay`, `r_rainLensWetDecay` — debug drying rates (cheat).
- `r_rainLensHeavyFlow`, `r_rainLensPeripheralBias` — debug heavy flow and edge placement (cheat).
- `r_rainLensPBO`, `r_rainLensMipBlur` — compare field upload and blur performance.

### [Autodetectable] Rain and leaving water

Enable `r_rainLens` and restart. Existing weather supplies rain; updated game/client modules submit water-entry/exit and nearby splash events. `r_rainLensFilmModel 1` selects the film-first look.

### [For mod authors] Waterfall spray and scripted splashes

Add `LensWaterEmitters` to `cubemaps/<map>/env.json` or use an EFX `lensWater` primitive for a local spray source. Scripts can submit `r_we lenswater splash 1`. These let authored effects wet the lens even outside normal rain.

A bounded droplet simulation builds a small optical field. The final image samples that field for distortion, blur and reflections; the film-first model emphasizes draining water rather than long-lived large beads. [Details](../rend2-rain-lens.md).
