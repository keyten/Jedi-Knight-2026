# Characters and animation

[All rendering settings](../rendering-features.md)

How characters, NPCs and other models are lit, skin and hair materials, and how bodies move at joint limits.

## Light across the body

Characters aren't lit by the map's lightmaps. Instead the map stores a 3D grid of light samples (the light grid), each with an ambient color, a main light color and that light's direction. The original game picks **one** sample at the character's position and lights the whole body with it, so a character walking through a doorway changes brightness all at once, and an arm in a beam of light is no brighter than the rest.

**Cost:** light for mode 1, medium for mode 2.

- `r_entityLightGrid 0` *(restart)* — **0** one sample per character (original). **1** three samples at feet, waist and head height, blended up the body: a character standing in a shaft of light gets a lit head and dark legs. **2** every pixel takes its own smoothly interpolated grid sample: light changes across the body and along outstretched limbs.
- `r_entityLightProbes 1` *(restart)* — mode 2 only. The grid stores one main light direction plus flat ambient; this turns each sample into a smooth directional light, so the lit side wraps naturally around the body and the shadowed side isn't uniformly flat.

**Debug**

- `r_entityLightGridDebug 1..11` *(restart)* — ambient, main light, direction, validity, grid cell, the three modes, the difference between them, probe brightness and direction.

Design notes: [rend2-grid-reconstruction-removal.md](../rend2-grid-reconstruction-removal.md).

## Sun on characters

