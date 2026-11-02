# Shadows

[All rendering features](../rendering-features.md)

Controls marked `vid_restart` need a renderer restart. Debug views use 0 to return to normal unless stated otherwise. Performance labels are rough relative costs, not benchmark guarantees.

## Soft sun shadows

*Requires the realtime sun-shadow path, because these settings change how its existing shadows are drawn.*

Gives buildings, trees and props more stable sun shadows with softer edges away from contact. Compared with the older cascades, camera movement causes less visible crawling. Medium performance cost; high-quality wide soft shadows can be heavy.

**Main controls**

- `r_sunShadowMode 0 | 1` — legacy / Shadows 2.0. Requires `vid_restart`.
- `r_shadowPCSS 0 | 1` — disable / enable PCSS.
- `r_sunShadowCache 0..3` — off / exact static BSP depth reuse / scrolling cascade clipmaps / clipmaps plus persistent world-space disk pages. Requires `vid_restart`.
- `r_sunShadowAlphaCasters 0 | 1` — alpha-tested foliage/cutout casters and receivers. Requires `vid_restart`.
- `r_shadowCascadeBlend <value>` — cascade overlap.
- `r_shadowDepthBias <units>` — constant receiver bias.
- `r_shadowNormalBias <value>` — normal offset.
- `r_shadowSlopeBias <value>` — slope-dependent bias.
- `r_shadowReceiverBiasClamp <units>` — maximum receiver correction.
- `r_shadowPCSSQuality 0..2` — low / high / ultra.
- `r_shadowSunAngularDiameter <degrees>` — apparent sun size.
- `r_shadowPCSSMaxPenumbra <units>` — maximum penumbra size.
- `r_sunShadowSplit 0 | 1` — Default 1: keep model shadows separate and sharp, then combine them with softer scenery shadows. Restart.
- `r_shadowPCSSCharacterLayers 0 | 1` — Default 1: the older character-edge treatment, used only with `r_sunShadowSplit 0`. Restart.

**Debug**

- `r_shadowDebug 1..13` — cascades, depth, PCSS, contact shadows, bias, point shadows, Ghoul2 receivers and character-layer view 13.

The renderer stabilizes the sun’s shadow-map regions and blends their borders. PCSS, percentage-closer soft shadows, looks for blockers and widens the shadow filter with separation. [Details](../rend2-shadows2.md).

## Character and small-object shadows

*Requires the corresponding sun, dynamic-light or contact-shadow feature, because these refine shadows that already exist.*

Reduces detached-looking shadows and inconsistent character self-shadowing. Keeping model shadows separate also prevents a broad scenery shadow from washing out a character’s contact shadow. Light to medium performance cost.

**Main controls**

- `r_shadowCasterLod 0 | 1` — shadow-view LOD / camera-view LOD for Ghoul2 shadow casters.
- `r_dlightShadowBias 0 | 1` — legacy / texel-scaled dynamic-light shadow bias.
- `r_contactShadowSoft 0 | 1` — binary / soft contact-shadow sampling.

**Debug**

- `r_shadowCasterStats 0 | 1` — print Ghoul2 caster and LOD statistics.
- `r_shadowDebug 10` — point-light shadow visibility.
- `r_shadowDebug 11` — Ghoul2 receiver/caster view.

The controls choose shadow-model detail, offset shadow tests more consistently and optionally soften short contact tests. The newer sun split draws models into separate layers before combining them with scenery. [Details](../rend2-shadows2.md).

## Shadow reuse

*Requires the corresponding shadow feature; the static point/spot cache needs GL 4.3 or image-copy support, because it copies reusable world depth.*

Keeps the same visible shadows while reducing work for unchanged lights and scenery. Moving characters can update their shadows without redrawing the whole static world.

**Main controls**

- `r_shadowCache` — Reuse unchanged point/spot shadow maps; conservative around animated world surfaces and dynamic casters. Default `1`.
- `r_shadowLayered` — With r_shadowSinglePass 1: 1 = all 6 faces of a point light cube in one layered draw command when supported. Default `1`.
- `r_shadowSinglePass` — Point light shadows: 1 = one scene walk per light, filtered per cube face (same depth as 0). Default `1`.
- `r_shadowStaticCache 0 | 1` — Disable / reuse static-world shadow depth. Default 1.

**Debug**

- `r_shadowStaticCache 2` — Compare one frame with a full redraw; slow diagnostic readback.
- `r_shadowStats` — Print per-frame CPU front-end / submit cost of shadow and camera views once per second. Default `0`.
- `r_shadowVerify` — 1 = next frame, render every point light shadow cube both ways (r_shadowSinglePass 0 and 1) and print the depth texture cells that differ. Default `0`.
- `r_sunShadowBenchmark` — Measure this many complete frame intervals, with GPU readbacks/stats disabled. Default `0`.
- `r_sunShadowVerify` — Compare cached sun depth with a full render: 1 next scene, 2 continuous (slow readbacks). Default `0`.

Caches keep reusable depth per light/slot. The new static cache restores world depth before drawing moving casters; sun modes separately reuse fixed or scrolling regions. Sun cache modes 2 and 3 remain experimental. [Details](../rend2-sun-shadow-cache-validation.md).
