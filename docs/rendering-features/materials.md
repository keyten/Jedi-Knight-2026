# Materials and surface detail

[All rendering features](../rendering-features.md)

Controls marked `vid_restart` need a renderer restart. Debug views use 0 to return to normal unless stated otherwise. Performance labels are rough relative costs, not benchmark guarantees.

## Material classification (PBR)

Stock armor, fabric, leather and plastic can react differently to the same light instead of sharing one generic shiny response. PBR means physically based rendering: roughness and material type help decide how light reflects.

**Main controls**

- `r_autoPBR 0 | 1 | 2` — legacy fallback / generic dielectric / heuristic material classes.

**Debug**

- `r_autoPBRDebug 1` — material classes.
- `r_autoPBRDebug 2` — source of the PBR parameters.
- `r_pbrDumpMaterials [used|all|auto|authored|gouraud|<class>]` — list registered material sources and values.

The renderer classifies legacy material names and supplies missing material parameters. Authored roughness or metal/specular maps take priority. Classification happens when materials load, with no meaningful frame-time cost. [Details](../rend2-auto-pbr.md).

## Automatic roughness detail

*Requires `r_autoPBR 1` or `2`, because this fills in roughness only for legacy materials without authored material maps.*

Adds small dull and glossy variations to stock surfaces, so a wall or armor panel is less uniformly shiny. It does not turn bright pixels into metal.

**Main controls**

- `r_autoPBRRoughness 0 | 1` — constant class roughness / generate an automatic roughness map. Turning it on requires `vid_restart`.

**Debug**

- `r_autoPBRDebug 3` — final generated roughness in grey; blue means the material is not using auto roughness.
- `r_pbrDumpMaterials` — also reports generated roughness maps and generation time.

Texture detail is converted into a restrained roughness map at load time. Existing authored material data is kept; generation adds loading work rather than a new rendering pass. [Details](../rend2-auto-pbr.md).

## Legacy shiny materials

Old shiny shaders can use the newer material response while keeping their intended highlights. This is most visible on polished floors, metal panels and armor.

**Main controls**

- `r_autoPBRConvert 0 | 1` — keep the old vertex-lit path / convert compatible shaders to per-pixel PBR. Requires `vid_restart`.

**Debug**

- No separate debug controls.

The renderer converts compatible legacy specular settings into the shared material parameters instead of drawing an unrelated extra shine layer. It adds little work to ordinary material shading.

## Matte-surface lighting

Changes how broad light spreads across rough surfaces such as cloth, painted walls and stone. This can soften the flat or plastic look of the original lighting.

**Main controls**

- `r_diffuseBRDF 0 | 1` — Lambert / Burley-Disney diffuse.

**Debug**

- No separate debug controls.

A BRDF is a rule for how a surface reflects light. The selected diffuse rule handles rough matte surfaces differently from the simple legacy diffuse response.

## Glowing materials

Lets glowing panels and effect surfaces carry actual scene brightness, so bloom and reflected/bounced light can respond to them. Enabling support alone does not add glow maps to every stock texture. Light performance cost on affected surfaces.

**Main controls**

- `r_autoEmissive 0 | 1` — legacy behavior / expose compatible legacy glow stages as emissive sources.

**Debug**

- No separate debug controls.

### [Autodetectable] Existing glow stages

Set `r_autoEmissive 1` to reuse compatible stock glow and safe additive stages. This works without changing those assets.

### [For mod authors] Authored glow maps

Add these inside a material stage; black parts of the map do not glow:

~~~text
emissiveMap textures/example/panel_e
emissiveColor 1 0.25 0.05
emissiveScale 4
~~~

`emissiveMap` chooses the image, `emissiveColor` tints it, and `emissiveScale` sets brightness.

Emission is a surface brightness term separate from reflected light. Compatible old glow stages can supply it automatically; authored maps can define it directly. [Details](../rend2-emissive-materials.md).

## Shared light and material brightness

Makes sun, lamps and glowing surfaces use a common brightness scale instead of unrelated multipliers. Existing maps use a compatibility conversion; switching modes can change the overall look. No extra image pass is added.

