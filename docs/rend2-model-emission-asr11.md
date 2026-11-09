# ASR11: luminous regions on static MD3 instances

`tools/rend2/model_emission/emission.py` extracts conditional luminous geometry
from the original MD3 poses/material names supplied by ASR10 and source shader/
image evidence. It writes `emission.json`, `regions.tsv` and
`emitting-footprints.obj`. Region/fixture groups use stable IDs and all polygons
are in original world units. Historical bake participation and occlusion
participation remain unknown.

Only offline research tools are added. ASR09/ASR10 behavior, canonical asset/
storage contracts, the historical solver, renderer paths and world shading are
unchanged. Nothing installs external geometry into an occlusion query, publishes
runtime lights or resolves double lighting.

## Extraction and provenance

The implementation reuses ASR10's original MD3 decoder, static-instance
classification, transforms, material/skin/remap interpretation, asset aliases and
BSP duplicate suppression. It reuses ASR09's shader tokenizer/stage parser,
UV-to-triangle mask clipping, connected planar components, geometric union, area
measurement and rectangle/tube/point suggestions. The source audit also covers
`tr_shader.cpp`, `tr_mesh.cpp`, `tr_backend.cpp`, and the separate emissive sampling
in `glsl/generic.glsl` and `glsl/lightall.glsl`.

Bright albedo alone never creates emitting geometry. A selected material needs
an explicit supported `glow` or Rend2 emissive declaration, a known source image
or explicitly declared `$whiteimage`, and supported static UVs. Compiler-only
`q3map_surfacelight` is a separate declaration diagnostic, not a substitute for
display emission. Missing/undecodable images, unknown materials, untextured meshes,
animated images, unsupported coordinates and deformed geometry retain unresolved
diagnostics **without whole-mesh emitting polygons**. This stricter boundary is
specific to ASR11; ASR09's uncertain whole-surface hints remain unchanged.

Each supported layer is sampled in original model UV space. Texture RGB is
decoded with an explicitly assumed sRGB transfer before box reduction. Constant
shader RGB modulation precedes the threshold; the lit-run rectangles are clipped
to each original model triangle. Repeat, clamp and ASR09's static scale/affine
texture transforms are supported. Source alpha, cutout/complex blending and
translucency remain optical-coverage uncertainty. A translucent lens may retain
conditional **RGB** support while `optical_coverage_known` is false.

Rend2 `emissiveMap`, constant `emissiveColor`, scalar/RGB `emissiveScale`, and
`emissiveNits`/`emissiveLuminance` retain authored priors. Their source image and
color parameters are separate from the base stage's `rgbGen`. Explicitly authored
zero emission contributes no support. Legacy glow and texture RGB do not become
nits. A varying glow `rgbGen` retains conditional texture support, unknown color
modulation and false static eligibility. Per-layer priors stay separate when
composition is unknown; overlapping layers are not silently added into a fitted
or calibrated radiance.

`model_manifest` pins MD3 digests, selected frames, submesh names and original
shader slots. `instance_manifest` pins entity ordinals, source fields, transforms,
eligibility and BSP correspondence. `shader_manifest` records the selected source
file, exact file-byte hash, normalized declaration hash, stage ordinal/tokens,
cull policy and authored parameters. `mask_manifest` records the selected texture
path, source archive, exact image-byte hash, source/reduced dimensions, transfer
assumption and alpha uncertainty. ASR09's explicit asset view is retained: loose
base files, then reverse lexical PK3 order; ambiguous shader declarations stay
unresolved. This does not certify an unspecified mod/runtime search path.

Layer identities include the image content and mask policy. Local source-region
IDs include MD3/frame identity, layer/source-triangle references and clipped
geometry. Instance-region IDs additionally include ASR10 placement identity.
Fixture IDs reference their component regions, with a separate source ID shared
by repeated instances. A recolored source mask therefore changes source lineage
even if its footprint is identical. Repeated identical inputs produce identical
JSON/TSV/OBJ bytes; fresh BSP compilations may start a new map lineage.

## Geometry, direction, grouping and coverage

Clipped local geometry is cached per asset/material selection, then transformed
with the actual ASR10 instance matrix. World area, area centroid and shape bounds
are recomputed after nonuniform scaling. No housing bounds replace a luminous
mask footprint.

Direction is a hypothesis from triangle winding and the selected cull policy.
JA's default front culling makes the visible side oppose the counterclockwise
cross product. A mirrored instance changes that visible side; shading normals
are retained as source evidence and disagreements are flagged rather than used
to flip the result. Two-sided surfaces keep opposite direction alternatives and
no single outward vector. Differing face normals remain separate region normals;
an angular/closed fixture has no fabricated common direction.

Coplanar luminous components retain actual clipped polygon unions. Regions within
one attested instance can form a fixture hypothesis through emitting-edge contact
or coplanar overlap; non-emissive housing never connects them. Nearby/disconnected
islands and different instances remain separate. These are connected luminous
components, not a recovered historical lamp census. All regions retain the parent
physical model instance, including when several islands share one prop.

