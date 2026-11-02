# Water, waves and waterfalls

[All rendering features](../rendering-features.md)

Controls marked `vid_restart` need a renderer restart. Debug views use 0 to return to normal unless stated otherwise. Performance labels are rough relative costs, not benchmark guarantees.

## Water surfaces

*Requires `r_waterSurface 1` and restart, because this replaces the old water stages and allocates reflection/refraction resources.*

Makes stock pools and rivers reflect their surroundings, bend the scene behind them and become darker with depth. The original scrolling water textures cannot react to actual water thickness this way. Medium performance cost; reflection quality can make it heavy.

**Main controls**

- `r_waterSurface` — Replace classified legacy water with the modern surface; 0 restores old stages. Default `0`; `vid_restart`.
- `r_waterSurfaceAbsorption` — shared liquid extinction scale for surface and volume (r_volumetricWaterExtinction / Color, Slime). Default `1.0`.
- `r_waterSurfaceDepthReject` — refraction never samples objects in front of the water (shrinks the offset, else none). Default `1`.
- `r_waterSurfaceDepthScale` — scale of the path length under the surface (water thickness from the depth buffer). Default `1.0`.
- `r_waterSurfaceExperimental` — also classify surfaces by a water-like shader name (pool, lake, river, water...) when they face up and have no water contents; experimental. Default `0`.
- `r_waterSurfaceIOR` — index of refraction of the liquid (Fresnel and refraction). Default `1.333`.
- `r_waterSurfaceNormal` — detail wave normal strength (0 removes micro detail; ambient macro is separate). Default `1.0`.
- `r_waterSurfaceReflection` — scale of the reflection (1 = physical). Default `1.0`.
- `r_waterSurfaceRefraction` — scale of the screen-space refraction offset (1 = physical for the depth behind the water). Default `1.0`.
- `r_waterSurfaceRoughness` — Base water roughness; smaller values make sharper highlights. Default `0.06`.
- `r_waterSurfaceSplit` — r_waterSurfaceDebug 9: position of the split (0-1 of the view width). Default `0.5`.
- `r_waterSurfaceSSR` — weight of the screen-space reflections over the cubemap (independent of opaque r_ssr; 0 = cubemap only). Default `1.0`.

**Debug**

- `r_waterSurfaceDebug` — Views 1..11 inspect classification, normals, reflection, depth, refraction and roughness; 9 compares old/new water. Default `0`.
- `r_waterBodies` — List resolved water-body identities and profiles.
- `r_waterInfo` — Inspect classified modern-water surfaces.
- `r_waterOverride` — Inspect/set water classification overrides; run without arguments for syntax.

### [Autodetectable] Stock water

Semantic water/slime surfaces are found from the BSP without editing textures or rebuilding the map. `r_waterSurfaceExperimental` adds a looser name-based fallback for exceptional maps.

### [For mod authors] Water bodies and overrides

Use the water-body and `Liquids` rules in `cubemaps/<map>/env.json` to choose body identity, optics and motion profiles. `r_waterOverride` helps inspect or override classification while testing. The detailed body guide lists the authoring keys.

The renderer identifies water interfaces from map contents and groups them into bodies. It then combines view-dependent reflection with depth-aware refraction and light absorption. [Details](../rend2-water-bodies.md).

## Underwater depth and color

*Requires `r_volumetricFog 2` and restart, because liquid brushes become media inside the spatial fog renderer.*

Makes underwater objects fade with actual distance through water, with separate slime and lava profiles. This replaces the original uniform underwater screen tint for handled liquids. Medium additional performance cost.

**Main controls**

