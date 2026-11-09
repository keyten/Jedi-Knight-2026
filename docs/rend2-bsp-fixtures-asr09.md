# ASR09: luminous BSP fixture catalogue

`tools/rend2/bsp_fixtures/catalogue.py` recovers connected planar source hypotheses
from original JA RBSP geometry and source shader/texture evidence. It writes a
research JSON catalogue, a TSV inventory and an OBJ containing the recovered
support polygons, with fixture IDs as OBJ group names. All records retain
`historical_bake_participation: unknown`.

This is an offline research adapter. The canonical evidence/archive contracts,
historical solver, shared LTC scanner, renderer and world shading are unchanged.
The output is not an occluder mesh or a reconstructed lighting asset. In
particular, an unavailable fixture asset or unsupported surface is not evidence
of empty space.

## Audit and implementation boundary

The audit covered `lightrecon/evidence.cpp`, `emission_geometry.cpp`,
`classification.h`, the material snapshot in `tr_lightrecon.cpp`, and shader/cull/
texture-transform interpretation in `tr_shader.cpp`, `tr_backend.cpp` and
`tr_shade_calc.cpp`. Existing source IDs, provenance, motion flags and shape
alternatives already cover much of the objective. ASR03's `fixture_hypothesis.h`
joins evidence to explicit object membership; it does not discover BSP regions.

The missing research information was full emitting support and all emission
layers. The current material snapshot retains the first emission texture and
flags additional layers. The shared sampler uses one UV lattice per connected
group, chooses the first triangle at a UV location, retains sample centres, and
approximates extents with a margin. Static `tcMod` coordinates are marked
unsupported. Those choices remain intact for existing consumers.

The separate catalogue makes the richer geometry inspectable without extending
the canonical material/evidence codec or changing historical extraction results:

- Original RBSP draw positions, UVs, indices, face planes, vertex normals,
  material names, surface ordinals and model ownership are retained. Face and
  triangle-soup polygons are supported. Patches, flares and other unsupported
  kinds get stable records with null position, normal and footprint.
- Every selected glow stage is considered separately. `q3map_surfacelight`
  remains a distinct authored legacy-tool prior; glow is a display hint with
  unknown intensity. A compiler-only material uses its light-image/editor/base
  texture as an explicitly inferred footprint prior. It is not proof that glow
  masking governed the original bake.
- Source texture RGB is decoded with an explicitly assumed sRGB transfer before
  box reduction. Contiguous lit runs preserve dark gaps. Each run is clipped to
  **every** source triangle using its own transformed UVs, so overlapping UVs
  cannot hide another polygon. Repeat, negative UVs, clamp, static `tcMod scale`
  and `tcMod transform`, in declaration order, are supported. Dynamic coordinate
  operations, missing masks and wholly dark surfaces retain uncertain support.
  A dark triangle beside a lit triangle does not become an extra lamp.
- Polygons connect through actual area overlap or a shared edge in the same
  material/model/plane. Nearby polygons and corner-only contacts remain
  separate. Spatial bins bound candidate comparisons; geometry quantization only
  indexes possible groups and does not itself connect them.
- Exact source-facet duplicates retain surface/triangle aliases. Overlapping
  source polygons and stage masks are geometrically unioned for fixture area
  and centroid. Per-stage footprints and relative color priors remain available;
  their areas are not summed to fabricate additional emitting area.
- The centroid is area weighted over the union, independently of the rectangle
  centre. RBSP index winding may oppose the stored face normal: the emitting
  side uses the original face plane, or compatible source vertex normals, with
  the cull policy applied. Unknown sides stay null. Brush-model coordinates are
  labelled `original_brush_model_unplaced` and are ineligible as static world
  placements; this task does not recover their entity transforms.
- The actual clipped polygon union is always available. A minimum-area enclosing
  rectangle becomes a shape alternative only for resolved RGB-mask support with
  a known side and at least 98% rectangle coverage. Compact support up to four
  world units adds a point suggestion; aspect ratios of at least 6:1 add a tube
  with explicit endpoints/radius. Irregular supports keep their surface shape.
  Disconnected fixtures are never replaced by one enclosing rectangle.

`emitting_footprint_known` describes the conditional static **RGB threshold**
support, not certified optical emission. `footprint_interpretation`,
`optical_coverage_known`, alpha/composition issues and the historical status make
that distinction explicit. Color priors use area-weighted lit-run averages,
including constant shader RGB, and remain relative. No encoded lightmap RGB8,
lightmap fitting, irradiance calibration or physical intensity inference occurs.
Extended Rend2 emission parameters, complex blending, animation, deformation,
entity-dependent color and optical alpha are flagged rather than certified.

## Identity, bounds and reproducibility

