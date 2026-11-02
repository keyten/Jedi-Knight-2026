# Lighting

[All rendering features](../rendering-features.md)

Controls marked `vid_restart` need a renderer restart. Debug views use 0 to return to normal unless stated otherwise. Performance labels are rough relative costs, not benchmark guarantees.

## Soft corner shading (GTAO)

*Requires the depth/normal information from the world view, because this estimates nearby occlusion from visible surfaces.*

Makes wall joints, feet, crates and small recesses feel grounded instead of evenly lit. This adds local soft shading that the original baked lighting cannot update for moving objects. Medium performance cost.

**Main controls**

- `r_aoMode -1 | 0 | 1 | 2` — follow legacy `r_ssao` / off / legacy SSAO / GTAO.
- `r_aoApply -1 | 0 | 1` — automatic / legacy application / indirect-light-only application.
- `r_aoMultiBounce 0 | 1` — preserve more indirect light on bright surfaces.
- `r_aoLightmapFraction <value>` — share of baked light treated as indirect.
- `r_aoSpecOcclusion -1 | 0 | 1 | 2` — auto / scalar / Lagarde / bent-normal cone specular occlusion.
- `r_gtaoQuality 0..3` — low / medium / high / ultra.
- `r_gtaoHalfRes 0 | 1` — full / half resolution. Requires `vid_restart`.
- `r_gtaoRadius <units>` — world-space radius.
- `r_gtaoFalloff <value>` — distance falloff.
- `r_gtaoThickness <value>` — thin-occluder compensation.
- `r_gtaoPower <value>` — visibility contrast.
- `r_gtaoDenoise 0..3` — edge-aware denoise passes.
- `r_gtaoBentNormals 0..1` — bent-normal strength for indirect lighting.

**Debug**

- `r_aoCompare 0 | 1` — split-screen legacy SSAO vs GTAO.
- `r_aoDebug 1..10` — AO, depth, normal, contact and bent-normal views.

Ground-truth ambient occlusion estimates how much nearby geometry blocks the surrounding light. It searches the depth image rather than tracing the whole map. [Details](../rend2-ambient-occlusion.md).

## Small contact shadows

*Requires the sun-lighting path, because these short rays refine its existing shadows.*

Adds small shadow details where objects touch and where the normal sun-shadow map misses thin features. Look under feet, around props and on carved surfaces. Light to medium performance cost.

**Main controls**

- `r_contactShadows 0 | 1` — disable / enable sun contact shadows.
- `r_contactShadowLength <units>` — maximum ray length.
- `r_contactShadowSteps <count>` — ray samples.
- `r_contactShadowThickness <units>` — assumed blocker thickness.
- `r_contactShadowStrength <value>` — shadow strength.
- `r_contactShadowSoft 0 | 1` — first-hit / softer depth-weighted result.

**Debug**

- No separate debug controls.

The shader searches the visible depth image a short distance toward the sun. This fills nearby gaps but cannot shadow from things outside the screen.

## More dynamic lights (Forward+)

Allows more saber, blaster and explosion lights to coexist, and avoids spending as much work on lights that cannot affect a pixel. It improves the older renderer’s small per-surface light lists. Usually light overhead and can improve performance in busy light scenes.

**Main controls**

- `r_forwardPlus 0 | 1` — legacy dynamic-light masks / clustered Forward+.
- `r_forwardPlusTileSize <pixels>` — screen tile size.
- `r_forwardPlusSlices <count>` — depth layers.
- `r_forwardPlusNearSlice <units>` — depth covered by the first slice.
- `r_forwardPlusMaxLightsPerCluster <count>` — light cap per cluster.
- `r_forwardPlusMaxShadowLights <count>` — strongest dynamic lights allowed shadow cubes when `r_dlightMode 2` is active.

**Debug**

- `r_forwardPlusDebug 1..9` — tiles, slices, clusters, light counts, overflow, shadows and selected light. Requires `vid_restart`.
- `r_forwardPlusDebugLight <index>` — selected light for debug mode 9.
- `r_forwardPlusStats` — print cluster statistics.
- `r_forwardPlusBenchmark [frames]` — benchmark the lighting path.
- `r_forwardPlusSpawnTestLights <count> [spread]` — spawn test lights.

Forward+ divides the space in front of the camera into small screen tiles and depth layers. Each cell receives a list of nearby lights, so a surface checks only the relevant ones. [Details](../rend2-forward-plus.md).

## Bounced light (SSGI)

