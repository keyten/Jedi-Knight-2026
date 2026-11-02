# Rendering settings

How to set up the graphics of this build's Rend2 renderer: what each feature changes in the game, how to turn it on, what it costs and which settings are worth touching.

Every new feature is **off by default**, so the game looks and behaves like stock Rend2 until you enable it. That includes the speed-ups (shader cache, faster loading, shadow caches, OpenGL 4.3 paths): they don't change the picture, but they are also off until you turn them on ([Loading](rendering-features/loading.md)). Settings inside a feature keep their recommended values, so turning a feature on gives its intended look. Type the commands in the console (`~`), or put them in a `.cfg` file and `exec` it.

## Reading the pages

- Each feature starts with what you will see in the game. If that doesn't interest you, skip to the next one.
- Settings are shown with their **default** value: `r_gtaoRadius 32`. In Debug lists the number is the view to pick instead.
- *(restart)* — takes effect after `vid_restart`.
- **Cost** — rough frame-time cost: light, medium or heavy. No cost line means no noticeable cost.
- **Compare** — the feature's compare cvar, see below.
- **How it works** — a short technical explanation, followed by the settings that only make sense after it.
- **Debug** — diagnostic views and commands. Set a view back to 0 for the normal image. Debug cvars only show debug views; comparing is the compare cvars' job.
- **Mod authors** — map and asset keywords; players can skip them.

## Comparing with the original

Every feature has one compare cvar named after it: `r_ssrCompare`, `r_volumetricFogHeightCompare`, `r_waterWavesCompare`… They are not saved, and none needs `vid_restart`.

- `0` — normal: the feature as its own settings configure it.
- `1` — split screen: left of the line the original Rend2 picture, right the feature. `r_compareSplit 0.5` moves the line (0 left edge … 1 right edge of the view).
- `2` — the feature off, the original everywhere, while its own setting stays on. Use it to switch features whose setting needs `vid_restart`: `r_ssr 1`, `vid_restart`, then flip `r_ssrCompare` between 0 and 2.
- `3` and up — extra views of some features, for example `r_toneMapCompare 3`.

A compare cvar does something only while its feature is on; otherwise it says so. A few features can't be split, because they move geometry, simulate the world or are decided once per frame for the whole view (plants, ragdolls, exposure…). Their compare cvar only has `2`, and `1` prints why.

- `r_compare` — list every compare cvar, what it compares, whether it can split and whether its feature is on. `r_compare reset` sets them all back to 0.
- `r_help <cvar>` — what a renderer cvar does, its values, default and an example. `r_help <text>` lists the renderer cvars whose name contains the text, e.g. `r_help puddle`.

A value a renderer cvar can't take — a word where it needs a number, a value out of range, a fraction where it needs a whole number, the wrong count of numbers for a color — is refused: the previous value stays and the cvar's help is printed, the way `r_vfog` prints its usage.

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
- [Changes since the 2026-10-28 audit](rendering-features/changes.md) (including the compare cvars, renamed and removed cvars, and the defaults that changed to off)
- [Cvar and command provenance](rendering-cvar-audit.md)
