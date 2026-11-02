# Materials and surface detail

[All rendering settings](../rendering-features.md)

How surfaces reflect light: metal vs cloth vs plastic, glossy highlights, glowing parts and depth in flat textures.

## Automatic materials from stock textures (Auto PBR)

Stock JKA textures are only color pictures: nothing tells the renderer whether a surface is metal, leather or cloth. Rend2 therefore lights them all as the same fully matte, non-metallic material. Auto PBR guesses the material from texture and model names — `metal`, `leather`, `cloth`, `helmet`, droids, stormtroopers, weapons and so on — and gives each kind its own glossiness and metalness. Armor then shines differently from a robe under the same lamp.

Textures that already come with material maps (PBR texture packs) are never changed.

- `r_autoPBR 0` — **0** original Rend2: everything fully matte, non-metal. **1** everything slightly less matte, still non-metal. **2** guess the material per texture.

**Compare:** `r_autoPBRCompare` — 1 split screen (Rend2's material left), 2 Rend2's material.

**How it works.** When a material loads, its shader and texture names are split into words and matched against lists: material words (`steel`, `leather`, `robe`…), `models/weapons2` (metal), whole characters (droids → metal, troopers and vehicles → painted plastic, Wookiees → fur) and body parts (`head`, `hands` → skin; `torso`, `legs` → cloth). Each class has a fixed roughness and metalness. Nothing is computed per frame.

**Debug**

- `r_autoPBRDebug 1` — color by guessed class: yellow metal, salmon skin, blue cloth, brown leather, green plastic, purple hair, grey generic, white authored.
- `r_autoPBRDebug 2` — where the values came from: authored map, automatic guess, converted legacy shine. Red means the surface is still drawn with the old vertex lighting (see [shiny models](#shiny-stock-models-troopers-droids-weapons)).
- `r_pbrDumpMaterials [used|all|auto|authored|gouraud|<class>]` — list materials with their class and values.

Design notes: [rend2-auto-pbr.md](../rend2-auto-pbr.md).

## Shiny stock models (troopers, droids, weapons)

In Rend2, about 130 stock shaders that use the original "shiny" effect are still drawn the old way, with lighting computed only at mesh vertices: every stormtrooper and droid, Boba Fett, most weapons, vehicles and some map objects. They get no normal maps, no sun shadows on themselves and no reflections, which is why they look flat next to everything else. This option converts them to the modern per-pixel path, and their old highlight mask decides which parts are glossy or metallic.

**Cost:** light.

- `r_autoPBRConvert 0` *(restart)* — 1 convert.

Combine it with `r_autoPBR 2` so the converted parts get the right material class.

No compare cvar: the shaders are rebuilt when they load, so only `vid_restart` switches it.

**How it works.** The old shaders draw a lit color layer plus an additive "specular" layer masked by a `_spec` texture, and sometimes a fake chrome layer. The conversion removes those layers and turns the `_spec` mask into a per-pixel glossiness/metal map: bright areas of the mask become glossier and, on metal classes, metallic. With `r_cubeMapping 1`, the fake chrome layer is dropped because real reflections replace it.

## Roughness variation

With Auto PBR, a whole texture gets one glossiness value, so a wall or armor plate shines evenly. This adds subtle dull and glossy patches derived from the texture's own detail. It does not turn bright pixels into metal.

- `r_autoPBRRoughness 0` — 1 on. Turning it on needs `vid_restart`; needs `r_autoPBR 1` or `2`.

**Compare:** `r_autoPBRRoughnessCompare` — 1 split screen (one roughness per texture left), 2 off.

**How it works.** At load time the renderer measures local contrast in each color texture — creases, scratches, painted detail — and turns it into a glossiness map that varies about ±0.1 around the class value. Materials with authored maps or converted shine are left alone. This adds loading time, not frame time.

**Debug**

- `r_autoPBRDebug 3` — final roughness in grey; blue means the surface doesn't use auto roughness.

## Glowing surfaces

Stock lamps, screens, lava and control panels are drawn with "glow" layers that the newer features don't see as light. This option marks them as light-emitting surfaces, so HDR [bloom](post-processing.md#bloom), [bounced light](lighting.md#bounced-light-ssgi) and [area lights](lighting.md#lamp-shaped-lights-ltc) can use them.

**Cost:** light, on glowing surfaces only.

- `r_autoEmissive 0` — 1 on.

**Compare:** `r_autoEmissiveCompare 2` — glow layers as plain added color. No split screen: a layer is drawn one way or the other for the whole surface.

**Mod authors:** give a stage its own glow map; black parts don't glow.

~~~text
emissiveMap textures/example/panel_e
emissiveColor 1 0.25 0.05
emissiveScale 4
~~~

`emissiveMap` is the glow image, `emissiveColor` tints it, `emissiveScale` sets brightness (or `emissiveNits` for real units with `r_pbrPhysical 1`).

**How it works.** Shader stages with the legacy `glow` keyword, and standalone unlit additive stages, are exported as emission instead of plain added color. Emission is a brightness of its own, added after lighting and shadows, and written to the bloom buffer.

**Debug**

- `r_emissiveDebug 1..3` — where the emission comes from, its brightness in false color, and classification confidence.

Design notes: [rend2-emissive-materials.md](../rend2-emissive-materials.md).

## Stable highlights

Fine normal-map detail on glossy surfaces makes highlights sparkle and crawl when you move. Specular anti-aliasing widens highlights that are too small for a pixel, so armor and polished floors stay calm. Energy compensation keeps rough metal from looking too dark, and takes the light a surface reflects off its diffuse part so it isn't counted twice.

**Cost:** light.

- `r_specularAA 0` *(restart)* — 1 on.
- `r_brdfEnergyCompensation 0` — 1 on: multiple-scattering compensation and the diffuse / specular energy split. 0 is Rend2's original BRDF.

**Compare:** `r_specularAACompare`, `r_brdfEnergyCompensationCompare` — 1 split screen (original left), 2 off.

**How it works.** The renderer estimates how much the surface normal varies inside one pixel (from screen derivatives and the normal map's mip levels) and raises the roughness of that pixel to match. The simple reflection model loses light on rough surfaces that would really bounce more than once inside the surface; energy compensation adds that light back.

- `r_specularAAStrength 0.15` — how much normal variation widens highlights.
- `r_specularAAMaxAdded 0.25` — limit on the added roughness.
- `r_pbrLegacyRoughness 1` — convert older glossiness conventions (Auto PBR, spec-gloss maps) to one roughness scale; 0 is a comparison mode.

**Debug**

- `r_specularAADebug 1..6` — material roughness, material alpha, effective roughness, added roughness, normal variation, effective alpha.
- `r_pbrEnergyDebug 1..4` — diffuse, specular and total energy, lost energy.

Design notes: [rend2-specular-aa.md](../rend2-specular-aa.md).

## Matte surfaces

Changes how rough matte surfaces — cloth, plaster, stone — catch light. With the Burley model rough surfaces brighten a little toward grazing angles and smooth ones darken slightly at their edges, which looks less like plastic. The difference is subtle.

- `r_diffuseBRDF 0` — 0 Lambert (the classic model), 1 Burley/Disney.

**Compare:** `r_diffuseBRDFCompare` — 1 split screen (Lambert left), 2 Lambert.

## Physical light units

Mostly for mod authors. Normally every light, the sun and glowing surfaces use the game's own unrelated brightness scales. This mode puts them on one real-world scale (lux, candela, nits), so authored lights and materials can be given real values. Stock content is converted to keep its look, but the image can change a little. Leave it at 0 if you only play stock content.

- `r_pbrPhysical 0` *(restart)* — 1 physical units.

No compare cvar: every light is converted when the map loads.

**How it works.** Game distances are converted to metres and legacy light values to physical units around a reference brightness, then scaled to the renderer's internal range. These are calibration settings; they don't recover the real brightness of the lamps the original mappers imagined.

- `r_pbrWorldUnitsPerMeter 33.333333` *(restart)* — game units per metre, used for light falloff and emitter size.
- `r_pbrLegacyReference 1` *(restart)* — reference brightness for converting legacy lights.
- `r_pbrPhysicalToScene 0.0001` *(restart)* — physical units to internal brightness.
- `r_pbrSunIlluminance 0` — sun brightness in lux; 0 converts the map's own sun.
- `r_emissiveLegacyNits 1000` *(restart)* — nits labelled for a legacy `emissiveScale 1` in debug views.

**Debug**

- `r_pbrInfo` — print the calibration.
- `r_pbrLight` — inspect or test calibrated lights; run without arguments for syntax.

Design notes: [rend2-physical-pbr.md](../rend2-physical-pbr.md).

## Relief detail (POM)

Makes cracks, brick joints and panel grooves look recessed instead of painted on, and shift correctly as you move. It only works on textures that come with a height map (`_nh` images from texture packs, or the `heightMap` shader keyword). Stock JKA textures have none, so without such a pack nothing changes.

**Cost:** medium on large surfaces close to the camera.

- `r_parallaxMapping 0` — 1 on.
- `r_pomAdaptiveSteps 0` — 1: spend fewer samples when looking straight at a surface and more at shallow angles, where steps would show. Usually better quality for the same cost.
- `r_pomFadeStart 0`, `r_pomFadeEnd 0` — distances where relief starts to flatten and where it switches off (plain normal mapping). 0 = never.

**How it works.** For each pixel the shader walks a short ray into the texture's height map until it hits the surface, then draws the texture from that point. More steps remove stair-step artifacts at shallow angles.

- `r_pomMinSteps 8`, `r_pomMaxSteps 32` — ray steps when looking straight at a surface / along it (adaptive mode).
- `r_pomBinarySteps 8` — final refinement of the hit point.

**Debug**

- `r_pomDebug 1..8` — height, displaced texture coordinates, ray depth, sun / local-light self-shadow, view steps, shadow steps, distance fade. Turning it on needs `vid_restart`.

Design notes: [rend2-pom.md](../rend2-pom.md).

## Shadows inside relief

Raised parts of a POM texture cast shadows into its grooves, so brick and carved stone show depth when a light moves across them. Needs `r_parallaxMapping 1` and height-mapped textures, as above.

**Cost:** medium to heavy with many local lights.

- `r_pomSelfShadow 0` *(restart)* — 1 on.
- `r_pomSelfShadowLightMode 1` — which lights cast relief shadows: **0** sun only, **1** sun + the strongest nearby light, **2** sun + the strongest few (`r_pomSelfShadowMaxLocalLights`), **3** all lights.
- `r_pomSelfShadowStrength 1` — 0 none … 1 full.

**Compare:** `r_pomSelfShadowCompare` — 1 split screen (no relief shadows left), 2 off.

**How it works.** After finding the visible point in the height map, the shader walks a second short ray toward each light and checks whether higher relief blocks it. Every extra light costs another ray.

- `r_pomSelfShadowMaxLocalLights 2` — lights for mode 2.
- `r_pomSelfShadowSteps 12` — samples per shadow ray.
- `r_pomSelfShadowSoftness 4` — edge hardness; higher is harder.
- `r_pomSelfShadowBias 0.02` — start offset of the shadow ray; raise it if grooves get speckled shadow.

**Debug**

- `r_pomDebug 4`, `5` — sun / local-light self-shadow.
- `r_pomDebugFreezeLight 1` — freeze the sun direction for comparisons.

## Relief silhouettes

Ordinary POM only fakes depth inside a surface; its outline stays flat. This draws the relief as a thin shell, so rocks and broken panels look uneven at their edges too, and the shell casts sun shadows. Far away it falls back to ordinary POM. Needs `r_parallaxMapping 1` and height-mapped textures.

**Cost:** heavy on large surfaces close to the camera.

- `r_pomSilhouette 0` *(restart)* — 1 on, for materials that ask for it (`pomSilhouette` keyword).
- `r_autoPOMSilhouette 1` — console command: also use it on every material that has an ordinary POM height map. Run after `r_pomSilhouette 1` and a map load; saved in `r_autoPOMSilhouetteMode`.
- `r_pomSilhouetteDistance 512` — shells are drawn this close; farther surfaces use ordinary POM.

**Compare:** `r_pomSilhouetteCompare` — 1 split screen (ordinary POM left), 2 ordinary POM.

**Mod authors:** add the shader keyword `pomSilhouette` and a stage with `normalHeightMap`, `parallaxDepth` and `parallaxBias`. Optional `pomSilhouetteDistance` and `pomSilhouetteSteps` override range and quality per shader. Use it for deep edge relief and keep ordinary POM for flat detail.

**How it works.** The renderer extrudes a shell above the mesh, traces the height map through it and discards empty parts. Unlike ordinary POM this can change the visible outline.

- `r_pomSilhouetteFade 96` — width of the crossfade to ordinary POM.
- `r_pomSilhouetteSteps 12`, `r_pomSilhouetteMaxSteps 48` — ray steps looking straight / along the surface.
- `r_pomSilhouetteViewDependence 1` — how fast steps grow toward shallow angles.
- `r_pomSilhouetteBinarySteps 6` — hit refinement.
- `r_pomSilhouetteShadows 1` — shells cast into the sun shadows.
- `r_pomSilhouetteContactShadows 0` — 1: apply contact shadows to shell pixels (tends to blacken grooves).
- `r_autoPOMSilhouette <shader> 0|1|default`, `r_autoPOMSilhouette clear` — per-shader override (a trailing `*` matches a prefix) and clearing them.
- `r_autoPOMSilhouetteMode 0` — stores the global choice of `r_autoPOMSilhouette`; set it with the command.

**Debug**

- `r_pomSilhouetteDebug 1..8`, `10`, `11` — shell, wireframes, walls, discarded pixels, hit depth, step count, depth, normal (the old split view 9 is `r_pomSilhouetteCompare 1`).
- `r_pomSilhouetteInfo` — shell counts, memory, fallbacks, last-frame statistics.
- `r_autoPOMSilhouette list` — candidates and why others were rejected; `r_autoPOMSilhouette <shader>` — one shader's state.

Design notes: [rend2-silhouette-pom.md](../rend2-silhouette-pom.md).
