# Sky, atmosphere and clouds

[All rendering settings](../rendering-features.md)

Haze over long distances, a computed sky, and 3D clouds with shadows. Both need a sun: maps that define one in their sky shader work as they are; on others use `r_forceSun 1` ([Shadows](shadows.md#sun-shadows)). `r_atmosphereInfo` and `r_cloudInfo` say why nothing is drawn.

## Atmosphere

In real air distant mountains and buildings turn pale and bluish, and the sky is brighter near the horizon. JKA draws the far distance as crisp as the near one. This adds that haze, and optionally replaces the painted skybox with a computed sky that matches the sun.

**Cost:** light to medium.

- `r_atmosphere 0` — 1 on.
- `r_atmosphereAerialScale 1` — **the main look setting.** 1 is physically correct, but JKA views span only a few hundred metres of air, so the haze is barely visible. 8–30 makes it clearly visible.
- `r_atmosphereSky 0` — **0** keep the skybox, add horizon haze and sun glow. **1** blend skybox and computed sky. **2** computed sky only. Maps with sky portals always use 0.
- `r_atmosphereSkyBlend 0.5` — mode 1: share of the computed sky.
- `r_atmosphereStart 0` — no haze closer than this distance (world units).
- `r_atmosphereSunGlow 1` — mode 0: glow around the sun over the skybox. Lower it if the skybox's painted sun is elsewhere.

**Compare:** `r_atmosphereCompare` — 1 split screen (original sky and distance left), 2 off.

**How it works.** The renderer models two kinds of scattering along long view paths: air molecules (Rayleigh), which make the sky blue and distances bluish, and dust/haze (Mie), which makes a whitish glow around the sun. The result is applied to distant surfaces and the sky before volumetric fog.

- `r_atmosphereRayleigh 1`, `r_atmosphereMie 1`, `r_atmosphereOzone 1` — density of air, of haze, and of ozone (sky color only).
- `r_atmosphereMieG 0.8` — how tightly the haze glow hugs the sun.
- `r_atmosphereSunIntensity 1` — sun brightness.
- `r_atmosphereSunColor 1` — sun color: **0** the map's sun as seen above the atmosphere, **1** as seen on the ground, **2** white.
- `r_atmosphereSunSize 0.53` — sun disc size in degrees.
- `r_atmosphereUnitScale 0.03` — metres per world unit (a 64-unit player is about 1.8 m).
- `r_atmosphereGroundZ auto` — world height of the ground; auto picks the lowest floor.
- `r_atmosphereAltitude 0` — ground altitude above sea level, in metres.

The original `r_drawSun` still decides whether a sun disc is drawn, so it isn't drawn twice.

**Debug**

- `r_atmosphereDebug 1..8` — air glow, haze glow, transparency, distance haze, computed sky alone, overlap with fog, lookup tables, distance bands.
- `r_atmosphereInfo` — configuration and status.

Design notes: [rend2-atmosphere.md](../rend2-atmosphere.md).

## Volumetric clouds

Real 3D clouds above the map, with shaded undersides and bright edges toward the sun, drifting with the wind. Optional cloud shadows move across the ground and through fog.

**Cost:** heavy.

- `r_clouds 0` *(restart)* — 1 on.
- `r_cloudCoverage 0.5` — 0 clear sky, 0.5 scattered clouds, 1 overcast.
- `r_cloudShadows 0` — 1: clouds shade the sunlit world and the sun in fog.
- `r_cloudWindSpeed 10`, `r_cloudWindDir 45` — wind in m/s and its direction in degrees.
- `r_cloudBase 1500`, `r_cloudTop 4000` — cloud layer height, in metres above the ground.
- `r_cloudDensity 1` — how thick the clouds are.
- `r_cloudScale 2` *(restart)* — render resolution divisor: 1 full, 2 half, 4 quarter. Lower numbers look sharper and cost much more.
- `r_cloudSkyPortal 0` — 1: also draw clouds on maps with sky portals (approximate).
- `r_cloudLegacy 1` — SP `fx_cloudlayer` entities: **0** ignored (their old haze is drawn), **1** used as a hint for the cloud style, **2** drawn as a literal ring (testing).

**Compare:** `r_cloudsCompare` — 1 split screen (original sky left, no cloud shadows there), 2 off. `r_cloudShadowsCompare` — 1 split screen, 2 off, for the shadows alone.

**How it works.** For each pixel a ray is marched through a layer of noise-shaped cloud density, checking light toward the sun at each step. The result is rendered at reduced resolution and averaged over frames.

- `r_cloudSteps 64`, `r_cloudStepLength 150` — maximum steps per ray and step length in metres.
- `r_cloudMaxDistance 60` — how far clouds are drawn, in km.
- `r_cloudShadowSteps 5` — light steps toward the sun.
- `r_cloudMSOctaves 3` — approximated multiple scattering; 1 single scattering only (darker clouds).
- `r_cloudAmbient 1` — sky light on clouds.
- `r_cloudAnisotropy 0.6` — strength of the bright edge toward the sun.
- `r_cloudDetail 1` — 0 skip the fine erosion noise (cheaper, blobbier).
- `r_cloudShapeScale 12`, `r_cloudDetailScale 1.5`, `r_cloudWeatherScale 40` — sizes (km) of the shape noise, detail noise and coverage map.
- `r_cloudTemporal 0.9` — frame averaging; 0 off.
- `r_cloudShadowExtent 8` — size of the cloud-shadow area around the camera, in km.
- `r_cloudShadowInterval 1` — frames between cloud-shadow updates.

**Debug**

- `r_cloudDebug 1..11` — ray interval, density, noises, sun light, scattering, history, radiance, legacy mask, coverage map, shadow map.
- `r_cloudInfo` — configuration and status.
- `r_cloudLayerTest` — test cloud-layer entity handling.

Design notes: [rend2-volumetric-clouds.md](../rend2-volumetric-clouds.md).

## Sky portal speed-up

On maps with a sky portal (a separate scene shown through the sky), the portal scene is shaded only where the main view leaves the sky visible. The image is unchanged, so there is no compare cvar.

- `r_skyPortalCull 0` — 1 on: shade only the visible part of the portal view.