Fixture IDs are full SHA-256 strings over the research schema, original BSP
digest, material/model identity, source surface/triangle aliases and clipped
support geometry. Separate layer IDs include shader/stage and selected image
identity. IDs identify hypotheses in this research lineage, not exact historical
lamps and not canonical `EmitterEvidence` IDs. Repeated input produces identical
JSON, TSV and OBJ bytes. A changed/recompiled BSP starts a new map lineage;
q3map2's complete BSP bytes need not reproduce across fresh compilations.

Asset lookup is an **explicit** base-directory view: loose files, then reverse
lexical PK3 order. Matching asset paths are normalized; selected shaders and
images carry source paths and content hashes. Duplicate shader definitions use
the first lexical shader file and are flagged on affected fixtures. This is not
a claim to reproduce an unspecified engine/mod search path. Stock counts below
are scoped to this view and the recorded mask resolution/threshold.

Bounds include 128 MiB per source asset/BSP, 64 MiB shader text, 100,000 shader
definitions, 1,048,576 original vertices, 4,194,304 indices, 131,072 surfaces,
500,000 luminous triangles, 4,096 source masks, 250,000 cached mask rectangles and
16,777,216 pixels per decoded image. The default mask axis is 64 (4–512 supported); threshold is 0.1 in assumed
relative linear RGB luminance. Work/polygon budgets default to five million
operations and 250,000 pieces; stock inventory uses one million operations.
Limits abort construction instead of returning a truncated catalogue. These are
algorithm/data limits, not a process RSS guarantee. Extraction finishes before
export starts; callers should use a fresh output directory for each experiment.

## Reproduce

Python 3.8+ and Pillow are needed for source-image decoding. Unit geometry tests
use only the standard library. Run from the repository root:

```powershell
python -m unittest discover -s tools/rend2/bsp_fixtures -p test_catalogue.py -v

python tools/rend2/bsp_fixtures/compiled.py `
  --compiler build/toolchain/netradiant-20220628/portable/netradiant_1.5.0-20220628-windows-amd64/q3map2.exe `
  --output build/asr09/compiled

python tools/rend2/bsp_fixtures/inventory.py `
  --base C:/path/to/GameData/base --maps hoth2 hoth3 --output build/asr09/stock

# One map, with a different explicit sampling policy:
python tools/rend2/bsp_fixtures/catalogue.py `
  --base C:/path/to/GameData/base --map maps/hoth3.bsp `
  --mask-axis 64 --threshold 0.1 --output build/asr09/hoth3-64

cmake --build build/lightrecon-disk-standalone --parallel 4 --target lightrecon_evidence_test lightrecon_fixture_tool lightrecon_bsp_capture_test lightrecon_offline_test
ctest --test-dir build/lightrecon-disk-standalone --output-on-failure -R '^lightrecon_(evidence|fixture_discovery|bsp_capture|offline)$'
```

The compiler runner authors its own map, textures and shader, runs JA q3map2
`-meta`, and checks position, outward side, exact extent/area, split masks, static
scale/transform, layered-mask union, nearby separate bulbs and point/tube
alternatives against the independently declared brush dimensions. It does not
run the compiler light phase or historical solver. Its source, BSP, compiler log,
JSON/TSV/OBJ and digest manifest stay under the requested output directory.

## Validation, 2026-11-09

Fourteen unit tests pass, including overlapping UVs on disconnected geometry,
partial/complete duplicate support, corner-only contact, nonrectangular support,
mask clamp/repeat/negative coordinates, unknown side, singular UVs, unavailable/
dark masks, motion/deformation, bounded failure and deterministic exports. A
fresh q3map2 compile passes **105 geometry checks**, yielding ten emitting regions
from eight luminous BSP surfaces/sixteen triangles. All four existing evidence,
ASR03 fixture, BSP-capture and offline archive regression tests pass after rebuild.

At mask axis 32/threshold 0.1, two repeated exports of each installed stock map
are byte-identical:

| Map | BSP surfaces | Luminous surfaces | Source triangles | Hypotheses | RGB footprints resolved | Unsupported surfaces | Point / rectangle / tube |
|---|---:|---:|---:|---:|---:|---:|---|
| hoth2 | 7,421 | 149 | 368 | 337 | 304 | 43 | 5 / 67 / 0 |
| hoth3 | 12,911 | 229 | 516 | 314 | 205 | 19 | 0 / 121 / 0 |

Shape alternatives overlap. Resolved RGB footprints include conditional optical
coverage and moving-model cases; neither this column nor static eligibility is a
recovered-light count. Whole-surface dark-mask hints are 7/48 and unavailable-mask
hints are 26/61 for hoth2/hoth3. Stock truth/recall and historical bake membership
are unknown. The stricter clipping/connection policy also means these counts are
not equivalent to the legacy extractor's inventory.

`tools/rend2/bsp_fixtures/results/stock-inventory.json` records source digests,
counts, issues, export hashes and representative stable IDs. Full stock positions
and support polygons remain in local `build/asr09/stock/<map>/catalogue.json`,
`fixtures.tsv` and `footprints.obj`; stock geometry/textures are not redistributed.
