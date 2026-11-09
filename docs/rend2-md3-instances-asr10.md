# ASR10: static MD3 instances in world coordinates

`tools/rend2/model_instances/instances.py` is a bounded offline catalogue of
retained JA model placements. It provides stable per-map instance IDs, MD3 asset
digests, source entity ordinals/fields, selected poses, transforms, world bounds,
model-surface references and explicit asset/geometry uncertainty. It uses the
ASR09 explicit base-directory asset view; no game installation is modified.

This change does not extend canonical lighting/storage schemas, fit any lights,
change the historical solver, load a renderer or submit runtime geometry. Every
instance retains unknown historical bake and occlusion participation. A missing,
malformed, dynamic or unresolved model never becomes an invented empty volume.

## Source audit and selected semantics

The implementation follows these repository sources:

- `code/game/g_misc.cpp`: `SP_misc_model` frees the compiler entity;
  `SP_misc_model_static` queues external MD3 placement, then frees the game entity.
- `code/cgame/cg_main.cpp`: `CG_CreateMiscEntFromGent`, `CG_CreateMiscEnts` and
  `CG_DrawMiscEnts` retain origin/angles/scale, select frame 0, and leave
  `customSkin`, `customShader` and `skinNum` at their zero defaults.
- `codemp/cgame/cg_spawn.c`: MP reads static placements from original entity text,
  gives `angles` precedence over `angle`, and vector scale precedence over scalar.
- `shared/qcommon/q_math.c`: `AnglesToAxis`/`AngleVectors`; axes are local X/Y/Z
  columns, including the negated right vector used as the Y axis.
- `code/cgame/cg_ents.cpp`: SP `ScaleModelAxis` skips zero components. In contrast,
  the MP static loader directly scales its axes, so a zero component is singular.
- `shared/rd-rend2/tr_model.cpp`, `tr_mesh.cpp`, `tr_skin.cpp`, `tr_bsp.cpp`,
  `tr_shader.cpp` and `tr_backend.cpp`: MD3 quantized vertex/normal decoding,
  surface-name normalization, default shader selection, skin/custom-shader
  precedence, and one-step inherited worldspawn shader remapping.
- The retained pinned compiler source
  `build/bake-model-sources/net/bspfile_abstract.c::UnparseEntities` explicitly
  strips `misc_model`. Fresh JA q3map2 `-meta` compilation validates compiler
  transforms, frame and semicolon material remaps independently of the renderer.

The two runtime profiles are explicit (`--profile sp` or `mp`). In SP, a nonzero
`modelscale` overrides `modelscale_vec`, and angle assignments follow original
entity pair order as `G_ParseField` does. MP prefers a supplied vector scale and
an `angles` vector. The tested q3map2 compiler profile prefers vector scale; it
remains labelled a compiler hypothesis until geometry correspondence is checked.
All profiles apply `world = origin + R S local`. `zoffset` affects culling only.

One stock hoth2 entity supplies `modelscale "1.75 1.75 1.75"`. The loader's `atof`
uses its leading scalar. The catalogue reproduces that result, retains the
original text and flags the ignored tail. Nonfinite/unusable transforms retain
an unresolved record with null bounds rather than a guessed placement.

## Catalogue and geometry ownership

`build(original_bsp_bytes, assets, profile, source_map=None, ...)` returns the
research catalogue. `write(result, directory, export_obj=False)` produces:

- `instances.json`: shared local MD3 asset geometry and per-instance references,
  source fields, effective transform, world bounds, selected frame, material
  provenance, geometry role, matching BSP references, and uncertainty flags.
- `instances.tsv`: stable IDs, entity ordinals, model paths, geometry roles,
  availability, eligibility, bounds and issues for inspection.
- Optional `instances.obj`: world-space meshes only for eligible external static
  instances with no matched BSP support. Group names contain instance/surface IDs.
  Its header explicitly identifies conditional poses, not certified occluders.

Asset geometry contains selected-frame vertices decoded at 1/64 model units,
source UVs, shading normals, triangle indices, original surface names and the
renderer-normalized names. Asset digests/aliases and per-instance references
preserve multiple placements without duplicating local geometry. World bounds
are computed from transformed vertices, not by rotating two AABB corners.
`transform(point, pose)` recovers a world vertex; `normal_transform(normal, pose)`
applies the inverse transpose to a source shading normal. Negative determinants
are flagged; original triangle winding is retained, including in OBJ output.
Shading normals are not themselves a certified emitting direction or cull policy.

Instance identity hashes the original BSP digest, SP/MP profile, entity source
and ordinal, source-map digest where applicable, and normalized model path.
Asset identity hashes the MD3 content and selected frame; surface identity also
includes the asset identity. Asset edits therefore preserve a placement's identity
while changing its geometry references. These are research IDs, separate from
canonical BSP surface ordinals and `EmitterEvidence` IDs.
Recompiling a BSP starts a new map lineage; compiler metadata can change complete
BSP bytes even when recovered geometry is equivalent.

Roles and eligibility remain distinct:

- `external_static_runtime`: retained `misc_model_static`, available MD3 and valid
  unattached frame-0 placement. Multi-frame assets retain the selected pose and
  an explicit multi-frame issue; the static loader does not animate them.
- `compiler_baked_model`: `misc_model` metadata from a retained entity or optional
  original `.map`. These records are **never** eligible to add another copy of
  their geometry, including when source/compiler correspondence is unresolved.
- `brush_model`: `*n` references remain separate mover/model records. This tool
  does not treat their BSP geometry as a static external MD3 instance.
- `dynamic_or_effect_model`: breakables, gameplay models and other MD3-bearing
  classes retain metadata/availability without fabricated static pose bounds.
  Unresolved parent/bolt attachments similarly prevent geometry placement.