- `r_volumetricWater` — Liquid types handled by the volume: add 1 water, 2 slime and 4 lava; 0 disables it. Default `0`; `vid_restart`.
- `cg_underwaterTint` — Legacy full-screen underwater tint; updated cgame skips liquid classes handled by volumetric water. Default `1`.
- `r_volumetricLavaAlbedo` — r_volumetricWater 4: lava scattering color "r g b" (no emission: absorption and scattering only). Default `0.6 0.15 0.02`.
- `r_volumetricLavaAnisotropy` — r_volumetricWater 4: lava scattering direction. Default `0.3`.
- `r_volumetricLavaColor` — r_volumetricWater 4: lava relative extinction "r g b". Default `0.6 1.2 1.2`.
- `r_volumetricLavaExtinction` — r_volumetricWater 4: lava light absorption per world unit. Default `0.02`.
- `r_volumetricSlimeAlbedo` — r_volumetricWater 2: slime scattering color "r g b". Default `0.25 0.7 0.2`.
- `r_volumetricSlimeAnisotropy` — r_volumetricWater 2: slime scattering direction. Default `0.4`.
- `r_volumetricSlimeColor` — r_volumetricWater 2: slime relative extinction "r g b". Default `1.6 0.5 0.9`.
- `r_volumetricSlimeExtinction` — How quickly light is absorbed in slime. Also used for matching slime-surface/body overrides. Default `0.005`.
- `r_volumetricWaterAlbedo` — Color of light scattered by water, as red/green/blue. Default `0.10 0.45 0.75`.
- `r_volumetricWaterAnisotropy` — water scattering direction (forward scattering). Default `0.75`.
- `r_volumetricWaterCausticFocus` — depth below the surface where the caustics reach full contrast (world units). Default `48`.
- `r_volumetricWaterCaustics` — strength of the sun caustics under water (0 = off, 1 = full pattern contrast); the average sunlight is kept. Default `0.35`.
- `r_volumetricWaterCausticScale` — caustic pattern period in world units. Default `160`.
- `r_volumetricWaterCausticSpeed` — caustic drift in pattern periods per second. Default `0.06`.
- `r_volumetricWaterColor` — Relative red/green/blue absorption; the default absorbs red first.
- `r_volumetricWaterExtinction` — Water absorption per world unit; higher values hide distant underwater objects sooner. Default `0.0014`.
- `r_volumetricWaterSunPath` — Attenuate sun and dynamic light by the distance it travels through liquid. Default `1`.
- `r_volumetricWaterSurfaces` — Apply underwater sun attenuation and caustics to surfaces; needs sunlight rendering. Default `1`; `vid_restart`.

**Debug**

- `r_volumetricWaterActive` — Set by the renderer: liquid classes drawn as a medium on this map (1 water, 2 slime, 4 lava), read by cgame. Default `0`.
- `r_liquids` — Inspect resolved participating liquid brushes and profiles.

Liquid brushes contribute absorption and scattered light to fog cells. Updated game code reads the active-liquid mask to avoid adding the old tint a second time.

## Looking up from underwater

*Requires modern water, because the air/water interface supplies the refracting surface.*

Shows a window to the scene above when looking upward, and internal reflections at flatter viewing angles. The original water surface does not provide this view-dependent boundary. Light to medium additional performance cost.

**Main controls**

- `r_waterSnell` — Enable the underwater sky window and internal reflection at shallow view angles. Default `0`.
- `r_waterSnellOptics` — 0 original interface lighting, 1 transmit sunlight through the surface, 2 also include atmosphere in the air path. Default `0`; `vid_restart`.

**Debug**

- `r_waterSnellDebug` — Views 1..6 inspect crossing direction, internal reflection, reflection strength, ray direction and source. Default `0`.

Snell’s law bends rays crossing water and air. Beyond a critical angle the surface reflects them back into the water; optional optics modes also handle sun transmission and the atmosphere along the air path. [Details](../rend2-water-realism-followup.md).

## Water waves

*Requires modern water; visible displacement also needs water geometry, because shading alone cannot move the surface outline.*

Adds coherent waves instead of only scrolling detail textures. Wind can change their direction and strength, while shallow areas can move differently from deeper pools. Light for shading, medium with displaced geometry.

**Main controls**

- `r_waterWaves` — Ambient macro/medium water waves; 0 keeps existing water. Default `0`.
- `r_waterWind` — Wind-driven outdoor water response (requires r_waterWaves); 0 preserves existing waves. Default `0`.
- `r_waterWaveAmplitude` — Ambient wave amplitude scale. Default `1`.
- `r_waterWaveChoppiness` — Reserved for future horizontal wave displacement; currently not an extra wave effect. Default `1`.
- `r_waterWaveDispersion` — Wave dispersion: 0 legacy nondispersive, 1 authored-speed finite-depth, 2 calibrated physical for non-flowing water. Default `1`.
- `r_waterWaveGravity` — Physical renderer water gravity in world units per second squared (dispersion mode 2 and variable-depth interaction). Default `313.6`.
- `r_waterWaveLength` — Ambient wave wavelength scale. Default `1`.
- `r_waterWaveMicro` — Micro slope scale in addition to r_waterSurfaceNormal. Default `1`.
- `r_waterWaveQuality` — Ambient waves: 0 = 2 terms, 1 = 4 terms, 2 = 8 terms. Default `1`.
- `r_waterWaveShallow` — Dampen analytical waves using stable body mean depth. Default `1`.
- `r_waterWaveSpeed` — Ambient wave speed scale. Default `1`.
- `r_waterWindScale` — Global scale for resolved wind response on water. Default `1`.
- `r_waterWindTransition` — Seconds to crossfade old and target wind wave sets. Default `4`.

