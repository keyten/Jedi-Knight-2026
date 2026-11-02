# Lighting

[All rendering settings](../rendering-features.md)

Soft shading in corners, dynamic lights from sabers and blasters, bounced light, and lights with a real shape. How characters are lit is on [Characters](characters.md); shadows are on [Shadows](shadows.md).

## Ambient occlusion (GTAO)

Darkens corners, wall joints, the ground under feet and crates, and small recesses, so objects look grounded instead of evenly lit. The map's baked lighting already has some of this, but it can't follow moving characters or props. GTAO is a more accurate replacement for Rend2's original SSAO, with fewer halos.

**Cost:** medium.

- `r_aoMode -1` — **-1** follow the original `r_ssao` setting (off unless `r_ssao 1`). **0** off. **1** original SSAO. **2** GTAO. Turning it on from off needs `vid_restart`.
- `r_gtaoQuality 2` — 0 low, 1 medium, 2 high, 3 ultra.
- `r_gtaoRadius 32` — how far an object reaches to darken its surroundings, in world units (a player is about 64 tall).
- `r_gtaoPower 1.5` — strength; higher is darker.

**Compare:** `r_aoCompare` — 1 split screen (left the original AO: Rend2's SSAO with `r_ssao 1`, none with `r_ssao 0`), 2 the original AO.

**How it works.** For each pixel the shader looks around in the depth image to estimate how much of the surrounding sky of light is blocked by nearby geometry. It only knows what is on screen, so occlusion from objects just outside the view is missing. GTAO darkens only indirect light (ambient light, the indirect share of baked light and reflections), not direct sun or lamps, as in reality.

- `r_gtaoHalfRes 1` *(restart)* — compute at half resolution; 0 full resolution, sharper and slower.
- `r_gtaoFalloff 0.6` — how quickly occlusion fades toward the radius.
- `r_gtaoThickness 0.2` — treat objects as thin, so things in front of a wall don't darken it as much (fewer dark halos around characters).
- `r_gtaoDenoise 2` — smoothing passes, 0–3.
- `r_gtaoBentNormals 1` — also tilt ambient light toward the open direction; 0 off, up to 1 full.
- `r_aoApply -1` — **-1** automatic. **0** original: darken ambient light and reflections. **1** physically based: darken only indirect light.
- `r_aoMultiBounce 1` — bright surfaces keep more light in creases, as light bounces inside them.
- `r_aoLightmapFraction 0.5` — share of baked lighting treated as indirect and therefore darkened by AO.
- `r_aoSpecOcclusion -1` — how AO darkens reflections: **-1** automatic, **0** plain AO, **1** from AO, roughness and view angle, **2** from the bent-normal cone.

**Debug**

- `r_aoDebug 1..12` — original SSAO, raw / denoised GTAO, normals, depth, contact shadows, cascade shadows, sun visibility, ambient visibility, final AO, bent normals, reflection occlusion.

Design notes: [rend2-ambient-occlusion.md](../rend2-ambient-occlusion.md).

## Many dynamic lights (Forward+)

Rend2 allows at most 32 dynamic lights per frame — sabers, blaster bolts, explosions, muzzle flashes. In big fights the extra lights are simply dropped. Forward+ raises the limit to 256 and makes every pixel check only the lights that actually reach it. [Lamp-shaped lights](#lamp-shaped-lights-ltc) and [blade-shaped saber light](#saber-light-along-the-blade) need it.

**Cost:** light; it can be faster than the original in busy scenes.

- `r_forwardPlus 0` — 1 on.
- `r_forwardPlusMaxShadowLights 4` — with dynamic-light shadows (`r_dlightMode 2`, see [Shadows](shadows.md#dynamic-light-shadows)), how many of the strongest lights cast them.

**Compare:** `r_forwardPlusCompare 2` — the original 32-light path. No split screen: one frame uses one light path (and the picture only differs with more than 32 lights).

**How it works.** The view is split into screen tiles and depth slices (clusters). Each frame every light is added to the clusters it touches, and each pixel loops only over its cluster's list.

- `r_forwardPlusTileSize 64` — tile size in pixels.
- `r_forwardPlusSlices 16` — depth slices.
- `r_forwardPlusNearSlice 48` — depth covered by the first slice.
- `r_forwardPlusMaxLightsPerCluster 64` — lights kept per cluster; the least important ones are dropped.

**Debug**

- `r_forwardPlusDebug 1..9` *(restart)* — tiles, slices, clusters, lights per cluster, overflow, shadowed and unshadowed lights, light spheres, one light (`r_forwardPlusDebugLight <index>`).
- `r_forwardPlusStats` — print cluster statistics.
- `r_forwardPlusBenchmark [frames]` — time the lighting path.
- `r_forwardPlusSpawnTestLights <count> [spread]` — spawn test lights.

Design notes: [rend2-forward-plus.md](../rend2-forward-plus.md).

## Bounced light (SSGI)

A saber or explosion near a wall lights the wall — and with this, the wall also throws a weaker, colored glow onto the floor and characters next to it. Glowing materials can light their surroundings the same way. The map's own baked lighting already contains bounced light, so by default only dynamic lights and glowing surfaces bounce. Works best with `r_hdr 1`; without it bright sources are clipped.

**Cost:** medium to heavy.

- `r_ssgi 0` *(restart)* — 1 on.
- `r_ssgiIntensity 1` — strength of the bounce.
- `r_ssgiQuality 1` — 0 low, 1 medium, 2 high, 3 ultra.
- `r_ssgiSource 0` — what bounces: **0** dynamic lights and glowing materials, **1** glowing materials only, **2** everything, including baked lighting (experimental: baked bounce is counted twice).

**Compare:** `r_ssgiCompare` — 1 split screen (no bounce left), 2 off.

**How it works.** For each pixel the shader traces a few short rays through the on-screen depth image and picks up the light of the surfaces they hit. It only sees what is on screen, and the noisy result is averaged over several frames and smoothed.

- `r_ssgiMaxDistance 256` — how far light bounces, in world units.
- `r_ssgiEmissiveScale 1` — strength of materials with their own glow maps as sources.
- `r_ssgiGlowScale 0` — strength of stock glow layers (and [auto-emissive](materials.md#glowing-surfaces)) as sources; 0 ignores them.
- `r_ssgiRays 0`, `r_ssgiSteps 0` — rays per pixel and steps per ray; 0 takes them from the quality.
- `r_ssgiThickness 12` — assumed thickness of surfaces on screen.
- `r_ssgiTemporal 1`, `r_ssgiHistoryWeight 0.9` — averaging over frames and how much history to keep.
- `r_ssgiDenoise -1` — smoothing passes 0–4; -1 from the quality.
- `r_ssgiHalfRes -1` — trace at half resolution: -1 from the quality, 0 full, 1 half (applied at `vid_restart`).
- `r_ssgiHiZ -1` — faster hierarchical tracing: -1 from the quality, 0 off, 1 on.

**Debug**

- `r_ssgiDebug 1..10` — hit mask, hit distance, raw / averaged / smoothed GI, history weight, dynamic and glow sources, final light, receiver color.
- `r_ssgiFreezeHistory 1` — stop updating the frame history.

Design notes: [rend2-ssgi.md](../rend2-ssgi.md).

## Lamp-shaped lights (LTC)

A dynamic light in JKA is a point. Real lamps have size and shape: a long ceiling strip should leave a long highlight on a glossy floor, not a round spot. Area lights give lights a rectangle or line shape. On stock maps, glowing lamp surfaces are detected automatically and turned into rectangle lights that add only highlights, since the lightmap already has their light. Needs `r_forwardPlus 1`.

**Cost:** medium where many shaped lights overlap.

- `r_ltcAreaLights 0` *(restart)* — 1 on.
- `r_ltcAutoAreaLights 1` — find lamps on maps without a light file: **0** off, **1** clear lamp shapes, **2** also doubtful ones (more lamps, more mistakes). Applied at map load.
- `r_ltcIntensityScale 1` — brightness of map area lights.
- `r_ltcStaticShadows 0` — shadows of map area lights: **0** off, **1** world shadows (cached), **2** also characters near up to four lights.

**Compare:** `r_ltcAreaLightsCompare` — 1 split screen (no map area lights left), 2 off (sabers fall back to their point light).

**Mod authors:** a map's own `maps/<map>.arealights.json` replaces auto-detection. A warm rectangle lamp:

~~~json
{"lights":[{"type":"rect","center":[0,0,128],"right":[1,0,0],"up":[0,1,0],
"halfWidth":64,"halfHeight":8,"color":[1,0.85,0.7],"intensity":4,"mode":"static_specular"}]}
~~~

A line light uses `start`, `end` and `radius`. `static_specular` adds only highlights over the baked light; `static_full` and `dynamic` also light surfaces. `r_ltcExtractLights` writes the auto-detected lamps to `maps/<map>.arealights.generated.json` as a starting point.

**How it works.** LTC (linearly transformed cosines) computes the light from a whole polygon exactly in one step, instead of summing many point lights, so there is no quality setting.

- `r_ltcMaxLights 64` — map lights used per scene, most important first.
- `r_ltcStaticDiffuse 0` — 1: auto-detected and `static_specular` lamps also light surfaces, although the lightmap usually contains that light already.

**Debug**

- `r_ltcDebug 1..14` *(restart)* — specular/diffuse, light source, cluster counts, bounds, outlines, axes, strongest light, shadow and saber views. `r_ltcDebugLight <id>` picks the highlighted light; -1 the nearest.
- `r_ltcList`, `r_ltcNearest` — list loaded lights / print the nearest.
- `r_ltcReloadLights` — reload the light file or rerun detection.
- `r_ltcShadowStats` — shadow selection and cache statistics.

Design notes: [rend2-ltc-area-lights.md](../rend2-ltc-area-lights.md).

## Saber light along the blade

The saber light normally comes from one point at the middle of the blade, so floors and walls get a round spot. With this, the whole blade lights its surroundings: a long, even glow along the floor and long reflections in armor. Works with stock sabers. Needs `r_forwardPlus 1` and `r_ltcAreaLights 1`.

**Cost:** medium near lit surfaces.

- `r_ltcSaberAreaLights 0` — **0** point light (original). **1** blade-shaped light on surfaces and in fog. **2** blade-shaped light on surfaces, point light with shadow in fog.
- `r_ltcSaberShadows -1` — saber shadows: **-1** from a point at the blade middle (original), **0** off, **1** stretched along the blade, **2** plus screen-space blade visibility, **3** precise three-point shadows for up to two sabers (slow).
- `r_ltcSaberIntensityScale 1` — brightness.
- `r_ltcSaberLuminance 40000` — blade brightness in nits with `r_pbrPhysical 1`.

**Compare:** `r_ltcSaberCompare 2` — the original point light. No split screen: a saber gives the frame either its blade light or its point light.

## Spot lights (mod authors)

Flashlight-like cones instead of lights that shine in every direction. Stock effects have no cones; a mod adds them in EFX files. Cones can cast shadows (`r_dlightMode 2`) and show a beam in [volumetric fog](fog.md).

**Cost:** light; shadows and fog beams medium.

- `r_spotLights 0` — 1 on; 0 draws authored spots as ordinary point lights.
- `r_spotLightShadows 1` — 0 no spot shadows.

**Compare:** `r_spotLightsCompare 2` — spots as point lights. No split screen: a spot's shadow is a cone or a cube for the whole frame.

Add a `spot` block inside an EFX `Light`; its size and color still set range and color:

~~~text
spot
{
    innerAngle 16
    outerAngle 26
    direction 1 0 0
    shadows 1
}
~~~

**Debug**

- `r_spotLightDebug 1..4` — cones, shadow frustums, surfaces lit by spots only, fog lit by spots only.
- `r_spot add|attach|spin|noshadow|list|clear` — place test spots.
- `fxplay <efx> [distance|muzzle]` — play an EFX for testing (developer).

Design notes: [rend2-spot-lights.md](../rend2-spot-lights.md).

## Projected patterns (mod authors)

A spot light can project an image — a grate, a window frame, colored glass — onto walls and through fog. Needs an authored spot.

**Cost:** light.

- `r_spotLightCookies 0` — **0** off, **1** pattern brightness only, **2** in color.

**Compare:** `r_spotLightCookiesCompare` — 1 split screen (no pattern left, also in fog), 2 off.

Inside the EFX `spot` block add `cookie textures/imperial/grate02`; `cookieRoll <degrees>` rotates it.

**Debug**

- `r_spotLightCookieDebug 1..3` — projected coordinates, pattern, pattern × shadow.
- `r_spot cookie <index> <image|none>` — put a pattern on a test spot.
