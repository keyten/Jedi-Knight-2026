# Rend2 weather material controls (`weatherResponse`)

Rain wetness, puddles and runoff (`r_weatherWetness`, `r_weatherPuddles`,
`r_weatherRunoff`) stay fully automatic. Existing shaders need no changes.
The optional `weatherResponse` keyword only fixes exceptions.

## Syntax

```
weatherResponse <wetness> [<puddle> [<runoff>]]
```

- Scales run from 0 to 4. `1` is the automatic response and `0` turns that channel off.
- Missing values are 1. The exception is a single `0`, which turns the whole response off.
- **Shader level** (outside a stage): sets the default for every stage.
- **Stage level**: overrides the shader-level value for that stage.
- The wetness scale multiplies the wet strength. Puddles and runoff only appear on wet surfaces, so `weatherResponse 0 1 1` still turns everything off.

```
textures/example/grate
{
	weatherResponse 0          // stays dry
	...
}

textures/example/floor_tiles
{
	{
		map textures/example/floor_tiles
		weatherResponse 1 0    // gets wet, no standing water
	}
}

textures/example/roof_slope
{
	weatherResponse 1.3 1 0.3  // wetter, weaker streaks
	...
}
```

## Automatic exclusions (`tr_weather.cpp` `R_WetnessStageExclusion`)

These are decided from shader and stage state, never from the file name:

- sky (`isSky`, `SURF_SKY`)
- portal
- water, slime, lava and fog contents
- sort after opaque
- blended or additive stages
- glow stages
- uniformly emissive stages (`emissiveColor` / `emissiveScale` with no emissive map)
- the first-person view weapon
- `weatherResponse 0`
- depth and shadow passes, and `r_lightmap`

**Cloth** (the `cloth` stage keyword or auto-PBR class cloth):
- never holds a puddle
- runoff × 0.3
- the wet response table already darkens it without making it glossier

**Metal** needs no flag. The wet layer only changes roughness, normal and the water film, never metalness.

## GPU

- The scales are folded on the CPU into the existing strength and coverage uniforms (`u_WetnessParams.x`, `u_PuddleParams.x`, `u_RunoffParams.x`), so the shading path is unchanged.
- `u_WeatherMaterial` (wetness, puddle and runoff scale, exclusion reason) is read only by the debug views.
- Cubemap, SSR and SSGI pick up the final normal and roughness automatically.

## Debug

`r_weatherWetnessDebug` (only while it is raining):

| Mode | Shows |
|------|-------|
| 27 | weather on (green) or excluded (red) |
| 28 / 29 / 30 | wetness / puddle / runoff scale: black = 0, grey = 1, yellow = above 1 |
| 31 | exclusion reason |

Mode 31 colours:

| Colour | Reason |
|--------|--------|
| grey | none |
| light blue | sky |
| purple | portal |
| blue | liquid or fog |
| cyan | translucent sort |
| orange | blended |
| yellow | glow |
| cream | emissive |
| pink | first person |
| red | `weatherResponse 0` |
| black | pass |

`r_weatherMaterialPrint 1` prints, for one frame, every drawn shader with a non-default response or an exclusion (name, scales, reason). The cvar then resets itself.