*Requires HDR and the feature’s screen-space buffers, because it gathers light from surfaces already drawn in the view.*

Lets a saber lighting a wall also cast a weaker colored glow onto nearby surfaces. The original game’s baked light cannot follow a new explosion or moving saber this way. Medium to heavy performance cost.

**Main controls**

- `r_ssgi 0 | 1` — disable / enable SSGI. Requires `vid_restart`.
- `r_ssgiSource 0 | 1 | 2` — dynamic + emissive / emissive only / full scene, with mode 2 experimental.
- `r_ssgiIntensity <value>` — indirect-light strength.
- `r_ssgiQuality 0..3` — low / medium / high / ultra.
- `r_ssgiRays <count>` — rays per traced pixel; `0` uses the quality preset.
- `r_ssgiSteps <count>` — ray-march steps; `0` uses the preset.
- `r_ssgiMaxDistance <units>` — bounce distance.
- `r_ssgiThickness <units>` — assumed depth-buffer surface thickness.
- `r_ssgiTemporal 0 | 1` — averaging across frames.
- `r_ssgiHistoryWeight <value>` — history weight.
- `r_ssgiDenoise -1..4` — denoise passes; `-1` uses the preset.
- `r_ssgiHalfRes -1 | 0 | 1` — preset / full / half-resolution tracing; changing trace resolution takes effect after `vid_restart`.
- `r_ssgiHiZ -1 | 0 | 1` — preset / linear / hierarchical tracing.
- `r_ssgiEmissiveScale <value>` — physical emissive contribution.
- `r_ssgiGlowScale <value>` — legacy glow/auto-emissive contribution.

**Debug**

- `r_ssgiCompare 0 | 1` — split screen without / with SSGI.
- `r_ssgiDebug 1..10` — hit, distance, raw GI, history, denoise, source and final GI views.
- `r_ssgiFreezeHistory 0 | 1` — freeze temporal history.

Screen-space global illumination traces short paths through the visible depth image and gathers dynamic light and glow from nearby hits. Its default source mode avoids bouncing the already baked map lighting again. [Details](../rend2-ssgi.md).

## Light from the surrounding environment

*Requires usable runtime cubemap probes, because these provide the surrounding colors and directions of light.*

Makes opposite sides of a character receive different soft ambient light instead of one flat room color. This is noticeable in doorways and beside bright colored surroundings. Light performance cost.

**Main controls**

- `r_diffuseIBL 0 | 1` — disable / enable directional probe ambient. Requires `vid_restart`.
- `r_diffuseIBLStrength <value>` — modulation strength.

**Debug**

- `r_diffuseIBLDebug 1..5` — incoming diffuse light, modulation, legacy/new ambient and selected probe.

Image-based lighting uses small panoramic environment captures. Here their broad directional light modulates the original light-grid ambient rather than replacing the whole map lighting.

## Rectangle and line lights (LTC)

*Requires `r_forwardPlus 1` and `r_ltcAreaLights 1`, because shaped lights use the newer dynamic-light path.*

The original game represents dynamic lamps as points. Real lights also have width and length: a long ceiling panel should make a long highlight, not a round spot. This adds rectangular and line-shaped light sources. Medium performance cost where many shaped lights overlap.

**Main controls**

- `r_ltcAreaLights 0 | 1` — disable / enable LTC rectangle and line lights. Requires `vid_restart`.
- `r_ltcIntensityScale <value>` — overall light brightness.
- `r_ltcStaticDiffuse 0 | 1` — also add diffuse for `static_specular` lights.
- `r_ltcMaxLights <count>` — maximum map area lights used per scene.
- `r_ltcAutoAreaLights 0 | 1 | 2` — off / confident lamp shapes / also looser candidates.
- `r_ltcSaberIntensityScale` — LTC saber surface lighting: radiance multiplier. Default `1`.
- `r_ltcSaberLuminance` — Physical mode: artistic saber blade luminance in cd/m^2 (nits). Default `40000`.
- `r_ltcSaberShadows` — Saber shadow method: -1 center approximation, 0 off, 1 shaped approximation, 2 screen-space hybrid, 3 slower three-point reference. Default `-1`.
- `r_ltcStaticShadows` — Static rectangle LTC shadows: 0 off, 1 cached world, 2 cached world plus dynamic characters. Default `0`.

**Debug**

