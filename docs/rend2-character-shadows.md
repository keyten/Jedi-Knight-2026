# Rend2: character (Ghoul2) self-shadowing

This is an audit of how animated Ghoul2 characters cast and receive shadows with the existing systems, plus
three small opt-in fixes and new debug views. It adds no new shadow system: the sun cascades
([rend2-shadows2.md](rend2-shadows2.md)), the dynamic light shadow cubes and the screen-space contact
shadows ([rend2-ambient-occlusion.md](rend2-ambient-occlusion.md)) are unchanged in structure.

All new behaviour is off by default. With `r_shadowCasterLod 0`, `r_dlightShadowBias 0` and
`r_contactShadowSoft 0` the output is the same as before.

## What Ghoul2 already casts and receives (from the code)

| Question | Answer | Where |
|---|---|---|
| Do G2 models cast into the sun cascades? | Yes. Every cascade view runs `R_GenerateDrawSurfs → R_AddEntitySurfaces → R_AddGhoulSurfaces` | `tr_main.cpp:2078`, `:2942` |
| Into dlight shadow cubes? | Yes, same path, one view per cube face | `tr_main.cpp:2746` |
| Which entities are skipped in shadow views? | Only `RF_FIRST_PERSON` (the view weapon, `VPF_NOVIEWMODEL`). `RF_THIRD_PERSON` bodies (own body in first person) *are* drawn into shadow views | `tr_main.cpp:1925`, `tr_ghoul2.cpp:3356` |
| Does `RF_NOSHADOW` stop CSM / cube casting? | No. It only controls the legacy stencil / projection shadows (`cg_shadows 2/3`) | `tr_ghoul2.cpp:2573`, `:2680` |
| Which surfaces cast? | Opaque sort, plus `q3map_alphashadow` cutouts with `r_sunShadowAlphaCasters` | `tr_main.cpp:1857` |
| Is skinning applied in the depth passes? | Yes: `LIGHTDEF/GENERICDEF_USE_SKELETAL_ANIMATION` for depth prepass and shadow draws | `tr_shade.cpp:1613`, `:1657` |
| Same bones in shadow and main views? | Yes. `RB_UpdateGhoul2Constants` transforms and uploads every skeleton once per scene before any view renders; shadow, depth prepass and main draws read the same `boneCache->uboOffset`. There is no path that uses the previous frame's bones (previous bones are only used for motion vectors) | `tr_backend.cpp:3032`, `tr_ghoul2.cpp:3595-3690` |
| Do G2 models receive sun shadows? | Yes: lightall entity stages get `USE_SHADOWMAP` and `TB_SHADOWMAP` like world surfaces | `tr_shade.cpp` (`r_sunlightMode` block) |
| Do the characters appear in the depth buffer used by contact shadows / GTAO? | Yes, through the depth prepass with the same skinning | `tr_backend.cpp` depth prepass |

### How contact shadows are combined (already correct)

```
shadowValue = cascadeShadow (CSM or PCSS) * contactShadow * N·L_sun      lightall.glsl:3085
r_sunlightMode 1: lightColor = mix(ambientScale * lightColor, lightColor, shadowValue)  :3095
r_sunlightMode 2: primary light color *= shadowValue                                   :3296
```

The contact term only reaches the direct primary (sun) light: for G2 entities that is the light grid's
directed light in mode 1 and the real-time sun in mode 2. Ambient, dynamic lights, cubemaps, SSR and SSGI are
not multiplied by it, and nothing multiplies the final scene colour. There is one contact system
(`ao_composite.glsl` `ContactShadow`) and no second one was added.

Observed and left unchanged:

- `N·L_sun` is part of `shadowValue`, and entity lighting then applies `N·L` of the light grid direction again
  (legacy double cosine on the sun side terminator).
- The PCSS sample rotation is hashed from 4-unit world cells (`ShadowStableAngle`). This is stable on static
  geometry. On moving characters the cell changes every few frames; the filter radius at character scale is
  about 0.5 texel, so the effect should be small. Check it in the temporal captures.

## Scale of the problem (default cvars)

`r_shadowMapSize 1024`, `r_shadowCascadeZNear 4`, `ZFar 3072`, `ZBias -320` give splits of about 210 / 870 / 3072
units. The cascade 0 bounding sphere has a radius of about 257 units, so one texel covers about 0.5 world units.
Normal bias is 0.75 texel (about 0.38 u), depth bias is 0.15 u, and the PCSS filter is at least 0.5 texel. A nose
is about 1.5 u, or about 3 texels. The cascades cannot resolve nose, brow, chin or armour overlap detail at any
reasonable resolution. That detail comes from contact shadows. Raising `r_shadowMapSize` is not the fix.

## Gaps and fixes