**Debug**

- `r_waterWaveDebug` — Ambient waves debug: 1 height, 2 macro normal, 3 micro normal, 4 combined, 5 profile, 6 depth mask, 7 displacement, 8 split, 9 multiplier, 10 legacy deform. Default `0`.
- `r_waterWaveTime` — Ambient wave time: -1 running, >=0 fixed seconds. Default `-1`.
- `r_waterWindDebug` — Water wind debug: 1 resolved wind, 2 current wave direction, 3 target direction, 4 blend/source. Default `0`.

A small set of analytic waves supplies height and surface slopes. Dispersion controls how wavelength and water depth affect speed; wind changes are blended instead of snapping. [Details](../rend2-water-waves.md).

## Moving water geometry

*Requires modern water and restart; optional GPU tessellation also needs the modern OpenGL path, because the surface must have enough vertices to move.*

Lets waves change the water’s actual outline and follows that moving surface when the camera enters or leaves water. Without it, optical waves still lie on a flat mesh. Medium performance cost; dense geometry can be heavy.

**Main controls**

- `r_waterGeometry` — Subdivided render-only water mesh; requires r_waterSurface and vid_restart. Default `0`; `vid_restart`.
- `r_waterTessellation` — Distance-adaptive GPU tessellation of the r_waterGeometry mesh (GL 4.3, r_gl43); coarser base mesh, requires vid_restart. Default `0`; `vid_restart`.
- `r_waterGeometryBodyVerts` — Maximum subdivided vertices per water body. Default `250000`; `vid_restart`.
- `r_waterGeometryEdge` — Target maximum water mesh edge length in world units. Default `64`; `vid_restart`.
- `r_waterGeometryMapVerts` — Maximum subdivided vertices per map. Default `1000000`; `vid_restart`.
- `r_waterlineHysteresis` — Visual camera waterline enter/exit band in world units; fast crossings reduce the band. Default `1.5`.
- `r_waterTessLodDistance` — Distance from the camera (world units) with the finest water tessellation; beyond it edges grow by r_waterTessPixels. Default `0`.
- `r_waterTessMax` — Maximum water tessellation level (8, 16, 32): the base mesh is that much coarser; requires vid_restart. Default `16`; `vid_restart`.
- `r_waterTessMinEdge` — Shortest tessellated water edge in world units: 64 = never finer than the static r_waterGeometry mesh, 0 = only r_waterTessMax limits the detail near the camera. Default `0`.
- `r_waterTessPixels` — Target tessellated water edge length in pixels (smaller is denser and slower; waves too short for the mesh stay in shading). Default `96`.

**Debug**

- `r_waterGeometryDebug` — Water geometry debug view. Default `0`.
- `r_waterlineDebug` — Visual waterline diagnostics: 1 = once-per-second state, 2 = state plus recent camera-relative-height graph. Default `0`.
- `r_waterTessForce` — Water tessellation level for every edge (0: by distance), for tests. Default `0`.
- `r_waterGeometryInfo` — Print subdivided/tessellated water geometry and budgets.

The renderer subdivides water within vertex budgets. Optional tessellation refines nearby edges on the GPU; waterline hysteresis prevents rapid underwater/above-water flicker. [Details](../rend2-water-geometry.md).

## Interactive water

*Requires modern water and restart, because each active body needs its own disturbance field.*

Lets impacts and movement send ripples across a pool instead of only spawning a flat splash picture. Optional depth-aware propagation makes shallow regions behave differently. Medium performance and memory cost within the chosen body budgets.

**Main controls**

