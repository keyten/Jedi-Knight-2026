# rend2 depth prepass without a velocity buffer

When nothing wants a velocity buffer (`r_ssr 0`, `r_ssgi 0`, no SMAA T2x, no
motion blur, no LTC saber screen pass), the depth prepass used to draw lightall
stages with the full lightall program family (colour mask off) and the other
stages with the generic programs. The SSR review of 2026-10-29 measured that
path as about 3x slower than the velocity prepass (`docs/rend2-ssr-validation.md`,
"Measured and rejected").

## Change

- `glsl/velocity.glsl` has a `USE_DEPTH_ONLY` permutation
  (`VELOCITYDEF_DEPTH_ONLY`, 0x20): the same current-frame position, deforms,
  skeletal animation, tcGen/tcMod, RF_DISINTEGRATE colour, alpha test, parallax
  and leaf flutter / plant skin as the velocity prepass. It has no
  previous-frame position, no previous bones / entity / temporal blocks and
  no colour output.
- `SelectShaderProgram` (`tr_shade.cpp`) uses it for a depth fill that is not
  a shadow map (`VPF_DEPTHSHADOW`: cube layers and the sun cache snap stay in
  lightall / generic) and not the weather impact pass (that one writes the
  impact class to colour). This covers the main view, portals, cubemaps and
  the sky portal occluders. POM silhouette shells and surface sprites keep
  their own depth programs.
- `r_depthPrepassFast` (default 1 after the [2026-11-23 audit](rend2-optimization-defaults.md), archived): 1 uses the depth-only velocity programs, 0 keeps the old programs.
- `r_depthPrepassVerify 1` (debug): after the prepass, draws the same surfaces
  again with the other program family, using depth test LESS and no depth
  write, inside a `GL_SAMPLES_PASSED` query. The order alternates each frame.
  It prints the samples that one family puts strictly nearer than the other;
  0 in both orders means the depth is bit-identical.

## Measurements (2026-10-29, mp/ffa3, 640x360, `r_picmip 2`, hidden SP run)

`tools/rend2/test_depth_prepass_runtime.py`, "Depth prepass" GPU timer medians
(`r_speeds 100`), interleaved A/B rounds, view `671 -769 -70 290`:

| GPU | depth-only, old (lightall/generic) | depth-only, new | velocity prepass (`r_ssr 1`) |
|---|---|---|---|
| Intel UHD, no other game, 4 rounds | 0.428-0.430 ms | 0.224-0.227 ms | 0.186-0.235 ms |
| RTX 2060, 2 rounds | 0.184-0.185 ms | 0.136-0.137 ms | not measured |

Main view: Intel 3.48-3.50 -> 3.28-3.32 ms, NVIDIA 1.73 -> 1.68 ms.
The 0.30 / 0.10 ms figures from the SSR review came from that run's settings
(SSR config, different feature set). In the same config, the new depth-only
prepass costs about as much as the velocity prepass.

Correctness:

- `r_depthPrepassVerify`: 0 samples nearer, in both orders, on every frame.
  Intel: 60 frames with 244 surfaces (static view) and 60 frames with 265
  surfaces (a reborn and a stormtrooper in view, so skeletal models are
  covered). NVIDIA: 60 frames with 265 surfaces.
- Captures (A/B/A with `timescale 0`): NVIDIA fast == legacy == repeat,
  pixel-identical. On Intel, two shots of the same mode already differ
  (6958 pixels, vs 6884 between the modes), so the Intel captures are not
  evidence either way; the depth check above is.
- GLSL: all 64 velocity permutations compile and link on Intel UHD (27.20.100.8280)
  and RTX 2060 (616.92). `tools/rend2/test_foliage_gl.py` (now with depth-only
  permutations) passes on both.

Not covered: an in-game weather impact pass run. It is excluded from the new
path, so it still uses the old programs.