### 1. Caster LOD differs from receiver LOD (`r_shadowCasterLod`)

`G2_ComputeLOD` (`tr_ghoul2.cpp:1122`) calls `ProjectRadius` (`tr_mesh.cpp:26`) with the *current view*.
In a sun cascade that view is orthographic, and the projected radius becomes `r * 2 / cascadeWidth`, which is
tiny. Characters then cast with LOD 1–3 while the camera renders them at LOD 0. When the model is on the sun
side of the cascade centre, `dist <= 0` and the LOD snaps to 0. Two symptoms follow:

- the caster mesh differs from the receiver mesh, which gives self-shadow leaks and acne on the face, chin and
  armour edges;
- the shadow pops between LODs as the camera moves (temporal instability without any animation).

Point cubes pick the LOD from the light distance, which also differs from the camera LOD.

`r_shadowCasterLod 1`: shadow views use the projected radius the camera sees
(`G2_CameraProjectRadius`, `r / (dist * tan(fov_y/2))`, the value `ProjectRadius` returns for the main
perspective view). Casters behind the camera keep the old shadow-view LOD. No caster gets more detail than the
camera would give it, so the cost follows the main view and far NPCs stay coarse.

### 2. Point-light shadow bias is unbounded (`r_dlightShadowBias`)

The old code shortens the cube compare vector by `tan(θ)` world units (`sampleVector += L * tan(acos(dot(n,-L)))`):
about 1.7 u at 60°, 5.7 u at 80°, and without limit near grazing angles. The bias does not depend on
the cube texel size. At a saber 10–20 u from the body, a 512² cube texel is about 0.05 u, so arm-on-torso and
hand-on-torso shadows are biased away. This is peter-panning at exactly the character scale.

`r_dlightShadowBias 1` (`lightall.glsl` `EvaluateDynamicLight`):

- `texel = 2 * distance / DSHADOW_MAP_SIZE` (the world size of a cube texel at the receiver);
- normal offset `1.5 * texel * (1 - N·L)` along the geometric normal (the lookup direction follows the offset
  point);
- a compare bias of `texel * (1 + 2 * min(tanθ, 4))`, which covers the about 2-texel PCF footprint on slopes.

At 15 u from the light the largest bias is about 0.5 u, where the old code gave several units.

### 3. Binary contact shadows (`r_contactShadowSoft`)

`ContactShadow` stops at the first hit and returns a hard 0/1 per pixel with uniform 1.33 u steps. Edges on faces
alias, flicker by a pixel while characters animate, and the first unit (where nose and armour overlaps sit) is
undersampled.

`r_contactShadowSoft 1` (`ao_composite.glsl:157`):

- steps packed quadratically towards the receiver, `t = ((i + 0.5) / steps)² * length`;
- per step, soft occlusion by depth: `smoothstep(bias, bias + 2px + 0.1, behind) * (1 - smoothstep(thick/2, thick, behind))`;
- the same end-of-ray fade, and the maximum over all steps with no early exit.

This uses the same number of depth fetches. It adds a few ALU per step, and the worst case is always all steps.

Contact shadows stay sun/primary only. Dynamic lights already have real cube shadow maps, and no screen-space
rays were added for them.

### Saber and blaster lights

These are real dlights. `CG_DoSaber` / `CG_AddSaberBlade` add one at the blade midpoint (radius about
1.4 × length, `codemp/cgame/cg_players.c`). Weapon muzzle flashes and missiles add them in `cg_weapons.c` /
`cg_ents.c`. They get shadow cubes only with `r_dlightMode 2`: in legacy mode every UBO light, with Forward+ the
top `r_forwardPlusMaxShadowLights` (4). No lights are created by the renderer.

## Debug

| Cvar | View |
|---|---|
| `r_shadowDebug 1` | cascade index |
| `r_shadowDebug 6` | CSM/PCSS only |
| `r_shadowDebug 7` | contact only |
| `r_shadowDebug 8` | CSM × contact |
| `r_shadowDebug 9` | effective sun bias |
| `r_shadowDebug 10` (new) | point shadow only: lowest cube visibility over the dlights reaching the pixel (1 without cubes) |
| `r_shadowDebug 11` (new) | G2 view: skinned receivers magenta × (CSM × contact), everything else grey × (CSM × contact). Shows G2 self shadowing and G2 shadows on the world |
| `r_shadowCasterStats 1` (new, cheat) | once per second: G2 models per frame in camera views, cascade 0/1/2, dlight cubes; LOD histogram; number whose LOD differs from the camera LOD |

The `r_shadowDebug` views need `r_sunShadowMode 1` and are written unlit, bypassing tone mapping.

## Cvars