Needs realtime sun shadows ([`r_forceSun 1`](shadows.md#sun-shadows) on stock maps).

The light grid merges the sun and every lamp into one main light whose direction is an average, rarely where the sun really is. When a character steps into a sun shadow, Rend2 just darkens that merged light, so the shading still comes from the wrong side and doesn't match the shadow on the ground.

**Cost:** light.

- `r_entitySunSplit 0` *(restart)* — 1: take the sun out of the grid light and light models from the real sun direction, with per-pixel sun shadows. Only with `r_entityLightGrid 0`.
- `r_entityShadowAmbient 0` — in sun shadow the world loses its ambient light too, but characters keep theirs and look too bright. 0 darkens only the main light (original), 1 also the whole ambient, like the world, values in between partly.

**Debug**

- `r_shadowDebug 12` — model sun split.

## Ambient light from the surroundings

A character's ambient light from the grid is one color from every side. With this, the side facing a bright wall or an open door gets more light and the side facing a dark corridor less. Uses the map's cubemaps, which are built at map load.

**Cost:** light; longer map loading.

- `r_diffuseIBL 0` *(restart)* — 1 on.
- `r_diffuseIBLStrength 1` — how strongly the direction modulates the ambient.

**How it works.** For each cubemap the renderer computes how much light arrives from each direction. Models use that to vary the original grid ambient around the body; it modulates, rather than replaces, the map's lighting.

**Debug**

- `r_diffuseIBLDebug 1..5` — incoming light, modulation, old and new ambient, selected cubemap.

## Skin (subsurface scattering)

In real life light enters skin, scatters under it and comes out a few millimetres away, often reddened by blood. That's why faces have soft light-to-shadow transitions, and why ears and fingers glow red against a bright light. Without it skin looks like painted plastic. This works on existing characters: faces and hands of organic characters are recognized automatically. Droids, armor and fur are left alone.

**Cost:** light for mode 1, medium for mode 2.

- `r_skinSSS 0` *(restart)* — **0** off. **1** cheap soft falloff: light wraps a little further around the face, reds more than blues. An approximation, not real scattering. **2** real screen-space scattering: the skin's lighting is blurred over a few millimetres around each pixel, so pores and hard shadow edges soften while highlights stay sharp. Needs `r_hdr 1`.
- `r_skinSSSStrength 1` — mode 2: how much of the skin lighting is replaced by the scattered version.
- `r_skinSSSWidth 8` — mode 2: scattering distance in millimetres; 8 matches real skin.
- `r_skinSSSTransmission 0` — light shining through thin parts (ears, fingers) from behind; 0 off.
- `r_skinSSSMixedHeads 0` *(restart)* — 1: also scatter `*_head` textures, which mix face, hair and ears in one image. Eyes and hair may then blur.

`r_skinSSSCompare 1` shows the left half without and the right half with skin scattering.

**Mod authors:** `skinScatter <0..1>` sets a material's scattering strength (0 turns it off); `skinMask <image>` limits it to skin on a mixed texture (red channel).

**How it works.** Mode 2 renders the scene, then blurs only the diffuse light of skin pixels with a skin-shaped profile, in two passes, stopping at depth and normal edges so the blur doesn't leak onto the background. Mode 1 just changes the shading falloff per color channel.

- `r_skinSSSQuality 1` — blur taps per pass: 0 = 11, 1 = 17, 2 = 25.
- `r_skinSSSFollowSurface 1` — how strongly depth and normal edges stop the blur.
- `r_skinSSSWrap 0.3` — mode 1: how far light wraps.

**Debug**

- `r_skinSSSDebug 1..8` — classification, skin mask, raw skin lighting, blur passes, final change, everything but skin, blur radius.
- `r_skinSSSList [used|all|skin]` — which materials scatter and why.
- `r_skinSSSKernel` — print the blur profile.

Design notes: [rend2-skin-sss.md](../rend2-skin-sss.md).

## Hair and eyes

Hair gets a stretched highlight shaped by the strand direction, like real hair, and eyes a smooth wet shine. There is no switch: it is always on where a material supports it. On stock models that is only the few hair surfaces named exactly `hair`/`hairs` (some Jedi, Ugnaught and Weequay heads). Most stock hair is painted into the head texture, and eyes share textures with the face, so they need modded assets.

**Cost:** light, on these materials only.

**Mod authors:** declare `materialType hair` or `materialType eye` inside a lit stage; `materialType standard` opts out. `materialMap` holds hair direction in RG and coverage in B. On a mixed skin/eye texture use the same image as `skinMask`.

**How it works.** Hair reflects light differently along and across the strand direction, which stretches the highlight. Eyes use a smooth, non-metallic surface — a wet highlight, not a simulated eyeball.

**Debug**

- `r_materialModelDebug 1..7` — type, tangent, bitangent, hair direction, hair highlight, eye highlight, mask.

Design notes: [rend2-hair-eye-materials.md](../rend2-hair-eye-materials.md).

## Softer ragdolls

When a character dies, the ragdoll swings its limbs until each joint hits its hard limit and stops dead, so bodies look stiff and snap. With soft limits a limb slows down as it nears the end of its range and springs back a little. Works in SP and MP.

**Cost:** light (CPU), only while ragdolls are active.

- `r_ragSoftLimits 0` — 1 on.
- `r_ragSoftLimitZone 0.18` — how much of the end of each joint range is springy.
- `r_ragSoftLimitStiffness 0.25` — spring strength.
- `r_ragNeutralPull 0.0002` — weak pull toward the joint's middle position.

The original hard limit stays as the final stop.

## Softer animation (SP)

Animated limbs ease in and rebound slightly when they reach the end of their usual movement, instead of stopping abruptly. Works with the existing animations; single player only.

**Cost:** light (CPU), plus a one-time scan of the animation files.

- `r_animSoftLimits 0` — **0** off, **1** all characters, **2** everyone except the player.
- `r_animSoftLimitBounce 0.35` — how much a stopped motion bounces back.
- `r_animSoftLimitZone 0.18` — width of the soft zone at each end of the range.
- `r_animSoftLimitMargin 0.1` — extra range allowed beyond the measured motion; applied when the animations are scanned.

**How it works.** The renderer scans each animation file once to measure how far every joint moves, then eases motion near those limits. Only the displayed pose changes; gameplay and animation choice don't.

**Debug**

- `developer 1` — print the range-scan results.
