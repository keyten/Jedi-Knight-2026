# Legacy particle rendering improvements

## Stage 1: soft intersections and near-camera visibility

World sprite and oriented-quad stages can fade against a separate scene-depth
copy. It is taken after opaque/decal/fog and modern water depth, before the
remaining transparent layers. The copy is never the attachment being drawn to;
MSAA depth is resolved by the same blit. The default main view has a null
`targetFbo` (resolved to `renderFbo` by the backend); explicit renderFbo views
are supported too. Orthographic, shadow, sky-portal, view-model and no-world
draws do not use this effect.

The shader fades alpha for straight-alpha blending, and RGB plus alpha for
premultiplied/additive ONE blending. Ordinary geometry uses its existing generic
permutations. No new texture or stock EFX override is needed.

| Control | Default | Meaning |
| --- | --- | --- |
| `r_softParticles` | 1 | 0 off, 1 alpha sprites, 2 includes additive sprites; restart |
| `r_softParticleDistance` | 8 | Intersection fade distance in world units; 0 is a live A/B bypass |
| `r_particleNearFade` | 1 | Fade from the actual near plane across the next 16 units |

In both JA SP and MP, near fading replaces the old distance cutoff for world
FX particles when using rend2. Camera-facing quads are discarded behind the
camera plane; oriented quads use a conservative corner bound, including the
possible overshoot of size waves. Depth-hack/player-view exclusions retain
their legacy behavior. Setting `r_particleNearFade 0` restores legacy culling.

### Validation

- `tests/fx_visibility_tests.cpp`: 20,000 geometry/size-wave cases, comparing
  the production culler with projected corners of independently rotated quads.
- `python tools/rend2/test_particles_gl.py`: 44 hidden-context cases with the
  production generic fragment shader, covering intersection, near fade,
  straight alpha, premultiplied alpha and both additive forms.
- SP/MP engine, SP game and both rend2 DLLs built. This checkout's existing
  generic lambda in lens water requires C++14 for GCC; Windows GCC renderer
  test builds also need `-Wl,--export-all-symbols` for their entry points.
- `tools/rend2/capture_particles.py` mounts only stock assets0–3, uses an
  isolated home and controlled renderer puffs with the stock smoke material.
  These are renderer integration fixtures, not replacement EFX assets or a
  claim of visual review of every gameplay effect. Normal captures require
  bit-identical A/B/A controls. MSAA permits at most 0.02% of pixels differing by one
  display code due to sparse FP16/resolve rounding; larger changes fail.
  The visible fixture uses 12 overlapping stock alpha-smoke sprites. A separate
  no-smoke control must differ by at least 12 display codes in 5,000 pixels,
  with a peak difference of at least 24. The feature A/B must change at least
  2,000 pixels with a peak of at least 12; merely detecting changed pixels is
  insufficient. Captures are also inspected visually.

Developer command `r_particleTest <distance> [radius] [count]` draws controlled smoke
in front of the rendered view. `r_particleTest 0` removes it. With `surface`
in place of distance it probes the center depth once and places
the puff two units in front of that surface (developer-only GPU readback).
It is registered and removed with the renderer. `r_hiddenWindow 1` creates an SDL window hidden
from the start and reports fatal errors to the log without a dialog. Capture
launches also set `s_initsound 0`, `in_mouse 0`, `in_nograb 1`, `r_fullscreen 0`;
the harness checks that its process creates no visible windows.

This does not solve general transparent-surface ordering or reconstruct 3D
smoke from the texture. Sorting and performance work are separate stages.
