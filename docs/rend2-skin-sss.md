# Rend2: skin subsurface scattering (r_skinSSS)

Skin scattering applies only to surfaces classified as skin, with no new textures required. Characters
are not made translucent: only the **diffuse light of skin** is diffused. Specular, eyes, metal, saber glow
and emission stay sharp.

Code: `shared/rd-rend2/tr_skinsss.cpp` (classification, kernel, passes), `glsl/skin_sss.glsl` (blur +
composite), the skin diffuse split and wrap in `glsl/lightall.glsl`, shared screen-space infrastructure
`tr_screenspace.cpp` (with SSR / SSGI). Test assets: `tools/skinsss/`.

| `r_skinSSS` | what | cost |
|---|---|---|
| 0 (default) | off. lightall is compiled without `USE_SKIN_SSS*`; the preprocessed lightall is identical to the previous one (48 permutations checked) | none |
| 1 | **cheap approximation, not SSS**: per channel wrapped diffuse lobe `(N·L + w) / (1 + w)`, w = `r_skinSSSWrap` × (1, 0.45, 0.25) for the direct, sun and dynamic lights of skin stages. The terminator gets soft and slightly red. Baseline / fallback | a few ALU in lightall |
| 2 | **screen-space diffusion** of the skin diffuse light (below). Needs `r_hdr 1` and 8 color attachments; otherwise it prints why and falls back to 1 | one extra RGBA16F MRT + 3 fullscreen passes that return early outside skin |

`r_skinSSS` is latched (`vid_restart`), because the mode decides GLSL defines and render targets.
`r_skinSSSStrength 0` in mode 2 gives the legacy look at runtime, and so does `r_skinSSSCompare 1` on the left half of the screen.

## Asset inventory (base + assets8_pbr PK3s)

`tools/rend2_pbr_inventory.py <base> --skin [--mixed]` ports `R_SkinSSSClassifyShader`.

- 305 `.skin` files, 73 player dirs. The humanoid layout is `head_face` → face texture, `head` → `*_head` texture,
  `head_eyes_mouth` → `mouth_eyes.tga`, `l/r_hand` → `*hand*` texture. Arms are usually part of the torso
  (cloth) texture.
- **Faces** are separate from hair and cloth, but the texture includes brows, hairline and beard (kyle_face).
- **Eyes** are separate surfaces for most humans (`mouth_eyes.tga`, 18 textures). They are **painted into the
  face** for jedi_hf (`heada_eyes` → `face.tga`), alora2 and tavion_new.
- **`*_head` textures** (kyle_head, luke_head_new, jedi_hm hair02_head, ...): **hair, ears and neck in one texture**.
  Per-surface classification cannot separate them.
- Separate hair textures exist only for jedi_hf / hm / rm, ugnaught and weequay. No player shader uses the
  JKA `material` / `q3map_material` keyword, so that is no skin signal. Most faces, heads and hands are implicit
  shaders (lightall, per-pixel lighting).
