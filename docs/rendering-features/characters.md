# Characters and animation

[All rendering features](../rendering-features.md)

Controls marked `vid_restart` need a renderer restart. Debug views use 0 to return to normal unless stated otherwise. Performance labels are rough relative costs, not benchmark guarantees.

SP means single-player; MP means multiplayer.

## Lighting across characters

*Requires the map’s baked light grid, because it supplies the existing room lighting.*

Stops a whole character being lit from just one sample. A head, torso and legs can receive different brightness near doorways or lamps; sunlight can also come from the actual sun direction. Light cost for multiple body samples, medium for per-pixel sampling.

**Main controls**

- `r_entityLightGrid 0 | 1 | 2` — legacy / three CPU samples / per-fragment GPU samples. Requires `vid_restart`.
- `r_entityLightProbes` — Use the original grid’s light direction for per-pixel character probes in `r_entityLightGrid 2`. Default `1`; `vid_restart`.
- `r_entityShadowAmbient` — How much sun shadows darken ambient model light: 0 directed light only, 1 all ambient too. Requires `r_sunlightMode 1`. Default `0`.
- `r_entitySunSplit` — Light legacy-grid models from the sun’s actual direction. Needs `r_sunlightMode`, `r_entityLightGrid 0` and normal or specular mapping. Default `0`; `vid_restart`.

**Debug**

- `r_entityLightGridDebug 1..9` — ambient, direct, direction, validity, cell, old/new modes and difference. Requires `vid_restart`.

The original path samples the grid once per entity. The new modes sample several body positions or interpolate the grid per pixel; optional directional probes retain more of the original sample’s light direction. [Details](../rend2-grid-reconstruction-removal.md).

## Softer skin lighting

*Requires HDR for mode 2, because that mode blurs the skin’s diffuse light before final display conversion.*

Makes skin transitions softer and can add light through thin parts such as ears. Mode 1 is a simple soft wrap; mode 2 spreads light over nearby skin pixels. Light performance cost for mode 1, medium for mode 2.

**Main controls**

- `r_skinSSS 0 | 1 | 2` — off / wrapped diffuse / screen-space diffusion. Requires `vid_restart`.
- `r_skinSSSMixedHeads 0 | 1` — also process mixed `*_head` textures. Requires `vid_restart`.
- `r_skinSSSStrength 0..1` — share of skin diffuse replaced by the diffused copy.
- `r_skinSSSWidth <mm>` — diffusion radius.
- `r_skinSSSQuality 0..2` — 11 / 17 / 25 taps.
- `r_skinSSSWrap <value>` — mode-1 wrap width.
- `r_skinSSSFollowSurface <value>` — depth/normal edge stopping.
- `r_skinSSSTransmission <value>` — optional back-light term.

**Debug**

- `r_skinSSSCompare 0 | 1` — split screen without / with SSS.
- `r_skinSSSDebug 1..8` — classification, mask, raw diffuse, blur passes, delta and radius.
- `r_skinSSSList` — print skin classification.
- `r_skinSSSKernel` — print the diffusion kernel.

### [Autodetectable] Stock skin

Enable `r_skinSSS` to use the existing skin classification. `r_skinSSSMixedHeads` also allows mixed head textures, which may need more care around eyes and hair.

### [For mod authors] Skin masks

Use `skinScatter <0..1>` for a material’s scattering strength and `skinMask <image>` to limit it to skin on a mixed texture.

Subsurface scattering approximates light entering skin and leaving nearby. The screen-space mode blurs only the skin diffuse term, preserving sharper reflections and limiting the blur at depth and normal edges. [Details](../rend2-skin-sss.md).

## Hair and eye highlights

Gives hair a stretched highlight that follows its direction, and gives authored eye areas a smoother wet shine. It does not infer separate eyes from every painted face texture. Light performance cost on these materials.

**Main controls**

- No separate switch; use the feature or asset settings described here.

**Debug**

- `r_materialModelDebug` — Hair/eye debug: 1 = type, 2 = tangent, 3 = bitangent, 4 = anisotropy direction, 5 = hair specular, 6 = eye specular, 7 = mask. Default `0`.

### [Autodetectable] Existing hair

Exact `hair`/`hairs` name tokens can select the hair model without changing assets. There is no global enable switch; ambiguous whole-head textures are left alone.

### [For mod authors] Hair direction and eye masks

Declare `materialType hair` or `materialType eye` inside a lit stage. `materialMap` stores hair direction in RG and special-material coverage in B. For mixed skin/eye textures use the compatible shared `skinMask` described in the detailed guide.

Hair reflects differently along and across a strand direction. Eyes use a smooth dielectric surface response; this is a wet highlight, not a simulated refracting eyeball. [Details](../rend2-hair-eye-materials.md).

## Soft ragdoll joints

*Requires an active Ghoul2 ragdoll, because this changes its joint limits rather than starting a ragdoll.*

Makes a falling body slow down and spring back near a joint’s limit instead of hitting a rigid stop. Existing character models can use it in SP and MP. Light CPU cost for active ragdolls.

**Main controls**

- `r_ragSoftLimits 0 | 1` — Enable soft limits; default 0.
- `r_ragSoftLimitZone <fraction>` — How much of the end of a joint range becomes springy; default 0.18.
- `r_ragSoftLimitStiffness <value>` — Strength of that spring; default 0.25.
- `r_ragNeutralPull <value>` — Pull toward the joint’s resting center; default 0.0002.

**Debug**

- No separate debug controls.

A small restoring force grows near the end of the joint range, with a weak pull toward its center. The existing hard limit remains as a final bound.

## Soft animation joints (SP)

*Requires the updated SP renderer and a skeletal animation, because this adjusts the rendered animation pose.*

Can give animated limbs a small elastic slowdown and rebound at the ends of their usual movement range. It works with existing animation files and is off by default. Light ongoing CPU cost plus an animation-file scan.

**Main controls**

- `r_animSoftLimits 0 | 1 | 2` — Off / all characters / everyone except the player; default 0. SP only.
- `r_animSoftLimitZone <fraction>` — Width of the soft end zone; default 0.18.
- `r_animSoftLimitMargin <fraction>` — Extra range allowed around measured animation motion; default 0.1, applied when the range is scanned.
- `r_animSoftLimitBounce <value>` — How much stopped motion rebounds; default 0.35.

**Debug**

- `developer 1` — Print animation range-scan diagnostics.

The renderer measures each animation’s joint ranges, then eases and rebounds motion near their ends. This affects the displayed pose rather than replacing gameplay animation selection.
