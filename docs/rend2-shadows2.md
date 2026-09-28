# Rend2 sun shadows 2.0

`r_sunShadowMode` selects the complete sun-shadow implementation at renderer
startup. It is latched because it changes both the GLSL sampler type and the GL
texture comparison state.

- `0`: legacy three-cascade fitting, snapping, hardware comparison texture and
  the existing `r_shadowFilter` PCF path.
- `1`: stable cascade spheres, independent texel snapping, explicit split
  blending, manual raw-depth filtering and optional directional-light PCSS.

Dynamic-light cube shadow maps are unchanged. Ghoul2 caster/receiver behaviour, the caster LOD option and the
dynamic light cube bias option are described in [rend2-character-shadows.md](rend2-character-shadows.md).

## Modern cascade fit

The split distances retain the existing `CalcSplit()` and
`r_shadowCascadeZBias` policy. Each transition expands both adjacent render
ranges by half of `r_shadowCascadeBlend` times the smaller adjacent split span.

For each expanded slice, the renderer computes the analytic bounding sphere of
the symmetric perspective frustum slice. The radius is independent of camera
rotation and translation and is quantized to 1/16 world unit. The light-view
origin is projected onto the two shadow-plane axes and snapped independently to
that cascade's world-units-per-texel. Light-space depth is intentionally not
snapped.

The old implementation used function-static texel-size values inside the
cascade loop. They were initialized by the first cascade and then incorrectly
reused by the other two cascades. Legacy mode preserves that behavior for an
exact A/B path; modern mode calculates both axes for every cascade and snaps the
view before its matrix and frustum are built.

## Filtering and bias

Modern mode creates the sun array as `GL_DEPTH_COMPONENT24` without
`GL_TEXTURE_COMPARE_MODE`. Shaders bind it as `sampler2DArray`, fetch raw depth
with `texelFetch`, and perform comparisons manually. Legacy mode retains the
16-bit `sampler2DArrayShadow` texture. No sampler-object dependency is added.

The modern comparison depth combines:

1. a constant receiver bias expressed in world units;
2. a geometric-normal offset expressed in cascade texels;
3. a receiver-plane correction for every kernel offset.

The receiver-plane gradient is solved from screen-space derivatives of shadow
UV and depth. Corrections are clamped in world units to prevent derivatives at
geometry discontinuities from producing extreme bias.

PCSS uses a stable world-space Vogel pattern:

1. Search raw depth in a bounded world-space radius and collect depths less
   than the bias-corrected receiver depth.
2. Average the blocker depths.
3. Convert receiver/blocker depth separation back to world units and compute
   `penumbra = separation * tan(sunAngularDiameter / 2)`.
4. Clamp the penumbra in world units, convert it to the current cascade's UV
   scale, and run variable-radius manual PCF.

Because both separation and penumbra are in world units, cascade resolution
does not change the apparent light size. Only pixels in a configured split
overlap evaluate two cascades. Preset sample counts are:

| Quality | Blocker search | PCF |
| --- | ---: | ---: |
| low | 8 | 8 |
| high (default) | 12 | 16 |
| ultra | 24 | 32 |

## Screen-space contacts

The contact pass reconstructs the view-space receiver position and a
full-resolution geometric normal from the main depth buffer. It marches a
bounded ray towards the sun, rejects self hits with a normal/light origin
offset, applies a configurable depth thickness, fades the hit near maximum ray
length and fades towards viewport edges. A fixed midpoint step pattern avoids
screen-locked random jitter during motion. Its visibility multiplies the CSM or
PCSS visibility; it never replaces it.

## Controls

| Cvar | Default | Meaning |
| --- | ---: | --- |
| `r_sunShadowMode` | 1 | 0 legacy, 1 shadows 2.0; latched |
| `r_sunShadowAlphaCasters` | 1 | sun-shadow cutouts/receivers for `q3map_alphashadow` foliage and `surfaceSprites`; latched |
| `r_shadowCascadeBlend` | 0.10 | overlap as fraction of smaller adjacent span |
| `r_shadowDepthBias` | 0.15 | constant world-space receiver bias |
| `r_shadowNormalBias` | 0.75 | normal offset in cascade texels |
| `r_shadowSlopeBias` | 1.0 | receiver-plane correction multiplier |
| `r_shadowReceiverBiasClamp` | 4.0 | derivative correction clamp in world units |
| `r_shadowPCSS` | 1 | enable sun PCSS |
| `r_shadowPCSSQuality` | 1 | 0 low, 1 high, 2 ultra |
| `r_shadowSunAngularDiameter` | 0.53 | apparent source diameter in degrees |
| `r_shadowPCSSMaxPenumbra` | 32 | maximum world-space penumbra/search radius |
| `r_shadowDebug` | 0 | debug view, listed below |

The contact controls are `r_contactShadows`, `r_contactShadowLength`,
`r_contactShadowSteps`, `r_contactShadowThickness`, and
`r_contactShadowStrength`.

`r_shadowDebug` values are: 1 cascade colors, 2 raw depth, 3 fixed-radius PCF,
4 average blocker depth, 5 penumbra radius, 6 PCSS visibility, 7 contact-only,
8 final CSM/PCSS times contact visibility, 9 effective bias, 10 dynamic light cube
shadows only, and 11 Ghoul2 receivers (see rend2-character-shadows.md).

### Alpha-tested foliage casters

With `r_sunShadowMode 1` and `r_sunShadowAlphaCasters 1`, the runtime keeps the
legacy `q3map_alphashadow` keyword as an explicit caster hint. Such materials
may enter the sun cascade pass even when their visible sort is blended. The
shadow draw uses the authored `alphaFunc`; when the material only declares
alpha blending, it uses a 0.5 cutout. Non-converted blended materials disable
blending and force depth writes only for depth-only draws. Glass, smoke and
other unmarked blended materials remain excluded.

Simple one-stage unlit foliage such as `models/map_objects/yavin/fern3b` is
converted from alpha blend to an alpha-tested, depth-writing cutout. Its color,
camera depth and sun CSM use the same 0.5 silhouette, so later water cannot
paint over visible leaf pixels. The base stage uses the lightall vertex-light
path to receive CSM without sampling the world entity's light grid at the
origin. This intentionally loses the legacy soft alpha fringe; setting
`r_sunShadowAlphaCasters 0` restores the original blend after renderer restart.
Other marked blended materials still receive a 0.5 fallback in depth passes,
so their partially transparent fringes may not occlude later surfaces.

Generated `surfaceSprites` use their normal alpha test and retain the visible
camera's billboard axes, distance fade and wind phase in every cascade. This
prevents the light view from rotating or distance-culling grass independently
in each shadow map. Setting `r_sunShadowAlphaCasters 0` and restarting the
renderer disables these paths; legacy sun shadows and dynamic-light shadow maps
are unchanged.

## Performance and validation

GPU timers label each sun cascade render separately, the main-view sample cost
is visible in the main-view timer, and the contact pass is labelled
`AO composite/contact`. Compare those timers with `r_sunShadowMode 0/1`, PCSS
off/on, each quality preset, and contact shadows off/on. A split-overlap pixel
can execute twice the normal sun sample count; pixels outside overlaps never
sample a second cascade.

Recommended visual checks are slow forward/strafe motion, both transition
bands, close contacts, long shadows, sloped floors, rails and foliage,
wide/narrow FOV, several shadow-map sizes, and low sun angles. Use debug views
1, 3, 5, 7, and 8 to isolate the common failure modes.