- `r_waterInteraction` — Per-body interactive water disturbances; requires r_waterSurface and vid_restart. Default `0`; `vid_restart`.
- `r_waterBathymetry` — Build exact body-local liquid depth/confidence fields; requires water interaction and vid_restart. Default `0`; `vid_restart`.
- `r_waterInteractionVariableDepth` — Use bathymetry and g*div(H*grad(height)) in interactive water. Default `0`.
- `r_waterInteractionDamping` — Profile disturbance damping multiplier. Default `1`.
- `r_waterInteractionMaxBodies` — Maximum bodies with interaction fields. Default `8`; `vid_restart`.
- `r_waterInteractionMaxTexels` — Total interaction texel budget. Default `524288`; `vid_restart`.
- `r_waterInteractionMemoryMB` — GPU interaction-field memory budget in MiB. Default `16`; `vid_restart`.
- `r_waterInteractionQuality` — Interaction density: 0=32, 1=16, 2=8 world units per texel. Default `1`; `vid_restart`.
- `r_waterInteractionSpeed` — Profile disturbance propagation-speed multiplier. Default `1`.
- `r_waterInteractionStrength` — Global water interaction strength multiplier. Default `1`.

**Debug**

- `r_waterBathymetryDebug` — Print body-local bathymetry coverage and confidence audit. Default `0`.
- `r_waterInteractionDebug` — Interaction debug: 1 domain, 2 mask, 3 height, 4 velocity, 5 slope, 6 source, 7 foam, 8 active/sleep, 9 mapping. Default `0`.
- `r_waterImpulse` — Inject a diagnostic impulse at the aimed water body; optional strength and radius.
- `r_waterInteractionInfo` — Print allocated disturbance fields, masks and resource budgets.

Each body stores water height and vertical speed on a small grid. Bathymetry means the depth map beneath the surface; it can change how quickly disturbances travel. [Details](../rend2-water-interaction.md).

## Flowing water

*Requires modern water; wall response also needs the spatial flow field, because it uses local banks and obstacles to redirect flow.*

Makes rivers move consistently along their channel, carrying surface detail and disturbances. Water can slow near banks and pile or foam at walls. Light for uniform flow, medium for the field and wall response.

**Main controls**

- `r_waterFlow` — Directional per-body water flow; 0 preserves the pre-flow water result. Default `0`.
- `r_waterFlowField` — Spatial per-body flow field baked at map load: slower water at banks, flow turning along walls (requires r_waterFlow 1). Default `0`; `vid_restart`.
- `r_waterFlowImpact` — Flow running into a wall piles up, sloshes, foams and wets the wall (requires r_waterFlowField 1). Default `0`; `vid_restart`.
- `r_waterFlowDetail` — Directional advection scale for medium and micro water detail. Default `1`.
- `r_waterFlowSpeed` — Global multiplier for resolved per-body flow velocity. Default `1`.

**Debug**

- `r_waterFlowDebug` — Water flow debug: 1 direction, 2 magnitude, 3 confidence/source, 4 advected detail, 5 field speed, 6 wall impingement, 7 field shore distance. Default `0`.
- `r_waterFlowOverride` — Inspect/set per-body flow overrides; run without arguments for syntax.

### [Autodetectable] Existing streams

`r_waterFlow 1` uses supported stock/body flow decisions. Enable `r_waterFlowField` before restart to account for banks, then `r_waterFlowImpact` for wall response.

### [For mod authors] Authored flow

Supply explicit flow direction/speed in water-body metadata when the stock decision is unsuitable. Use `r_waterFlowOverride` while testing; authoring a direction alone does not enable the global flow mode.

A per-body direction, or optional local flow map, moves the water fields. The wall-response layer adds local disturbance and wet contact rather than a full 3D fluid simulation. [Details](../rend2-water-flow.md).

## Foam around obstacles

*Requires modern water; persistent foam needs its allocated body field, because it must survive and move between frames.*

Adds foam where water meets objects, along supported shores and under waterfalls. Foam can drift and fade instead of staying glued to one texture. Medium additional performance cost.

**Main controls**

