# Initial baked-emitter evidence (CPU, version 1)

This is a source of hypotheses for a future solver. It does not recover physical
lights, fit lightmap/grid brightness, publish reconstructed emitters, or submit
lights to the renderer. `EmitterEvidenceSet` is separate from
`ReconstructedLightingAsset`. Existing LTC drawing, authored-light parsing,
calibration and the experimental `vrAreaSource` attribution remain independent.

## Public boundary and ownership

`shared/rd-rend2/lightrecon/evidence.h` defines:

- `EmitterEvidenceInput`: immutable original `SurfaceGeometry` lease, BSP entity
  text, selected shader bodies, optional original area JSON, and an injected
  source-image `EmissionMaskProvider`.
- `ExtractEmitterEvidence(input, options, context, output)`: explicit synchronous
  extraction. Success includes a legitimately empty set. Failure clears output:
  `InvalidInput`, `MemoryLimit`, `WorkLimit`, `Cancelled`, or provider error.
  A missing image returns `Unavailable` from the provider and preserves an
  uncertain whole-surface candidate; other provider failures abort publication.
- `EmitterEvidenceSet::Map/Evidence/Candidates/Report`: const references with no
  work, file access, GL, cvar access, or mutation. Hold the shared lease while
  using references. Published sets own all evidence strings and geometry hints;
  they retain no world, shader, image, input-buffer or mask-provider pointer.
- `InputDataset::emitterEvidence` / `InputReader::EmitterEvidence()`: optional
  initial hypotheses for standalone sessions, never canonical output lights.
- `R_EmitterEvidence()`: atomic renderer publication getter. Old leases survive
  unloading safely. `r_lightReconEvidence [list]` reports only existing data;
  it does not scan or load files. `list` prints source IDs, origins and issues.

The adapter runs only in `RendererWorldSource::Prepare` after original geometry
and exact parsed material facts have been frozen. Policies 2/3 may prepare it;
policy 0, cache-only misses and valid-cache hits do not run it. No master-use,
consumer, LTC, Forward+, PBR or fog cvar is consulted to create evidence. Reload
and failure cleanup clear publication with the map observations. No worker,
background cache, GPU allocation or renderer asset publication is introduced.

## Evidence, candidates and IDs

Every record has an origin, source path/material/texture identity, original BSP
surface/entity ordinals, optional raw source record, shape alternatives, separate
known-value flags, geometry, colour, optional intensity/unit, style and named
uncertainty flags. A candidate references one or more complete evidence records.
Its primary record preserves exact authored JSON geometry and intensity; matching
surface records are not allowed to overwrite them. Raw JSON also retains unknown
extension fields, defaults omitted by the author, original colour/axis parameters,
mode, range and endpoints. An omitted intensity stays unknown rather than taking
LTC's presentation default of 1. `automatic` entries are labelled separately.

IDs are nonzero 64-bit prefixes of SHA-256 over the original BSP content digest,
evidence schema version and deterministic source key: JSON path + array ordinal,
entity ordinal, selected shader + declaration ordinal, or original surface/group/
mask-blob ordinal. Collisions reject publication. No addresses, runtime light-list
positions, atlas coordinates or `PROXY_MAX` enter IDs or ordering. Evidence is
sorted by ID; primary/alias source IDs preserve the origins of a merged candidate.
Identical input, options and floating-point implementation produce identical IDs
and ordering. Changed BSP content or schema gives a new lineage; shader/mask
edits that split/reorder blobs can change their source keys. These IDs are not a
cross-map or cross-rebuild matching algorithm; consumers must reacquire the set.
No incomplete content fingerprint is claimed cacheable by this provider.

## Source geometry and shared LTC utilities

`emission_geometry.h/.cpp` contains the shared CPU triangle grouping, UV mask
sampling, area weighting, connected-mask blobs and planar principal-axis rectangle
fit previously embedded in `tr_arealights.cpp`. LTC adapts its existing surfaces,
GPU mip masks, thresholds, filters and colours to these helpers; its 0.25-unit
presentation offset and all acceptance budgets are unchanged. The new provider
uses original BSP geometry/UVs and CPU source-image masks, independently of LTC.
The legacy collector is not called by the new provider, so line/tube hypotheses
and small fixtures are not lost through its rectangle-only gate.

