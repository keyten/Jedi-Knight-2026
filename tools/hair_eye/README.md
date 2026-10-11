# Hair/eye material-model test PK3

This directory builds a removable stock-asset override for three representative
human materials. It never modifies the original Jedi Academy PK3 files.

Coverage:

- `jedi_hf/hair_02_alpha`, `hair_03_alpha`: separate alpha-blended hair cards,
  explicitly annotated as `materialType hair` while preserving their original
  cull, blend and depth-write state.
- `jedi_rm/head01_alpha`, `head02_alpha`: separate alpha-blended hair cards
  whose names are not safe enough for the code-only fallback.
- `jedi_hf/face_a`: spherical eye geometry shares a face atlas; a B-channel
  map selects only the two spherical eye islands in B; its complementary R
  channel prevents skin scattering from softening those islands.
- `kyle/mouth_eyes`: eye and mouth geometry share an atlas; a B-channel mask
  selects the eyeball island and leaves teeth/tongue/mouth unchanged.

The material map convention is RG = signed tangent-frame direction and B =
coverage. These eye-only maps keep RG neutral. Run `python make_test_pk3.py` to
regenerate masks and `zzzz_hair_eye_test.pk3` deterministically.

Useful debug views are `r_materialModelDebug 1` (type), 2 (tangent), 3
(bitangent), 4 (hair direction), 5 (hair specular), 6 (eye specular), and 7
(mask). Set it back to 0 for normal rendering.