- jedi_tf (Twi'lek) face, lekku and skin shaders have two stages: an entity-tinted base plus an alpha blended
  painted layer.

Eligibility with the default `r_skinSSSMixedHeads 0` (materials under models/, number used by a model skin):

| decision | materials | used |
|---|---|---|
| skin (scatters): faces, hands, neck, forehead, lekku / tentacles, howler, rancor, sand_creature | 134 | 129 |
| excluded part: eyes, teeth, mouth, cap (bespin_cop hat) | 28 | 28 |
| mixed head (`*_head`: hair + skin), needs a mask or `r_skinSSSMixedHeads 1` | 92 | 67 |
| layered (jedi_tf: lit alpha layer on top) | 8 | 8 |
| blended base (jedi_rm hair heads, saboteur face plate) | 4 | 4 |

With `r_skinSSSMixedHeads 1`: 224 scatter (194 used), 10 layered (the jedi_tf heads join them).

All 47 organic characters get at least one scattering surface. The 29 without one are droids, troopers,
vehicles, Chewbacca / wampa / tauntaun (fur) and tusken (wrapped).

**How far surface classification goes:** faces and hands work. It fails on `*_head` textures (hair) and
on faces with painted eyes. The beard in a face texture also scatters, which is mild. So the default excludes
`*_head`, and masks are **only** made for two test characters (below). A global mask pass over all
characters is not needed at this point.

## Eligibility (`R_SkinSSSClassifyShader`, once per shader)

Only the opaque, lit lightall base stage scatters:

1. keywords win: `skinScatter <0..1>` (0 turns it off), `skinMask <image>` (R = scatter per texel, implies 1)
2. not lit / blended base stage → no
3. class `skin` of the auto PBR classifier (`tr_autopbr.cpp`). Stages with authored PBR maps are classified
   with the same rules without changing their class (`R_ClassifyMaterialName`)
4. token eyes / eye / eyesmouth / moutheyes / teeth / mouth / cap / caps → no ("excluded part")
5. token `head` → no unless `r_skinSSSMixedHeads 1` ("mixed head")
6. a later lit stage alpha blended on top → no ("layered")

`r_skinSSSList [used|all|skin]` prints the decision. `r_skinSSSDebug 1` shows it on screen: orange = scatters,
yellow = excluded part, purple = mixed head / layered, blue = off by keyword, gray = not skin.

## Mode 2: buffers and passes

**MRT:** attachment 7 of renderFbo, `GL_RGBA16F` (`tr.skinDiffuseImage`, the MSAA resolve target).
It is the last of 8 slots; SSR uses 3–4 and SSGI 5–6, and slot 2 is the shared normal.

| channel | content |
|---|---|
| rgb | skin diffuse × scatter (× mask), scene space (the space of color 0: linear or legacy display encoded) |
| a | linear view depth of the skin fragment, 0 = not skin |

- Every opaque lightall draw of a screen-space view writes it: non-skin fragments write 0, so skin behind
  them is overwritten. Draws that are blended, depth-only or vertex-lit (generic.glsl) have the attachment
  masked (`RB_WritesScreenMaterial` / `GL_SetScreenAuxWrite`). The composite therefore checks the stored
  depth against the depth buffer (stale data is ignored).
- The shared normal attachment (2, octahedral) is written for skin fragments when neither SSGI nor SSR
  wrote it already. The receiver bit stays 0, so SSR is not affected.
- It is cleared in `RB_ScreenSpaceBeginView` and MSAA resolved with the other screen attachments.

**What goes into rgb:** the diffuse terms lightall already computes, split from specular: the light vector /
deluxe light `lightColor·Fd·att·N·L`, ambient and diffuse IBL `diffuseAmbientColor·albedo` (after AO), the sun
`u_PrimaryLightColor·Fd·N·L` (shadowed), the dynamic lights (legacy or Forward+) and LTC area light diffuse. Never
included: specular, cubemap IBL, SSR, emission, SSGI (it is added after the diffusion).

**Frame order:** opaque surfaces (sharp skin in color 0, skin in MRT 7) → MSAA resolve, depth pyramid mip 0 →
**skin H → skin V → skin composite** → SSGI → SSR → transparent surfaces → volumetric fog / motion blur →
bloom → tone mapping. Bloom and tone mapping see the final skin, and so do SSR reflections. The passes are
skipped when no skin stage was drawn in the view.

**Profile:** the d'Eon and Luebke six-Gaussian skin profile (GPU Gems 3, ch. 14; variances 0.0064, 0.0484,
0.187, 0.567, 1.99, 7.41 mm²). Red dominates the wide terms, blue and green the narrow ones. It is sampled as
a separable 1D kernel (Jimenez et al.): 11 / 17 / 25 taps (`r_skinSSSQuality`), offsets denser near the centre,
weights integrated per tap and normalized per channel (the diffusion moves light, it adds none). The
17-tap kernel over ±8 mm is as follows. The centre keeps 18 / 31 / 38 % of R / G / B and ±0.125 mm another
17 / 28 / 29 %. Blue and green stay almost sharp while red spreads: mean distance 0.64 mm for red, 0.14 mm for
green, 0.09 mm for blue. **The face does not turn into a gaussian blur.** `r_skinSSSKernel` prints it.

**Screen scale:** radius (pixels) = `r_skinSSSWidth` [mm] / 28 mm per unit × `P[5]`·height/2 / view depth.
A 64-unit player is 1.8 m tall. `r_skinSSSWidth 8` is the physical profile; larger values widen the whole
profile (JKA textures are coarse). At 1080p, fov y 50°: 11 px at 30 units (close-up), 3.3 px at 100, 1.1 px at 300.
Below 0.5 px a pixel returns after two fetches, so distant NPCs cost nearly nothing. The radius is clamped to 48 px.

**Edge preservation (per tap):** a tap that is not skin, or not the visible surface, is replaced by the
centre. Otherwise the tap fades to the centre by `saturate(r_skinSSSFollowSurface × (|Δz| / radius + (1 − N·N')))`.
This follows Jimenez: the diffusion does not cross silhouettes (nose → background), face → hair, skin → cloth, or
depth steps, and energy is kept.

**Composition** (additive, color 0 only, float target, so negative values are fine):

    color += r_skinSSSStrength · (diffused(skin) − skin)

Only where the stored depth matches the depth buffer. Everything that is not skin diffuse (specular,
eyes, metal, emission, other surfaces) is untouched. A partial mask m gives
`color = rest + (1 − m)·D + diffused(m·D)`.

## Cvars

| cvar | default | |
|---|---|---|
| `r_skinSSS` | 0 | 0 off, 1 wrap (approximation), 2 screen-space diffusion. Latched |
| `r_skinSSSMixedHeads` | 0 | also scatter `*_head` textures (hair + skin). Latched (registration) |
| `r_skinSSSStrength` | 1 | mode 2: share of the skin diffuse replaced by the diffused one |
| `r_skinSSSWidth` | 8 | mode 2: radius in mm, 8 = physical profile |
| `r_skinSSSQuality` | 1 | taps 11 / 17 / 25 |
| `r_skinSSSWrap` | 0.3 | mode 1: red wrap width |
| `r_skinSSSFollowSurface` | 1 | depth / normal stop of the diffusion, 0 = plain profile blur inside the skin |
| `r_skinSSSTransmission` | 0 | optional back light term (below) |
| `r_skinSSSCompare` | 0 | split screen, left half without |
| `r_skinSSSDebug` | 0 | see below (cheat) |

