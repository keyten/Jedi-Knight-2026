# Rend2 emissive materials

Rend2 supports an optional, stage-local emissive material channel. It is
separate from the diffuse/albedo texture and is added after reflected lighting,
AO and shadowing. The same masked emission is also written to `glowImage` for
the bloom pipeline.

In strict legacy mode existing stages do not enable this path. The legacy
`glow` keyword keeps its old output there. Physical mode classifies compatible
stock glow/additive stages on the CPU and replaces their arbitrary amplitude
with canonical scene radiance.

## Shader keywords

```text
{
    map textures/example/panel_d
    emissiveMap textures/example/panel_e
    emissiveColor 1.0 0.25 0.05
    emissiveNits 2000
}
```

- `emissiveMap <image>` loads an sRGB-authored color texture. Black texels are
  non-emissive, so a texture can mask small lights without making the whole
  material bloom. `$whiteimage` creates constant emission.
- `emissiveColor <r> <g> <b>` sets the linear RGB emission color. It also uses
  `$whiteimage` if no emissive map was specified.
- `emissiveNits <value>` and `emissiveLuminance <value>` are aliases. A white
  texel has this reference luminance in cd/m2. Texture RGB and
  `emissiveColor` multiply it without per-pixel normalization.
- `emissiveScale <value>` remains a legacy engine-unit scalar. In physical
  mode one unit is `r_emissiveLegacyNits` (default 1000) reference-white nits;
  thus `emissiveScale 4` is 4000 reference-white nits, never 4 nits.
- `emissiveScale <r> <g> <b>` is shorthand for an RGB intensity with scalar
  intensity 1. It replaces the current emissive color.

Values are clamped only at zero. Values above 1 are intentional and remain HDR
when `r_hdr` is enabled. The effective linear emission is:

```text
texture(emissiveMap).rgb * emissiveColor * canonicalReferenceMultiplier
```

Emission uses the diffuse texture coordinates, including parallax adjustment in
the `lightall` path. On maps using the legacy display-encoded scene buffer, Rend2
decodes the existing reflected color, adds emission in linear HDR, and encodes
the combined result back for storage. Authoring values and scaling therefore
remain linear on both map types.

The nits value names reference white. A colored texture or color multiplier
naturally changes chromaticity and resulting photometric luminance; pixels are
not normalized back to the same luminance.

## Compatibility

With `r_pbrPhysical 0`, a stage must contain an emissive keyword, so unmodified
shader scripts and legacy glow follow their prior expressions. Physical mode
automatically migrates compatible stock stages; no asset edit is required.

## Automatic compatibility

`r_autoEmissive` is an immediate, archived switch. Its default is `0`, preserving
the legacy output. Set it to `1` to export these existing stages as emissive
sources without adding their color to the scene a second time:

- stages carrying the legacy `glow` keyword;
- standalone unlit `blendFunc add` stages that do not use lightall, a lightmap,
  detail rendering, or a sky surface.

The structural rule intentionally rejects lit color generators, lightstyle
passes, white diffuse surfaces and bright skies. Automatic filename-suffix
discovery is not used in strict legacy mode. In `r_pbrPhysical 1`, stock
glow/additive stages are classified once at shader registration and calibrated
to conservative defaults: indicator 60, screen 250, decorative/unknown 600,
panel 2000, lamp 3000, hot/lava 5000 and fictional energy/saber 40000 nits.
Names, `q3map_surfacelight`, texture mask/geometry, LTC confidence and
reconstructed baked-light calibration provide the evidence. Explicit nits
always win and stock PK3s are not modified.

`r_emissiveDebug 1` shows provenance, mode 2 shows false-color nits bands, and
mode 3 shows confidence. Physical SSGI uses the same visible scene radiance;
`r_ssgiEmissiveScale 1` is neutral. `r_ssgiGlowScale` remains a legacy-only
artistic approximation. Modern bloom is pre-exposure scene-linear; legacy
glow remains the compatibility mode.