| Cvar | Default | Meaning |
|---|---:|---|
| `r_shadowCasterLod` | 0 | 1 = G2 casters in sun cascades and dlight cubes use the camera LOD |
| `r_dlightShadowBias` | 0 | 1 = texel-scaled normal offset + clamped slope bias for dlight cubes |
| `r_contactShadowSoft` | 0 | 1 = soft, receiver-dense contact shadow march |
| `r_shadowCasterStats` | 0 | cheat, console statistics |
| `r_shadowDebug` | 0 | now 0–11 |

None of them is latched.

## GPU cost

- `r_shadowCasterLod`: more vertices only in cascades/cubes, for characters the camera sees at a finer LOD than
  the shadow view picked. The worst case is each visible character drawn at its main-view LOD in each cascade
  that contains it. Casters behind the camera are unchanged. Compare the sun cascade timers with `r_speeds`.
- `r_dlightShadowBias`: about 15 ALU per shadowed light per fragment. The number of samples is unchanged.
- `r_contactShadowSoft`: the same texel fetches (at most `r_contactShadowSteps`, 12), with no early exit. About
  10 ALU per step. It runs at full resolution once per view and does not depend on the number of characters. See
  the `AO composite/contact` timer.
- Debug views and stats cost nothing when off.

## Captures (to be taken in game)

Kit: `tools/charshadow_ab.cfg` (copy it to `base/`, `exec charshadow_ab.cfg`). F9 gives a face close-up camera,
F8 an A/B series, F7 a debug series, F3 stats, F2 timing. Spawn several generic NPCs (`npc spawn stormtrooper`,
`npc spawn reborn`).

Scenes: nose on cheek, brow / eye socket, chin / neck, arm / torso, leg / leg, character / ground. Conditions:
moving animation, saber combat (`r_dlightMode 2`), crouch, several characters, face close-up, sunlight, dynamic
lamp, character partly off screen. For temporal stability, record a short video for each A/B while the character
idles and runs.

To find the cause of each defect, fill in one row per capture:

| Capture | Defect | Cascade (dbg 1) | CSM only (6) | Contact (7) | Bias (9) | Point (10) | Cause |
|---|---|---|---|---|---|---|---|
| nose/cheek | | | | | | | resolution / cascade / receiver bias / normal bias / peter-panning / missing screen-space detail |

Reading guide:

- missing in 6 but present in 7: CSM resolution; contact covers it as designed;
- present in 6 with the wrong shape, and it changes with F6: LOD mismatch;
- light leak along the terminator that grows with 9: normal bias;
- shadow detached from its caster: peter-panning (depth/slope bias; point lights: F4);
- missing in 6 and 7: missing screen-space detail (occluder off screen or thinner than one pixel).

Only if the captures show acne or peter-panning on characters that these three fixes do not cover will a
follow-up add near-character CSM bias or filter tuning. That tuning would be driven by generic G2 entity bounds
and would never be model-specific.

## Sun light of models: the grid sun split (`r_entitySunSplit`)

The three fixes above improve the shadow map. The bigger problem with sun shadows on players and NPCs is the light
that the shadow darkens. Models are lit by the light grid, which stores one directed light per cell: the sum of
every light the cell sees (sun, lamps, bounce) along their mean direction. With `r_sunlightMode 1` (the default),
lightall computes:

```
shadowValue = cascadeShadow * contactShadow * NPL          // NPL: the SUN direction
lightColor  = mix(sunAmbCol * gridDirected, gridDirected, shadowValue)
out         = lightColor * reflectance * NL                // NL: the GRID direction
```

This has five defects:

1. The shadow follows the sun, but the light it darkens comes from the grid direction. Self-shadows (an arm on
   the torso, the head on the shoulders) fall where the main light on the model does not come from.
2. The cosine is applied twice (NL along the grid direction times NPL along the sun), so the terminator is too dark.
3. The sun shadow also darkens lamps and bounce light that are in the grid light.
4. The modulation can only darken. The grid is sampled at one point. If that point is in shade, a model whose
   shoulders are in the sun stays dark, because the cascades cannot add light the sample does not have.
5. With `r_sunlightMode 2` the sun is added analytically, but the grid keeps its baked sun. The result is a double
   sun, and the shadow is weak because the grid sun is never shadowed.

With `r_entitySunSplit 1` (latched, default 0 = legacy, bit for bit) models have three terms:

- **ambient**: the grid ambient, unchanged;
- **residual directed**: the grid directed light minus its sun part, along its own direction. The sun shadow does
  not touch it;
- **sun**: an analytic light along the scene sun direction, with the strength of the grid's sun, shadowed per pixel
  by the cascades, contact shadows, POM self-shadow, cloud shadows and liquid transmittance. It goes through the
  same BRDF lobe as `r_sunlightMode 2` (Burley/GGX, skin SSS, energy partition), with a single cosine.

