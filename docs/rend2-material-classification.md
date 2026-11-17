# Semantic surface materials

`r_materialClassification 1` enables registration-time semantic classification
in the shared SP/MP rend2 renderer. It defaults to **0** and is archived and
**latched**: use `vid_restart` after changing it. The classifier only records
metadata. It does not change textures, BRDF parameters, AutoPBR classes, weather
response, or apply procedural aging.

```
set r_materialClassification 1
vid_restart
set r_materialClassificationDebug 1
r_materialClassificationDump
r_materialClassificationDump sand
```

`r_materialClassificationDebug` defaults to 0, is a live cheat cvar, and requires
the master switch. It uses a distinct flat RGB for every type (including grey
for unknown), bypasses tone mapping, suppresses blending/refraction on the debug
draws, preserves alpha-tested silhouettes and skips 2D UI and depth/shadow passes.
Generic, lightall, skybox and surface-sprite paths carry the view. The dump lists
the RGB legend, evidence source, confidence, flags and available PBR statistics.
Other simultaneous debug views can still take priority in their own specialized
passes; use this view by itself.

## Types

| Group | Types |
| --- | --- |
| Ground | `sand`, `soil`, `mud`, `gravel`, `rock` |
| Construction | `concrete`, `plaster`, `masonry`, `tile` |
| Manufactured | `metal`, `painted_metal`, `glass`, `rubber`, `plastic`, `electronics` |
| Vegetation | `wood`, `bark`, `grass`, `foliage` |
| Water/cold | `water`, `snow`, `ice` |
| Organic/textiles | `fabric`, `carpet`, `leather`, `skin`, `hair` |
| Special/unresolved | `sky`, `effect`, `unknown` |

Marble/granite belong to rock, ceramic to tile, canvas to fabric, dry and green
leaves to foliage. Solid/hollow wood and metal share semantic types. Painted
metal is a **hypothesis about a dielectric coating over a named/tagged metal
substrate**, not a measured substrate identity. Metalness alone cannot identify
the substrate underneath a dielectric. Armor is conservatively plastic-like.

## Evidence and priority

1. An explicit shader-level `surfaceMaterial <type>` overrides inference, even
   `surfaceMaterial unknown`. It does not overlap the stage-level `materialType`
   keyword, which chooses hair/eye BRDFs. Invalid values warn and are ignored.
2. Structural shader semantics (`skyParms`, water contents) identify sky/water.
3. JA `material` / `q3map_material`: all 31 nonzero stock material values map to
   the types above. Metadata remains authoritative if a PBR map disagrees.
4. Explicit `cloth` and `materialType hair|eye` identify fabric/hair/skin.
5. Authored/discovered packed PBR maps identify predominantly metallic surfaces:
   mean metalness >= 0.65 and at least 65% of sampled texels >= 0.8. This outranks
   a name heuristic. AutoPBR-generated maps/defaults are excluded as evidence.
6. Material words in shader/diffuse basenames. Token boundaries
   include separators, digits and camel-case transitions. A small explicit stock
   compound vocabulary handles `metalsec`, `basemetal`, `rockwall`, etc. Map family,
   wall/floor, generic architecture and colour words do not imply a material.
7. Existing conservative character/body-part heuristics can supply organic,
   textile or armor fallbacks, at lower confidence. Generic weapons/droids are
   not automatically declared bare metal.
8. Material words in directories, after known organic characters (a
   `sand_creature` is skin, rather than sand terrain).

Known/tagged metal with >=85% dielectric texels (metalness <=0.2) and mean <=0.15
becomes `painted_metal`, unless explicitly overridden. If >=20% of samples are
metal and >=20% dielectric, `MixedPBR` is retained. Strong metallic PBR that
disagrees with an authoritative dielectric semantic type sets `ConflictingPBR`.
These flags do not automatically change rendering. Confidence (0..100) indicates
heuristic strength, not a calibrated statistical probability.

Roughness is retained as evidence for future consumers but does **not** identify
dielectric families: sand, concrete and rock have overlapping PBR parameters.
An unresolved surface stays unknown. A shader with several materials blended
across one atlas/terrain still has one primary semantic result and a possible
mixed flag; this is not per-pixel segmentation.

## Data and cost

`shader_t::surfaceMaterial` stores `Rend2Material::Result`: type, source,
confidence, flags, reason and PBR mean/coverage statistics. Future procedural
consumers should check confidence and mixed/conflicting flags before using it.
The pure CPU API lives in `tr_surface_material.h/.cpp`; renderer adaptation is
in `tr_material_classification.cpp`. The base stage is chosen after stage
collapse, preferring lit diffuse stages over lightmaps/detail/glow/reflections.
Surface-sprite shaders are classified separately from their ground shader using
their own diffuse image.

Packed maps are sampled while their original RGBA pixels are already decoded,
before upload/picmip/compression: <=4096 deterministic jittered samples, no GL
readback, new textures or frame-time classification. ORM(S), RMO(S), MOXR/MOSR
layouts use the same channels as the loader's GL swizzle. Statistics belong to
the cached image and survive image reuse. Scalar packed material values are
also accepted. Non-unit packed-map scales conservatively invalidate aggregate
coverage evidence. Spec-gloss has no unambiguous metalness channel and is not
treated as direct metallic evidence; shader semantics/names still work.
With the switch off, the sampling and classification work is skipped.

