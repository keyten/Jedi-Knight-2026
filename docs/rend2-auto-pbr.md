# rend2 auto PBR for legacy materials (`r_autoPBR`)

Experimental. Gives Jedi Academy materials that have no authored PBR data sensible constant PBR
parameters. It extends the existing `lightall` material path; it is not a new material system.
With the default `r_autoPBR 0`, rendering is unchanged.

Status (2026-09-23): implemented, part 2 (`r_autoPBRConvert`) added the same day. Verified by MSVC builds (SP + MP), offline GLSL compiles of
lightall (Intel UHD + RTX 2060), and a PK3 inventory plus classifier dry run whose C++ and Python
outputs are identical on 4000 materials. **Not yet run in game:** no screenshots or A/B captures
exist yet. The capture checklist is at the end.

## Part 2: legacy "shiny" shaders were never PBR (`r_autoPBRConvert`)

First in-game feedback: stormtroopers still looked vertex lit, protocol droids got no cubemap or SSR,
and `r_autoPBRDebug 1` showed them dark grey. Root cause:

- `CollapseStagesToGLSL` (`tr_shader.cpp`) **skips the whole shader** when any stage has
  `alphaGen lightingSpecular`. The shader then never becomes lightall, and `r_autoPBR` cannot touch it.
- Skipped lit stages are drawn by `generic.glsl` `CalcColor()`: `u_DirectedLight * N·L + u_AmbientLight`
  **per vertex**, plus a fake `pow(R·V, 4)` highlight times the `_spec` texture. That is Gouraud
  shading: no normal map, GGX, cubemap, SSR or sun shadows.
- The grey was not an unknown class. These stages got no debug colour at all, and the debug view turns
  off tone mapping for the frame.
- It hits exactly the hard, shiny content: 138 lit shaders. That is 85 player shaders (every trooper,
  boba_fett, protocol, mark1, saber_droid, r5d2, remote, swoop, x-wing, lambdashuttle, rancor …), 29 of
  the 34 `weapons2` shaders, and 20 map_objects.

The legacy pattern (players.shader, weapons.shader …):

```
{ map <diffuse>  rgbGen lightingDiffuse }                                  lit diffuse
{ map gfx/effects/chr_inv  blendFunc GL_DST_COLOR GL_SRC_COLOR  tcGen environment }   fake chrome (optional)
{ map <diffuse>_spec  blendFunc GL_SRC_ALPHA GL_ONE  detail  alphaGen lightingSpecular }  highlight mask
```

With **`r_autoPBRConvert 1`** (latched, needs `vid_restart`), `ConvertLegacyShinyStages` runs before
the skip test:

1. The diffuse is the first non-glow `tcGen texture` stage lit by `rgbGen lightingDiffuse(Entity)`, or
   any such stage in a shader that has a lightmap.
2. The first following `alphaGen lightingSpecular` stage with `blendFunc GL_SRC_ALPHA GL_ONE` is
   removed. Its image becomes the diffuse stage's `legacySpecImage`.
3. `tcGen environment` stages with `GL_DST_COLOR GL_SRC_COLOR` or `GL_ONE GL_ONE` blending after a
   model-lit diffuse are removed when `r_cubeMapping` is on, since cubemap IBL and SSR replace them
   (no double reflection).
4. Everything else is unchanged. A shader whose `lightingSpecular` stage does not match still skips,
   and `shader.lightallSkipReason` records why.

In `CollapseStagesToLightall`, when no authored map exists, `R_BuildLegacySpecORMSImage`
(`tr_image.cpp`) turns the mask into an ORMS texture (`<mask>_lORMS`). With `l` = linear luminance of
the mask:

| channel | value | meaning |
|---|---|---|
| O | 1 | |
| R | `1 - 0.55·sqrt(l)` | roughness **multiplier**: the class roughness is the maximum, bright mask areas are glossier |
| M | `smoothstep(0.08, 0.35, l)` | metal **mask**, multiplied by the class metalness (0 for non-metal classes) |
| S | 1 | |