The runtime static paths do not apply entity `skin`, `_remap`, custom shader or
custom skin keys. Such hints remain in `source_fields` with an explicit issue;
the inherited MD3 shader at slot zero is used. Worldspawn `remapshader*` declarations
are preserved and applied as conditional one-step source-name remaps. Conditional
`vertexremapshader*`, missing shader registration/availability, and later runtime
remap events are not silently resolved. Compiler `_remap*` semicolon pairs are
retained as source hypotheses; conflicting rules and compiler skin selection
remain unresolved. A general skin/renderer emulator is outside this adapter.

## Compiler correspondence and unknown geometry

An optional source `.map` contributes only `misc_model` declarations; brush/patch
bodies are skipped and runtime placements continue to come from the BSP. This
avoids duplicating retained runtime entities. Without source placements,
compiler-stripped instances cannot be enumerated or assigned original instance
IDs just from nearby world surfaces/material names.

A bounded BSP index verifies all three world corners, UVs and compatible source
normal sides, together with material identity. Spatial bins only identify possible
matches; adjacent bins handle floating-point boundary cases. Runtime world remaps
have a separate comparison view. Complete and partial/ambiguous correspondence
are distinguished, with original BSP surface/triangle references. Shared matches
are flagged rather than claiming unique instance ownership. Any matched support
suppresses adding an entire duplicate external mesh. No match is an unresolved
search result, not proof that the model was absent from the historical bake.

Topology changes, compiler subdivision/clipping, unknown skins or differing
assets may prevent exact correspondence. Optional source-map records are not a
claim that a supplied `.map` is the exact original authoring source. Complete
correspondence checks geometry only; historical lamp and bake membership remain
unknown. No external mesh is automatically installed into an occlusion query.

The adapter handles primary MD3 placements, not scripted spawns, Ghoul2, model2
attachments, RMG parent transforms, general skin selection or renderer LOD fallback.
Those paths do not become static geometry by assumption. No image RGB or lightmap
sample is used; no lighting or irradiance calibration occurs.

## Bounds, reproduction and validation

Input bounds include the ASR09 original BSP limits, 8 MiB entity/source-map text,
65,536 entity records, 4,096 instance records and 512 model/frame cache entries.
Each MD3 is at most 64 MiB, 1,024 frames, 1,024 tags and 128 surfaces, with explicit
array extents, index checks and finite UV validation. Decoded model data and
transformed scene geometry each default to two million vertices; scene triangle
queries are also limited to two million. BSP matching
limits one million triangles/five million comparisons; output defaults to a
combined 256 MiB JSON/TSV/OBJ budget. Count/work/output failures abort rather than
returning a truncated catalogue. These are algorithm/data limits, not an RSS
guarantee. Use a fresh directory when changing an export policy.

Python 3.8+ standard library suffices; no renderer or Pillow is needed:

```powershell
python -m unittest discover -s tools/rend2/model_instances -p test_instances.py -v

python tools/rend2/model_instances/compiled.py `
  --compiler build/toolchain/netradiant-20220628/portable/netradiant_1.5.0-20220628-windows-amd64/q3map2.exe `
  --output build/asr10/compiled

python tools/rend2/model_instances/instances.py `
  --base C:/path/to/GameData/base --map maps/hoth3.bsp --profile sp `
  --output build/asr10/hoth3 --export-obj

python tools/rend2/model_instances/inventory.py `
  --base C:/path/to/GameData/base --maps hoth2 hoth3 taspir1 --output build/asr10/stock

# Only when original compiler placements are available:
python tools/rend2/model_instances/instances.py `
  --base build/asr10/compiled/authoring/base --map maps/asr10.bsp `
  --source-map build/asr10/compiled/authoring/base/maps/asr10.map `
  --output build/asr10/source-correspondence
```

On 2026-11-09, fourteen MD3 instance tests pass. They cover translation, yaw/
pitch/roll, nonuniform/mirrored/singular scaling, SP/MP precedence, frame zero,
zoffset, inherited/remapped materials, ignored runtime skins, asset aliases,
case-insensitive archive lookup, stable IDs, duplicate BSP suppression, differing
UV/normal sides, missing/invalid/dynamic models, attachment rejection, and limits.
The fresh owned compiler fixture passes **28 checks**, including complete
correspondence for both stripped model placements. The fourteen ASR09 tests and
four existing evidence/fixture/BSP/offline C++ regressions also pass.

| Stock map | Entity records | Eligible external static MD3 instances | Available selected MD3 assets/poses | Excluded brush-model records | Excluded other MD3 records | Independent light/lamp-name pose checks |
|---|---:|---:|---:|---:|---:|---:|
| hoth2 | 1,506 | 376 | 38 | 124 | 7 | 46 |
| hoth3 | 1,352 | 379 | 27 | 88 | 2 | 142 |
| taspir1 | 1,334 | 267 | 15 | 191 | 37 | 79 |

All retained static-model assets in these three maps were available. Missing
assets occur only in excluded other-model records (4/2/6). Selected light/lamp
examples use independent scalar/yaw formulas to verify the transformed MD3 vertex
bounds against entity values; names only select inspection examples and do not
certify emission. Two complete JSON/TSV exports per map are byte-identical. No
stock external placement has exact BSP triangle correspondence under this policy;
that does not establish historical bake absence or occlusion clearance.

Committed metadata-only manifests under `tools/rend2/model_instances/results/`
record checks, source/model digests, counts and representative stable IDs/poses.
The complete stock catalogues and model geometry remain local under
`build/asr10/stock/<map>/`; commercial MD3 geometry is not redistributed.
