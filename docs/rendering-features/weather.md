# Rain, wet surfaces and lens water

[All rendering settings](../rendering-features.md)

Better-looking rain, surfaces that get wet, puddles, splashes and drops on the camera. Everything here only shows while it rains. Rain comes from the map; to try it anywhere use `r_we rain` (or `lightrain`, `heavyrain`, `acidrain`) and `r_we clear` to stop.

Most of these features know where rain can't reach: the renderer keeps a top-down depth map of the level (the weather map), so roofs, overhangs and interiors stay dry.

## Rain streaks

Original rain is thin bright lines that look the same in a lit street and a dark alley. Modern streaks are shaped by fall speed and wind, lit by the sun and the map's light, fade naturally up close and in the distance, and stop at roofs.

**Cost:** light to medium in rain.

- `r_rainStreaks 0` — 1 modern streaks.
- `r_rainStreakOpacity 1`, `r_rainStreakWidth 1`, `r_rainStreakLength 1` — opacity and size multipliers.
- `r_rainStreakLighting 1` — 0 fixed brightness, as originally; 1 lit by the sun and the map.
- `r_rainDynamicLighting 1` — dynamic lights (sabers, blasters) on drops and splashes: **0** none, **1** the strongest nearby light, **2** several nearby lights.
- `r_rainDynamicMaxLights 4` — lights per drop in mode 2, 1–8.

**Debug**

- `r_rainStreakDebug 1..5` — coverage, distance fade, lighting, roof contact, variation.
- `r_rainDynamicDebug 1..3` — static light only, dynamic light only, number of lights used.

## Rain haze

Heavy rain hides the distance: courtyards fade into grey, and lamps glow through the downpour. This adds that haze where rain falls; covered interiors stay clear instead of getting a screen-wide grey tint. Needs `r_volumetricFog 2`.

**Cost:** light.

- `r_rainVolume 0` — 1 on.
- `r_rainVolumeStrength 0.00018` — haze density in normal rain.
- `r_rainVolumeHeavyMultiplier 4` — extra density in heavy rain.
- `r_rainVolumeGroundStrength 0` — splash mist close to the ground in heavy rain; 0 off.
- `r_rainVolumeGroundHeight 96` — height of that mist layer.
- `r_rainVolumeMaxDistance 6000` — how far the haze reaches.
- `r_rainVolumeAnisotropy 0.72` — how strongly rain glows when you look toward a light.

**Debug**

- `r_volumetricFogDebug 71..74` — rain density, weather exposure, unshadowed light, final rain haze.

## Distant rain

