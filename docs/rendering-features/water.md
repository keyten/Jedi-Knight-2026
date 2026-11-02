# Water, waves and waterfalls

[All rendering settings](../rendering-features.md)

Pools, rivers and waterfalls that reflect, refract, move and react to you, and what you see underwater. Almost everything here builds on **modern water** (`r_waterSurface 1`), so start with the first section.

## Modern water

Stock water is a scrolling texture with a fixed tint. Modern water reflects its surroundings, bends the view of the bottom, is clear where shallow and darker where deep, and gets the brighter reflections at grazing angles that real water has. Stock water and slime are found automatically; nothing has to be edited.

**Cost:** medium; high reflection quality can make it heavy.

- `r_waterSurface 0` *(restart)* — 1 on; 0 restores the original water.
- `r_waterSurfaceSSR 1` — weight of on-screen reflections over cubemap reflections; 0 cubemap only. Independent of `r_ssr`.
- `r_waterSurfaceRoughness 0.06` — smaller gives sharper highlights and reflections.
- `r_waterSurfaceAbsorption 1` — how fast water gets darker with depth (shared with [underwater fog](#underwater-fog)).
- `r_waterSurfaceReflection 1`, `r_waterSurfaceRefraction 1` — strength of reflection and of bending; 1 physically correct.
- `r_waterSurfaceNormal 1` — strength of small ripples; 0 removes them.
- `r_waterSurfaceExperimental 0` — 1: also treat surfaces with water-like shader names (`pool`, `lake`, `river`…) as water, for maps where detection misses some.

**Compare:** `r_waterSurfaceCompare` — 1 split screen (the original water shaders left; waterfalls split with it), 2 the original water.

**Mod authors:** water-body and `Liquids` rules in `cubemaps/<map>/env.json` choose which surfaces form one body and its look and motion; see the design notes.

**How it works.** At map load water surfaces are found from map contents and grouped into bodies. When drawn, reflection (on-screen tracing, falling back to cubemaps) is mixed with refraction by the viewing angle (Fresnel); the depth image behind the water gives its thickness and so how much light it absorbs.

- `r_waterSurfaceIOR 1.333` — refractive index of the liquid.
- `r_waterSurfaceDepthScale 1` — scale of the measured water thickness.
- `r_waterSurfaceDepthReject 1` — refraction never shows objects in front of the water.
- `r_waterSSROptimize 7` — reflection speed-ups, add bits: **1** coarser geometry in the reflection pre-pass, **2** skip hidden water, **4** simpler pre-pass shader. 0 turns them off for comparison.
- `r_waterSSRWaveReprojection 0` *(restart)* — 1: use exact previous-frame wave geometry for frame-averaged reflections.

**Debug**

- `r_waterSurfaceDebug 1..8`, `10`, `11` — classification, normals, reflection, depth, refraction, roughness (the old split 9 is `r_waterSurfaceCompare 1`).
- `r_waterMotionDebug 1..10` — water motion and history validity.
- `r_waterInfo` — list modern-water surfaces. `r_waterBodies` — bodies and profiles. `r_waterOverride` — inspect or override classification (run without arguments for syntax).

Design notes: [rend2-water-bodies.md](../rend2-water-bodies.md), [rend2-water-surface.md](../rend2-water-surface.md).

## Underwater fog

Underwater the original game tints the whole screen one color. With this, water, slime and lava are real fog inside [volumetric fog](fog.md#volumetric-fog): objects fade with their actual distance through the liquid, red light disappears first, sunlight dims with depth, and a moving light pattern (caustics) plays on the bottom. Needs `r_volumetricFog 2`.

**Cost:** medium.

- `r_volumetricWater 0` *(restart)* — which liquids, add: **1** water, **2** slime, **4** lava. 7 for all.
- `r_volumetricWaterExtinction 0.0014` — how fast water hides distant objects.
- `r_volumetricWaterAlbedo "0.10 0.45 0.75"` — color of light scattered by water.
- `r_volumetricWaterColor` — relative absorption of red, green and blue; by default red goes first.
- `r_volumetricWaterCaustics 0.35` — strength of the [caustic pattern](#underwater-light-patterns-caustics) from the sun.
- `cg_underwaterTint 1` — the original full-screen tint; skipped automatically for liquids drawn as fog.

**Compare:** `r_volumetricWaterCompare 2` — the original screen tint. No split screen: the game draws that tint over the whole screen.

**How it works.** Liquid brushes add absorption and scattering to the fog cells inside them. Sun and dynamic light lose strength with the distance they travel through the liquid.

- `r_volumetricWaterAnisotropy 0.75` — how strongly water glows toward the light.
- `r_volumetricWaterSunPath 1` — dim sun and dynamic light by the path through the liquid.
- `r_volumetricWaterSurfaces 1` *(restart)* — also dim sunlight and add caustics on underwater surfaces.
- `r_volumetricWaterCausticScale 160`, `r_volumetricWaterCausticSpeed 0.06`, `r_volumetricWaterCausticFocus 48` — caustic size, drift speed, depth of full contrast.
- `r_volumetricSlimeExtinction 0.005`, `r_volumetricSlimeAlbedo "0.25 0.7 0.2"`, `r_volumetricSlimeColor "1.6 0.5 0.9"`, `r_volumetricSlimeAnisotropy 0.4` — the same for slime.
- `r_volumetricLavaExtinction 0.02`, `r_volumetricLavaAlbedo "0.6 0.15 0.02"`, `r_volumetricLavaColor "0.6 1.2 1.2"`, `r_volumetricLavaAnisotropy 0.3` — the same for lava (no glow).

**Debug**

- `r_volumetricFogDebug 59..70` — liquid density, brushes, camera membership, boundaries, transparency, underwater sun, path length, scattering.
- `r_liquids` — list liquid brushes and profiles.
- `r_volumetricWaterActive` — set by the renderer: which liquids are drawn as fog on this map (read by the game).

Design notes: [rend2-volumetric-fog.md](../rend2-volumetric-fog.md#underwater-medium-r_volumetricwater).

## Looking up from underwater

From below, the world above is visible only through a bright circle overhead (Snell's window); at flatter angles the surface turns into a mirror reflecting the underwater scene. Needs modern water.

**Cost:** light to medium.

- `r_waterSnell 0` — 1 on.
- `r_waterSnellOptics 0` *(restart)* — **0** basic, **1** sunlight passes through the surface, **2** also the atmosphere along the path above.

**Compare:** `r_waterSnellCompare 2` — the plain surface from below. No split screen: Snell's window is a water program of its own.

**How it works.** Light bends when crossing the water surface (Snell's law); beyond a critical angle it can't leave and reflects back (total internal reflection).

**Debug**

- `r_waterSnellDebug 1..6` — crossing direction, internal reflection, reflection strength, ray direction, source.

Design notes: [rend2-water-realism-followup.md](../rend2-water-realism-followup.md).

## Waves

Coherent waves roll across the water instead of a scrolling texture. Wind can turn and strengthen them outdoors, and shallow water moves less than deep pools. Without [wave geometry](#wave-geometry) the waves change lighting only; the surface itself stays flat.

**Cost:** light; medium with wave geometry.

- `r_waterWaves 0` — 1 on.
- `r_waterWaveAmplitude 1`, `r_waterWaveLength 1`, `r_waterWaveSpeed 1` — wave height, length and speed.
- `r_waterWind 0` — 1: outdoor waves follow the wind.
- `r_waterWaveQuality 1` — 0, 1, 2: 2, 4 or 8 wave layers.

**Compare:** `r_waterWavesCompare` — 1 split screen (the original surface left; with wave geometry the shape still moves on both sides), 2 off.

**How it works.** A few analytic wave trains are summed to give height and slope. Dispersion makes long waves faster than short ones and all waves slower in shallow water; wind changes are crossfaded.

- `r_waterWaveDispersion 1` — **0** all waves the same speed, **1** speed depends on wavelength and depth, **2** physically calibrated (still water only).
- `r_waterWaveGravity 313.6` — gravity for mode 2, in units/s².
- `r_waterWaveShallow 1` — damp waves in shallow bodies.
- `r_waterWaveMicro 1` — fine slope detail, on top of `r_waterSurfaceNormal`.
- `r_waterWindScale 1` — strength of the wind response.
- `r_waterWindTransition 4` — seconds to blend into a new wind.

**Debug**

- `r_waterWaveDebug 1..7`, `9`, `10` — height, normals, profile, depth mask, displacement, multiplier, legacy deform (the old split 8 is `r_waterWavesCompare 1`).
- `r_waterWaveTime -1` — freeze waves at a time in seconds; -1 running.
- `r_waterWindDebug 1..4` — wind, current and target direction, blend.

Design notes: [rend2-water-waves.md](../rend2-water-waves.md).

## Wave geometry

Lets waves actually move the water surface, so its outline rises and falls against shores and objects, and the camera crossing the surface follows the moving waterline. Needs modern water.

**Cost:** medium; dense geometry can be heavy.

- `r_waterGeometry 0` *(restart)* — 1: split water into a fine mesh.
- `r_waterTessellation 0` *(restart)* — 1: let the GPU add detail near the camera and less far away (OpenGL 4.3). Faster and finer than the fixed mesh alone.
- `r_waterTessPixels 96` — tessellation: target edge length on screen; smaller is denser and slower.
- `r_waterGeometryEdge 64` *(restart)* — fixed mesh: longest edge in world units.

**Compare:** `r_waterGeometryCompare 2` — a flat surface (the mesh stays, the waves don't move it). No split screen: the mesh moves for the whole view.

**How it works.** The water is subdivided within vertex budgets, and each vertex is moved by the wave height. Waves shorter than the mesh stay in shading only. The waterline uses a small tolerance band so the screen doesn't flicker between above and below water.

- `r_waterGeometryBodyVerts 250000`, `r_waterGeometryMapVerts 1000000` *(restart)* — vertex budgets per body and per map.
- `r_waterTessMax 16` *(restart)* — maximum tessellation level (8, 16, 32); the base mesh is that much coarser.
- `r_waterTessLodDistance 0` — distance with the finest detail.
- `r_waterTessMinEdge 0` — shortest edge in world units; 0 lets `r_waterTessMax` decide.
- `r_waterlineHysteresis 1.5` — the waterline tolerance band.

**Debug**

- `r_waterGeometryDebug 1` — geometry view. `r_waterGeometryInfo` — meshes and budgets.
- `r_waterTessForce <level>` — force one tessellation level everywhere; 0 by distance.
- `r_waterlineDebug 1..2` — waterline state, and its height graph.

Design notes: [rend2-water-geometry.md](../rend2-water-geometry.md).

## Ripples from movement and impacts

Shots, falling objects and characters send real ripples across a pool, which spread, reflect from edges and fade. Needs modern water.

**Cost:** medium, within memory budgets.

- `r_waterInteraction 0` *(restart)* — 1 on.
- `r_waterInteractionStrength 1` — strength of all disturbances.
- `r_waterInteractionQuality 1` *(restart)* — detail: 0 coarse, 1 medium, 2 fine.
- `r_waterBathymetry 0` *(restart)* — 1: measure the depth under each body, so that with `r_waterInteractionVariableDepth 1` ripples slow down in shallow water as real waves do.

**Compare:** `r_waterInteractionCompare` — 1 split screen (no ripples or rain rings on the water left), 2 off.

**How it works.** Each body gets a small grid of water height and vertical speed that is simulated every frame.

- `r_waterInteractionSpeed 1`, `r_waterInteractionDamping 1` — spread speed and damping.
- `r_waterInteractionMaxBodies 8`, `r_waterInteractionMaxTexels 524288`, `r_waterInteractionMemoryMB 16` *(restart)* — budgets.

**Debug**

- `r_waterInteractionDebug 1..9` — domain, mask, height, velocity, slope, sources, foam, sleep, mapping.
- `r_waterInteractionInfo` — fields and budgets. `r_waterBathymetryDebug 1` — depth-measurement audit.
- `r_waterImpulse [strength] [radius]` — poke the water you are aiming at.

Design notes: [rend2-water-interaction.md](../rend2-water-interaction.md).

## Splashes, wakes and rain on water

Jumping in or climbing out throws spray, shots raise splashes, characters leave wakes, and rain dimples pools. Wakes and rain need [ripples](#ripples-from-movement-and-impacts) (`r_waterInteraction 1`) to show.

**Cost:** medium while active.

- `r_waterSplashes 0` — 1: entry, exit and projectile splashes.
- `r_waterSplashStrength 1`, `r_waterSplashQuality 1` — strength and particle quality (0–2).
- `r_waterSplashProjectiles 1` — splashes from shots and thrown sabers.
- `r_waterSplashFoam 1` — foam left by splashes.
- `r_waterWakes 1` — wakes behind moving characters.
- `r_waterWakeStrength 1` — wake strength.
- `r_waterRainRipples 1` — rain on real water bodies.
- `r_waterRainRippleQuality 1` *(restart)* — 0, 1, 2.

No separate compare cvar: splashes and spray are spawned for the whole world, and `r_waterInteractionCompare` covers the wakes and rain rings.

**How it works.** Game events push impulses into the body's ripple grid and spawn spray particles. Rain adds a statistical sample of its impacts plus fine disturbance.

- `r_waterWakeMinSpeed 35`, `r_waterWakeFoamSpeed 260` — speed where wakes start and where wake foam is full.
- `r_waterWakeMaxEntities 16` — characters making wakes per frame.
- `r_waterRainRippleDensity 0.22` — share of rain drops turned into individual ripples.
- `r_waterRainRippleImpulse 0.55`, `r_waterRainRippleMicro 1` — their strength and the fine rain roughness.

**Debug**

- `r_waterSplashDebug 1`, `r_waterWakeDebug 1` — show and print events.
- `r_waterRainRippleDebug 1..4` — impact mask, source, field, energy.

Design notes: [rend2-physical-water-splashes.md](../rend2-physical-water-splashes.md).

## Flowing water

Rivers and streams move along their channel, carrying ripples and foam with them. With the flow field, water slows near banks and turns along walls; with impact, it piles up and foams where it runs into a wall. Needs modern water.

**Cost:** light; medium with the field and wall impact.

- `r_waterFlow 0` — 1: flowing water on streams the renderer recognizes.
- `r_waterFlowField 0` *(restart)* — 1: compute flow around banks and obstacles at map load.
- `r_waterFlowImpact 0` *(restart)* — 1: water piles up, sloshes, foams and wets walls it runs into. Needs the flow field.
- `r_waterFlowSpeed 1` — flow speed.
- `r_waterFlowDetail 1` — how strongly small detail is carried by the flow.

**Compare:** `r_waterFlowCompare 2` — still water. No split screen: the flow moves the waves, foam and ripples of a whole body.

**Mod authors:** set flow direction and speed in the water-body data when the automatic guess is wrong; `r_waterFlowOverride` helps while testing.

**How it works.** Each body gets a flow direction, or a flow map computed at load, that moves its surface detail, ripples and foam. Wall impact adds local disturbance and wetness; it is not a full fluid simulation.

**Debug**

- `r_waterFlowDebug 1..7` — direction, speed, confidence, carried detail, field speed, wall impact, distance to shore.
- `r_waterFlowOverride` — inspect or set flow per body.

Design notes: [rend2-water-flow.md](../rend2-water-flow.md).

## Foam

White foam where water meets objects, along shores, behind wakes and under waterfalls. It drifts with the flow and fades over time. Needs modern water.

**Cost:** medium.

- `r_waterFoamField 0` *(restart)* — 1: foam that persists and moves.
- `r_waterIntersectionFoam 0` — 1: a foam ring where objects stand in shallow water.
- `r_waterFoamDecay 0.55` — how fast foam fades.
- `r_waterFoamShoreline 1`, `r_waterFoamWaterfall 1`, `r_waterFoamImpact 1`, `r_waterFoamIntersection 1`, `r_waterFoamSteepness 0` — foam from shores, waterfalls, splashes and wakes, objects, steep waves; 0 turns a source off.

**Compare:** `r_waterFoamCompare` — 1 split screen (no foam left), 2 off; both kinds of foam.

**How it works.** Each body has a small foam texture: sources add foam, flow moves it, and it decays. Objects are found where their depth meets the water surface.

- `r_waterFoamResolution 128` *(restart)* — foam texture size.
- `r_waterFoamAdvection 1`, `r_waterFoamDiffusion 0.12` — drift with the flow, spreading.
- `r_waterIntersectionFoamStrength 1`, `r_waterIntersectionFoamWidth 8`, `r_waterIntersectionFoamPersistence 1.5` — object foam strength, width and fade time.

**Debug**

- `r_waterFoamDebug 1..3` — amount, velocity, sources.
- `r_waterIntersectionFoamDebug 1` — raw object mask. `r_waterIntersectionVerify` — check object detection.

Design notes: [rend2-water-intersection-foam.md](../rend2-water-intersection-foam.md).

## Shorelines

With wave geometry, the water edge moves up and down the shore. This softens the hard line where water meets ground, calms waves near rigid banks, and leaves a wet band that dries as the water pulls back. Needs `r_waterGeometry 1`.

**Cost:** light to medium.

- `r_waterShoreline 0` *(restart)* — 1 on.
- `r_waterShoreWetStrength 0.8`, `r_waterShoreWetPersistence 8` — wet band strength and drying time in seconds.
- `r_waterShoreFoam 1` — shore foam (needs the foam field).
- `r_waterShoreAttenuation 96` — distance over which waves recover from a bank.
- `r_waterShoreContactSoftness 6` — width of the soft water edge.

**Compare:** `r_waterShorelineCompare 2` — a hard edge and no wet band. No split screen: the shoreline also lowers the waves of the surface mesh.

**Debug**

- `r_waterShoreDebug 1..9` — depth, shore distance, wave reduction, contact, wet edges, foam.

Design notes: [rend2-water-shoreline.md](../rend2-water-shoreline.md).

## Underwater light patterns (caustics)

Rippling bright lines on the bottom of pools, made by waves focusing sunlight. Mode 1 uses a generic pattern; mode 2 derives it from the actual waves above. Needs [underwater fog](#underwater-fog) with `r_volumetricWaterSurfaces 1`; mode 2 also modern water.

**Cost:** medium; more light sources cost more.

- `r_waterCausticsMode 0` — **0** off, **1** generic pattern, **2** from the real water surface.

**Compare:** `r_waterCausticsCompare` — 1 split screen (no caustics left, on surfaces and in the fog), 2 off.
- `r_waterCausticsLightMask 1` *(restart)* — light sources, add: **1** sun, **2** dynamic lights, **4** baked light.
- `r_waterCausticsQuality 1` — mode 2 detail: 0, 1, 2.

**How it works.** A curved water surface bends sunlight into bright lines and dark gaps. Mode 2 estimates that from the wave slopes above each underwater point.

- `r_waterCausticsSlope 1` — influence of wave slope.
- `r_waterCausticsFilter 1` — blur deeper patterns.
- `r_waterCausticsMaxDepth 2048` — depth where caustics fade out.
- `r_waterCausticsLocalMaxLights 1` — dynamic lights per point.
- `r_waterCausticsBakedMode 0` — baked light: **0** none, **1** from the surface, **2** generic pattern.
- `r_waterCausticsBakedStrength 0.35` — strength on baked light.

**Debug**

- `r_waterCausticsDebug 1..7` — slope, refracted ray, focus, result, projection, attenuation, interaction.

Design notes: [rend2-surface-caustics.md](../rend2-surface-caustics.md).

## Waterfalls and rapids

Falling water breaks up into streaks, turns white where it is aerated, throws mist at the base and churns the pool it falls into. Rapids carry white water downstream. Supported stock waterfalls are recognized automatically; others keep their original look. Needs modern water.

**Cost:** medium to heavy with all layers on.

- `r_waterfall 0` *(restart)* — 1 modern waterfall sheets.
- `r_waterfallMist 0` — 1 spray and mist at the base (best with volumetric fog).
- `r_waterfallImpact 0` — 1 ripples, turbulence and foam where it lands (needs ripples or the foam field).
- `r_waterWhitewater 0` *(restart)* — 1 white water in rapids and turbulent streams.
- `r_waterfallQuality 1` — 0–2.

**Compare:** `r_waterfallCompare 2` — the original waterfall shaders (split screen: `r_waterSurfaceCompare 1`, which splits waterfalls and water together). `r_waterWhitewaterCompare` — 1 split screen, 2 off.

**Mod authors:** `Waterfalls` data in `cubemaps/<map>/env.json` selects sheets and places mist and impact points.

**How it works.** Each waterfall gets a profile (small, medium, heavy) that drives its breakup, spray and pool disturbance. White water is carried by the flow and decays.

- `r_waterfallSmallStrength 1`, `r_waterfallMediumStrength 1`, `r_waterfallHeavyStrength 1` — strength per profile.
- `r_waterfallWhitewater 1`, `r_waterfallRefraction 1` — aeration brightness, sheet refraction.
- `r_waterfallGeometry 1` *(restart)* — finer waterfall mesh (visual only).
- `r_waterfallMistBalance 0.65` — 0 spray particles only, 1 full fog mist.
- `r_waterfallMistDensity 1`, `r_waterfallMistRadius 1` — mist density and size.
- `r_waterfallMistLens 0` — 1: nearby spray wets the [camera lens](weather.md#water-on-the-camera-lens).
- `r_waterfallImpactImpulse 1`, `r_waterfallImpactTurbulence 1`, `r_waterfallImpactFoam 1`, `r_waterfallImpactRadius 1` — pool push, turbulence, foam, area.
- `r_waterWhitewaterStrength 1`, `r_waterWhitewaterThreshold 0.45`, `r_waterWhitewaterDecay 1.1`, `r_waterWhitewaterFoam 0.35` — white water look, how much turbulence starts it, fade speed, foam left behind.

**Debug**

- `r_waterfallDebug 1..12` — classification, flow, motion, refraction, foam, spray, impacts, wireframe.
- `r_waterfallMistDebug 1`, `r_waterfallImpactDebug 1` — mist emitters, impact areas.
- `r_waterWhitewaterDebug 1..4` — flow, turbulence, white water, foam sources.
- `r_waterfalls` — list waterfall sheets.

Design notes: [rend2-waterfalls.md](../rend2-waterfalls.md).

## Underwater specks and bubbles

Drifting specks and tiny rising bubbles around the camera underwater, so clear and murky water feel different. Decorative only.

**Cost:** light.

- `r_waterParticles 0` — 1 on.
- `r_waterParticlesProfile 0` — **0** automatic (from the map, else clean), **1** clean, **2** murky, **3** dirty, **-1** none.
- `r_waterParticlesDensity 1`, `r_waterParticlesSize 1`, `r_waterParticlesSpeed 1` — amount, size, drift and rise speed.
- `r_waterParticlesBubbles -1` — share of bubbles 0–1; -1 from the profile.
- `r_waterParticlesDistance 160` — radius of the particle cloud around the camera.

**Compare:** `r_waterParticlesCompare` — 1 split screen, 2 off.

**Debug**

- `r_waterParticlesDebug 1..3` — placement, without fog in front, statistics.

Design notes: [rend2-water-particles.md](../rend2-water-particles.md).