Commands: `r_skinSSSList`, `r_skinSSSKernel`.

## Debug views (`r_skinSSSDebug`)

1 classification (flat colors, modes 1 and 2) · 2 skin mask (scattering surfaces, shaded by their diffuse
light) · 3 raw sharp skin diffuse · 4 horizontal pass · 5 vertical pass (diffused) · 6 final delta, luminance
(red = gained, blue = lost, ×8) · 7 everything but the skin diffuse (scene − sharp skin: what stays sharp,
the "specular only" comparison) · 8 kernel radius in pixels (blue 0 → red 16, gray = none).
Views 1, 2, 6 and 8 bypass tone mapping; 3, 4, 5 and 7 are radiance and go through it.
GPU timers with `r_speeds 100`: "Skin SSS H", "Skin SSS V", "Skin SSS composite".

## Optional back light transmission

`r_skinSSSTransmission` (default 0) is a separate cheap term for the light vector and dynamic lights:
`light · albedo · (1, 0.35, 0.2) · strength · saturate(−V·L)⁴ · saturate(0.3 − N·L)`. It uses no thickness data;
thickness maps are not required. It goes into the skin diffuse, so mode 2 diffuses it too. The sun is left out,
because its shadow map puts the far side of an ear in shadow. Shadowed dynamic lights are affected the same way.

## Optional test PK3 (`tools/skinsss/`)

Only for kyle and jedi_hf, whose textures mix skin with hair, beard or painted eyes:

- `make_masks.ps1`: masks from the game textures (YCbCr skin tone + luminance, plus the UV strip of the
  jedi_hf eyes, softened). kyle_head keeps ear, neck and cheek and removes hair. kyle_face removes beard and
  brows. jedi_hf face / face_a / face_b remove the painted eyes, brows and hair wisps.
- `pk3/shaders/zz_skinsss_test.shader`: overrides with the same single lit stage plus `skinMask`. The auto
  discovered `_n` / `_rmo` maps still apply.
- `make_pk3.py` → `zz_skinsss_test.pk3` (not installed; copy into `base/`).

## Validation

Done (no game launch, user decision):

- MSVC Release `rd-rend2_x86_64` + `rdsp-rend2_x86_64`: no errors, no warnings. Pre-change DLLs for A/B:
  `build/ab/*-presss.dll`.
- The preprocessed lightall without `USE_SKIN_SSS*` is byte-identical to the previous file (48 permutations:
  none / SSR / SSGI / SSR+SSGI+LTC × lightmap / light vector / vertex / unlit × 3 material sets).
- Offline GLSL (Intel UHD + NVIDIA RTX 2060): 3 skin_sss programs and 80 lightall permutations with
  `USE_SKIN_SSS` / `USE_SKIN_SSS_BUFFER`, alone and with SSR / SSGI / LTC, shadows, POM and cloth. 166 compiles, 0 failures.
- Kernel (Python port): channel sums 1, red widest; pixel radius table above.
- Inventory port: eligibility table above, selftest passes.

Not done: in-game captures and GPU timings. Capture with `tools/skinsss/skinsss_ab.cfg` (F8 sequence):

| scene | r_skinSSS 0 / 1 / 2 | compare | debug 1–8 | notes |
|---|---|---|---|---|
| face close-up (kyle, jan, jedi_hf) | | | | |
| strong side light (terminator) | | | | |
| red / blue saber next to the face | | | | red saber: red spreads, blue saber: stays tighter |
| outdoor sun | | | | |
| dark room (ambient only) | | | | |
| face against a bright background edge | | | | no halo over the edge (debug 6) |
| hair edge (kyle, jedi_hf, with and without the test PK3) | | | | |
| multiple NPCs at distance | | | | debug 8: gray / blue |

| GPU (r_speeds 100) | Skin SSS H | Skin SSS V | composite | lightall delta (mode 2 vs 0) |
|---|---|---|---|---|
| close-up | | | | |
| crowd at distance | | | | |

## Known limitations

- First person hands (depth hack) and views without a world model (menus, UI player model) do not get mode 2.
  Mirrors and portals don't either.
- Fog drawn over the skin before the composite (fog pass) does not scale the delta; dense fog on faces may
  show a small difference.
- Mode 1 wraps the sun only where the (legacy) sun term already reaches: its shadow value includes N·L.
- Faces with painted eyes and `*_head` textures need masks (test PK3) for correct results. The beard in
  face textures scatters. jedi_tf is excluded (layered).
- The diffusion runs in the scene's space: with legacy (display encoded) lighting it blurs encoded values,
  which is an approximation.