For new evidence, grouping requires exactly shared source vertices on the same
plane/material/motion class. The legacy adapter explicitly retains its existing
quantized grouping. Disconnected lamps with identical colour/material remain
separate. Within a group, at most 96 x 96 UV samples use triangle barycentrics,
nearest mask samples and 8-neighbour lit blobs. Each UV sample receives its
triangle world-area weight; discrete coverage is normalized to geometry area.
The relative luminance threshold is 0.1. Threshold, sample count, point extent
(default 4 units) and tube aspect (6:1) are configurable extraction options.
These are shape heuristics, not calibrated emitter detection probabilities.

Small shapes retain point and rectangle alternatives. Elongated panels retain
rectangle and tube alternatives, with a provisional radius from the narrow
extent. Irregular/large shapes additionally retain a surface alternative.
Point/spot geometry from retained `light`/`light_spot` entities is separate.
A dark mask never deletes the shader hint: it retains an uncertain whole-surface candidate labelled `NoLuminousMaskSamples`, including genuinely black colour without a fabricated white value.
Patches and non-polygonal source surfaces are retained as unsupported evidence
with no fabricated position. Missing masks/unsupported animated texture
coordinates preserve uncertain whole-surface geometry. Multiple emission layers,
alpha/cutout/translucent coverage, animation/deformation, moving brush models and
unknown bake/colour transfer remain explicit issues. Such hints are not proof of
static baked emission; `staticEligible` only expresses known static restrictions.

The source-image adapter decodes through the existing engine CPU loader, box
averages to at most 128 x 128, and assumes sRGB for LDR texture RGB. This is labelled
relative colour evidence; it is not the rendering mip/readback and never used as
nits. Per-extraction mask cache is at most 16 MiB and is discarded after extraction;
one source decode buffer is freed through `Z_Free`. Engine image decoders own
their original full-image scratch allocation/format validation, outside the core
budget. Procedural images and unavailable source images remain explicit missing
masks. Source alpha/complex stage composition is not an optical model.

## Authored hints, sun/sky and retained entities

The provider parses original `maps/<map>.arealights.json` independently of the
LTC list. Rectangles retain raw `center/right/up/halfWidth/halfHeight`; lines retain
raw endpoints in the JSON plus centre/axis/length/radius. `luminanceNits` (and the
early `luminance` spelling) is explicitly authored cd/m2. Legacy `intensity` is
relative shader scale. No physical-PBR presentation, range clamping or legacy
reference scale is baked into evidence. Authored dynamic mode remains ineligible
for a static prior. Exact raw records are owned by the publication.

Cross-references `surface`/`entity` may be supplied by authored JSON. Evidence
without explicit references can match only a rectangle with equivalent complete
geometry/axes, within 0.001 units in its plane and the existing 0.25-unit normal
offset. Surface-light and glow hints for the same support/blob share a candidate.
Colour/proximity alone never merges lamps. Several matching support blobs make
an authored link ambiguous; the authored prior remains separate rather than
collapsing the fixtures. Candidate aliases retain every evidence ID. Two distinct authored JSON records are never collapsed through a shared surface; each keeps its own primary geometry/intensity. Any linked moving/animated evidence conservatively prevents static eligibility.

The read-only shader-text adapter uses the selected definition already held by
Rend2. It never registers a shader or reads `tr.sunDirection`, `tr.sunLight`, the
sun calibration or their fallback values. `sun`, `q3map_sun`, `q3map_sunExt`, and
`q3gl2_sun` retain raw RGB, intensity, angles and the full declaration (including
extension parameters). `q3map_skylight` and `skyparms` produce distinct diffuse sky/
environment hints. A sky declaration supplies no invented directional sun. The
provider does not interpret extension sampling/deviance as measured uncertainty.

Retained `light` entities preserve original ordinal and full key/value record,
`origin`, `_color`/`color`, `light`/`_light`, `style`, `target`, `radius`, and cone
hints. A uniquely resolved target supplies a potential spot propagation vector;
missing, degenerate or ambiguous targets remain uncertain. Styles other than zero
are labelled animated/unknown-static. `parent`/`model` makes static eligibility
false. Entity radius is `sourceRadiusHint` (an uncalibrated bake/attenuation hint),
not a certified physical emitting radius. Stripped BSP lights are normal: zero
entity evidence is a valid result.