- `r_waterFoamField` — Persistent body-local water foam fields; requires r_waterSurface and vid_restart (never ocean foam). Default `0`; `vid_restart`.
- `r_waterIntersectionFoam` — Opaque-object intersection foam on modern shallow water. Default `0`.
- `r_waterFoamAdvection` — Foam advection multiplier for resolved body flow. Default `1`.
- `r_waterFoamDecay` — Persistent foam concentration decay per second. Default `0.55`.
- `r_waterFoamDiffusion` — Mild foam diffusion in texture cells per second. Default `0.12`.
- `r_waterFoamImpact` — Impact-pool and wake foam source strength (0 disables this source). Default `1`.
- `r_waterFoamIntersection` — Obstacle/intersection foam source strength (0 disables this source). Default `1`.
- `r_waterFoamResolution` — Longest side of each active local foam field. Default `128`; `vid_restart`.
- `r_waterFoamShoreline` — Enabled shoreline foam source strength (0 disables this source). Default `1`.
- `r_waterFoamSteepness` — Optional local wave-steepness foam source strength. Default `0`.
- `r_waterFoamWaterfall` — Waterfall/turbulent-stream foam source strength (0 disables this source). Default `1`.
- `r_waterIntersectionFoamPersistence` — Seconds for intersection foam in the interaction field to fade. Default `1.5`.
- `r_waterIntersectionFoamStrength` — Object/water intersection foam strength. Default `1`.
- `r_waterIntersectionFoamWidth` — World-space half-width of the object/water intersection band. Default `8`.

**Debug**

- `r_waterFoamDebug` — Persistent foam debug: 1 concentration, 2 velocity, 3 sources. Default `0`.
- `r_waterIntersectionFoamDebug` — Show the raw depth/normal-qualified object intersection mask. Default `0`.
- `r_waterIntersectionVerify` — Verify GPU water/object intersection classification.

A body-local texture receives foam sources, moves with flow and gradually decays. Depth/normal tests find visible object intersections without treating every nearby pixel as a contact. [Details](../rend2-water-intersection-foam.md).

## Water at the shore

*Requires modern water, water geometry and restart, because this follows the displaced waterline against solid ground.*

Softens abrupt water/shore intersections, reduces waves near rigid banks and leaves a drying wet fringe as the surface retreats. Light to medium additional performance cost.

**Main controls**

- `r_waterShoreline` — Displaced-water shoreline/contact treatment; requires r_waterSurface, r_waterGeometry and vid_restart. Default `0`; `vid_restart`.
- `r_waterShoreAttenuation` — World distance over which macro waves recover from a rigid/shallow boundary. Default `96`.
- `r_waterShoreContactSoftness` — World width of the water/geometry soft contact and overlap. Default `6`.
- `r_waterShoreFoam` — Profile-scaled shoreline source strength for the existing water foam field. Default `1`.
- `r_waterShoreWetPersistence` — Seconds for a retreated displaced waterline to dry. Default `8`.
- `r_waterShoreWetStrength` — Water-contact wet-fringe strength through the existing material wetness response. Default `0.8`.

**Debug**

- `r_waterShoreDebug` — Views 1..9 inspect depth, shoreline distance, wave reduction, contact, wet edges and foam. Default `0`.

Shore depth and boundary distance reduce wave motion near contact. The same moving contact feeds wetness and optional foam history. [Details](../rend2-water-shoreline.md).

## Splashes, wakes and rain on real water

*Requires updated game/client modules and the corresponding water fields, because game events must reach the renderer.*

Adds entry/exit splashes, projectile impacts, wakes behind moving characters and rain ripples on real pools. These follow the actual water bodies rather than using one generic overlay. Medium performance cost during active disturbances.

**Main controls**

- `r_waterSplashes` — Physical water entry, exit and projectile splashes. Default `0`.
- `r_waterWakes` — Submit movement-driven water wakes from updated game/client code. Default `1`.
- `r_waterRainRipples` — Rain disturbances on classified real water bodies. Default `1`.
- `r_waterRainRippleDensity` — Statistical fraction of real-water rain impacts injected as explicit ripples. Default `0.22`.
- `r_waterRainRippleImpulse` — Velocity impulse scale of explicit real-water rain ripples. Default `0.55`.
- `r_waterRainRippleMicro` — Aggregate rain micro-disturbance scale on real water. Default `1`.
- `r_waterRainRippleQuality` — Real-water rain field quality: 0=128, 1=256, 2=512 (vid_restart). Default `1`; `vid_restart`.
- `r_waterSplashFoam` — Physical splash foam scale. Default `1`.
- `r_waterSplashProjectiles` — Projectile and thrown-saber water splashes. Default `1`.
- `r_waterSplashQuality` — Physical splash particles: 0 low, 1 medium, 2 high. Default `1`.
- `r_waterSplashStrength` — Physical splash impulse and spray strength scale. Default `1`.
- `r_waterWakeFoamSpeed` — Entity speed at which the wake foam response reaches its reference strength. Default `260`.
- `r_waterWakeMaxEntities` — Maximum wake-producing entities per frame. Default `16`.
- `r_waterWakeMinSpeed` — Minimum entity speed for wake submission, world units per second. Default `35`.
- `r_waterWakeStrength` — Wake impulse strength multiplier. Default `1`.