## Detail Texture Synthesis

Rend2 can use the semantic classification to add microdetail to the lit base
stage of an opaque or alpha-tested surface. The separate archived, latched
switch defaults to 0.

```
set r_materialClassification 1
set r_detailTextureSynthesis 1
vid_restart
```

The renderer generates a mipmapped array of seamless material tiles at startup.
Rock uses warped cellular fissures and pits, metal uses brushing and short
scratches, plaster uses fine aggregate and sparse pores, and wood uses
elongated grain and splits aligned to the diffuse UVs.
Other profiles cover grains, pores, coatings, fibers, weave and frost. The
detail slope is reoriented over the base normal map. Small adjustments to
perceptual roughness and the diffuse lobe help the detail register under different
lighting. The base layer fades from 96 to 256 world units. A finer sample blends
in from 96 to 32 units and is fully present within 32 units.

`r_detailTextureMicro` is archived and latched, defaults to 0 and accepts
strengths from 0 to 2. Set it to 1 and run `vid_restart` for an additional
analytic layer on magnified surfaces. It fades from 14 to 48 world units, with
additional attenuation based on diffuse magnification and the projected width
of the procedural features. The shader evaluates continuous noise slopes and
the shapes of small pits, scratches and threads in UV space. The closest views
can resolve a further band of finer grain. This adds new detail above the
resolution of the authored maps and retains their large-scale features.

`r_detailTexturePBR` is an archived live strength control in the range 0 to 2,
with 1 as the default. It scales the signed perceptual-roughness variation of
both layers. The pattern follows the surface finish, with variation across
mineral grains, pores, scratches, fibers, coatings and thread. The renderer
uses the authored metalness, F0 and AO inputs when evaluating the material.
Both cvars apply with `r_detailTextureSynthesis 1`.

The detail layering follows the same material channels as
[Unity HDRP detail maps](https://docs.unity.cn/Packages/com.unity.render-pipelines.high-definition%4010.1/manual/Mask-Map-and-Detail-Map.html)
and [CryEngine merged detail maps](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/29449936).
Visual references for the generator scale and finish include
[Beige Wall 001](https://polyhaven.com/a/beige_wall_001),
[Rock 01](https://polyhaven.com/a/rock_01),
[Metal 009](https://ambientcg.com/view?id=Metal009),
[Blue Metal Plate](https://polyhaven.com/a/blue_metal_plate), and
[Fine Grained Wood](https://polyhaven.com/a/fine_grained_wood).

Classification confidence scales the effect. Mixed or conflicting inferred PBR
evidence attenuates it further. Low authored roughness reduces relief and
diffuse variation for polished finishes. Unknown materials, water, sky, effects, blended
stages and stages with texture-coordinate modifiers are skipped. The feature
uses the per-pixel lightall path (normal or specular mapping enabled) and
requires at least 32 fragment texture units.

## Licensed asset inspection and verification

The implementation was based on the local stock `assets0-3.pk3` and
`assets8_pbr1-3.pk3` content. Stock `assets1.pk3` contains 68 shader scripts, with
explicit tags including sand, grass, gravel, rock, concrete, metal, glass, snow,
ice, water, plaster, wood and carpet. Examples include `desert/stucco` (plaster),
`factory/outside_rock1b` (rock), `hoth/mp_rock2` (**ice despite its name**),
`bespin/basic8` (concrete) and `rooftop/office_floor_carpet_trim` (carpet).
The three PBR archives contain replacement images and `_rmo` maps, with no
additional `.shader` scripts. This is why both stock metadata and physically
authored packed maps matter.

CPU checks can be built without Boost:

```
cmake -S . -B build/material-classification -DBuildMaterialClassificationTests=ON
cmake --build build/material-classification --config Release --target rend2_material_classification_test
ctest --test-dir build/material-classification -C Release -R rend2_material_classification --output-on-failure
```

`tools/rend2_material_audit.py` feeds the real C++ classifier's TSV probe with
stock definitions and PBR texture data (requires Pillow). It reads the selected
licensed archives in place and writes only a metadata report. It uses complete
RMO histograms as an independent offline check; threshold-borderline results
can differ from the runtime's bounded sampling. It does not emulate the whole
renderer stage collapse or organic model fallback.

```
python tools/rend2_material_audit.py --base <licensed-base> --probe <rend2_material_classification_test.exe> --output build/material-audit.json
```

Verified on this change: Release builds of both SP/MP renderer DLLs with MSVC,
520 CPU checks through CTest and a separate GCC build with `-Wall -Wextra
-Werror`, and compile/link of nine real debug GLSL programs in a hidden GL 4.3
context (no draw calls). The GLSL check covers unlit/sky lightall, lit PBR,
generic, surface sprites, FX sprites and sprite velocity; run it with
`python tools/rend2/test_material_classification_gl.py`.

The [offline audit summary](rend2-material-classification-audit.json) covers
4,235 definitions/PBR texture entries and 1,261 decoded RMO maps with zero image
errors. It records 177 mixed maps and three semantic/PBR conflicts. 2,954 entries
remain unresolved in this deliberately conservative audit (it includes UI/FX and
does not emulate the renderer's model fallback). Counts are coverage, not an
accuracy measurement or hand-labelled ground truth.

No game launch or visual verification is part of these checks.
