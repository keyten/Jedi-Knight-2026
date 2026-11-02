# Shadows

[All rendering settings](../rendering-features.md)

Realtime shadows from the sun and from dynamic lights, and the small contact shadows between them. The map's baked shadows are always there; these settings add shadows that move.

## Sun shadows

Characters, props and the world cast realtime sun shadows that move with them. Shadows are sharp where an object touches the ground and soften with distance, like real sunlight, and they no longer crawl and flicker when the camera moves.

**Stock maps don't get realtime sun shadows by default.** Rend2 only draws them on maps whose sky shader has the Rend2 keyword `q3gl2_sun`, and stock JKA maps don't. `r_forceSun 1` turns them on with the map's own sun direction. Many other features use the sun too — character sun lighting, sun light in fog, atmosphere, clouds — and need the same.

**Cost:** medium; wide, high-quality soft shadows can be heavy.

- `r_forceSun 0` — 1 realtime sun on maps without `q3gl2_sun`. (2 is a test mode: the sun circles the sky.)
- `r_forceSunAmbientScale 0.5` — with `r_forceSun`: brightness left in shadow; 0 black shadows, 1 no shadows.
- `r_sunShadowMode 1` *(restart)* — **0** Rend2's original shadows, **1** the improved ones described here.
- `r_shadowPCSS 1` — soft shadows that harden near contact; 0 uniformly soft edges.
- `r_shadowPCSSQuality 1` — 0 low, 1 high, 2 ultra.
- `r_sunShadowAlphaCasters 1` *(restart)* — leaves, grates, grass and other see-through textures cast shadows with holes, not solid blocks.
- `r_sunShadowCache 0` *(restart)* — reuse static world shadows instead of redrawing them every frame: **0** off, **1** exact reuse (safe), **2** scrolling cache, **3** scrolling cache with saved pages. 2 and 3 are experimental.

**How it works.** The sun's view is rendered into several shadow maps (cascades), each covering a larger area farther from the camera. Their position is snapped so the shadow texels don't swim, and neighbouring cascades are blended. PCSS (percentage-closer soft shadows) finds how far each shadowed point is from its blocker and widens the blur accordingly.

- `r_shadowSunAngularDiameter 0.53` — apparent size of the sun in degrees; larger gives softer shadows.
- `r_shadowPCSSMaxPenumbra 32` — upper limit of the softness, in world units.
- `r_shadowCascadeBlend 0.10` — width of the blend between cascades.
- `r_forceSunLightScale 1.0`, `r_forceSunMapLightScale 1.0` — with `r_forceSun`: sun and lightmap brightness.

Bias settings fix two opposite artifacts: dark speckles on lit surfaces ("acne") when too low, and shadows detached from their objects ("peter-panning") when too high.

- `r_shadowDepthBias 0.15` — constant offset, in world units.
- `r_shadowNormalBias 0.75` — offset along the surface normal, in shadow-map texels.
- `r_shadowSlopeBias 1.0` — extra offset on surfaces at a steep angle to the sun.
- `r_shadowReceiverBiasClamp 4.0` — limit on that slope correction.

**Debug**

- `r_shadowDebug 1..13` — cascades, depth, filtering, blockers, penumbra, PCSS, contact, final, bias, point shadows, character receivers, model sun split, character layers.
- `r_sunShadowVerify 1` — compare cached sun shadows with a full redraw on the next frame; 2 every frame (slow).
- `r_sunShadowBenchmark <frames>` — time that many frames without debug readbacks.

Design notes: [rend2-shadows2.md](../rend2-shadows2.md), [cache validation](../rend2-sun-shadow-cache-validation.md).

## Character sun shadows

A soft shadow that suits a building makes a character's shadow look blurry and detached, and broad scenery shadows wash out a character's own contact shadow. By default characters and other models are drawn into a separate sharp shadow layer and combined with the softer scenery shadows.

- `r_sunShadowSplit 1` *(restart)* — 1 separate sharp layer for models (characters, NPCs, MD3 props); 0 one shared shadow.
- `r_shadowPCSSCharacterLayers 1` *(restart)* — older variant: harder edges where character shadows meet scenery. Used only with `r_sunShadowSplit 0`.
- `r_shadowCasterLod 0` — 1: characters cast shadows with the same detail level you see. By default the shadow uses the detail level for its own distance, which can make it look simpler than the model.

**Debug**

- `r_shadowDebug 11`, `12`, `13` — character receivers and casters, model sun split, character layers.
- `r_shadowCasterStats 1` — print character shadow casters and their detail levels once per second.

Design notes: [rend2-character-shadows.md](../rend2-character-shadows.md).

## Contact shadows

Adds small sun shadows where the shadow maps are too coarse: under feet, behind small props, in carved detail. Needs realtime sun (see above).

**Cost:** light to medium.

- `r_contactShadows 0` — 1 on. Turning it on from off needs `vid_restart`.
- `r_contactShadowLength 16` — how far a contact shadow reaches, in world units.
- `r_contactShadowStrength 0.85` — darkness.
- `r_contactShadowSoft 0` — 1 softer edges.

**How it works.** For each pixel the shader steps a short way toward the sun through the on-screen depth image and checks whether something is in front. It can't see objects outside the screen.

- `r_contactShadowSteps 12` — steps per ray.
- `r_contactShadowThickness 6` — assumed thickness of what blocks the ray.

**Debug**

- `r_aoDebug 6` — contact shadows only.

## Dynamic light shadows

Sabers, explosions and other dynamic lights can cast realtime shadows. This is an original Rend2 setting; several features here build on it.

**Cost:** medium to heavy, depending on how many lights cast shadows.

- `r_dlightMode 1` *(restart)* — 2 dynamic lights cast shadows. With [Forward+](lighting.md#many-dynamic-lights-forward), `r_forwardPlusMaxShadowLights` limits how many.
- `r_dlightShadowBias 0` — 1 a more consistent offset, with less shadow acne and fewer detached shadows.

These keep the same shadows but redraw less. All are on by default:

- `r_shadowCache 1` — reuse a light's shadow while nothing in it changes.
- `r_shadowStaticCache 1` — keep the static world part and redraw only moving objects over it. Needs OpenGL 4.3 or `ARB_copy_image`.
- `r_shadowSinglePass 1` — walk the scene once per light instead of once per cube face.
- `r_shadowLayered 1` — draw all six cube faces in one call where supported.

**How it works.** Each shadowed light renders the scene into a cube of six depth images. The caches above skip redrawing parts that haven't changed.

**Debug**

- `r_shadowDebug 10` — point-light shadow visibility.
- `r_shadowStaticCache 2` — compare one frame with a full redraw (slow).
- `r_shadowVerify 1` — draw every cube both ways on the next frame and print differences.
- `r_shadowStats 1` — CPU cost of shadow and camera views, once per second.
