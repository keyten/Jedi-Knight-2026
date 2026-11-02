# Fog and light beams

[All rendering features](../rendering-features.md)

Controls marked `vid_restart` need a renderer restart. Debug views use 0 to return to normal unless stated otherwise. Performance labels are rough relative costs, not benchmark guarantees.

## Spatial fog and light beams

*Requires `r_volumetricFog 2`, because this selects the new spatial fog renderer.*

Makes fog occupy the scene: beams pass through doorways, shadows cut into haze, and moving lights color smoke around them. The original fog mostly colors surfaces by distance and cannot show the same detailed beams. Medium performance cost; fine grids and extra shadow options can be heavy.

**Main controls**

- `r_volumetricFog 0 | 1 | 2` — off / legacy light-grid ray march / froxel volumetric fog. Requires `vid_restart`.
- `r_volumetricFogQuality 0..2` — low / medium / high. Requires `vid_restart`.
- `r_volumetricFogGridScale <pixels>` — manual screen pixels per froxel; `0` uses the preset. Requires `vid_restart`.
- `r_volumetricFogSlices <count>` — manual depth slices; `0` uses the preset. Requires `vid_restart`.
- `r_volumetricFogFar <units>` — froxel range; `0` uses the automatic default.
- `r_volumetricFogAnisotropy <g>` — default scattering direction; 0 scatters evenly in all directions.
- `r_volumetricFogTemporal 0 | 1` — averaging across frames.
- `r_volumetricFogHistoryWeight <value>` — history weight.
- `r_volumetricFogSunScale <value>` — sun scattering multiplier.
- `r_volumetricFogDlightScale <value>` — dynamic-light scattering multiplier.
- `r_volumetricFogStaticScale <value>` — baked-light scattering multiplier.
- `r_volumetricFogDlightShadows 0 | 1` — dynamic-light shadows inside fog; needs `r_dlightMode 2`.
- `r_volumetricFogBloom <value>` — add bright in-scattering to bloom.

**Debug**

- `r_volumetricFogReset 1` — gameplay hook to clear temporal history after a camera cut.
- `r_volumetricFogDebug 1..56` — density, lighting, history, media, particle, self-shadow, multiple-scattering and RGB-extinction views.
- `r_volumetricFogFreeze 0 | 1` — freeze the current volume and camera.
- `r_vfog` — inspect/control the active volumetric fog state.

The space in front of the camera is divided into small 3D cells called froxels, meaning camera-frustum voxels. Each stores fog and illumination; the renderer adds their contributions along the view ray instead of tinting everything with one distance formula. [Details](../rend2-volumetric-fog.md).

## Rain haze

*Requires `r_volumetricFog 2` and active rain, because the rain haze is another medium in the same fog volume.*

Makes distant rainy courtyards look hazy and lets lights glow through the rain. Covered interiors remain clearer rather than receiving a screen-wide grey overlay. Light additional performance cost.

**Main controls**

- `r_rainVolume 0 | 1` — disable / enable the rain medium.
- `r_rainVolumeStrength <value>` — normal-rain light absorption per world unit at full normalized intensity.
- `r_rainVolumeHeavyMultiplier <value>` — additional heavy-rain density multiplier.
- `r_rainVolumeAnisotropy <g>` — rain light-scattering direction; default `0.72`.
- `r_rainVolumeGroundStrength <value>` — optional heavy-rain splash-mist density; `0` disables it.
- `r_rainVolumeGroundHeight <units>` — height of that layer above the exposed surface.
- `r_rainVolumeMaxDistance <units>` — camera range with a soft fade over the outer 15%.

**Debug**

- `r_volumetricFogDebug 71..74` — rain-only density, weather exposure, unattenuated in-scattering source and final integrated rain contribution.

The renderer uses the shared rain state and weather-occlusion map to place a thin scattering medium only where rain can reach. It receives the same lights and shadows as other fog.

## Baked room light in fog

*Requires `r_volumetricFog 2` and a usable BSP light grid, because existing baked samples supply the room illumination.*

Lets stock room colors illuminate haze while the sun can still be shadowed separately. No recovered hidden lamps are added automatically.

**Main controls**

- No separate switch; use the feature or asset settings described here.

**Debug**

- No separate debug controls.

The renderer separates a usable authored-sun contribution from the original merged light grid and keeps the rest as baked room light. When it cannot make a valid split, it uses the ordinary grid. [Details](../rend2-grid-reconstruction-removal.md).