**Main controls**

- `r_pbrPhysical` — 0 exact Rend2 compatibility, 1 unified scene-referred physical PBR contract (vid_restart). Default `0`; `vid_restart`.
- `r_emissiveLegacyNits` — Reference-white nits represented by legacy emissiveScale 1 in physical mode. Default `1000`; `vid_restart`.
- `r_pbrLegacyReference` — Reference brightness used to convert legacy lights. Default `1`; `vid_restart`.
- `r_pbrPhysicalToScene` — Scale from physical light units to the stored scene brightness. Default `0.0001`; `vid_restart`.
- `r_pbrSunIlluminance` — Physical sun illuminance in lux; 0 migrates the map sun. Default `0`.
- `r_pbrWorldUnitsPerMeter` — Game-world units per metre, used for physical distance and light size. Default `33.333333`; `vid_restart`.

**Debug**

- `r_emissiveDebug` — Emissive debug: 1 provenance, 2 false-color nits, 3 confidence. Default `0`.
- `r_pbrInfo` — Print physical PBR calibration and unit state.
- `r_pbrLight` — Inspect/test calibrated physical lights; run without arguments for syntax.

`r_pbrPhysical` converts game distances and legacy light values into a shared scene-light scale. These are calibration controls, not a way to recover the original map designer’s real lamp wattage. [Details](../rend2-physical-pbr.md).

## Stable highlights and roughness

Reduces sparkling on fine normal-map detail and keeps rough metal from becoming too dark. Look at armor and glossy floors while moving the camera. Light performance cost.

**Main controls**

- `r_pbrLegacyRoughness` — Convert older roughness conventions to the shared material scale; 0 is an alternative comparison interpretation. Default `1`.
- `r_specularAA` — Reduce sparkling in glossy normal-map detail. Default `1`; `vid_restart`.
- `r_brdfEnergyCompensation` — Restore light lost by the simplified glossy reflection; 0 disables only that restoration. Default `1`.
- `r_specularAAMaxAdded` — Maximum extra roughness from highlight antialiasing. Default `0.25`.
- `r_specularAAStrength` — How strongly fine normal detail broadens glossy highlights. Default `0.15`.

**Debug**

- `r_pbrEnergyDebug` — Views 1..5 inspect matte/glossy/combined energy and compare energy conservation. Default `0`.
- `r_specularAADebug` — Specular AA debug: 1 material roughness, 2 material alpha, 3 effective roughness, 4 added roughness, 5 fine normal-map variation, 6 effective alpha. Default `0`.

GGX is the shape used for many glossy highlights. Specular antialiasing slightly broadens highlights that are too fine for a pixel; energy compensation returns light lost by its simplified reflection model. [Details](../rend2-specular-aa.md).

## Relief detail (POM)

*Requires parallax mapping and a material height map, because the renderer needs surface-depth information to draw grooves.*

Makes cracks, brick joints and panel grooves appear recessed instead of painted flat. Compared with the original game, nearby textured surfaces can look deeper. Medium performance cost on large close-up surfaces.

**Main controls**

- `r_pomAdaptiveSteps 0 | 1` — legacy fixed traversal / angle-adaptive traversal.
- `r_pomMinSteps <count>` — linear samples at normal incidence.
- `r_pomMaxSteps <count>` — linear samples at grazing angles.
- `r_pomBinarySteps <count>` — final hit refinement.
- `r_pomFadeStart <distance>` — distance where relief begins flattening.
- `r_pomFadeEnd <distance>` — distance where POM becomes normal mapping only.

**Debug**

- No separate debug controls.

Parallax occlusion mapping follows a short ray through a texture height field. Adaptive stepping spends fewer samples looking straight at a surface and more at a grazing angle. [Details](../rend2-pom.md).

## Shadows inside relief detail

*Requires working POM height detail, because the shadows are traced through that same height map.*

Lets raised parts of a material shadow their own grooves. Turning a light across brick, stone or carved panels reveals depth that the original flat surface cannot shadow. Medium to heavy performance cost with many local lights.