Nearby rain is drawn as individual drops; farther away the drops thin out and the [rain haze](#rain-haze) takes over. This removes the wall of tiny distant lines and the cost of drawing them. On by default; without rain haze, the ordinary streaks stay.

- `r_rainLOD 1` — 0 off.
- `r_rainLODNear 1200` — full drop density up to this distance.
- `r_rainLODMid 3600` — drops are gone beyond this distance.
- `r_rainLODDensityFloor 0.25` — lowest drop density between the two.
- `r_rainLODFarVolumeCoupling 1` — 1 rain haze fades in as drops fade out; 0 haze at every distance (the earlier behaviour).

**Debug**

- `r_rainLODDebug 1` — distance bands: green near, yellow middle, blue far.
- `r_speeds 100` — print the current rain distance state and weather counts.

## Wet surfaces

Rain-exposed surfaces darken and become glossy: armor and metal gain sharp reflections, stone darkens, fabric mostly just darkens. Areas under roofs stay dry. Each surface reacts according to its [material class](materials.md#automatic-materials-from-stock-textures-auto-pbr), so it works best with `r_autoPBR 2`.

**Cost:** light.

- `r_weatherWetness 0` *(restart)* — 1 on.
- `r_weatherWetnessStrength 1` — overall wetness, 0–1.
- `r_weatherWetnessEntityFacing 0.85` — wetness of the vertical sides of characters and props.

**How it works.** The weather map tells how exposed each point is; the material class decides how much wetness darkens it, smooths it and flattens fine bumps.

- `r_weatherWetnessRoughness 1`, `r_weatherWetnessDarkening 1`, `r_weatherWetnessNormal 1` — scale the per-class smoothing, darkening and bump flattening.
- `r_weatherWetnessBias 2` — tolerance of the roof test; raise it if exposed edges stay dry.

**Debug**

- `r_weatherSurfaceDebug 1..4` — exposure, wetness, roughness, dry/wet split.
- `r_weatherSurfaceDebug 16`, `21` — detected material class, surface normal.

Design notes: [rend2-weather-material.md](../rend2-weather-material.md).

## Wet after the rain

Surfaces get wet gradually when rain starts and dry slowly after it stops, instead of switching instantly. Steep surfaces dry faster. Needs `r_weatherWetness 1`.

**Cost:** light.

- `r_weatherWetnessPersistence 0` *(restart)* — 1 on.
- `r_weatherWetnessAccumulationRate 0.20` — how fast surfaces get wet.
- `r_weatherWetnessDryRate 0.02` — how fast they dry; 0.02 is about 50 seconds.
- `r_weatherWetnessSteepDryScale 2` — steep surfaces dry this much faster.
- `r_weatherWetnessAcidAccumulationScale 1`, `r_weatherWetnessAcidDryScale 1` — the same rates in acid rain.

**How it works.** A world-space texture over the level stores how much water each spot holds; rain adds to it and time removes it.

- `r_weatherWetnessResolution 256` *(restart)* — resolution of that texture (64–1024).
- `r_weatherWetnessUpdateHz 10` — updates per second.

**Debug**

- `r_weatherWetnessPersistenceDebug 1` — stored water.

Design notes: [rend2-weather-wetness-persistence.md](../rend2-weather-wetness-persistence.md).

## Puddles

Glossy puddles form on flat exposed floors and ground, with no map changes. On textures with a height map (texture packs), water fills cracks and low spots first. Needs `r_weatherWetness 1`.

**Cost:** light.

- `r_weatherPuddles 0` — 1 on.
- `r_weatherPuddleCoverage 0.35` — share of flat exposed ground covered.
- `r_weatherPuddleScale 192` — size of the puddle pattern.
- `r_weatherPuddleEdgeModel 0` — 1: realistic edges, going from dry ground through a dark damp fringe and a thin film to standing water.
- `r_weatherPuddleUseHeightMap 1` — follow texture height maps where available.

**How it works.** A world-space noise pattern, limited to flat (`r_weatherPuddleSlope`) and rain-exposed surfaces, decides where water stands. With a height map, a virtual water level fills the relief from the bottom.

- `r_weatherPuddleRoughness 0.04` — smoothness of standing water.
- `r_weatherPuddleSlope "0.90 0.98"` — how flat a surface must be (surface normal Z where puddles begin and reach full size).
- `r_weatherPuddleHeightSoftness 0.08` — sharpness of the water line in relief.
- `r_weatherPuddleWaterLevelBias 0` — above 0 floods more of the relief, below 0 only the deepest cracks.
- `r_weatherPuddleFringeDarkening 0.18`, `r_weatherPuddleFilmRoughness 0.18` — edge model: darkening of the damp fringe, smoothness of the thin film.

**Debug**

- `r_weatherSurfaceDebug 5..15` — slope, noise, exposure, mask, roughness, eligibility, height, low/high areas, pattern layers, final mask.
- `r_weatherSurfaceDebug 33..36` — edge model: zones (grey dry, brown damp, cyan film, blue water), roughness, darkening, normal.

## Rain rings in puddles

Raindrops make spreading rings in puddles that distort reflections. On by default once puddles are on.

**Cost:** light to medium.

- `r_weatherPuddleRipples 1` — **0** off. **1** a repeating pattern of rings. **2** rings where drops actually land. **3** real rings nearby, pattern farther away.
- `r_weatherPuddleRippleStrength 0.35` — how much rings bend reflections.
- `r_weatherPuddleRippleScale 20` — ring size.
- `r_weatherPuddleRippleRate 1.1` — rings per second (mode 1).
- `r_weatherPuddleRippleDistance 1500` — modes 2–3: how far real rings are drawn.

**How it works.** In modes 2 and 3 every rain impact pushes a small water-height field, which spreads and settles like waves.

- `r_weatherPuddleRippleImpulse 0.45` — push per drop.
- `r_weatherPuddleRipplePropagation 10` — wave speed.
- `r_weatherPuddleRippleDamping 2.5` — how fast waves die out.
- `r_weatherPuddleRippleFieldScale 1` — area covered by the field.
- `r_weatherPuddleRippleResolution 256` *(restart)* — field resolution (64–1024).

**Debug**

- `r_weatherSurfaceDebug 17..20` — ring height, normal offset, masking, final normal.
- `r_weatherPuddleRippleDebug 1..4` — impacts, height, velocity, final normal.

Design notes: [rend2-puddle-ripples.md](../rend2-puddle-ripples.md).

## Water running down walls

Thin streams of water run down exposed slopes and walls in the direction of gravity. Needs `r_weatherWetness 1`.

**Cost:** light.

- `r_weatherRunoff 0` — 1 on.
- `r_weatherRunoffStrength 1` — visibility of the streaks.
- `r_weatherRunoffSpeed 24` — flow speed.
- `r_weatherRunoffScale 48` — streak size.
- `r_weatherRunoffEntities 0` — 1: also on characters, props and moving objects.
- `r_weatherRunoffTrailPersistence 0` — seconds that trails stay dark and glossy after the water passes; needs [wet after the rain](#wet-after-the-rain).

**How it works.** Gravity projected onto each surface gives the flow direction; an animated pattern of thin streaks moves along it and changes color and gloss. Walls check for rain a little in front of them.

- `r_weatherRunoffProbe 1.5` — that distance; 0 off.

**Debug**

- `r_weatherSurfaceDebug 22..26` — slope class, gravity direction, mask, flow, roughness.
- `r_weatherRunoffTrailDebug 1..2` — stored trails, final trail mask.

Design notes: [rend2-weather-runoff.md](../rend2-weather-runoff.md).

## Rain splashes

Small splashes where drops hit the ground, roofs and puddles.

**Cost:** light; medium in very heavy rain.

- `r_rainSplashes 0` — 1 on.
- `r_rainSplashSize 6`, `r_rainSplashLifetime 350`, `r_rainSplashOpacity 0.5` — radius, lifetime in ms, opacity.
- `r_rainSplashSurfaceResponse 0` *(restart)* — 1: splashes depend on the surface: metal, soil, plants, puddles and real water each react differently.
- `r_rainSplashHardDryStrength 1`, `r_rainSplashHardWetStrength 1`, `r_rainSplashMetalStrength 1`, `r_rainSplashSoilStrength 1`, `r_rainSplashVegetationStrength 1`, `r_rainSplashPuddleStrength 1`, `r_rainSplashWaterStrength 1` — splash strength per surface type.

**How it works.** The GPU follows the falling drops and detects where they cross the weather map's surface; each crossing spawns a short-lived splash.

**Debug**

- `r_rainSplashDebug 1..3` — impact points, crossing test, drop paths.
- `r_rainSplashSurfaceDebug 1` — surface-type map.

Design notes: [rend2-rain-splashes.md](../rend2-rain-splashes.md).

## Water on the camera lens

Beads, running drops and a wet film on the screen when you stand in rain or come out of water. Under cover the water keeps running down and dries instead of vanishing. Needs `r_hdr 1`.

**Cost:** light while wet; nothing when dry.

- `r_rainLens 0` *(restart)* — 1 on.
- `r_rainLensQuality 1` *(restart)* — 0–2: resolution, number of drops, blur quality.
- `r_rainLensFilmModel 0` — **0** long-lived beads and drops. **1** mostly a draining film with short-lived small drops.
- `r_rainLensDensity 1.0` — how much rain reaches the lens.
- `r_rainLensDropSize 1.0` — drop size (not the amount of water).
- `r_rainLensRefraction 1.0` — how strongly drops bend the image.
- `r_rainLensBlur 1.0` — drop blur.
- `r_rainLensFilm 1.0` — visibility of the film and trails.
- `r_rainLensReflection 1.0` — reflections and glints of the sun and lights in drops; 0 bending only.
- `r_rainLensInertia 0` — water lags behind sharp camera movement.
- `r_rainLensPeripheralBias 1.0` — keep big drops and flows away from the screen center.

Film model (`r_rainLensFilmModel 1`):

- `r_rainLensEmergeTime 1.0`, `r_rainLensEmergeWater 1.0` — how long the lens drains and how much water stays after leaving water.
- `r_rainLensFilmMicro 1.0` — fine distortion in the film.
- `r_rainLensImpactSplat 0` — slanted drops splat and smear along their direction; 0 off.
- `r_rainLensMist 0.04` — mist reaching the lens when not facing the rain.

**Mod authors:** add `LensWaterEmitters` to `cubemaps/<map>/env.json` or an EFX `lensWater` primitive to wet the lens near a waterfall or spray. Scripts can call `r_we lenswater splash 1`.

**How it works.** A small simulation of drops and film produces a distortion map, and the final image is bent, blurred and given reflections through it.

**Debug**

- `r_rainLensDebug 1..9` — blur weight, normal, offset, before/after split, drops, film, sticking, transient state, control panel. Forces the effect on.
- `rainlens_event splash|spray|emerge`, `rainlens_profile light|rain|heavy|acid|auto`, `rainlens_clear`, `rainlens_stats` — test commands.
- `r_rainLensFieldHeight`, `r_rainLensAgentLimit`, `r_rainLensPinning`, `r_rainLensMerge`, `r_rainLensFilmDecay`, `r_rainLensWetDecay`, `r_rainLensHeavyFlow` — tuning of resolution, drop budget, sticking, merging, drying and flows.
- `r_rainLensPBO`, `r_rainLensMipBlur` — compare upload and blur methods for profiling.

Design notes: [rend2-rain-lens.md](../rend2-rain-lens.md).

## Acid rain

Gives acid rain one consistent sickly tint across streaks, haze, puddles, runoff, wet surfaces and lens water, instead of each using its own color. Only affects looks, not damage.

**Cost:** light.

- `r_acidRainVisuals 0` — 1 on.
- `r_acidRainTint "0.78 1.00 0.62"` — the tint.
- `r_acidRainStrength 1` — its strength.

**Debug**

- `r_precipitationInfo` — print the current precipitation type, intensity and what uses it.

Design notes: [rend2-acid-rain-profile.md](../rend2-acid-rain-profile.md).

## Weather on specific materials (mod authors)

Keep a roof wet but free of puddles, a cloth dry of standing water, or an indoor shader completely dry. Without an override every material reacts automatically.

Add `weatherResponse <wetness> [<puddle> [<runoff>]]` to a shader or stage, values 0–4, 1 normal:

~~~text
weatherResponse 1.3 1 0.3
~~~

A single `weatherResponse 0` disables all three.

**Debug**

- `r_weatherSurfaceDebug 27..31` — on/off, wetness, puddle and runoff scale, why a surface is excluded.
- `r_weatherMaterialList` — print the drawn shaders with an override or automatic exclusion.

## Rain culling

Rain is drawn in chunks around the camera; chunks outside the view are skipped. The rain looks the same; on by default.

- `r_weatherCull 1` — 0 draw every chunk.

**Debug**

- `r_weatherDebugChunks 1` — show chunk bounds; 2 also prints them.