## Ground haze

*Requires `r_volumetricFog 2`, because this adds a medium to the spatial fog volume.*

Adds low mist that is thick near the ground and thins higher up. It works on existing maps without adding fog brushes. Light additional performance cost.

**Main controls**

- `r_volumetricFogHeight 0 | 1` — disable / enable height fog.
- `r_volumetricFogHeightOpaqueDistance <units>` — opaque distance at the base height.
- `r_volumetricFogHeightBase <z>` — base world height.
- `r_volumetricFogHeightFalloff <units>` — exponential scale height.
- `r_volumetricFogHeightMaxDensity <value>` — density cap below the base.
- `r_volumetricFogHeightTopHeight <units>` — optional soft upper cutoff.
- `r_volumetricFogHeightColor "r g b"` — scattering color.

**Debug**

- No separate debug controls.

Fog density decreases with height above a chosen base level. This supplies a continuous ground layer rather than a full-screen tint.

## Uneven fog

*Requires spatial fog, because the noise varies its density rather than creating a separate effect.*

Breaks a uniform haze into drifting patches, so smoke and mist look less like a smooth wall. Light to medium additional performance cost.

**Main controls**

- `r_volumetricFogNoise <bits>` — 1 height fog, 2 BSP fog, 4 global fog, 8 noise-enabled local volumes; bits can be combined.
- `r_volumetricFogNoiseScale <units>` — macro-noise period.
- `r_volumetricFogNoiseContrast <value>` — macro contrast.
- `r_volumetricFogNoiseDetailScale <units>` — detail-noise period.
- `r_volumetricFogNoiseDetailContrast <value>` — detail strength; `0` avoids the second fetch.
- `r_volumetricFogNoiseWind "x y z"` — world-space drift speed.

**Debug**

- No separate debug controls.

World-space noise changes fog density, and a wind vector moves the pattern. Broad and fine noise can be tuned separately.

## [For mod authors] Local smoke and mist

*Requires `r_volumetricFog 2`, because these shapes are inserted into the spatial fog volume.*

Lets a mod place a pocket of steam, a low smoke bank or a box of dusty air without rebuilding the map. Enabling fog does not create these custom shapes by itself. Light cost for a few small volumes; many overlapping ones can be medium.

**Main controls**

- No separate switch; use the feature or asset settings described here.

**Debug**

- `r_fogvol` — print volume state and statistics.
- `r_fogvol add ...` — spawn a test volume.
- `r_fogvol test <count> [spread]` — deterministic stress test.
- `r_fogvol remove <index> | clear` — remove test volumes.
- `r_fogvol slices` — inspect per-slice lists.
- `r_fogvol dump` — print test volumes as an env.json `FogVolumes` array.

Add a `FogVolumes` array to `cubemaps/<map>/env.json`:

~~~json
{"FogVolumes":[{"Shape":"sphere","Origin":[0,0,128],"Radius":96,
"Opaque":250,"Color":[0.55,0.55,0.58],"Softness":0.6}]}
~~~

`Origin` and `Radius` place the cloud; `Opaque` sets its fog depth and `Color` its scattering color. Boxes use `Size` and optional `Angles`. The `r_fogvol dump` command exports test volumes.

Each authored sphere, ellipsoid or box contributes density inside its bounds. It receives the existing fog lighting and shadows rather than drawing a stack of flat smoke pictures. [Details](../rend2-volumetric-fog.md).

## [For mod authors] Different kinds of fog

*Requires spatial fog, because these properties belong to individual fog media.*

Lets steam scatter light differently from dusty air or dark smoke, even where they overlap. Existing fog keeps the global fallback until an override is authored. Light additional performance cost for overlapping media.

**Main controls**

- No separate switch; use the feature or asset settings described here.

**Debug**

- `r_volumetricFogDebug 35..39` — extinction, albedo, phase-lobe slots, mixed anisotropy and sun phase.

BSP fog shaders can add `fogAlbedo 0.9 0.4 0.3` and `fogAnisotropy 0.6` next to their existing `fogParms`. Local volumes use `"Anisotropy": 0.6`; EFX `volumetricMedia` uses `anisotropy 0.6`. Without an override the renderer uses `r_volumetricFogAnisotropy`.

Albedo controls how much light is scattered rather than absorbed. Anisotropy controls whether it scatters mostly forward or in other directions, changing how strongly a beam shows from different views.