**Main controls**

- `r_pomSelfShadow 0 | 1` — disable / enable relief self-shadowing. Requires `vid_restart`.
- `r_pomSelfShadowLightMode 0..3` — sun only / sun + strongest local / sun + limited local lights / all local lights.
- `r_pomSelfShadowMaxLocalLights <count>` — local-light budget for mode 2.
- `r_pomSelfShadowSteps <count>` — samples per shadow ray.
- `r_pomSelfShadowStrength <value>` — global shadow strength.
- `r_pomSelfShadowBias <value>` — ray-start bias inside the height field.
- `r_pomSelfShadowSoftness <value>` — penetration-to-shadow scale.

**Debug**

- `r_pomDebug 1..8` — height, displaced UV, hit depth, sun/local self-shadow, sample counts and distance fade. Requires `vid_restart` when enabling this debug view.
- `r_pomDebugFreezeLight 0 | 1` — freeze the sun direction for comparison.

After finding the visible point in the height field, the shader checks towards the light for higher material that blocks it. Each extra light can need another short trace. [Details](../rend2-pom.md).

## Relief silhouettes

*Requires `r_pomSilhouette 1`, `r_parallaxMapping 1`, normal mapping and suitable height data, because this draws an expanded surface around the original mesh.*

Makes chunky rocks and broken panels look uneven around their edges, not just inside their texture. Distant surfaces fall back to ordinary relief mapping. Heavy performance cost on large close-up areas.

**Main controls**

- `r_pomSilhouette 0 | 1` — disable / enable shell rendering. Requires `vid_restart`.
- `r_pomSilhouetteDistance <units>` — shell range; farther surfaces use ordinary POM.
- `r_pomSilhouetteFade <units>` — crossfade width.
- `r_pomSilhouetteSteps <count>` — normal-angle ray samples.
- `r_pomSilhouetteMaxSteps <count>` — grazing-angle ray samples.
- `r_pomSilhouetteBinarySteps <count>` — hit refinement.
- `r_pomSilhouetteViewDependence <value>` — how quickly samples increase toward grazing angles.
- `r_pomSilhouetteShadows 0 | 1` — displaced shells cast into sun cascades.
- `r_pomSilhouetteContactShadows 0 | 1` — apply screen-space sun contact shadows to shell pixels.
- `r_autoPOMSilhouette 0 | 1` — disable / enable automatic mode globally.
- `r_autoPOMSilhouette <shader> 0 | 1 | default` — per-shader override; a trailing `*` acts as a prefix.
- `r_autoPOMSilhouette clear` — remove per-shader overrides.
- `r_autoPOMSilhouetteMode` — Silhouette POM for every material with an ordinary POM height map (set with the r_autoPOMSilhouette command, needs r_pomSilhouette 1). Default `0`.

**Debug**

- `r_pomSilhouetteDebug 1..11` — shell, wireframe, walls, discarded pixels, virtual depth, sample count and comparison views.
- `r_pomSilhouetteInfo` — print shell counts, memory, fallbacks and last-frame statistics.
- `r_autoPOMSilhouette list` — list active candidates and fallback reasons.
- `r_autoPOMSilhouette <shader>` — print matching shader state.

### [Autodetectable] Existing height-mapped surfaces

Use `r_autoPOMSilhouette 1` after enabling silhouette support and loading a map. Only suitable materials that already have a height map qualify; this cannot create relief from an ordinary color texture. The command stores its global choice in `r_autoPOMSilhouetteMode`.

### [For mod authors] Explicit relief shells

Add the shader-level `pomSilhouette` keyword and a stage with `normalHeightMap`, `parallaxDepth` and `parallaxBias`. Optional shader-level `pomSilhouetteDistance` and `pomSilhouetteSteps` override its range and sampling. Use this for deep edge relief, and keep ordinary POM for flatter detail.

The renderer draws a thin shell and traces its height field, discarding empty parts. Unlike ordinary POM, this can change the visible outline; the prepared shell is limited to compatible surfaces. [Details](../rend2-silhouette-pom.md).
