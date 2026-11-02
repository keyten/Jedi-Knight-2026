# Rendering settings

How to set up the graphics of this build's Rend2 renderer: what each feature changes in the game, how to turn it on, what it costs and which settings are worth touching.

Nearly every new feature is **off by default**, so the game looks like stock Rend2 until you enable it. Type the commands in the console (`~`), or put them in a `.cfg` file and `exec` it.

## Reading the pages

- Each feature starts with what you will see in the game. If that doesn't interest you, skip to the next one.
- Settings are shown with their **default** value: `r_gtaoRadius 32`. In Debug lists the number is the view to pick instead.
- *(restart)* — takes effect after `vid_restart`.
- **Cost** — rough frame-time cost: light, medium or heavy. No cost line means no noticeable cost.
- **How it works** — a short technical explanation, followed by the settings that only make sense after it.
- **Debug** — diagnostic views and commands. Set a view back to 0 for the normal image.
- **Mod authors** — map and asset keywords; players can skip them.

## Before you start

- **HDR** (`r_hdr 1` and `r_toneMap 1`, both on by default) is needed for tone mapping, exposure, color grading, HDR bloom, motion blur, skin scattering and lens water. If you turned them off, these features do nothing.
- **Realtime sun.** Stock maps get no realtime sun shadows unless you set `r_forceSun 1`. Sun shadows, sun beams in fog and the sun on characters depend on it. See [Shadows](rendering-features/shadows.md#sun-shadows).
- **Forward+** (`r_forwardPlus 1`) is needed for lamp-shaped lights and blade-shaped saber light.
- **Volumetric fog** (`r_volumetricFog 2`) is needed for ground haze, rain haze, volumetric smoke, underwater fog and fog in reflections.
- **Modern water** (`r_waterSurface 1`) is needed for almost everything on the water page.

## Pages

- [Image and camera](rendering-features/post-processing.md) (tone mapping, exposure, bloom, color grading, motion blur, anti-aliasing)
- [Materials](rendering-features/materials.md) (metal, cloth and plastic from stock textures, shiny troopers and droids, glowing surfaces, relief)
- [Lighting](rendering-features/lighting.md) (ambient occlusion, many dynamic lights, bounced light, lamp- and blade-shaped lights)
- [Shadows](rendering-features/shadows.md) (realtime sun shadows, character shadows, contact shadows, dynamic-light shadows)
- [Characters](rendering-features/characters.md) (light across the body, sun on characters, skin, hair, softer ragdolls)
- [Reflections](rendering-features/reflections.md) (screen-space reflections, rough reflections, fog in reflections)
- [Fog and light beams](rendering-features/fog.md) (volumetric fog, ground haze, thick smoke)
- [Smoke and effects](rendering-features/effects.md) (volumetric smoke from effects, lit smoke sprites)
- [Sky](rendering-features/sky.md) (distance haze, computed sky, 3D clouds)
- [Weather](rendering-features/weather.md) (rain streaks and haze, wet surfaces, puddles, splashes, lens water)
- [Water](rendering-features/water.md) (reflective water, underwater fog, waves, ripples, foam, waterfalls)
- [Grass and plants](rendering-features/foliage.md) (fuller grass, wind, walking through plants, Force and saber interaction)
- [Loading and OpenGL](rendering-features/loading.md) (shader cache, faster loading, OpenGL 4.3 paths)
- [Lighting reconstruction](rendering-features/lighting-tools.md) (offline research tools, no effect on the game)
- [Changes since the 2026-10-28 audit](rendering-features/changes.md)
- [Cvar and command provenance](rendering-cvar-audit.md)