**Debug**

- `r_waterRainRippleDebug` — Water rain debug: 1 impact mask, 2 source texture, 3 sparse field, 4 aggregate energy. Default `0`.
- `r_waterSplashDebug` — Show and print physical splash events. Default `0`.
- `r_waterWakeDebug` — Print/draw movement-wake diagnostics. Default `0`.

Game events inject impulses into local water fields and submit spray particles. Wakes and rain add bounded sources; their switches do not enable modern water or its simulation by themselves. [Details](../rend2-physical-water-splashes.md).

## Moving light patterns underwater

*Requires enabled liquid attenuation and the relevant light source; surface-driven mode also needs modern water, because it uses its wave slopes.*

Makes bright underwater patterns move with the water above them instead of using an unrelated scrolling pattern. Sun, supported dynamic lights and directed baked light can be selected separately. Medium performance cost; extra light sources increase it.

**Main controls**

- `r_waterCausticsMode` — underwater caustics: 0 off, 1 original procedural, 2 actual water-surface driven. Default `1`.
- `r_waterCausticsLightMask` — caustic light sources: 1 sun, 2 dynamic point/spot, 4 directional baked light. Default `1`; `vid_restart`.
- `r_waterCausticsBakedMode` — directional baked-light caustics: 0 off, 1 surface-driven, 2 original procedural pattern. Default `0`.
- `r_waterCausticsBakedStrength` — strength of liquid attenuation and caustic modulation on the directional baked-light share. Default `0.35`.
- `r_waterCausticsFilter` — receiver-depth footprint filter for surface-driven caustics. Default `1`.
- `r_waterCausticsLocalMaxLights` — maximum dynamic point/spot lights evaluated for water caustics per receiver. Default `1`.
- `r_waterCausticsMaxDepth` — maximum liquid path depth for surface-driven caustic contrast. Default `2048`.
- `r_waterCausticsQuality` — surface-driven caustic derivative quality: 0 32u, 1 16u, 2 8u base spacing. Default `1`.
- `r_waterCausticsSlope` — surface slope contribution to surface-driven caustics. Default `1`.

**Debug**

- `r_waterCausticsDebug` — surface caustics debug: 1 slope, 2 refracted ray, 3 focus, 4 weighted result, 5 projected receiver, 6 attenuation, 7 interaction. Default `0`.

Caustics are light focused or spread by a curved water surface. Mode 2 estimates that focusing from the resolved waves; mode 1 keeps the earlier procedural pattern. [Details](../rend2-surface-caustics.md).

## Waterfalls and whitewater

*Requires the corresponding modern water resources, because falling sheets, mist and pool motion use separate surface/fog/simulation paths. Volume mist needs spatial fog; impact pools need interaction or foam fields.*

Adds broken-up falling sheets, bright aerated water, mist at the base and disturbed impact pools. This replaces flat waterfall stages only for supported or authored sheets. Medium to heavy performance cost when several layers are active.

**Main controls**