Union is applied across coincident triangles, layers and overlapping materials
before fixture area/centroid are computed. Per-layer color priors remain available;
an aggregate color is withheld for unresolved overlapping composition. A region
gets a rectangle alternative only with at least 98% rectangle coverage. Compact
support adds a point alternative; elongated planar support adds tube endpoints/
radius. Multiple angular regions retain a surface collection, with a compact
point alternative when justified by their extent. No large rectangle bridges
unsupported disconnected geometry.

`emitting_area` describes conditional thresholded RGB support in world units
squared. `source_triangle_RGB_coverage` divides that union by the referenced
original draw-triangle area; it is not an optical confidence. Per-instance
`resolved_RGB_fraction` compares unioned supported fixture area with the complete
known MD3 draw-geometry area. Source duplicates can affect the denominator, so it
is a diagnostic of source coverage, not a physical luminous fraction. Layer
diagnostics separately count unresolved UV/geometry, masked-out and lit triangles.
The relative color prior uses area-weighted lit-run averages and is explicitly an
approximation when a triangle covers only part of a varied-color run. RGB8 is
never interpreted as calibrated linear irradiance.

## Reproduction and limits

Python 3.8+ and Pillow suffice for extraction. The compiled controlled demonstration
also needs a JA-capable q3map2 executable:

```powershell
python -m unittest discover -s tools/rend2/model_emission -p test_emission.py -v

python tools/rend2/model_emission/controlled.py `
  --compiler build/toolchain/netradiant-20220628/portable/netradiant_1.5.0-20220628-windows-amd64/q3map2.exe `
  --output build/asr11/controlled

python tools/rend2/model_emission/emission.py `
  --base C:/path/to/GameData/base --map maps/hoth3.bsp --profile sp `
  --mask-axis 32 --output build/asr11/hoth3

python tools/rend2/model_emission/inventory.py `
  --base C:/path/to/GameData/base --maps hoth3 t2_wedge --output build/asr11/stock
```

`extract(asr10_catalogue, assets, ...)` consumes an ASR10 build result. Its default
limits are five million geometric work operations, 250,000 cached/local/world
polygons, 100,000 regions, 4,096 images/masks, one million reduced-image cells,
250,000 cached lit rectangles, 16 million decoded pixels per image, and 256 MiB
combined output. ASR09 text/asset and ASR10 model/instance limits also apply. The
default mask axis is 64 (4–512 supported), relative luminance threshold 0.1.
Budget failure returns no partial catalogue; formatting budgets are checked
before file publication. These are data/work bounds rather than an RSS promise.
Use a fresh output directory for each extraction policy.

## Validation, 2026-11-09

Thirteen new tests pass: masked bulb on bright non-emissive housing; repeated,
rotated and nonuniformly scaled placements; mirrored winding/reversed shading
normals; two-sided/translucent lenses; missing/black/animated/untextured materials;
authored emissive priors; duplicate mesh/layer/material union; angular fixtures;
separate mask islands; tube geometry; provenance/identity changes; deterministic
exports and budget failure. The fresh owned q3map2 scene passes **37 checks** and
emits three masked bulb regions for three real static placements, with no housing
emission. The 28 ASR09/ASR10 tests and four existing evidence/fixture/BSP/offline
C++ regressions also pass.

At mask axis 32/threshold 0.1, complete repeated stock exports are byte-identical:

| Map | Eligible static instances | Supported planar regions | Connected fixture hypotheses | Local asset/material extractions |
|---|---:|---:|---:|---:|
| hoth3 | 379 | 1,161 | 623 | 27 |
| t2_wedge | 40 | 266 | 178 | 10 |

Two independent stock submesh audits demonstrate emission on part of an object:

- `models/map_objects/hoth/light_wall.md3`: all 96 retained static placements
  emit only from submesh 2 (`mesh02`), using
  `models/map_objects/hoth/light_wall_bulb_glow`. Neither housing submesh enters
  the emitting geometry. There are 384 planar regions across these placements;
  one inspected instance has RGB support covering 15.69% of its draw-geometry area.
- `models/map_objects/hoth/bed_lamp.md3`: all seven static placements emit only
  from submesh 1 (`glass`), using the separate `_glw` texture. There are 175 planar
  regions; the inspected instance has 4.85% source-geometry RGB coverage.

The two factory `ceiling_light.md3` placements in `t2_wedge` are
`misc_model_breakable`, not static-model placements. They remain excluded with
unresolved static geometry; inspecting their glow shader does not promote them
to static emitters. Counts above overlap shape alternatives and remain source
hypotheses, not discovered-light precision/recall or historical bake proof.

Metadata-only manifests live in `tools/rend2/model_emission/results/`. Full licensed
geometry, shader text, source images and world-space footprint exports remain
local under `build/asr11/`; they are not redistributed with the research metadata.
