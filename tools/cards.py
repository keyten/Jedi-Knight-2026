"""Card and modal content for the site.

Each graphics card lists the sections of docs/rendering-features.md shown in its
modal (`sections`, by heading). Features without a section there carry their own
Markdown in `doc`. `how` is the short technical explanation. `media` holds
screenshots and videos, for example:

    media=[
        {"type": "image", "src": "assets/media/tonemap/aces.jpg", "caption": "ACES"},
        {"type": "video", "src": "assets/media/tonemap/compare.mp4"},
        {"type": "youtube", "id": "dQw4w9WgXcQ"},
    ]

Cards with no media show empty slots in the modal.
"""

GRAPHICS = [
    ("Color and post-processing", [
        dict(id="tonemap", title="Tone mapping", cvar="r_toneMapMode 0|1|2",
             short="Legacy rend2 filmic, ACES fitted or AgX-like conversion from HDR to the display. <code>r_toneMapDebug</code> shows them side by side.",
             sections=["Tone mapping"],
             how="The scene is rendered into a 16-bit floating-point buffer. Auto exposure measures the logarithmic average luminance of that buffer and smooths it over time. A single fullscreen pass then applies exposure, the selected curve and the display encoding; refractive surfaces use the same output transform, so both always agree."),
        dict(id="grading", title="Color grading", cvar="r_colorGrading 1",
             short="Applies a <code>.cube</code> LUT. A map can provide <code>maps/&lt;map&gt;.cube</code>; <code>r_colorGradingLUT</code> overrides it.",
             sections=["Color grading LUTs"],
             how="The <code>.cube</code> file is loaded into a 3D texture. After tone mapping, each pixel's color is used as a coordinate into that texture, and the looked-up color is blended with the original by the intensity setting."),
        dict(id="linear", title="Linear lighting", cvar="r_linearLighting 1",
             short="Lighting is computed in linear space instead of sRGB.",
             sections=["Linear lighting"],
             how="Textures and lightmaps are decoded from sRGB into linear values before lighting, so light is added and multiplied in physically proportional units. The result is encoded for the display only once, at the end of the frame, by the tone mapper."),
        dict(id="bloom", title="HDR bloom", cvar="r_bloom 1",
             short="Soft glow around genuinely bright sources, computed from scene brightness before tone mapping.",
             sections=["HDR bloom"],
             how="Glow and emissive surfaces write into a dedicated target next to the HDR scene. That target, optionally with the brightest scene pixels, is downsampled into a resolution pyramid and blurred on the way back up. The blurred light is added to the linear HDR image before exposure and tone mapping, so bloom follows the real brightness of its source."),
        dict(id="motionblur", title="Motion blur", cvar="r_motionBlur 1",
             short="Per-pixel camera and object motion blur with a shutter-angle model. HUD and menus stay sharp.",
             sections=["Velocity motion blur"],
             how="It reuses the temporal data rend2 keeps for SMAA T2x: a per-pixel velocity buffer, the previous frame's view projection and the previous entity matrices and bones. Each pixel is blurred along its own motion vector, scaled by the shutter angle and clamped; very large one-frame jumps are treated as camera cuts and not blurred."),
    ]),
    ("Lighting and shadows", [
        dict(id="gtao", title="GTAO and contact shadows", cvar="r_aoMode 2",
             short="Ground-truth ambient occlusion, with optional bent normals and specular occlusion, plus screen-space contact shadows.",
             sections=["GTAO", "Screen-space contact shadows"],
             how="Right after the depth prepass, fullscreen passes reconstruct positions from depth, search the horizon in several directions around each pixel and integrate the visible part of the hemisphere. The result is denoised and read by the main lighting pass. Contact shadows march a short ray toward the sun through the depth buffer to catch small occluders that shadow maps miss."),
        dict(id="forwardplus", title="Forward+", cvar="r_forwardPlus 1",
             short="Clustered dynamic lights instead of per-surface light lists.",
             sections=["Forward+ dynamic lighting"],
             how="The view frustum is split into clusters: screen tiles times logarithmic depth slices. Every frame the dynamic lights are assigned to the clusters they touch (with a compute shader on OpenGL 4.3), and the material shader reads the light list of its pixel's cluster. Shading stays forward; there is no G-buffer."),
        dict(id="ssgi", title="Screen-space GI", cvar="r_ssgi 1",
             short="One bounce of indirect light from what is on screen, mainly for dynamic lights.",
             sections=["Screen-space global illumination"],
             how="Dynamic lights and emissive surfaces write their outgoing light into a separate screen buffer. Rays are marched from each pixel through the depth buffer, gather that light where they hit, are accumulated over frames and denoised, and the result is added as diffuse indirect light. Baked lighting is not bounced again because the lightmaps already contain it."),
        dict(id="ambient", title="Ambient light on models", cvar="r_entityLightGrid 2",
             short="Light-grid lighting sampled across the body or per pixel, and directional ambient from cubemap probes.",
             sections=["Entity light-grid lighting", "Diffuse IBL / directional ambient"],
             how="The stock renderer lights a whole model from one light-grid sample at its origin. Here the grid is sampled at several body positions on the CPU, or per fragment on the GPU, so a character standing half in shadow is lit accordingly. Diffuse IBL derives directional irradiance from the runtime cubemaps and uses it to modulate the ambient term per direction."),
        dict(id="arealights", title="Area lights", cvar="r_ltcAreaLights 1",
             short="LTC area lights for lamps and panels, with automatic lamp detection, and line lights for sabers.",
             sections=["LTC area lights", "Automatic LTC lamp detection", "Saber line area lights"],
             how="Linearly Transformed Cosines turn the integral of a rectangle or line light over the GGX lobe into a lookup in two small precomputed tables plus a closed-form polygon integral. Area lights live in the Forward+ cluster lists. Lamp detection scans glowing surfaces at map load and fits rectangles to bright connected regions."),
        dict(id="spotlights", title="Spot lights", cvar="r_spotLights 1",
             short="Shadowed spot lights from effects, optionally with projected cookie textures.",
             sections=["Spot lights", "Spot-light cookies / gobos"],
             how="A dynamic light can be limited to a cone. It lights surfaces through the legacy dynamic light loop and through Forward+, renders one perspective shadow map, and is injected into the froxel fog with the same attenuation. A cookie is projected through the same frustum as the shadow and multiplies with it."),
        dict(id="sunshadows", title="Sun shadows", cvar="r_sunShadowMode 1",
             short="Stabilized, blended shadow cascades with better bias, optional PCSS softness and caching; fixes for character shadows.",
             sections=["Sun Shadows 2.0", "Character shadow improvements"],
             how="Cascades are fitted to bounding spheres and snapped to texels, so shadows don't shimmer when the camera moves, and neighbouring cascades blend across an overlap. PCSS estimates the blocker distance to widen the penumbra away from the contact point. The cache reuses static world depth between frames and only redraws moving casters."),
    ]),
    ("Materials and reflections", [
        dict(id="autopbr", title="Automatic PBR", cvar="r_autoPBR 0|1|2",
             short="Classifies legacy shaders by material and generates roughness variation for them.",
             sections=["Auto PBR material classification", "Automatic roughness variation", "Legacy shiny material conversion", "Diffuse BRDF"],
             how="At material registration, legacy stages without authored PBR data are classified from shader and texture names into classes such as metal, cloth, leather, skin and plastic, each with its own constant parameters. Roughness variation is derived from local detail of the diffuse texture and stored as a generated map. Authored material data always wins."),
        dict(id="pom", title="Parallax occlusion", cvar="r_pomSilhouette 1",
             short="Adaptive POM with self-shadowing and silhouettes.",
             sections=["Adaptive POM traversal", "POM self-shadowing", "Silhouette POM", "Automatic silhouette POM"],
             how="POM marches the view ray through the height field in texture space; the step count now depends on the view angle and fades out with distance. Self-shadowing marches a second ray from the hit point toward each light. Silhouette POM builds a thin shell of prisms over the surface, so the ray can leave the base polygon, change the outline and write displaced depth."),
        dict(id="skin", title="Skin", cvar="r_skinSSS 1|2",
             short="Subsurface scattering approximation for skin.",
             sections=["Skin subsurface scattering"],
             how="Skin surfaces are classified automatically and only their diffuse lighting is affected; specular, eyes and metal stay sharp. Mode 1 wraps the diffuse term around the terminator. Mode 2 writes skin diffuse into a separate buffer and blurs it in two separable passes with a kernel whose width follows depth, stopping at depth and normal edges."),
        dict(id="emissive", title="Emissive surfaces", cvar="r_autoEmissive 1",
             short="Glowing surfaces as light emitters, compatible with the original glow.",
             sections=["Emissive materials", "Automatic emissive compatibility"],
             how="Emission is a separate material channel, added after reflected light, AO and shadows, and written to the glow target used by bloom. Compatible stock glow and unlit additive stages are recognized on the CPU and exposed as emission sources, so SSGI and bloom see them without shader edits."),
        dict(id="ssr", title="Screen-space reflections", cvar="r_ssr 1",
             short="SSR combined with cubemaps; can include sabers and effects.",
             sections=["Screen-space reflections"],
             how="A hierarchical depth buffer is built from the scene, and reflection rays are traced through it at full or half resolution, then resolved by roughness and accumulated over frames. Where a ray finds the reflected surface on screen, SSR replaces part of the parallax-corrected cubemap reflection; elsewhere the cubemap stays. Sabers and effects are added analytically, so they reflect even when they are off screen."),
    ]),
    ("Volumetrics and sky", [
        dict(id="vfog", title="Volumetric fog", cvar="r_volumetricFog 2",
             short="Froxel fog lit by the sun, dynamic and spot lights. Has a compute path on OpenGL 4.3.",
             sections=["Froxel volumetric fog", "Height fog", "Volumetric density noise", "Directional baked light in volumetrics", "Rain suspension in froxel fog"],
             how="Fog is stored in a 3D grid aligned with the camera frustum (froxels). Each froxel gets density from the BSP fog volumes and other media, and in-scattered light from the light grid, the sun with its cascaded shadows and the dynamic lights with their shadows, weighted by a Henyey-Greenstein phase function. The grid is reprojected and blended with the previous frame, then integrated front to back; surfaces read the result as a 3D texture."),
        dict(id="media", title="Fog volumes and FX media", cvar="r_volumetricParticles 1",
             short="Local fog volumes, volumetric smoke from effects, glowing media and lit smoke sprites.",
             sections=["Local fog volumes", "Volumetric FX particles", "Volumetric emission", "Sprite particle lighting"],
             how="Fog volumes and opted-in effect particles are analytic shapes (spheres, ellipsoids, boxes) that are culled per depth slice and add their density, color and emission to the froxels they overlap. From there they are lit and shadowed exactly like the rest of the fog. Ordinary smoke sprites sample the same light field instead of using a fixed color."),
        dict(id="scattering", title="Scattering in dense media", cvar="r_volumetricSelfShadow 1",
             short="Per-medium color and phase, self-shadowing, multiple scattering and colored absorption.",
             sections=["Per-medium albedo and anisotropy", "Volumetric self-shadowing", "Approximate multiple scattering", "RGB extinction / wavelength-dependent absorption"],
             how="Media density is first written into its own volume. For each lit froxel a short ray is marched toward the sun (and the strongest dynamic lights) through that volume to compute how much light the medium itself absorbs. Multiple scattering adds back a few weaker, wider octaves of that light, following Oz 2013. RGB extinction stores optical depth and transmittance per color channel."),
        dict(id="atmosphere", title="Atmosphere", cvar="r_atmosphere 1",
             short="Physically based sky and aerial perspective.",
             doc="""Adds the air between the camera and distant surfaces, and optionally the sky itself. Distant terrain fades into haze and the sky gets a physically based color around the sun. The froxel fog keeps the local media; the atmosphere covers the long range beyond it.

**Requirements.** Works with or without froxel fog. A map sun direction is needed for the sun glow.

**Main controls**
- `r_atmosphere 0 | 1` — off / on.
- `r_atmosphereSky 0 | 1 | 2` — overlay on the skybox / blend with it / analytic sky.

**Other controls**
- `r_atmosphereSkyBlend <value>` — share of the analytic sky in mode 1.
- `r_atmosphereUnitScale <value>` — metres per world unit.
- `r_atmosphereAerialScale <value>` — aerial path length multiplier (8–30 for visible haze).
- `r_atmosphereAltitude <metres>` — ground altitude above sea level.
- `r_atmosphereGroundZ <z>` — world height of the ground; automatic by default.
- `r_atmosphereRayleigh`, `r_atmosphereMie`, `r_atmosphereOzone <value>` — density multipliers.
- `r_atmosphereMieG <g>` — haze anisotropy.
- `r_atmosphereSunColor 0 | 1 | 2` — map sun at the top of the atmosphere / at the ground / white.
- `r_atmosphereSunIntensity <value>`, `r_atmosphereSunSize <degrees>`, `r_atmosphereSunGlow <value>` — sun.
- `r_atmosphereStart <units>` — distance before which there is no aerial perspective.

**Debug**
- `r_atmosphereDebug` — debug views.
- `r_atmosphereInfo` — print the current state.""",
             how="Following Hillaire's model, small lookup textures are precomputed for transmittance and multiple scattering, and a sky-view texture is built each frame from the sun direction. The composite pass reads depth, computes the air path from the camera to each surface and applies transmittance and in-scattering analytically, before the froxel fog is composited."),
        dict(id="clouds", title="Clouds", cvar="r_clouds 1",
             short="Ray-marched volumetric clouds and cloud shadows.",
             doc="""Draws volumetric clouds in a layer high above the map, lit by the sun, moving with the wind, and optionally casting their shadows on the ground and into the fog. All noise is generated by the renderer; no assets are needed.

**Requirements.** Needs an outdoor map with a sky. Works best together with `r_atmosphere 1`.

**Main controls**
- `r_clouds 0 | 1` — off / on. Requires `vid_restart`.
- `r_cloudShadows 0 | 1` — cloud shadows on surfaces and fog.

**Other controls**
- `r_cloudScale 1 | 2 | 4` — march resolution divisor. Requires `vid_restart`.
- `r_cloudBase`, `r_cloudTop <metres>` — layer altitude above the ground.
- `r_cloudCoverage 0..1` — clear / weather map / overcast.
- `r_cloudDensity <value>` — extinction multiplier.
- `r_cloudAnisotropy <g>` — forward scattering lobe.
- `r_cloudWindSpeed <m/s>`, `r_cloudWindDir <degrees>` — wind.
- `r_cloudSteps <count>`, `r_cloudStepLength <metres>` — ray march.
- `r_cloudShadowSteps <count>` — light march taps.
- `r_cloudMSOctaves <count>` — multiple scattering octaves.
- `r_cloudDetail <value>` — erosion strength; `0` disables it.
- `r_cloudAmbient <value>` — sky ambient.
- `r_cloudTemporal <value>` — history weight; `0` disables it.
- `r_cloudMaxDistance <km>` — draw distance.
- `r_cloudShapeScale`, `r_cloudDetailScale`, `r_cloudWeatherScale <km>` — noise tile sizes.
- `r_cloudLegacy 0 | 1 | 2` — use of the map's legacy cloud layer.
- `r_cloudSkyPortal 0 | 1` — clouds on sky portal maps.
- `r_cloudShadowExtent <km>`, `r_cloudShadowInterval <frames>` — shadow map size and update rate.

**Debug**
- `r_cloudDebug` — debug views.

For lower cost use `r_cloudScale 4`, `r_cloudSteps 32` and `r_cloudDetail 0`.""",
             how="Clouds live in a spherical shell between two altitudes. The shell is ray marched only on sky pixels, at reduced resolution, with a light march toward the sun at each sample and an approximation of multiple scattering; the result is upscaled with a temporal history of its own. Cloud shadows are a top-down transmittance map that surfaces and fog sample."),
    ]),
    ("Water and weather", [
        dict(id="water", title="Water surface", cvar="r_waterSurface 1",
             short="PBR water with waves, tessellation, foam, flow, caustics, Snell's window and an underwater medium.",
             doc="""Replaces the stock water of the maps with a physically based surface: Fresnel reflection, depth-aware refraction, absorption and scattering in the liquid, sun and light glints. Water is found from the map's BSP data; no map is rebuilt.

**Requirements.** HDR is recommended.

**Main controls**
- `r_waterSurface 0 | 1` — stock / modern water. Requires `vid_restart`.
- `r_waterSnell 0 | 1` — Snell's window and total internal reflection when looking up from under water.

**Other controls**
- `r_waterSurfaceIOR <value>` — index of refraction.
- `r_waterSurfaceRoughness <value>` — base roughness; unresolved waves add to it.
- `r_waterSurfaceNormal <value>` — wave normal strength.
- `r_waterWind 0 | 1`, `r_waterWindScale <value>`, `r_waterWindTransition <seconds>` — wave response to wind and weather.
- `r_waterSurfaceRefraction <value>` — refraction offset scale.
- `r_waterSurfaceDepthReject 0 | 1` — reject foreground objects from the refraction.
- `r_waterSurfaceReflection <value>` — reflection scale.
- `r_waterSurfaceSSR <value>` — weight of water reflections over the cubemap.
- `r_waterSurfaceAbsorption <value>`, `r_waterSurfaceDepthScale <value>` — absorption and path length.
- `r_waterSnellOptics 0 | 1 | 2` — sun transport through the surface; 2 also shows the atmosphere through Snell's window.
- `r_waterSurfaceExperimental 0 | 1` — name-based water detection.

**Debug**
- `r_waterSurfaceDebug`, `r_waterSnellDebug`, `r_waterWindDebug` — debug views.
- `r_waterInfo [surfaces]` — print liquid brushes and water shader decisions.""",
             how="The surface normal comes from a deterministic sum of directional waves plus a tiled slope texture filtered so that unresolved waves raise roughness instead of aliasing; near the camera the mesh is tessellated. Reflection uses water SSR, then the cubemap. Refraction offsets the scene behind the surface, rejects foreground objects by depth, and attenuates color along the refracted path through the liquid."),
        dict(id="waterparticles", title="Underwater particles", cvar="r_waterParticles 1",
             short="Specks and bubbles under water.",
             doc="""Adds a few dozen to a few hundred tiny specks and small bubbles around the camera while it is under water. Near specks read as particles, far ones fade into the water color, which gives a sense of depth.

**Requirements.** The camera must be inside a water or slime volume.

**Main controls**
- `r_waterParticles 0 | 1` — off / on.

**Other controls**
- `r_waterParticlesDensity 0..8` — density scale.
- `r_waterParticlesSize 0.25..4` — size scale.
- `r_waterParticlesSpeed 0..4` — drift and rise speed.
- `r_waterParticlesBubbles 0..1` — share of bubbles; `-1` uses the water profile.
- `r_waterParticlesDistance <units>` — radius around the camera.
- `r_waterParticlesProfile -1..3` — auto / clean / murky / dirty water.

**Debug**
- `r_waterParticlesDebug 1..3` — placement, no medium in front, statistics.

With `r_waterParticles 0` nothing is simulated or drawn.""",
             how="Particles are simulated on the CPU in a sphere around the camera and only spawn where they are inside the water body and visible. They are drawn as small billboards lit from the light grid, after the other transparent surfaces, and fogged by the water medium like any other transparent draw."),
        dict(id="rain", title="Rain", cvar="r_rainStreaks 1",
             short="Lit rain streaks, distance LOD and splashes where drops hit geometry.",
             sections=["Modern rain streaks", "Rain streak distance LOD", "Rain impact splashes", "Weather chunk culling"],
             how="Drops are simulated on the GPU. Each one is drawn as a premultiplied streak stretched along its velocity and lit from the light grid, the sun and nearby lights. A splash is created in the frame when a drop crosses the top-down weather depth map, the first rain-exposed surface below it, without CPU traces or readbacks. Far away, streaks hand over to rain haze in the froxel fog."),
        dict(id="wetness", title="Wet surfaces", cvar="r_weatherWetness 1",
             short="Surfaces exposed to rain get wet, with puddles, ripples and runoff.",
             sections=["Wet surfaces", "Procedural puddles", "Height-aware puddles", "Puddle ripples", "Water runoff", "Per-material weather response"],
             how="Rain exposure is read per pixel from the same top-down weather depth map that occludes rain, so roofs keep the ground below them dry. Exposed surfaces get darker albedo, lower roughness and flatter normals by material class. Puddles, ripples and runoff are procedural and only change the normal and roughness, so direct light, cubemaps and SSR all react to them."),
        dict(id="lenswater", title="Lens water", cvar="r_rainLens 1",
             short="Drops and water film on the camera lens.",
             sections=["Rain on the camera lens"],
             how="A small CPU simulation tracks drops that pin, merge and slide, plus a wetness film and trails. It is rasterized into a lens field that the composite pass uses to refract and defocus the scene, with reflections from the nearest cubemap and glints from the sun and lights."),
    ]),
    ("Foliage and loading", [
        dict(id="grass", title="Grass cards", cvar="r_grassCardMode 3",
             short="Surface sprite grass drawn as two- or three-card tufts.",
             sections=["Grass card geometry", "Automatic foliage classification"],
             how="Stock surface-sprite grass is rebuilt at load into world-fixed crossed cards packed into static vertex buffers, instead of billboards that turn with the camera. In adaptive mode the third card fades out with distance."),
        dict(id="wind", title="Wind", cvar="r_foliageWind 1",
             short="A shared wind field for grass and plants; <code>r_leafFlutter</code> adds leaf motion on models.",
             sections=["Coherent foliage wind", "Leaf flutter"],
             how="Grass offsets come from a world-space breeze field, so neighbouring plants move together in broad gusts with smaller local motion on top. Leaf flutter adds a small high-frequency displacement and normal wobble only to model surfaces classified as leaves; trunks and branches stay still."),
        dict(id="interaction", title="Interaction", cvar="r_foliageInteraction 1",
             short="Characters bend grass and plants they walk through.",
             sections=["Plant fronds (wind and contact)", "Character / foliage interaction"],
             how="The game passes a few vertical capsules per character to the renderer. Grass reads a small state texture around the player that stores contact and recovery, and curves from the root. Fern and bush fronds are chains of rigid segments solved on the CPU for nearby plants, so a frond swings around its base as a whole instead of stretching."),
        dict(id="shadercache", title="Shader cache", cvar="r_shaderProgramCache 1",
             short="Stores compiled shader programs on disk to shorten startup and <code>vid_restart</code>.",
             sections=["Shader program cache"],
             how="Linked GLSL programs are saved with <code>GL_ARB_get_program_binary</code> and loaded on the next start instead of being compiled. Per-draw shader permutations are built only when a level actually needs them. Entries for a different driver or changed shaders are discarded automatically."),
    ]),
]