## Shadows inside smoke

*Requires spatial fog, because light must be checked through the density of the medium.*

Makes the far side of thick smoke darker than the side facing a lamp or the sun. Ordinary geometry shadows alone cannot create this effect inside a cloud. Heavy performance cost.

**Main controls**

- `r_volumetricSelfShadow 0 | 1 | 2` — off / sun / sun + strongest dynamic lights. Requires `vid_restart`.
- `r_volumetricSelfShadowSamples <count>` — density samples along the sun ray.
- `r_volumetricSelfShadowDistance <units>` — maximum light-ray march.
- `r_volumetricSelfShadowOutsideHeightFog 0 | 1` — ignore / analytically include height fog beyond the march.
- `r_volumetricSelfShadowMaxLights <count>` — strongest dynamic lights self-shadowed in mode 2.

**Debug**

- `r_volumetricFogDebug 40..45` — density, optical depth, media transmittance and geometry/media shadow separation.

The renderer samples density toward the light to estimate how much the cloud itself blocks. More samples, longer rays and more dynamic lights increase the work.

## Light bouncing inside fog

*Requires `r_volumetricSelfShadow 1` or `2`, because this restores part of the light that self-shadowing removed.*

Stops thick white steam or cloud interiors becoming unnaturally black, while dark absorptive smoke remains much darker. Medium additional performance cost.

**Main controls**

- `r_volumetricMultiScatter 0 | 1 | 2` — off / sun / sun + self-shadowed dynamic lights.
- `r_volumetricMultiScatterOctaves 1..3` — scattering orders beyond single scattering.
- `r_volumetricMultiScatterAttenuation <value>` — absorption reduction per extra scattering step.
- `r_volumetricMultiScatterContribution <value>` — returned energy per octave.
- `r_volumetricMultiScatterPhase <value>` — anisotropy reduction per octave.
- `r_volumetricMultiScatterLength <units>` — characteristic dense-medium size.
- `r_volumetricMultiScatterShadowFill <value>` — limited fill into geometry shadows.

**Debug**

- `r_volumetricFogDebug 46..50` — single scattering, multiple scattering, combined result, ratio and optical depth.

A small series of approximate extra scattering steps returns some blocked light to the medium. It imitates light bouncing within a cloud without tracing a full chain of physical bounces.

## Colored light absorption

*Requires spatial fog and supported texture resources, because the renderer must keep separate red, green and blue transmission.*

Lets deep water lose red light and lets colored fog tint the scene behind it. The ordinary single-density path can darken a background but cannot absorb each color differently. Heavy memory and performance cost.

**Main controls**

- `r_volumetricFogRGBExtinction 0 | 1` — scalar / per-channel extinction and transmittance. Requires `vid_restart`.
- `r_volumetricFogHeightExtinction "r g b"` — relative RGB extinction for procedural height fog.

**Debug**

- `r_volumetricFogDebug 51..56` — RGB extinction, transmittance, color shift, difference heat, chroma and tail transmittance.

### [Autodetectable] Existing liquid profiles

With volumetric liquids enabled, water/slime/lava profiles already supply their absorption colors. `r_volumetricFogRGBExtinction 1` lets the volume retain those color differences.

### [For mod authors] Colored media

Use `fogExtinctionColor 3 0.5 0.5` in a BSP fog shader or `"Extinction": [3,0.5,0.5]` in a local volume. Height fog has `r_volumetricFogHeightExtinction`. EFX particle media currently keep neutral absorption.

The fog stores three transmission values instead of one. Existing liquid profiles supply useful absorption colors; authored fog can supply its own.

## Older ray-marched fog

*Requires the older volumetric-fog mode, because these tune its ray sampling rather than the froxel grid.*

Keeps the earlier light-grid fog path available for comparison and tuning. These settings are independent of the new fog-grid resolution. Medium performance cost when active.

**Main controls**

- `r_volumetricFogDefaultScale` — Scales volumetric fog density unless scale has been explicitly defined. Default `1.0`; `vid_restart`.
- `r_volumetricFogSamples` — How many ray samples to take. Default `48`; `vid_restart`.
- `r_volumetricFogScale` — Temporarily scales volumetric fog density. Default `1.0`.

**Debug**

- No separate debug controls.

The older path samples the light grid along each view ray. More samples reduce stepping but increase work, while its scale settings change density.