The stage becomes `PBR_SOURCE_LEGACY_SPEC` with `specularType = SPEC_ORMS`. `r_autoPBR` still picks the
class values at draw time: `ORMS *= specularScale.zwxy`. Examples:

- stormtrooper armour, plastic: roughness 0.60 in matte areas, down to 0.27 where the mask is white;
- protocol droid, metal: gold panels metallic, dark joints dielectric;
- weapons, metal: metallic only where the old highlight mask was bright.

`CollapseStagesToLightall` is otherwise unchanged, so `_nh`/`_n` lookup and generated normal maps
(`r_genNormalMaps`) apply to the converted stages as well.

With `r_autoPBRConvert 0` the shader setup is unchanged, and so is rendering (the debug uniform is 0).

Inventory (`tools/rend2_pbr_inventory.py`, "Lightall conversion" section):

| area | lightall today | converted | env stage dropped | still vertex lit |
|---|---|---|---|---|
| models/players | 173 | 84 | 4 | 1 |
| models/weapons2 | 5 | 29 | 0 | 0 |
| models/map_objects | 300 | 20 | 0 | 0 |
| all lit shaders | 1641 | 133 | 4 | 5 |

Still vertex lit: `models/players/reborn/boss_torso` (its spec stage blends `GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA`, not additive) and four
slick-floor test shaders (`textures/bespin/floor_slick_nodamage`, `textures/tests/*`). The
`protocol/imp_*` variant uses `rgbGen identity` (not entity lit), so it is left alone.

## Current legacy material path (as found in the code)

- `CollapseStagesToLightall()` (`tr_shader.cpp`) turns the diffuse stage of a lit shader into a
  `lightall` stage.
- With `r_normalMapping` (latched), a stage without an explicit `normalMap` looks for
  `<diffuse>_nh` (normal + height, enables parallax), then `<diffuse>_n`. `R_CreateNormalMap`
  (`tr_image.cpp`) generates `_n` from the diffuse at load time for images flagged
  `IMGFLAG_GENNORMALMAP`. Auto PBR does not touch any of this and never enables `r_normalMapping`.
- With `r_specularMapping` (latched, default 1; it defines `USE_SPECULARMAP` for every lightall
  permutation):
  1. Explicit keywords: `specMap`/`specularMap` (spec-gloss, `R_BuildSDRSpecGlossImage` for SDR
     shaders), `rmoMap`/`rmosMap`, `moxrMap`/`mosrMap`, `ormMap`/`ormsMap`. Packed maps are loaded by
     `R_LoadPackedMaterialImage`, which uses a GL swizzle to bring every layout to ORMS.
  2. Otherwise, auto-discovery next to the diffuse: `_specGloss`, then `_rmo`, then `_orm`.
  3. Otherwise, the **fallback**: no image, `specularScale = (0, 0.5, 1, 1)`, and `specularType`
     stays `SPEC_ORM`. This also overwrites any `specularScale`/`roughness`/`gloss` keyword values;
     that existing behaviour is kept.
- `TB_SPECULARMAP == TB_ORMSMAP == 4`. `RB_IterateStagesGeneric` (`tr_shade.cpp`) binds `tr.whiteImage`
  when the stage has no map, and uploads `pStage->specularScale` as `u_SpecularScale`.
- `lightall.glsl`:
  ```glsl
  vec4 ORMS = texture(u_SpecularMap, texCoords);     // white = (1,1,1,1)
  ORMS.xyzw *= u_SpecularScale.zwxy;                  // -> specularScale = (Metal, Spec, AO, Rough)
  specular.rgb = mix(vec3(0.08) * ORMS.w, diffuse.rgb, ORMS.z);   // F0
  diffuse.rgb *= 1.0 - ORMS.z;
  roughness = mix(0.01, 1.0, ORMS.y);
  AO = min(ORMS.x, AO);                               // AO also carries SSAO/GTAO
  ```
  **The current fallback is therefore AO 1, roughness 1.0, metallic 0, F0 = 0.08 × 0.5 = 0.04**
  (confirmed against the shader code, not assumed).
