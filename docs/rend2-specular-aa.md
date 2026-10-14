# Rend2 specular anti-aliasing

Rend2 can optionally increase GGX roughness from the screen-space variance of
the final shaded world-space normal.  This reduces unstable, sub-pixel
specular highlights without changing geometry or requiring temporal AA.

## Controls

- `r_specularAA 0|1` enables the shader path.  It is archived and latched;
  changing it requires `vid_restart`.  The default is `0`, which compiles out
  the uniform, derivatives and filtering and preserves the previous path.
- `r_specularAAStrength 0.15` scales normal variance at runtime.  Its supported
  range is `0..0.25`.
- `r_specularAAMaxAdded 0.25` caps how much roughness the filter may add.  Its
  supported range is `0..1`.
- `r_specularAADebug 0..4` is a cheat-protected view: `1` is material
  perceptual roughness after material/wetness processing, `2` is its GGX
  alpha, `3` is effective perceptual roughness and `4` is added roughness.

## Filter

Material roughness `r` is perceptual and GGX uses `alpha = r*r`. For final
normalized shaded normal `N`, the filter is:

```glsl
variance = strength * (dot(dFdx(N), dFdx(N)) + dot(dFdy(N), dFdy(N)));
materialAlpha = max(r * r, 0.0025);
filteredAlpha = sqrt(clamp(materialAlpha * materialAlpha + 2.0 * variance, 0.0, 1.0));
filtered = sqrt(filteredAlpha);
filtered = max(r, min(filtered, min(1.0, r + maxAdded)));
```

This is the screen-space geometric-normal filtering form described by
Kaplanyan et al., *Filtering Distributions of Normals for Shading
Antialiasing* (2016), and used by established real-time normal-variance
filters. Variance broadens `alpha^2`; conversion back through alpha keeps the
stored, IBL, SSR and debug value in the perceptual domain.

The result never lowers material roughness.  A single filtered value feeds
direct/deluxe GGX, primary sun, point/spot lights, LTC area lights, cubemap mip
and environment BRDF selection, specular occlusion, and SSR/SSGI roughness
metadata.  Burley diffuse retains the unfiltered material roughness.  The
Charlie cloth lobe is not filtered.

## Scope and limitations

The derivative is evaluated after normal mapping and weather/puddle/runoff
normal changes, so it includes resolved vertex-normal, authored normal-map,
generated-normal and wet-surface variation.  It cannot reconstruct normal
variance already lost inside a texture mip, and a constant normal on one flat
triangle has zero interior variance.  Hard triangle and UV boundaries remain
important validation cases because derivatives use a 2x2 fragment quad.

MSAA and SMAA remain independent.  They can help geometry edges and temporal
stability respectively, but neither is required by this filter.

## Validation

Use repeatable camera motion, not still images alone.  Compare off/on at
multiple resolutions and with MSAA and SMAA independently.  Include thin
armor highlights, a saber moving past metal, grazing views, distant NPCs,
normal-mapped floors, generated normals, wet/puddle surfaces, UV seams and
flat-shaded geometry.  `r_speeds 100` reports the existing main-view GPU timer
for a fixed-view before/after comparison.
