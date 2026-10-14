# Rend2 specular anti-aliasing

Rend2 increases GGX roughness from unresolved normal variance. The existing
filter has two automatic inputs: a cheap screen-space derivative estimate and
normal length retained by filtered normal-map mips. This reduces unstable,
sub-pixel highlights without asset reauthoring or an extra texture fetch.

## Controls

- `r_specularAA 0|1` enables the shader and normal-mip path. It is archived
  and latched; changing it requires `vid_restart`. The conservative stock-asset
  default is `1`; `0` compiles out the filter and retains the historical normal
  texture format.
- `r_specularAAStrength 0.15` scales normal variance at runtime.  Its supported
  range is `0..0.25`.
- `r_specularAAMaxAdded 0.25` caps how much roughness the filter may add.  Its
  supported range is `0..1`.
- `r_specularAADebug 0..6` is a cheat-protected view: `1` is material
  perceptual roughness after material/wetness processing, `2` is its GGX
  alpha, `3` is effective perceptual roughness, `4` is added roughness, `5`
  is combined normal variance and `6` is effective GGX alpha.

## Filter

Material roughness `r` is perceptual and GGX uses `alpha = r*r`. For final
normalized shaded normal `N`, the filter is:

```glsl
geometric = min(derivativeVariance(geometricNormal), 0.01);
detail = min(max(derivativeVariance(shadingNormal) -
                 derivativeVariance(geometricNormal), 0), 0.25);
mipVariance = max(1 - dot(filteredMipNormal, filteredMipNormal), 0);
variance = strength * min(geometric + detail + mipVariance, 1);
materialAlpha = max(r * r, 0.0025);
filteredAlpha = sqrt(clamp(materialAlpha * materialAlpha + 2.0 * variance, 0.0, 1.0));
filtered = sqrt(filteredAlpha);
filtered = max(r, min(filtered, min(1.0, r + maxAdded)));
```

This is the compact normal-distribution filtering form described by
Kaplanyan et al., *Filtering Distributions of Normals for Shading
Antialiasing* (2016), and used by established real-time normal-variance
filters. GGX has no finite second moment, so broadening `alpha^2` by twice the
unit-normal variance is a bounded moment-matching approximation rather than an
exact GGX convolution. Variance is never added to perceptual roughness or to
alpha itself. Conversion back through alpha keeps the stored, IBL, SSR and
debug value in the perceptual domain.

The derivative estimate separates geometric-normal variation from residual
shading-normal detail. The geometric term has a tight cap so silhouettes,
hard triangle edges and invalid derivative helper lanes cannot make an entire
highlight matte. A looser residual cap retains useful filtering for strong
normal maps, POM and procedural wet normals.

## Normal-map mip variance

With AA enabled, normal-map upload reconstructs a trustworthy base Z and
retains the unnormalised average XYZ vector in every mip. Its length is the
Toksvig signal: for unit source normals, `1 - |mean(N)|^2` is the variance lost
inside the filtered footprint. The shader reads it from the normal sample it
already needed, then reconstructs shading Z from X/Y exactly as before.

BC7 is preferred, BC3 is the compressed fallback, and RGBA8 is the
uncompressed fallback because the old two-channel LATC representation cannot
retain the length signal. BC7, BC3 and LATC/BC5 use the same block storage;
RGBA8 only affects systems without those compression formats. Data is built
at texture upload, so source assets and the deterministic material cache are
unchanged and need no version migration.

The result never lowers material roughness.  A single filtered value feeds
direct/deluxe GGX, primary sun, point/spot lights, LTC area lights, cubemap mip
and environment BRDF selection, specular occlusion, and SSR/SSGI roughness
metadata.  Burley diffuse retains the unfiltered material roughness.  The
Charlie cloth lobe is not filtered.

## Scope and limitations

The derivative is evaluated after normal mapping and weather/puddle/runoff
normal changes. The mip signal covers authored and generated normal maps after
minification; a constant normal on one flat triangle correctly adds nothing.
The maximum-added control remains a final perceptual-domain safety bound.

Wetness changes material roughness first; filtering happens afterward, so a
smoother wet film cannot erase unresolved orientation. Burley diffuse keeps
the post-wetness material roughness. Standard, skin and eye GGX use the common
effective value. Hair uses it as an isotropic footprint broadening of the base
alpha before its tangent/bitangent stretch; this is deliberate and approximate.
Charlie cloth remains outside the GGX filter.

AutoPBR roughness is limited to a 0.75..1.0 multiplier, generated from local
detail magnitude rather than raw luminance, and ordinary mip filtering reduces
rather than amplifies its variation with distance. It therefore contributes
material variation, not unresolved normal variance, and is not folded into the
AA term a second time.

MSAA and SMAA remain independent.  They can help geometry edges and temporal
stability respectively, but neither is required by this filter.

## Validation

Use repeatable camera motion, not still images alone.  Compare off/on at
multiple resolutions and with MSAA and SMAA independently.  Include thin
armor highlights, a saber moving past metal, grazing views, distant NPCs,
normal-mapped floors, generated normals, wet/puddle surfaces, UV seams and
flat-shaded geometry.  `r_speeds 100` reports the existing main-view GPU timer
for a fixed-view before/after comparison.
