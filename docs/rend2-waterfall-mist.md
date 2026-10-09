# Waterfall spray and volumetric mist

This feature adds an opt-in waterfall-base treatment without a new volume
renderer or a second lens-droplet system. It reuses three existing paths:

- stock `fx_runner` effects provide visible spray particles;
- the existing FX physicalization `Mist` profile submits their
  `refVolParticle_t` medium to froxel fog;
- inferred/authored fallback media use the local fog-volume API, and camera
  spray feeds the existing LensWater `SPRAY` input.

Froxel injection therefore supplies sun, baked-light and dynamic-light
interaction. Mist uses water-like scattering albedo (`0.94, 0.97, 1.0`),
forward anisotropy (`g=0.62`) and the normal soft-volume density falloff.
There is no waterfall-specific lighting pass.

## Source resolution

Sources are resolved only for waterfall bodies classified by the dedicated
waterfall path, in this order:

1. A matching `WaterfallEmitters` entry with `ExistingFx: true`. Its map FX
   runner owns particles and volumetric media; only optional LensWater
   coupling is added.
2. A matching authored emitter. `Origin` is optional; without it the resolved
   bottom/impact edge of the selected sheet is used.
3. The bottom/impact edge inferred from an explicitly classified waterfall
   sheet.

No shader-name search or arbitrary world coordinate is performed by the
renderer. Effect-only waterfall-named entities in `vjun1` and `yavin2` remain
ordinary FX and are not promoted to waterfall geometry.

`WaterfallEmitters` can be placed in `cubemaps/<map>/env.json`, but the
preferred additive form is `cubemaps/<map>/waterfalls.json`:

```json
{
  "WaterfallEmitters": [
    {
      "Selector": { "Shader": "textures/h_evil/wfall" },
      "ExistingFx": true,
      "Radius": 176,
      "Density": 1.2,
      "LensStrength": 1.0
    }
  ]
}
```

Selectors accept the same `Shader`, `ShaderPrefix`, `BodyId`, `Point`, and
`Bounds` keys as `Waterfalls`. A sidecar is loaded after `env.json`; it does
not replace or erase `Cubemaps`, `FogVolumes`, `HeightFog`, or
`LensWaterEmitters`.

## Controls

All additions are off by default.

| CVar | Default | Meaning |
|---|---:|---|
| `r_waterfallMist` | `0` | master for waterfall spray and suspended medium |
| `r_waterfallMistDensity` | `1` | extinction/optical-depth multiplier |
| `r_waterfallMistRadius` | `1` | FX proxy, fallback volume and lens reach multiplier |
| `r_waterfallMistBalance` | `0.65` | `0` leaves particle spray only; `1` applies the full mist medium |
| `r_waterfallMistLens` | `0` | strength of nearby LensWater `SPRAY` coupling |
| `r_waterfallMistDebug` | `0` | cyan mist bounds and green lens-source crosses |

The stock `env/waterfall_mist.efx` fingerprint already has the reviewed
physicalization `Mist` profile. `r_waterfallMist 1` opts that one exact stock
effect into physicalization without enabling unrelated legacy FX. The normal
`r_volumetricFog 2`, depth-prepass and volumetric-particle requirements still
apply.

## Stock-map overlay

The installed-asset audit found waterfall sheets only in `t3_hevil`, `yavin1`
and `yavin1b`. `yavin1` and `yavin1b` already have spatially associated
waterfall FX, so their sidecars declare `ExistingFx`. `t3_hevil` has eight
classified strips and no nearby FX; its overlay preserves the complete stock
entity lump and appends eight reserved-bit `env/waterfall_mist` runners at the
audited impact points. Those runners are discarded at map spawn while the
master cvar is zero.

Build the non-destructive PK3:

```text
python tools/rend2/build_water_body_overlay.py <game-base> <output.pk3>
```

The builder copies and merges pre-existing `env.json` content, writes separate
waterfall sidecars, and copies the full existing `.ent`/BSP entity string
before appending the `t3_hevil` runners.

