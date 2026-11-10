# Shader-authored surface lights in reconstructed-light storage

The reconstruction producer now retains every captured surface with positive
`MaterialFacts::legacySurfaceLight` (`q3map_surfacelight`). Both original-map
producer tools supply the full original `SurfaceGeometry` through
`Producer::Request::shaderGeometry`. The extraction does not use the fitting
ROI, glow-mask threshold, candidate selection, `maxSources` fitting budget or
numerical shape support. Patches, sky, animated/deformed and inline-model
surfaces remain in the catalogue with their original type and flags. Glow-only
surfaces do not become light sources.

This uses the existing `ReconstructedLightingAsset::emitters`, provenance,
calibration, `DatasetPurpose::EmitterGeometry` and `.jklr` chunk/cache machinery.
There is no parallel JSON lighting store. Canonical schema 8 already supports
these extension records; the producer/cache algorithm version advances to 10.
Shader emission and geometry participate in the producer fingerprint, including
changes to selected material facts without a change to the BSP bytes.

## Origin and geometry

`Core::ShaderLights::IsShaderSource(asset, emitter)` identifies these records.
Their provenance kind is `Authored` with producer
`org.openjk.shader-surface-extraction/1`; each decoded payload also carries the
typed `Origin::Shader`. The provenance locator includes the source map,
original surface ordinal and shader name, and pins the original BSP SHA-256.

`ShaderLights::Read(asset, chunks, emitter, readContext, surface)` returns:

- the original `BakedSurfaceKind` (Face, Patch, Triangles, Flare or Unsupported),
  ordinal, model index, `SurfaceMotion`, shader and material index;
- original positions, vertex normals, base UVs and local triangle indices;
- patch width/height and its original Bezier control net, without flattening;
- source face basis/normal or patch LOD bounds, cull mode, original baked style
  channels, material signal/uncertainty flags;
- the raw positive `legacySurfaceLight`, raw `legacySourceColour`, and captured
  emission texture identifier.

Static world coordinates and inline-model local coordinates remain distinct.
Inline geometry requires the corresponding model entity transform before world
placement; an unknown transform never becomes a lamp at a guessed world origin.
The captured emission texture is the selected runtime material's emission hint,
not a promise that the original compiler used the same image. No new texture
color averaging, material parsing or `q3map_lightimage` inference is performed.

A four-vertex/two-triangle static planar rectangle with consistent original
source-side evidence uses canonical `EmitterKind::Rectangle`, position, frame,
half dimensions and Lambertian distribution. Orientation reuses
`SourceOrientation::Resolve`, which accounts for RBSP winding, authored normals
and culling. Its full original surface payload is retained too. Other surfaces
use `EmitterKind::Extension`, type `org.openjk.shader-surface/1`; the encoding
`org.openjk.shader-surface-light`, version 1, preserves the actual surface.
They can be consumed as mesh/Bezier surface lights without a point-light fallback.
Sky/deformed/moving/uncertain shapes are never silently replaced by rectangles.

## Photometry and fitting

The source record is an authored `Candidate`, separate from the numerical
reconstruction result. `legacySurfaceLight` remains in compiler units in the
surface payload. Canonical emission has uncalibrated-relative calibration and
unknown intensity/color; the importer does not relabel the compiler value as
nits, candela or a fitted amplitude. Future fitting/conversion must provide its
own calibration and validated result. Authored retention does not activate a
renderer consumer or make an automatic additive contribution alongside a
possibly corresponding fitted source.

Retained shader sources survive a `DiagnosticOnly` producer result even when
the numerical reconstruction is unavailable. They do not change the fitter's
candidate set, training/validation values, chosen amplitudes or final scores.
Cancellation, invalid identity, payload corruption and exceeded memory budgets
still fail publication rather than silently publishing a truncated catalogue.

## Validation and replay

The producer validates checksums, descriptor/chunk references, original geometry,
surface type and exact rectangle representation on sealing and cache load.
Geometry changes, missing/duplicate chunks, orphan shader datasets, map mismatch
and accidental promotion of imported metadata to calibrated/resolved emission
are rejected. IDs are deterministic and map/surface scoped; duplicate append
or collision fails explicitly.

Build and run the dedicated regression with the existing CPU test project:

```powershell
cmake --build build/lightrecon --target lightrecon_shader_lights_test lightrecon_shader_lights_tool
ctest --test-dir build/lightrecon -R lightrecon_shader_lights --output-on-failure
```

The test exercises complete extraction with a fitting source limit of one,
face/mesh/Bezier/moving/sky retention, glow exclusion, native rectangle geometry,
producer integration, deterministic output, existing disk-cache store/reload,
changed shader fingerprint, map mismatch, corruption, missing payload, budgets,
cancellation and unchanged numerical fitting with authored sources retained.

The explicit extraction/replay tool uses the real producer and existing cache:

```powershell
lightrecon_shader_lights_tool original-map.jkobs writable-cache-directory
```

It reads an original unstripped observation archive, stores the shader sources
as a diagnostic canonical asset and reloads every source from `.jklr`. It prints
source/type/rectangle counts and the cache key. Old observation archives already
contain the required surface/material facts and need no format migration.
Blind benchmark archives deliberately strip authored emission and are rejected
by this extraction-only tool.

## Verified stock replay (2026-11-10)

| Map | Sources retained and reloaded | Native rectangles | Patch surfaces |
|---|---:|---:|---:|
| hoth3 | 5 | 5 | 0 |
| taspir2 | 89 | 73 | 0 |
| t2_dpred | 140 | 132 | 0 |
| t3_hevil | 58 | 0 | 46 |
| mp/duel2 | 202 | 192 | 0 |
| Total | 494 | 402 | 46 |

All counts agree with the original BSP/shader inventory for these maps. Cache
store/load and decoding every retained source succeeded. The dedicated test
passed 120 checks; producer, disk, persistent, contract, contributions, API and
build-control regressions also passed. Aggregate receipts and cache keys are in
[native-storage.json](../tools/rend2/stock_lights/results/native-storage.json).
Original geometry, captures and `.jklr` outputs stay in ignored local build data.