## Coordinates and photometry

All positions, lengths and area use original world units and world units squared.
Fitted right/up are unit vectors; cross(right,up) is the surface outward emission
normal. JSON keeps exact author axes and exposes a separately normalized outward
normal. Surface evidence stays on its actual plane, without the LTC offset.
For targets, `outward` points from fixture toward the target (propagation).
For sun, `toSource` is receiver -> sun, and `outward` is its negative. Shader angles
use the renderer convention `(cos az cos el, sin az cos el, sin el)` in degrees.

Nonnegative finite colour is retained unnormalized. Optional chromaticity has
Rec.709 Y=1 using the physical-PBR contract; black has no known chromaticity.
Geometry coverage is an area ratio, **not physical-emitter confidence**. Unknown
position/orientation/colour/intensity have separate flags; zero is never an
implicit resolved measurement. NaN/Inf, invalid dimensions, invalid source numbers
and degenerate authored geometry reject publication.

Only explicitly authored luminance is nits. Sun/tool/retained-light numeric values
are `LegacyBakeTool`; glow masks and legacy JSON amplitudes remain uncalibrated.
The future candela/lux enum values do not certify any current legacy input.
Neither `r_pbrWorldUnitsPerMeter`, `r_pbrPhysicalToScene`, automatic emissive nits
nor normalized texture colour reveal historical bake intensity. There is no
lightmap division, grid attribution, sun fitting or renderer-light submission.

## Bounds and limitations

Default core limits: 64 MiB publication/work budget, 500,000 triangles (also
limited by the triangle byte budget), 100,000 evidence records, 8 MiB text (also
limited by budget/32), budget/1024 tokens per document, 16 JSON nesting levels,
50 million mask/triangle and deduplication comparisons. Allocation failure and
work-limit failure publish no partial set. `Report.bytes` estimates owned record,
string and reference payloads, not allocator RSS; input geometry and engine source
decode scratch are separately owned. All scratch and source providers die at the
end of explicit extraction. No per-frame copies/scans are necessary.

False positives include decorative indicators, monitors, additive reflections,
fires/animated glow, unrecognized stage composition and several connected lamps
without a dark mask gap. Geometry fits do not reveal whether a fixture contributed
to the original bake. No confidence score is promoted to physical certainty.
Patch emitter fitting, alpha emission optics, all-layer texture composition,
reliable scene-wide fixture identity matching and inverse-light optimization
remain future work. The source shader body is the selected renderer definition,
not a claim that historical bake-time scripts are available.

## Reproducible validation

From the repository root (CMake and a C++11 compiler on PATH):

```powershell
cmake -S tools/lightrecon_test -B build/lightrecon-disk-standalone
cmake --build build/lightrecon-disk-standalone --parallel 4
ctest --test-dir build/lightrecon-disk-standalone --output-on-failure -V
cmake --build build/lightrecon-core-renderers --config Release --parallel 4 --target rdsp-rend2_x86_64 rd-rend2_x86_64 lightrecon_evidence_test
ctest --test-dir build/lightrecon-core-renderers -C Release --output-on-failure -V
python tools/rend2/test_lightrecon_contract.py
python tools/rend2/lightrecon_regression.py --no-gpu --output build/evidence-shader-contract
```

`lightrecon_evidence_test` covers panels, rotated rectangles, two neighbouring
lamps, two luminous texture blobs, long tubes, small fixtures, moving/animated/
unsupported geometry, exact JSON priorities and ambiguous links, explicitly
relative/physical metadata, targets/styles, all four sun spellings, separate sky,
missing sources, invalid data, limits, cancellation and repeated lease lifetimes.
The production dependency fence allows only the named shared geometry helpers in
LTC, not asset consumption. Its mutation test rejects other canonical/GPU types.

For actual SP stock-map diagnostics (licensed installation required):