- `r_ltcDebug 1..8` — specular/diffuse, source mode, cluster counts, bounds, outlines, axes and strongest ID. Requires `vid_restart`.
- `r_ltcDebugLight <id>` — choose the highlighted map light; `-1` chooses the nearest.
- `r_ltcReloadLights` — Reload authored lights, or rerun stock-lamp detection when there is no authored file.
- `r_ltcList` — list loaded lights.
- `r_ltcNearest` — print the nearest light.
- `r_ltcExtractLights` — write detected candidates to `maps/<map>.arealights.generated.json` for review.
- `r_ltcShadowStats` — Print LTC shadow selection/cache statistics.

### [Autodetectable] Stock lamps

Enable `r_ltcAutoAreaLights 1` to find confident lamp shapes in existing glow/emissive surfaces. Mode 2 also accepts looser candidates. No edited map is needed; a map’s authored area-light file takes priority.

### [For mod authors] Custom rectangle and line lights

Create `maps/<map>.arealights.json`. This example adds a warm rectangular lamp:

~~~json
{"lights":[{"type":"rect","center":[0,0,128],"right":[1,0,0],"up":[0,1,0],
"halfWidth":64,"halfHeight":8,"color":[1,0.85,0.7],"intensity":4,"mode":"static_specular"}]}
~~~

Use `start`, `end` and `radius` for a line. `static_specular` adds highlights without relighting the baked diffuse; `static_full` and `dynamic` include their corresponding diffuse lighting. Support alone does not create a custom lamp.

Linearly transformed cosines, or LTC, is a fast approximation for reflection from a light with a visible shape. It preserves the width and orientation of the emitter without tracing many separate point lights. [Details](../rend2-ltc-area-lights.md).

## Saber-shaped lights

*Requires area lights, Forward+ and matching updated game/client modules, because the renderer needs the blade’s actual endpoints.*

Makes a long saber cast a long highlight along floors, armor and walls instead of a point-shaped spot. It works with existing sabers. Medium performance cost near illuminated surfaces.

**Main controls**

- `r_ltcSaberAreaLights 0 | 1` — point-light sabers / LTC line-light sabers.

**Debug**

- No separate debug controls.

Game code sends the blade as a line emitter. The area-light shader evaluates that line; it does not need a new saber asset. [Details](../rend2-ltc-area-lights.md).

## [For mod authors] Spot lights

*Requires an authored light cone and updated game/client modules, because they define and submit the cone. Shadows also need `r_dlightMode 2`; a visible fog beam needs `r_volumetricFog 2`.*

Adds flashlight-like cones instead of lighting equally in every direction. Existing point-light effects keep their old shape; switching spot support on does not add flashlights to stock effects. Light cost for the cone; shadows and visible fog beams can have a medium cost.

**Main controls**

- `r_spotLights 0 | 1` — submit authored spots as ordinary point lights / as spot lights.
- `r_spotLightShadows 0 | 1` — disable / enable spot shadows.

**Debug**

- `r_spotLightDebug 1..4` — cone list, shadow frustums, spot-only surface lighting and spot-only fog lighting.
- `r_spot add|attach|spin|noshadow|list|clear` — renderer-side test spots.
- `fxplay <efx> [distance|muzzle]` — developer test command for authored EFX spots.

Add a `spot` block inside an EFX `Light`:

~~~text
spot
{
    innerAngle 16
    outerAngle 26
    direction 1 0 0
    shadows 1
}
~~~

The light’s existing size and RGB settings still set its range and color.

A spot light limits its point-light illumination to an inner/outer cone. The same projection can cast a shadow and illuminate fog. [Details](../rend2-spot-lights.md).

## [For mod authors] Projected light patterns

*Requires an authored spot light, because a point light has no projection direction for the image.*

Lets a lamp project a grate, window pattern or colored image onto walls and through fog. Stock spots remain unchanged until a pattern is authored. Light performance cost.

**Main controls**

- `r_spotLightCookies 0 | 1 | 2` — off / intensity-only cookie / colored cookie.

**Debug**

- `r_spotLightCookieDebug 1..3` — projected UV, cookie factor, cookie multiplied by geometry shadow.
- `r_spot cookie <index> <image|none>` — assign a cookie to a renderer-side test spot.

Inside the EFX `spot` block add `cookie textures/imperial/grate02`. `cookieRoll <degrees>` rotates the projected image. Mode 1 uses brightness only; mode 2 preserves its colors.

A cookie, also called a gobo, is an image projected through the light cone. Its pattern is multiplied by ordinary geometry shadows. [Details](../rend2-spot-lights.md).