- Lighting: GGX with Smith visibility (`CalcSpecular`) for the sun, the light vector, the deluxe
  direction and dynamic lights. IBL is the cubemap mip `roughness × ROUGHNESS_MIPS` times the EnvBRDF
  LUT, with specular occlusion. SSR (`r_ssr`) reuses the same roughness and F0 from the lightall MRT.
- The `cloth` stage keyword selects the `LIGHTDEF_USE_CLOTH_BRDF` permutation (Charlie sheen, wrapped
  diffuse). Auto PBR does **not** switch it on; cloth here means only the base parameters.

## Detection order (per lit lightall stage, at shader registration)

| # | condition | `pbrSource` | r_autoPBR |
|---|---|---|---|
| 1 | `r_specularMapping 0`, or the stage is not a lit lightall stage | `NONE` | no effect |
| 2 | explicit `specMap` / `rmoMap` / `moxrMap` / `ormMap` (incl. `$whiteimage`) | `EXPLICIT` | never touched |
| 3 | `<diffuse>_specGloss` / `_rmo` / `_orm` found | `DISCOVERED` | never touched |
| 4 | no map, but `specularScale` / `roughness` / `gloss` / `specularReflectance` / `specularExponent` keyword | `SCALAR` | never touched (keeps today's fallback values) |
| 5 | diffuse only | `LEGACY` | classified, values chosen at draw time |

A future explicit `materialType` keyword would slot in as step 4½ and bypass the heuristic.

## Heuristic classification (`R_ClassifyMaterial`, `tr_autopbr.cpp`)

Input: the shader name and the diffuse image name. Each is tokenized on `/ _ - .`, digits and
lower→Upper case changes, then lowercased; single letters are dropped. Only **whole tokens** match:
`metalgrate` does not match `metal`, but `metal_grate` does. Diffuse brightness and colour are
never used. First hit wins:

1. **Material word in the file name**: hair/beard/fur → hair; leather/belt/holster/boot(s)/glove(s)
   → leather; cloth/fabric/robe(s)/cape/cloak/tunic/skirt/sleeve(s)/scarf/pants/shirt/carpet/rug/
   curtain/banner/flag/tapestry → cloth; rubber/plastic/hose/tire/**armor**/helmet → plastic;
   metal/steel/iron/chrome/grate/grating/pipe(s)/rivet/aluminum/brass/copper/bronze/gold/silver/hilt
   → metal. `armor` alone is **not** metal.
2. **`models/weapons2/**`** → metal, except `noweap`, `tusken_staff` and `noghri_stick` (generic).
3. **Whole-character archetype** from `models/players/<dir>`:
   - droids (`droids`, `assassin_droid`, `saber_droid`, `gonk`, `mouse`, `probe`, `protocol`, `r2d2`,
     `r5d2`, `remote`, `remote_sp`, `sentry`, `interrogator`, `mark1`) → metal
   - troopers (`stormtrooper`, `shadowtrooper`, `snowtrooper`, `stormpilot`, `swamptrooper`,
     `hazardtrooper`, `rockettrooper`, `boba_fett`) → plastic
   - vehicles (`atst`, `lambdashuttle`, `tie_*`, `x-wing`, `z-95`, `swoop`) → plastic, since painted
     hulls are a dielectric coat
   - `chewbacca`, `wampa`, `tauntaun` → hair (fur)
   - `rancor`, `mutant_rancor`, `howler`, `sand_creature` → skin
   - `tusken` → cloth
4. **Body part of an organic character**, reading tokens from the last one first (so
   `torso_01_hands` is a hand): head/face/forehead/eye(s)/mouth/teeth/hand(s)/neck/caps/tentacles/lekku
   → skin; torso/leg(s)/hips/lower/coat/jacket/vest/uniform/cuff(s)/clothes/hood/flap/dress/collar →
   cloth. `arms` is deliberately left out, because JKA uses it for both bare arms and sleeves.
5. **Material word in a directory name** (same list as rule 1), e.g. `textures/metal/*`.
6. Otherwise **generic** (`no token`).

The Python port in `tools/rend2_pbr_inventory.py` must stay in sync (`--selftest` holds 32 cases;
`--pairs` prints every classification for diffing against the C++).

## Class table → `specularScale`

`specularScale = (metal, spec, AO, rough)`, F0 = 0.08 × spec, final GGX roughness
`mix(0.01, 1, rough)`. All values live in one table, `materialDefaults[]` in `tr_autopbr.cpp`.

| class | AO | rough | metal | spec (F0) | specularScale | debug colour |
|---|---|---|---|---|---|---|
| legacy fallback (`r_autoPBR 0`) | 1 | 1.00 | 0 | 0.50 (0.040) | (0, 0.5, 1, 1) | — |
| generic (all of `r_autoPBR 1`; unmatched in 2) | 1 | 0.85 | 0 | 0.50 (0.040) | (0, 0.5, 1, 0.85) | grey |
| metal | 1 | 0.55 | 0.9 | 0.50 | (0.9, 0.5, 1, 0.55) | yellow |
| skin | 1 | 0.65 | 0 | 0.35 (0.028) | (0, 0.35, 1, 0.65) | salmon |
| cloth | 1 | 0.95 | 0 | 0.50 (0.040) | (0, 0.5, 1, 0.95) | blue |
| leather | 1 | 0.72 | 0 | 0.50 (0.040) | (0, 0.5, 1, 0.72) | brown |
| plastic / rubber | 1 | 0.60 | 0 | 0.50 (0.040) | (0, 0.5, 1, 0.6) | green |
| hair / fur | 1 | 0.80 | 0 | 0.50 (0.040) | (0, 0.5, 1, 0.8) | purple |
| authored map (explicit or discovered) | from map | | | | untouched | white |
| authored scalar keywords | | | | | untouched | light cyan |

Metal uses the existing metallic workflow: the diffuse texture becomes the reflectance and the
diffuse term drops to 10%. AO stays 1 because there is no spatial data; SSAO/GTAO still apply.
Skin SSS, hair anisotropy and runtime spatial roughness are out of scope.

## Runtime behaviour

- Classification runs once, in `CollapseStagesToLightall`. The class, reason and token are stored on
  the stage (`pbrSource`, `materialClass`, `materialReason`, `materialToken`) and copied along with
  lightstyle stages.
- The selection happens at draw time: `RB_IterateStagesGeneric` calls `R_AutoPBRSpecularScale`
  before uploading `u_SpecularScale`. `r_autoPBR` therefore switches live, with no `vid_restart`,
  no GLSL change for the values, no new permutations and no generated textures (it reuses whiteImage).
- Auto PBR has no effect with `r_specularMapping 0`; `pbr_dumpMaterials` warns about this.

## Cvars and commands

| name | default | flags | meaning |
|---|---|---|---|
| `r_autoPBR` | 0 | archive, runtime | 0 = current rend2 fallback, 1 = generic dielectric for every legacy material, 2 = heuristic classes |
| `r_autoPBRDebug` | 0 | cheat | 1 = class colours (authored white, scalar cyan), 2 = source: explicit map green, discovered map teal, scalar cyan, auto orange, converted with spec mask magenta, legacy with `r_autoPBR 0` grey. **Red in both views = lit stage still vertex lit** (generic.glsl, not lightall). Unlit; bypasses tone mapping, sun rays and glow like `r_shadowDebug`. Forces the SSR contribution to 0 on those pixels. |
| `r_autoPBRConvert` | 0 | archive, **latch** | 1 = convert `alphaGen lightingSpecular` / fake `tcGen environment` shaders (vertex lit today) to lightall, mask → ORMS. Needs `vid_restart`. |
| `pbr_dumpMaterials [used\|all\|auto\|authored\|gouraud\|<class>]` | `used` | | Lists the lit lightall stages of the registered shaders: `used` = drawn since registration, `*` marks drawn stages. Columns: shader, source, class, reason:token, and the AO / rough / metal / F0 the shader receives *now*. Converted stages show `auto+specmask`, `x` after rough and metal (multiplied per texel), the mask image, and `(env stage dropped)`. Vertex-lit stages show as `GOURAUD` with the skip reason. Ends with per-source and per-class totals. |

The roughness and F0 views reuse the existing `r_ssrDebug 2` (roughness) and `r_ssrDebug 3`
(specular reflectance), which need `r_ssr 1`.

## Changed files

- new `shared/rd-rend2/tr_autopbr.cpp`: defaults table, classifier, draw-time scale, debug colours,
  `pbr_dumpMaterials`
- `shared/rd-rend2/tr_local.h`: `pbrSource_t`, `materialClass_t`, stage fields, `UNIFORM_MATERIALDEBUG`,
  cvar externs, prototypes
- `shared/rd-rend2/tr_shader.cpp`: `specularScaleAuthored` flag at the five scalar keywords; source,
  classification and comments in `CollapseStagesToLightall`
- `shared/rd-rend2/tr_shade.cpp`: draw-time `u_SpecularScale` override, `u_MaterialDebug` upload,
  used flag
- `shared/rd-rend2/tr_glsl.cpp`: `u_MaterialDebug` uniform entry
- `shared/rd-rend2/glsl/lightall.glsl`: `u_MaterialDebug` and the debug early-out (a uniform branch,
  no new define)
- `shared/rd-rend2/tr_ao.cpp`: `RB_AODebugBypassesToneMap` includes `r_autoPBRDebug`
- part 2: `tr_shader.cpp` (`ConvertLegacyShinyStages`, `lightallSkipReason`, mask hookup),
  `tr_image.cpp` (`R_BuildLegacySpecORMSImage`), `tr_autopbr.cpp` (`PBR_SOURCE_LEGACY_SPEC`, gouraud
  debug colour, dump), `tr_shade.cpp` (debug uniform for every stage), `glsl/generic.glsl`
  (`u_MaterialDebug`), both `tr_init.cpp` (`r_autoPBRConvert`)
- `code/rd-rend2/tr_init.cpp`, `codemp/rd-rend2/tr_init.cpp`: cvars and command
- `code/rd-rend2/CMakeLists.txt`, `codemp/rd-rend2/CMakeLists.txt`: new source file
- new `tools/rend2_pbr_inventory.py` (read-only PK3 inventory and classifier port),
  `tools/autopbr_ab.cfg` (capture binds)

## PK3 inventory (install `OpenJK/build-rend2/base`, 9 PK3s incl. `assets8_pbr1-3`)

`python tools/rend2_pbr_inventory.py <base>`: it reads the PK3s as ZIP files and never writes. It
parsed 3407 shaders/mtr, 8528 images and 13828 skin surface entries, giving 4000 materials with a
diffuse. Links run skin → surface → shader → diffuse.

Authored vs legacy:

| area | materials | authored | legacy | `_n`/`_nh` |
|---|---|---|---|---|
| models/players | 1191 | 44 | 1147 | 42 |
| models/map_objects | 935 | 324 | 611 | 306 |
| models/weapons2 | 71 | 0 | 71 | 0 |
| textures/hoth | 122 | 73 | 49 | 64 |
| textures/rail | 77 | 49 | 28 | 44 |
| textures/factory | 80 | 46 | 34 | 33 |
| textures/doomgiver | 103 | 8 | 95 | 5 |
| textures/kejim | 98 | 9 | 89 | 5 |
| textures/bespin | 71 | 6 | 65 | 6 |

Authored sources: `_rmo` discovered 765, `rmoMap` 142, `specularMap` 7, scalar keywords 2. The rest,
3084, is legacy. The pbr packs cover much of the world and map objects, and almost no characters or
weapons.

Heuristic classes of the legacy materials:

| class | all | players | weapons2 | map_objects | textures |
|---|---|---|---|---|---|
| generic | 2003 | 209 | 2 | 594 | 1184 |
| metal | 161 | 32 | 68 | 12 | 47 |
| skin | 251 | 251 | 0 | 0 | 0 |
| cloth | 463 | 454 | 0 | 5 | 4 |
| leather | 39 | 38 | 1 | 0 | 0 |
| plastic | 137 | 137 | 0 | 0 | 0 |
| hair | 30 | 26 | 0 | 0 | 4 |

Deciding rules: no token 2001, body part 647, file name word 185, character 178, weapons2 69,
directory 2. JKA player textures are named by body part (`torso` 260, `legs` 124, `head` 115, `arms`
84, `face` 50, `hands` 40, `hips` 36, `boots` 32) under a character directory; that drove the
two-level design. The world keeps generic unless a material word is present (e.g. `grate`, `pipe`,
`metal`, `iron`).

Heads-up: 241 legacy shaders also carry old "shiny" stages (`tcGen environment` or
`alphaGen lightingSpecular`), among them generic 121, plastic 62 and metal 46. Those stages keep
drawing on top of lightall, so with `r_autoPBR 2` they can stack with the new specular (the "double
highlight" risk). Check them first in game, e.g. `models/players/protocol/c3po_*` and
`models/players/sentry`.

## Known / likely misclassifications

- `*/boots_hips` atlases (imperial, reborn, galak, prisoner, …) → leather for the whole hips/legs
  texture, including the trousers.
- `rodian/boots_belt_vest` → leather, including the vest.
- `swoop/swoop_gold`, `swoop_silver` → metal: the name word beats the vehicle rule.
- `stormtrooper/armor`, `noghri/armor` are used on the `*_arm_sleeve` surfaces (the black bodysuit)
  and land in plastic, which is fine for rubbery material.
- `jedi_hm/robes02_hands` → cloth: the name word beats the part rule.
- `*_arms` (84 textures) stay generic on purpose, since the word means both skin and sleeves.
- `textures/fur/*` (test textures) → hair. `vjun/pipe_effect*` and `imp_mine/pipe_glowing*` → metal,
  but these are effect or glow shaders that rarely reach lit lightall.
- World `textures/**` without material words (~1180) stay generic. That includes stone, which is
  correct, and painted metal panels, which should really be metal/plastic but generic is the safe side.
- Droids are all metal, including R2's painted body and the Mouse droid's black shell.

## In-game validation checklist (to do: not run yet)

Part 2 first: `seta r_autoPBRConvert 1; vid_restart`. Then:

- `r_autoPBRDebug 2`: troopers, protocol droids and first-person weapons should be magenta, **not red**.
  Red means the model is still vertex lit.
- `pbr_dumpMaterials gouraud` lists what is still vertex lit and why.
- Stormtrooper: a smooth per-pixel highlight that follows the old spec mask, and cubemap/SSR reflections
  on the white plates. There should be no second, vertex-lit highlight.
- Protocol droid: metallic reflections in the gold, no `chr_inv` fake chrome swimming on top.
- Compare `r_autoPBRConvert 0/1` (with `vid_restart`) at the same spot.

`exec autopbr_ab.cfg` (copy it from `tools/` to `base/`), then `devmap <map>`, `npc spawn <npc>`, and F8
for each scene/subject. F8 captures `r_autoPBR 0/1/2` + `r_autoPBRDebug 1/2`, F7 = `pbr_dumpMaterials`.

- Subjects: `kyle` (skin and cloth), `stormtrooper` (armour must be plastic green in debug 1,
  **not** metal), `protocol` / `r2d2` (metal), `reborn` / `cultist` (robes), `rodian` (leather boots),
  first-person weapons after `give all`, and a generic env model or crate.
- Scenes: bright indoor, dark indoor, outdoor sun (`yavin1b`, `t1_surprise`), a surface near a cubemap
  (`r_cubeMapping 1`), a lit saber or dynamic light held close to skin and armour.
- Reject if: surfaces look wet; bright armour becomes metal; skin looks like plastic (it should be
  softer than the plastic class); baked diffuse highlights get a second large specular lobe (watch
  the legacy shiny-stage shaders above); metal goes black in dark areas without a cubemap.
- Tune only `materialDefaults[]` in `tr_autopbr.cpp`, then update the class table here.

## Next realism levers (not in this task)

- **Model ambient.** Models get one light-grid direction plus a flat `u_AmbientLight`. The flat
  ambient flattens everything not facing the grid light, and it contributes more to the "toy" look
  than the BRDF does. Directional ambient, from spherical harmonics out of the light grid or diffuse
  IBL from the cubemaps, is the next big step.
- **Normal maps.** Only 42 of 1191 player textures have `_n`/`_nh`. Generated normal quality
  (`r_genNormalMaps`) or hand-made normal maps for key characters matter a lot for per-pixel lighting.

## Part 3: roughness variation (`r_autoPBRRoughness`, task 10)

`r_autoPBRRoughness 0` (default) = constant class roughness. `1` = a generated `<diffuse>_aORMS`
map for `PBR_SOURCE_LEGACY` stages only. Authored `_specGloss/_rmo/_orm`, explicit maps, scalar
keywords and converted `LEGACY_SPEC` stages are never touched. The maps are built at shader
registration (turning it on needs `vid_restart`). At draw time the map is bound instead of
`whiteImage` only while `r_autoPBR` and `r_autoPBRRoughness` are both on, so turning it off is instant.

Algorithm (`R_BuildAutoRoughnessORMSImage`, tr_image.cpp; numpy port `tools/rend2_autorough_preview.py`):
1. linear luminance, box downsampled to ≤512;
2. `d = log L − lowpass(log L)` (2× box, radius size/32): baked light, creases and painted highlights
   become a ratio to their neighbourhood, absolute brightness is gone;
3. `v` = 3×3 std-dev of `d` (magnitude, sign-free), normalised by its own 90th percentile, `1−e^(−2v)`;
4. highlight guard: where the low pass is in the top 3 %, `v` fades to its mean;
5. `m = 1 − 0.25·(1 − v)`; texel = canonical ORMS (1, m, 1, 1), a multiplier of the class values.

The mean of `m` is stored in `image_t::autoRoughness`. `R_AutoPBRSpecularScale` sets
rough = class / mean (up to 2), and lightall clamps `ORMS.y` to 1 per texel, so the average stays at
the class value. Result: roughly ±0.1 around the class value, standard mips (the multiplier
averages toward its mean, which is never glossier; no sparkle source).

Refused on purpose: no `1 − luma`, no AO from diffuse, no spatial metalness (painted metal + cloth
in one texture keeps its class constant, and the fix is an authored `_orm`). A flat texture
(p90 < 0.01) gets a constant map.

Cost: numpy port 50–80 ms for 512², ~300 ms for 2048² (the downsample dominates). The C++ version
is O(pixels) with sliding-window blurs; real timings are printed per map with `developer 1`, and as
a total by `pbr_dumpMaterials` ("N roughness maps generated in X ms"). Memory: ≤512² RGBA +
mips per legacy diffuse.

Debug: `r_autoPBRDebug 3` shows the final roughness in grey (blue = not auto PBR);
`pbr_dumpMaterials` lists `rough-gen <map> mean sigma`.

Known artifact: UV island borders against a black atlas background count as detail and become
rough outlines. They are mostly off-surface texels, but they can bleed at low mips.
