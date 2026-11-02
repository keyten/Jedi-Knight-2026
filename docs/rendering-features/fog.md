# Fog and light beams

[All rendering settings](../rendering-features.md)

Fog that fills the air with light: sun beams through windows, glowing haze around sabers, mist on the ground, thick smoke. Smoke from effects is on [Smoke and effects](effects.md), rain haze on [Weather](weather.md#rain-haze), underwater fog on [Water](water.md#underwater-fog).

## Volumetric fog

The original fog only fades surfaces toward a color with distance. Volumetric fog fills the space itself: sun beams fall through windows and are cut by shadows, sabers and blaster bolts glow in the haze around them, and lamps light the fog near them.

It lights the fog a map already has (its fog volumes). **A map without fog shows nothing new** until you add a fog source: [ground haze](#ground-haze), [rain haze](weather.md#rain-haze) or authored [local fog](#local-fog-volumes-mod-authors). Sun beams need realtime sun ([`r_forceSun 1`](shadows.md#sun-shadows) on stock maps).

**Cost:** medium; fine grids and the extra options below can make it heavy.

- `r_volumetricFog 0` *(restart)* — **0** off. **1** Rend2's original volumetric fog: fog lit only by the map's baked light, no sun beams or dynamic lights (see [the end of this page](#original-rend2-volumetric-fog)). **2** the volumetric fog described here.
- `r_volumetricFogQuality 1` *(restart)* — 0 low, 1 medium, 2 high: the size of the 3D grid the fog is computed in.
- `r_volumetricFogSunScale 1` — strength of sunlight in fog (beams).
- `r_volumetricFogDlightScale 1` — strength of dynamic lights in fog.
- `r_volumetricFogStaticScale 1` — strength of the map's baked light in fog.
- `r_volumetricFogAnisotropy 0.2` — how much fog glows when you look toward a light: 0 the same from every side, higher brighter toward the light (stronger beams against the sun), negative brighter with the light behind you.
- `r_volumetricFogDlightShadows 1` — dynamic lights cast shadows into fog (needs `r_dlightMode 2`).
- `r_volumetricFogBloom 0` — let bright fog glow add to bloom.

**Compare:** `r_volumetricFogCompare 2` — no fog grid: surfaces get their own fog again (as with `r_volumetricFog 1`). No split screen: opaque surfaces get their fog either from one composite of the whole view or from their own fog passes.

The features below build on the fog grid and can be split: their compare cvars split the grid by screen column, so the line is about one fog cell wide.

**How it works.** The space in front of the camera is divided into a 3D grid of small cells shaped to the view (froxels: frustum voxels). Every frame each cell gets its fog density and the light that reaches it — baked light from the map's light grid, the sun with its shadow maps, dynamic lights with theirs — and then light and fog are summed along each view ray. Where the map's baked light includes the sun, that part is replaced by the realtime sun, so it can cast beams. The grid is coarse, so results are jittered and averaged over frames.

- `r_volumetricFogGridScale 0` *(restart)* — screen pixels per cell; 0 from the quality.
- `r_volumetricFogSlices 0` *(restart)* — depth slices; 0 from the quality.
- `r_volumetricFogFar 0` — how far the grid reaches; 0 automatic (4096). Beyond it, the last slice is extended.
- `r_volumetricFogTemporal 1` — jitter and average over frames; 0 is noisier but reacts instantly.
- `r_volumetricFogHistoryWeight 0.9` — how much history to keep.
- `r_volumetricFogReset 0` — set by the game to clear history after a camera cut.

**Debug**

- `r_volumetricFogDebug 1..29` — density, sun (unshadowed / shadowed), dynamic lights, baked light, scattering, transmittance, history, slices, density by source (map fog, ground haze, noise, local volumes, effects), dynamic lights per cell. Later numbers are listed in the sections below.
- `r_volumetricFogFreeze 1` — freeze the current fog volume and camera.
- `r_vfog` — inspect or change the fog state.

Design notes: [rend2-volumetric-fog.md](../rend2-volumetric-fog.md).

## Ground haze

Low mist that is thick near the ground and thins with height, on any map, without fog brushes. A simple way to get visible beams on maps without fog. Needs `r_volumetricFog 2`.

**Cost:** light.

- `r_volumetricFogHeight 0` — 1 on.
- `r_volumetricFogHeightOpaqueDistance 3000` — at ground level, the distance where the haze becomes opaque. Lower is thicker.
- `r_volumetricFogHeightFalloff 256` — how quickly it thins with height; larger means a taller layer.
- `r_volumetricFogHeightColor "0.7 0.75 0.8"` — haze color.
- `r_volumetricFogHeightBase auto` — world height of the ground; auto picks the lowest floor (a map's `env.json` can override it).
- `r_volumetricFogHeightMaxDensity 1` — density cap below the base height.
- `r_volumetricFogHeightTopHeight 0` — height where the haze fades out completely; 0 never.

**Compare:** `r_volumetricFogHeightCompare` — 1 split screen, 2 off.

**Debug**

- `r_volumetricFogDebug 12` — ground-haze density.

## Uneven fog

Breaks uniform fog into drifting patches and wisps, so mist looks less like a smooth wall. Choose which fog sources get it.

**Cost:** light to medium.

- `r_volumetricFogNoise 0` — add bits: **1** ground haze, **2** map fog volumes, **4** whole-map fog, **8** local volumes marked for noise. Example: `3` for ground haze and map fog.
- `r_volumetricFogNoiseScale 4096` — size of the large patches, in world units.
- `r_volumetricFogNoiseContrast 1` — patchiness: 0 uniform, 1 between none and double density, higher sparser clumps. Average density stays the same.
- `r_volumetricFogNoiseWind "0 0 0"` — drift speed and direction, world units per second.
- `r_volumetricFogNoiseDetailScale 900`, `r_volumetricFogNoiseDetailContrast 0` — a second, finer noise layer; 0 contrast turns it off (cheaper).

**Compare:** `r_volumetricFogNoiseCompare` — 1 split screen (uniform fog left), 2 off.

**Debug**

- `r_volumetricFogDebug 13..15` — noise, density without and with noise.

## Shadows inside thick fog

In thick smoke or dense mist, the side facing the light should be bright and the far side dark, because the fog itself blocks light. Plain volumetric fog lets light through every cell unhindered, so dense clouds glow evenly. This makes the fog shadow itself.

**Cost:** heavy.

- `r_volumetricSelfShadow 0` *(restart)* — **0** off, **1** from the sun, **2** from the sun and the strongest dynamic lights.
- `r_volumetricSelfShadowMaxLights 2` — dynamic lights for mode 2 (0–4).

**Compare:** `r_volumetricSelfShadowCompare` — 1 split screen, 2 off (light bouncing below turns off with it).

**How it works.** For each fog cell the renderer samples density along a short ray toward the light and dims the light by what it passes through. More samples, longer rays and more lights cost more.

- `r_volumetricSelfShadowSamples 6` — samples along the sun ray (dynamic lights use half).
- `r_volumetricSelfShadowDistance 768` — length of that ray, in world units.
- `r_volumetricSelfShadowOutsideHeightFog 1` — beyond the ray, count ground haze analytically; 0 ignore it.

**Debug**

- `r_volumetricFogDebug 40..45` — density, optical depth toward the sun, fog shadow, geometry shadow, both.

## Light bouncing inside fog

Real clouds and steam are white inside because light bounces around in them many times. With self-shadowing alone thick white fog gets unnaturally dark at its core. This adds back the light those bounces would bring, while dark smoke stays dark. Needs `r_volumetricSelfShadow 1` or `2`.

**Cost:** medium.

- `r_volumetricMultiScatter 0` — **0** off, **1** for the sun, **2** for the sun and self-shadowed dynamic lights.

**Compare:** `r_volumetricMultiScatterCompare` — 1 split screen, 2 off.

**How it works.** A few cheap extra scattering steps ("octaves") each let light penetrate deeper, with less energy and more evenly in all directions — an approximation of many bounces without tracing them.

- `r_volumetricMultiScatterOctaves 2` — extra steps, 1–3.
- `r_volumetricMultiScatterAttenuation 0.25` — how much deeper each step reaches; lower is deeper.
- `r_volumetricMultiScatterContribution 0.5` — light returned per step.
- `r_volumetricMultiScatterPhase 0.5` — how quickly the steps lose their direction.
- `r_volumetricMultiScatterLength 64` — typical size of a dense cloud; thin fog gets almost none.
- `r_volumetricMultiScatterShadowFill 0.25` — how far it may brighten geometry shadows inside thick fog (0–0.5).

**Debug**

- `r_volumetricFogDebug 46..50` — single, multiple and combined scattering, their ratio, optical depth.

## Colored fog absorption

Normally fog dims everything behind it evenly, so it can only darken or tint by its own glow. With this, fog can absorb colors differently: deep water swallows red first and turns things blue-green, colored smoke tints the scene behind it.

**Cost:** heavy (memory and time).

- `r_volumetricFogRGBExtinction 0` *(restart)* — 1 on.
- `r_volumetricFogHeightExtinction "1 1 1"` — ground haze: relative absorption of red, green and blue.

**Compare:** `r_volumetricFogRGBCompare` — 1 split screen (gray absorption left), 2 gray absorption.

Underwater fog and water/slime/lava already define their colors; this lets the fog keep them. Effect smoke stays neutral.

**Mod authors:** `fogExtinctionColor 3 0.5 0.5` in a map fog shader, `"Extinction": [3,0.5,0.5]` in a local volume.

**How it works.** Each fog cell stores three transparencies, one per color channel, instead of one; compositing then needs two draws.

**Debug**

- `r_volumetricFogDebug 51..56` — absorption, transparency, color shift, difference, chroma, far transparency.

## Local fog volumes (mod authors)

Place a pocket of steam, a low smoke bank or a box of dusty air on a map without recompiling it. Volumes are lit and shadowed like any other fog. Needs `r_volumetricFog 2`.

**Cost:** light for a few small volumes; many overlapping ones medium.

Add a `FogVolumes` array to `cubemaps/<map>/env.json`:

~~~json
{"FogVolumes":[{"Shape":"sphere","Origin":[0,0,128],"Radius":96,
"Opaque":250,"Color":[0.55,0.55,0.58],"Softness":0.6}]}
~~~

`Origin` and `Radius` place it, `Opaque` is the distance at which it becomes opaque, `Color` its color. Boxes use `Size` and optional `Angles`; ellipsoids are also supported.

**Compare:** `r_fogVolumesCompare` — 1 split screen (no local volumes left), 2 off.

**Debug**

- `r_volumetricFogDebug 16..19` — local density, share, bounds, volumes per slice.
- `r_fogvol` — print volumes and statistics. `r_fogvol add ...` adds a test volume, `r_fogvol test <count> [spread]` a stress test, `r_fogvol remove <index> | clear` removes them, `r_fogvol slices` shows per-slice lists, `r_fogvol dump` prints test volumes as a `FogVolumes` array to paste into `env.json`.

## Different kinds of fog (mod authors)

Steam, dust and dark smoke scatter light differently: steam is bright and glows toward the light, smoke absorbs. Fog sources can set this individually; without it the global `r_volumetricFogAnisotropy` applies.

- Map fog shaders: `fogAlbedo 0.9 0.4 0.3` (share of light scattered rather than absorbed, per color) and `fogAnisotropy 0.6` (how strongly it glows toward the light), next to `fogParms`.
- Local volumes: `"Anisotropy": 0.6`. Effect media: `anisotropy 0.6`.

**Debug**

- `r_volumetricFogDebug 35..39` — absorption, albedo, scattering lobes, mixed anisotropy, sun phase.

## Original Rend2 volumetric fog

`r_volumetricFog 1` keeps Rend2's original volumetric fog: the map's fog volumes are lit by its baked light only, with no sun beams, shadows or dynamic lights. It is still used for views without the new fog grid (portals, mirrors, sky portals).

**Cost:** medium.

- `r_volumetricFogSamples 48` *(restart)* — samples per ray; fewer is faster but banded.
- `r_volumetricFogDefaultScale 1.0` *(restart)* — density of fog volumes that don't set their own scale.
- `r_volumetricFogScale 1.0` — temporary density multiplier (not saved).