```powershell
python tools/rend2/test_lightrecon_evidence_runtime.py --installation "C:/Program Files (x86)/Steam/steamapps/common/Jedi Academy/GameData" --binaries build/ssr-msvc/RelWithDebInfo --renderer build/lightrecon-core-renderers/Release/rdsp-rend2_x86_64.dll --baseline-renderer build/evidence-baseline-renderers/rdsp-rend2_x86_64.dll --output build/lightrecon-evidence-runtime/accepted
```

The baseline DLL is a retained local build of commit `7c8122b96` before this
refactor; it is not committed. Repeat with `--ltc 1` and a fresh output directory
to exercise the existing automatic LTC/Forward+ path. The script reuses the
existing isolated SP launcher and image comparator; stock PK3 files are read-only
and remain under ignored `build/`. It compares generated LTC candidate JSON
byte-for-byte, reports CPU evidence counts, records actual camera transforms,
and marks visual comparisons **unverified** on camera mismatch or unstable
controls. Legacy image tolerance is exact; optional LTC tolerance is one display
code step on <=512/518400 pixels, mean <=0.0005. No GPU matrix compilation is
implied by `--no-gpu`.

Final local results and stock counts are recorded in
`tools/rend2/lightrecon_baseline/evidence-validation.json`. No proprietary map,
texture, renderer binary or large screenshot is versioned. All experiments,
including unsuccessful camera/control runs, remain under ignored build output.

## Local results (2026-10-24)

Both SP/MP Rend2 Release targets compile with MSVC 19.27. GCC 16.2 and MSVC
standalone suites each pass **14/14 executables, 647,564 assertions**, including
2,546 new emitter-evidence checks. The new core translation units also compile
under `g++ -std=c++11 -Wall -Wextra -Werror -pedantic`. There are no source warnings;
the renderer build retains its existing x64 `/arch:SSE2` ignored-option warning.
All 15 production/mutation tests pass; 27 controlled shader configurations /
51 preprocessed stages have zero differences. GPU matrix compilation was not run.
Actual SP loaded both stock maps successfully, with master use off, every consumer
request on, and with existing LTC/Forward+ independently off and on.

| Stock map | Candidates | Evidence | Point / rectangle / tube alternatives | Sun / sky hints | Unsupported | Owned payload |
|---|---:|---:|---|---|---:|---:|
| hoth3 | 612 | 661 | 95 / 593 / 39 | 0 / 0 | 19 | 374,486 B |
| hoth2 | 553 | 553 | 220 / 508 / 4 | 1 / 1 | 43 | 316,816 B |

Classes overlap, and these are **not recovered-light counts**. Dark-mask records
(182 / 38) are uncertain source hints, rather than lights discarded by a rendering
threshold. Animated records are 142 / 62; moving/deformed records are 88 / 62.
Neither map retains original `light` entity records or an authored area JSON in
the isolated stock setup. Fixture tests cover those sources instead. Every source
ID, shape, provenance and issue tuple matches with LTC/Forward+ off and on.

The old/new LTC candidate JSON exports are byte-identical on both maps (229 /
300 legacy export candidates respectively), including geometry, radiance and
review annotations. This is a compatibility result, not a claim that those older
heuristics recover physical emitters. Existing LTC GPU mip masks, fitting offset
and acceptance rules remain part of that legacy feature.

Cross-binary visual equivalence remains **unverified**: map scripts shifted the
actual cameras between processes, and some original-binary frozen controls showed
small changes. The script records and checks actual camera transforms rather
than accepting requested coordinates as evidence. Screenshots, control failures,
logs and exact raw reports remain under `build/lightrecon-evidence-runtime/`.
The final LTC-on retry succeeded after removing redundant private PK3 copies that
had exhausted disk space; the retained private stock files were hard-linked into
the retry directory. Installed archives were never modified.

Touched implementation groups: `lightrecon/evidence.*`, shared
`emission_geometry.*`, core CMake and input accessors; source material-coordinate
facts and observation schema 4; LTC CPU adapters; `tr_lightrecon.*` lifecycle /
source-image adapter / diagnostic; read-only shader-text access in `tr_shader.cpp`;
SP/MP command registrations and declarations; standalone tests, runtime diagnostic
script, controlled dependency-fence extension, documentation and small validation
witness. No GLSL file, legacy colour conversion, lightstyle animation, world
lighting evaluation or runtime-light submission implementation changed.
