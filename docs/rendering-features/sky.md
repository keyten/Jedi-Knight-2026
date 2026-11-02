# Atmosphere and clouds

[All rendering features](../rendering-features.md)

Controls marked `vid_restart` need a renderer restart. Debug views use 0 to return to normal unless stated otherwise. Performance labels are rough relative costs, not benchmark guarantees.

## Atmosphere and sky

*Requires a usable map sun or `r_forceSun`, because sunlight drives the air scattering.*

Adds a hazy horizon and distance fading, with an optional analytic sky. Mountains and distant buildings blend into the air instead of staying as clear as nearby ones. Light to medium performance cost.

**Main controls**

- `r_atmosphere` — Long range atmosphere: aerial perspective of distant surfaces and the sky (Rayleigh / Mie), composited before the froxel fog. Needs a map sun (or r_forceSun). Default `0`.
- `r_atmosphereSky` — 0 keep the skybox with haze, 1 blend skybox and computed sky, 2 computed sky. Sky portals keep mode 0; the existing `r_drawSun` choice prevents duplicate sun discs. Default `0`.
- `r_atmosphereAerialScale` — multiplier of the aerial perspective path length only (1 = physical; JA views are a few hundred metres of air, 8-30 makes the haze visible). Default `1`.
- `r_atmosphereAltitude` — altitude of the ground in metres above sea level. Default `0`.
- `r_atmosphereGroundZ` — world z of the ground (altitude 0 + r_atmosphereAltitude), auto = lowest floor of the map. Default `auto`.
- `r_atmosphereMie` — Mie (aerosol, haze) density multiplier. Default `1`.
- `r_atmosphereMieG` — Mie anisotropy g (Cornette-Shanks). Default `0.8`.
- `r_atmosphereOzone` — ozone density multiplier (sky colour only). Default `1`.
- `r_atmosphereRayleigh` — Rayleigh (air molecules) density multiplier. Default `1`.
- `r_atmosphereSkyBlend` — Atmosphere sky 1: share of the analytic sky. Default `0.5`.
- `r_atmosphereStart` — distance in world units before which there is no aerial perspective. Default `0`.
- `r_atmosphereSunColor` — Atmosphere sun illuminance: 0 map sun at the top of the atmosphere, 1 map sun at the ground (the air above the camera is undone), 2 white. Default `1`.
- `r_atmosphereSunGlow` — Atmosphere sky 0: scale of the Mie sun glow over the skybox (lower it when the painted sun is elsewhere). Default `1`.
- `r_atmosphereSunIntensity` — sun illuminance multiplier. Default `1`.
- `r_atmosphereSunSize` — angular diameter of the sun disc in degrees. Default `0.53`.
- `r_atmosphereUnitScale` — metres per world unit (0.03: the 64 unit player box is ~1.8 m). Default `0.03`.

**Debug**

- `r_atmosphereDebug` — Views 1..8 inspect air/haze light, transmission, computed sky, overlap with local fog and distance bands. Default `0`.
- `r_atmosphereInfo` — Print atmosphere configuration and runtime status.

The renderer estimates scattering by air molecules and haze particles along long view paths. The skybox can remain, be blended with the computed sky, or be replaced; sky-portal maps use the overlay path. [Details](../rend2-atmosphere.md).

## Clouds and their shadows

*Requires a usable sun and `r_clouds 1` with restart, because the renderer allocates and lights a 3D cloud layer.*

Adds clouds with depth and shaded interiors instead of only painted sky. Optional cloud shadows dim sunlit scenery and fog as clouds move overhead. Heavy performance cost.

**Main controls**

- `r_clouds` — Enable the 3D cloud layer; needs a map sun or `r_forceSun`. Default `0`; `vid_restart`.
- `r_cloudShadows` — shadows of the clouds on the world (sun of lightall, froxel fog sun). Default `0`.
- `r_cloudAmbient` — sky ambient multiplier. Default `1`.
- `r_cloudAnisotropy` — scattering direction of the forward lobe (silver lining towards the sun). Default `0.6`.
- `r_cloudBase` — altitude of the cloud base in metres above the ground (r_atmosphereGroundZ, r_atmosphereUnitScale). Default `1500`.
- `r_cloudCoverage` — 0 clear, 0.5 the weather map, 1 overcast. Default `0.5`.
- `r_cloudDensity` — extinction multiplier (1 = 60 per km in a full cloud). Default `1`.
- `r_cloudDetail` — erosion by the detail noise (0 = off, cheaper). Default `1`.
- `r_cloudDetailScale` — size in km of one tile of the detail (erosion) noise. Default `1.5`.
- `r_cloudLegacy` — SP fx_cloudlayer entities: 0 ignored (legacy haze drawn), 1 style hint (tube: open sky overhead, alt: thin haze), 2 literal disc / ring at the entity height (debug). Default `1`.
- `r_cloudMaxDistance` — march distance limit in km (faded out towards it). Default `60`.
- `r_cloudMSOctaves` — multiple scattering octaves (1 = single scattering). Default `3`.
- `r_cloudScale` — march resolution divisor per axis (1, 2, 4), vid_restart. Default `2`; `vid_restart`.
- `r_cloudShadowExtent` — size in km of the cloud shadow map around the camera. Default `8`.
- `r_cloudShadowInterval` — frames between cloud shadow map updates. Default `1`.
- `r_cloudShadowSteps` — light march steps towards the sun (self shadow); the shadow map uses twice as many. Default `5`.
- `r_cloudShapeScale` — size in km of one tile of the shape noise. Default `12`.
- `r_cloudSkyPortal` — Clouds on sky portal maps: 0 off, 1 composited over the portal scene (approximate). Default `0`.
- `r_cloudStepLength` — march step length in metres (the step count adapts to the path through the layer, at least 16, at most r_cloudSteps). Default `150`.
- `r_cloudSteps` — max march steps per ray. Default `64`.
- `r_cloudTemporal` — weight of the reprojected history (0 = no temporal filter). Default `0.9`.
- `r_cloudTop` — altitude of the cloud tops in metres above the ground. Default `4000`.
- `r_cloudWeatherScale` — size in km of one tile of the weather (coverage) map. Default `40`.
- `r_cloudWindDir` — wind direction in degrees (world x axis = 0, y = 90). Default `45`.
- `r_cloudWindSpeed` — wind speed in metres per second. Default `10`.

**Debug**

- `r_cloudDebug` — Views 1..11 inspect cloud bounds, density, noise, lighting, frame history and cloud shadows. Default `0`.
- `r_cloudInfo` — Print cloud layer configuration and runtime status.
- `r_cloudLayerTest` — Exercise cloud-layer/legacy-entity selection.

The renderer traces a layer of noisy cloud density, checks light toward the sun and blends the result over time. Lower tracing resolution saves work; sky-portal support is opt-in and approximate. [Details](../rend2-volumetric-clouds.md).

## Sky-portal visibility optimization

*Requires a sky-portal map, because this limits which part of its extra view is shaded.*

Avoids shading sky-portal scenery behind parts of the main world that already cover it. The intended visible image stays the same.

**Main controls**

- `r_skyPortalCull` — 1 = the skyportal view only shades the pixels the opaque world of the main view leaves uncovered. Default `1`.

**Debug**

- No separate debug controls.

A coverage mask restricts the extra portal view to gaps left by the opaque main view.
