# Rend2 hair and eye material models

## Stock inventory

The audit used only `assets0.pk3` through `assets3.pk3` (ZIP search order), not
the installed mod PK3. It found 305 `models/players/**/*.skin` files, 7,218
active surface mappings, 76 player directories, 77 GLMs, 944 unique player
shader references, 5,872 images and 3,365 authored shader definitions.

The GLM/skin topology does not support a global player-model rule:

- Hair may be separate alpha-blended cards (`jedi_hf`), a separate surface
  with an ambiguous shader name (`jedi_rm/head01_alpha`), or painted into the
  head diffuse (`kyle`).
- Eyes may have separate geometry but still share an atlas/shader with face or
  mouth (`jedi_hf/face_a`, `kyle/mouth_eyes`). Painted face eyes are not a
  renderer-detectable material region.
- Droid and alien surface names overlap humanoid vocabulary (`r2d2/head_eye`),
  so no eye-name or all-player heuristic is used.
- G2 meshes have MikkTSpace tangents. Their usefulness as strand direction is
  asset-dependent and is exposed by the debug views rather than inferred from
  diffuse RGB.

The existing material classifier found 23 stock player hair-class material
references. Nine exact `hair`/`hairs` name-token references, spread over the
`jedi_hf`, `jedi_hm`, `ugnaught` and `weequay` families, are conservative
code-only candidates. The other 14 include ambiguous alpha names and
character-wide fur classifications; they are not automatically switched and
need review plus explicit annotation. Mixed eye/face or eye/mouth atlases need
per-pixel masks; this test pass supplies two representative masks, not a global
asset conversion.

## Shader syntax and precedence

Inside a lit shader stage:

```text
materialType hair
materialMap path/to/map
```

or `materialType eye`. `materialType standard` explicitly opts out. Explicit
type always wins. Without it, only exact `hair`/`hairs` name-token matches use
the hair model; material class, player-model membership, color and brightness
never select hair or eye shading.

`materialMap` uses RG as a signed tangent-frame hair direction and B as model
coverage. A zero-length RG vector keeps the mesh tangent. On a mixed skin/eye
stage, `skinMask` must reference the same packed image: R is skin-scatter
coverage and B is eye coverage. The renderer disables a declared map rather
than applying the special model to a whole atlas when the shared sampler is
unavailable or incompatible.

## Models

Hair uses a dielectric anisotropic GGX direct-light lobe. The tangent roughness
is broadened and bitangent roughness narrowed by anisotropy 0.75, producing an
elongated highlight along the authored/UV direction. Fresnel remains Schlick
with F0 0.04; metallic energy is rejected and perceptual roughness is clamped
to 0.25–0.65 for the covered hair texels. Existing blend, depth-write,
alpha-test, shadow and SMAA paths are unchanged. LTC rectangles/sabers use a
closest-point anisotropic approximation because the shipped LTC tables are
isotropic.

Eyes use the existing GGX dielectric model with F0 0.04 and roughness 0.08 on
covered texels. This is a wet outer-specular response, not corneal refraction;
geometry and UVs are not assumed to be a physical cornea.

## Debug and validation

`r_materialModelDebug` values are: 1 type, 2 tangent, 3 bitangent, 4 hair
direction, 5 hair specular only, 6 eye specular only, and 7 mask.

Validate a visible-haired human, bald human, armored human, alien, droid,
painted-eye character, and separate-eye character under sunlight, indoor light,
a moving saber, and grazing view. Alpha-card checks must include depth/shadow
behavior and cutout/blended edges; this change intentionally adds no OIT.

The removable test archive is built by `tools/hair_eye/make_test_pk3.py`. It
overrides two Jedi HF hair-card shaders, two Jedi RM ambiguous hair-card
shaders, and the masked Jedi HF/Kyle eye atlases. It contains no diffuse image
and removing the one PK3 reverts every asset-side change.