FORCE = [
    dict(id="telekinesis", title="Telekinesis", color="blue", cvar="setForceTelekinesis 3",
         short="Hold an NPC or an object and move it in space, similar to Grip in The Force Unleashed but without damage. Releasing it while moving throws it. Movement keys move the held target.",
         doc="""Grabs an NPC or an object and lets you move it in space, like Grip in The Force Unleashed. Unlike Force Grip it does no damage to the NPC.

- If you release the NPC or object while moving it, it is thrown in that direction.
- Works together with Push and Pull.
- Physical objects added with `add_object` can be moved as well.
- You can't move while holding something: the movement keys move the held target.

**Command**
- `setForceTelekinesis 3`"""),
    dict(id="soar", title="Soar", color="green", cvar="setForceSoar 3",
         short="Stick to walls, start wall runs and flips in mid-air, jump again after touching a wall, and stay in the air for a few attacks.",
         doc="""Gives more aerial movement, but needs some practice.

- You can stick to a wall, not only push off it.
- Wall runs and wall flips can be started when you touch a wall in mid-air.
- A second jump becomes available after touching a wall (wall runs and wall flips).
- You can hang in the air for a few attacks before landing.

**Command**
- `setForceSoar 3`"""),
    dict(id="deflect", title="Deflect", color="amber", cvar="setforcedeflect 3",
         short="Stop blaster bolts without a saber. Level 1 freezes a bolt, level 2 lets you move while doing it, level 3 sends it back.",
         doc="""Protects you from blaster fire without a saber, in the way Kylo Ren stops a bolt in Episode IX.

- Level 1: freezes a blaster bolt; it disappears afterwards.
- Level 2: you can also move while holding it.
- Level 3: sends the bolt back at the enemy.

**Command**
- `setforcedeflect 3`"""),
    dict(id="chainlightning", title="Chain Lightning", color="purple", cvar="setForceChainLightning 3",
         short="Lightning that passes from one NPC to the next: up to 2, 5 or any number of targets. Bind it with <code>+force_chain_lightning</code>.",
         doc="""Lightning that holds one NPC for two seconds and passes to the next one after one second. It is also triggered by Force Grip 4 + Lightning, but can be used as a separate power.

- Level 1: up to 2 NPCs.
- Level 2: up to 5 NPCs.
- Level 3: no limit.

**Commands**
- `setForceChainLightning 3`
- `bind J +force_chain_lightning`"""),
]
