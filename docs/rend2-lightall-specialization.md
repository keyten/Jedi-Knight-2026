# Exact lightall specialization for draws without local light

`r_lightallSpecialize 1` (SP and MP default) lazily removes the local-light loop
from an ordinary lightall draw whose final recorded `u_LightMask` is zero.
`r_lightallSpecialize 0` immediately selects the original program, without a
renderer restart. It is an A/B and fallback switch, not a lighting-quality mode.

## Selection and compatibility

The recorded flag comes from the same final `enableDLights` decision which
uploads `u_LightMask = 0`, after the per-view Forward+ grid check. It does not use
only `num_dlights`: Forward+ area lights and every other source continue to use
the renderer's existing mask/readiness rules. A light appearing, disappearing,
or reaching a different surface changes the next draw's selection. Portal,
mirror, sky and main views each keep their own recorded input.

Selection runs in `RB_DrawItems` after rigid MD3 grouping. The material program
pointer used by recording, sorting and grouping remains the original pointer.
Only ordinary lightall programs with an active light-mask uniform request the
new variant. Existing MD3, Phong, silhouette-POM and other specialized programs
retain their original path. Water reflection draws and depth/velocity/shadow
passes retain their programs. Ordinary POM remains eligible.

The GLSL change replaces only the two `CalcDynamicLightContribution` bodies with
exact zero. Their original zero-mask exits return the same value before any
observable output/global write. Sun shadows, baked lighting, environment light,
material terms, emission, debug overlays and SSR/SSGI receivers/sources remain in
the production material shader. The vertex source preprocesses identically.

The `_NO_LOCAL_LIGHTS` name and `USE_NO_LOCAL_LIGHTS` source definition produce
separate existing program-cache keys. Only a requested draw variant compiles;
there is no additional eager family or Cartesian startup matrix. A failed variant
compile falls back to the full program. Debug rebuilds and renderer shutdown use
the existing draw-variant invalidation path. The code uses the existing GL 3.2
shader/UBO path and adds no modern-context requirement.

## Validation

`tools/rend2/test_lightall_specialization_gl.py` compiles the real lightall vertex,
fragment and material libraries in a hidden SDL context. It checks seven bounded
pairs: fast lightmap, PBR lightmap with sun/local shadows and LTC, model grid with
split sun, SSR/SSGI/specular-AA, ordinary POM, skeletal animation, and RT/debug.
It compares preprocessed vertex sources and retained std140 member offsets, then
renders retained baked/sun light and emission through both production programs.
Color/glow, written screen-space attachments and depth are compared. Skeletal
animation is compile/ABI-only in this harness; game runtime covers models.

`--bench` reports interleaved full-screen GPU nanoseconds per submitted fragment
at 512x512, including the retained material and MRT writes. Those are shader-scope
measurements, not whole-game FPS. `--case NAME` bounds a diagnostic repeat.
Set `REND2_CPP` to a GCC/Clang preprocessor if it is not on PATH; Optimus uses the
existing `SHIM_MCCOMPAT=0x800000001` adapter selection.

## Measured results (2026-11-26)

Both SP/MP renderer targets built as MSVC 19.27 x64 Release. The driver tests
passed on Intel UHD 27.20.100.8280 and RTX 2060 616.92, using an actual GL 3.2
context. Each adapter compiled/linked all seven pairs, retained the shared UBO
member offsets, and produced **zero** float/color/screen-output or depth
differences in all six rendered pairs. Separate negative controls changed the
reference by 0.10 (emission), 0.127 (sun) and 0.276 (baked light), confirming those
retained inputs are exercised. The ordinary vertex **and fragment** sources also
preprocessed identically to baseline `ce579b0d7` for all seven combinations.

Median GPU nanoseconds per submitted fragment, original -> specialized:

| Actual lightall program inputs | Intel UHD | RTX 2060 |
| --- | ---: | ---: |
| PBR lightmap, sun/local shadows, LTC | 97.674 -> 8.891 | 6.393 -> 3.616 (clock drift) |
| PBR model light grid, split sun, local shadows | 66.787 -> 8.075 | 2.079 -> 1.638 |
| PBR lightmap, sun/local shadows, SSR/SSGI, specular AA | 84.605 -> 9.480 | 3.206 -> 2.864 |

Every measurement uses the same zero local-light mask and retains baked/sun
light, material work and emission. Five GPU samples per variant are interleaved
A/B, B/A, A/B, B/A, A/B, with five full-screen draws per sample at 512x512. CPU
renderer builds had finished before timing. External CPU-heavy background work
remained active, so these are shader-scope results and **no whole-frame/FPS gain
is claimed**. The full user-quality game diagnostic did not reach a usable Main
View inside its bounded startup; it does not establish a dominant pass. The
large Intel delta is consistent with removing inactive shader-code pressure;
there is no direct ISA/register-spill counter proof.

The RTX lightmap/LTC samples changed clock regime during the interleaved run:
about 6.4/3.61 ns became 2.66/1.515 ns (original/specialized). Its table medians
mix those regimes, so its absolute values are unstable; both regimes show about
43% paired reduction. The RTX grid/screen samples were stable. Raw samples are
retained rather than hiding the clock transition.

The ordinary shader is unchanged when local lights are enabled. Actual scene
gain depends on eligible zero-mask coverage and the program mix; existing
MD3/Phong/silhouette-POM variants deliberately keep their current implementation.
First use can compile one additional variant of a material program; subsequent
use loads the existing program/binary cache.

Raw samples, source/DLL hashes, driver versions and compile/image/ABI evidence
are in [the numerical receipt](../tools/rend2/lightall_specialization/validation-2026-11-26.json).
## Independent game runtime validation

The same candidate SP DLL passed a frozen 1280x720 Yavin fixture with 40 static
barrels, native grass, sun shadows and the velocity path initialized. Requested
controls 3-6 stayed enabled (`worldSurfaceList 1`, `modelOcclusion 1`,
`surfaceSpritesCompute 2`, `md3Instancing 1`). Six strict specialization A/B and
repeat image pairs had **zero changed channels**. Eight renderer test lights
changed 1,667,580 channels, and removing them restored the original image
exactly. `glslusage all` confirmed specialized programs on the zero-mask path,
full programs with specialization off, and a mixture of full/specialized programs
when the lights appeared. That proves the switch and fallback were exercised.

The bounded runtime uses legacy local lights (`forwardPlus 0`, `dlightMode 1`),
normal/specular mapping, HDR, sun/alpha-caster shadows and velocity. Full-feature
PBR/LTC/Forward+/SSR/SSGI/RT/debug shader coverage comes from the driver matrix;
the game fixture disables those effects to bound cold compilation. Frame and
pass timings drifted, including unchanged passes, so the runtime establishes
correctness and activation rather than a net frame-time gain. Portal/mirror
coverage is by the preserved per-draw/per-view predicate, not a separate visual
runtime fixture in this receipt.

The independent harness, exact settings, image controls and timing limitations
are in [the runtime receipt](../tools/rend2/lightall_specialization/runtime-2026-11-26.json).