CPU side (`tr_light.cpp`):

- `R_CalibrateEntitySun` runs at map load and finds the grid's sun. This is the median directed light (style 0,
  raw grid units) of the cells whose direction is within 10° of the sun, or within 20° if fewer than 16 cells
  qualify. If no sun is found, or the map has no sun, the map keeps the legacy light.
- `R_SetupEntitySunSplit` runs per entity per frame. It casts 5 probe rays toward the sun from the lighting origin,
  the head (+40), the feet (−16) and ±32 across the sun. The rays use the same world trace as the static lighting
  sun visibility, and the results are cached on an 8-unit grid for 1 s. The sun part of the sample is
  `sunlit · min(S, D · max(dot(gridDir, sunDir), 0))`: it is bounded by the projection on the sun direction, so
  light from elsewhere is never removed. The residual vector is `D·gridDir − sunPart·sunDir`. The analytic sun is
  the full calibrated `S` if any probe sees the sky (the cascades shape it per pixel). If no probe sees it (deep
  inside, or probes that start in solid, like a ship parked into the world), the model keeps the legacy light,
  which stays darkened by the cascades, so the sun never leaks into interiors and grid light is never left unshadowed.

GPU side: `USE_ENTITY_SUN_SPLIT` (name suffix `_ESUN`) is set on `_GRID` permutations when the split is wanted. The
Entity block gains `u_EntitySun` (rgb sun, w = 1 for split, 0 = legacy light). The world entity, non-model entities
and maps without a calibrated sun upload w = 0 and keep the legacy path in the same program. In a view without the
sun (no cascades of its own) the analytic sun is left unshadowed, so the total light is preserved.

Requirements: `r_sunlightMode` 1 or 2, `r_entityLightGrid 0` without its debug view, and normal or specular mapping
(the fast vertex-light path is not split). The multi-point and GPU grids are a follow-up: they would subtract the
sun per sample.

Debug: `r_shadowDebug 12` (needs `r_sunShadowMode`). Red is the sun's share of the model's direct light, green is
the sun's visibility. Legacy light and the world show as dark blue.

Expected visual change: shadow edges and self-shadows on characters line up with the lit side, lamps stay lit in
the sun's shadow, and shoulders in the sun stay bright when the feet are in shade. Shadows on models in the sun's
shadow get darker than with `sunShadowScale` (they keep the ambient and the non-sun grid light, like the world's
baked shadows), and brightness in full sun stays about the same.

Not run in game yet. Builds (MSVC SP+MP) and all 12 lightall permutations (mode 1/2 × split on/off × skeletal,
skin SSS, POM, physical PBR, cubemap, dlight shadows) link on Intel UHD and RTX 2060.

## Grid ambient in the sun's shadow (`r_entityShadowAmbient`)

Why models read brighter than the ground in shadow (`r_sunlightMode 1`, including `r_forceSun 1`): the world
modulates its **whole** lightmap by the shadow, `mix(sunAmbCol · L, L, shadow)`, so ambient and baked sun drop
together to `sunAmbCol` (0.5 by default: `q3gl2_sun` shadow scale or `r_forceSunAmbientScale`). A model only
modulates its grid **directed** light. The grid ambient (×`r_ambientScale` 0.6, plus the LDR minimum light) stays
at full strength. In a sunlit cell a shadowed model keeps roughly 65–75 % of its lit brightness, while the ground
keeps 50 %.

`r_entityShadowAmbient` (archived, not latched, 0..1, default 0 = legacy, bit for bit) is the share of the grid
ambient that follows the lightmap's factor:

```
ambient *= mix(1, mix(sunAmbCol, 1, visibility), r_entityShadowAmbient)
visibility = cascades × contact shadows (× POM self-shadow × liquid sun transmittance)
```

Ambient has no direction, so unlike the directed light it uses no sun cosine: the side of a sunlit model that faces
away from the sun is not darkened. At 1 a model in shadow drops like the lightmap. Values in between keep part of
the ambient as unoccluded sky light. Physically only the sun share of the ambient should drop, which is the
argument for staying below 1.

- Applies to every grid mode (legacy, `r_entityLightGrid 1/2`, L1 probes) and works with or without
  `r_entitySunSplit`. It acts before AO, which multiplies on top.
- `r_sunlightMode 1` only. In mode 2 `sunAmbCol` is a light colour rather than a factor, so the value is not
  uploaded.
- It needs no new permutation: the value rides in `u_AOParams2.w` (Scene block, previously unused).

Not run in game. MSVC SP+MP build OK; 16 lightall `_GRID` permutations link on Intel UHD and RTX 2060.