- `r_waterfall` — Dedicated modern waterfall sheets selected by env.json Waterfalls metadata or the audited stock mapping; 0 restores exact legacy stages; vid_restart. Default `0`; `vid_restart`.
- `r_waterfallMist` — Waterfall base spray/mist generated from authored emitters or classified sheet impact edges. Default `0`.
- `r_waterfallImpact` — Localized waterfall impact-pool ripples, turbulence and foam; requires r_waterInteraction and/or r_waterFoamField. Default `0`.
- `r_waterWhitewater` — Advected stream/rapid aeration; requires r_waterSurface and vid_restart. Default `0`; `vid_restart`.
- `r_waterfallGeometry` — Render-only waterfall subdivision; BSP collision is unchanged; vid_restart. Default `1`; `vid_restart`.
- `r_waterfallHeavyStrength` — Heavy/high-volume waterfall profile breakup, whitewater and spray strength. Default `1`.
- `r_waterfallImpactFoam` — Waterfall impact-pool persistent foam injection strength. Default `1`.
- `r_waterfallImpactImpulse` — Waterfall impact-pool central downward impulse strength. Default `1`.
- `r_waterfallImpactRadius` — Waterfall impact-pool authored/inferred radius multiplier. Default `1`.
- `r_waterfallImpactTurbulence` — Waterfall impact-pool stochastic secondary disturbance strength. Default `1`.
- `r_waterfallMediumStrength` — Medium waterfall profile breakup, whitewater and spray strength. Default `1`.
- `r_waterfallMistBalance` — Waterfall particle/mist balance: 0 keeps particle spray only, 1 uses full froxel mist. Default `0.65`.
- `r_waterfallMistDensity` — Waterfall suspended mist density scale. Default `1`.
- `r_waterfallMistLens` — Couple nearby waterfall spray to the existing LensWater SPRAY input. Default `0`.
- `r_waterfallMistRadius` — Waterfall mist and lens emitter radius scale. Default `1`.
- `r_waterfallQuality` — Waterfall coherent breakup quality: 0 low, 1 medium, 2 high. Default `1`.
- `r_waterfallRefraction` — Waterfall thin-sheet refraction strength. Default `1`.
- `r_waterfallSmallStrength` — Small waterfall profile breakup, whitewater and spray strength. Default `1`.
- `r_waterfallWhitewater` — Waterfall aeration and lit whitewater scale. Default `1`.
- `r_waterWhitewaterDecay` — Advected whitewater turbulence decay per second. Default `1.1`.
- `r_waterWhitewaterFoam` — Persistent surface-foam injection from whitewater. Default `0.35`.
- `r_waterWhitewaterStrength` — Whitewater bulk optical strength. Default `1`.
- `r_waterWhitewaterThreshold` — Turbulence source threshold for whitewater formation. Default `0.45`.

**Debug**

- `r_waterfallDebug` — Views 1..12 inspect classification, flow, movement, refraction, foam, spray, impacts and wireframe. Default `0`.
- `r_waterfallImpactDebug` — Draw resolved waterfall impact regions and print resolution diagnostics. Default `0`.
- `r_waterfallMistDebug` — Draw and list resolved waterfall mist emitters. Default `0`.
- `r_waterWhitewaterDebug` — Whitewater debug: 1 flow, 2 turbulence source, 3 final whitewater, 4 foam sources. Default `0`.
- `r_waterfalls` — List selected waterfall sheets and metadata.

### [Autodetectable] Supported stock waterfalls

`r_waterfall 1` plus restart selects audited stock sheets. Enable mist, impact pools and whitewater separately; each adds its own effect.

### [For mod authors] Custom falling sheets

Use `Waterfalls` metadata in `cubemaps/<map>/env.json` for sheet selection and emitter/impact placement. Unrecognized legacy waterfalls keep their old stages until covered by a rule.

Falling sheets share a resolved waterfall profile. Separate bounded sources feed spray, fog, pool disturbances and foam; whitewater carries aeration along turbulent streams. [Details](../rend2-waterfalls.md).

## Underwater motes and bubbles

*Requires a supported water body and underwater camera, because the particles use that body’s profile.*

Adds drifting specks and tiny bubbles around the camera, making clear and murky water feel different from empty tinted air. Light performance cost.

**Main controls**

- `r_waterParticles` — Underwater suspended particles and micro-bubbles around the camera (0 off, 1 on). Default `0`.
- `r_waterParticlesBubbles` — share of micro-bubbles 0..1, -1 = the body profile's. Default `-1`.
- `r_waterParticlesDensity` — density scale of the body profile. Default `1`.
- `r_waterParticlesDistance` — radius of the particle volume around the camera (world units). Default `160`.
- `r_waterParticlesProfile` — body profile, 0 auto (env.json WaterParticles, else clean), 1 clean, 2 murky, 3 dirty, -1 none. Default `0`.
- `r_waterParticlesSize` — particle size scale. Default `1`.
- `r_waterParticlesSpeed` — drift and bubble rise speed scale. Default `1`.

**Debug**

- `r_waterParticlesDebug` — r_waterParticles debug: 1 placement (opaque, cyan motes, yellow bubbles), 2 no fog in front, 3 print stats every second. Default `0`.

A small camera-local particle volume uses a clean, murky or dirty body profile. Density and distance bound the work; this is decorative material suspended in water, not fluid simulation. [Details](../rend2-water-particles.md).
