# Original lightmap texels to static BSP geometry

`shared/rd-rend2/lightrecon/surfaces.h/.cpp` implements the CPU correspondence
between original logical lightmap images and static world geometry. It belongs
to the shared C++11 `rend2_lightrecon_core`, linked once by SP, MP and standalone
tests. Schema: `org.openjk.baked-surface-observations`, version 1.

This is observation geometry, not reconstructed lighting. It does not estimate
emitters, invert lighting, classify lightstyles/deluxe directions, calibrate
historical brightness, or supply a static visibility solver. The subsequent
material/deluxe/style classification task is separate.

## Capture and lifetime

`LightReconBspCapture::Surfaces` copies source surface, vertex, index, model and
shader identities from the original BSP lease, after original lightmaps are
captured and **before** `ReleaseSource`, `R_LoadSurfaces`, `FatPackU/V`, patch
subdivision, stitching or surface merging. Thus positions/UVs never come from
the GPU atlas, a readback, merged draw surface or camera-dependent mesh.

The adapter checks source spans/alignment/counts, index/vertex ranges and budgets
before allocation. It retains the original `dsurface_t` ordinal, all four logical
lightmap indices, style bytes, chart X/Y and width/height, lightmap origin/basis,
patch dimensions, vertex positions, original UVs and normal fields, and original
relative triangle indices. Material identity is the BSP shader ordinal/name.

The original model surface ranges identify the main static world. Nonzero brush
models, ambiguous ownership and unknown ownership are retained as distinct input
records but excluded from static mapping. At `R_LoadSurfaces`, after each
`ParseFace/ParseMesh/ParseTriSurf`, the adapter copies deformation and exclusion
facts from the already parsed authored shader. This lookup does not use
`r_singleShader`'s drawing override. Deformed geometry, sky, nodraw and missing
default materials are excluded. No shader or geometry is changed by that hook.

`SurfaceCapture` stages owned vectors synchronously and publishes a const
`SurfaceGeometry` snapshot only at the main-world finish boundary. It owns no
`world_t`, hunk pointer, shader/image pointer, GPU allocation or background job.
`R_LuxelGeometry`, `InputDataset.surfaceGeometry` and `InputReader::Geometry`
return strong read-only leases. Map change, failed load, shutdown and
`vid_restart` reset the owner; old pinned snapshots remain valid until released.
All renderer-module leases must be released before DLL unload. MP sub-BSPs are
excluded from the main-world owner.

As with the existing raw image/grid capture, policies 0/1 perform no geometry
capture. Policy 2 captures only without a validated cache hit; policy 3 captures
regardless of a hit. Consumer/master flags do not start mapping or enable debug
drawing. Complete production input fingerprinting and a solver remain unavailable.

## Canonical observations and identity

`MapLuxels` explicitly computes one requested image tile. `LuxelTile` pins both
geometry and raw images, and contains a row-major coverage mask and geometric
`SurfaceLightingObservation` records. `ReadSurfaceObservation` joins a requested
mapped record with raw RGB, decoder candidates, the immutable decoding recipe
and image lease in a `DecodedSurfaceObservation`. This complete observation has:

| Field / source | Meaning |
| --- | --- |
| `geometry->Map()` | Original BSP path, byte count and full source SHA256 |
| `surface`, `styleSlot` | Original BSP surface ordinal and channel; together with map identity these identify the chart |
| `logicalLightmap` | Original logical page, including original interleaved deluxe numbering when present |
| `x`, `y`, `centreUv`, `footprintMin/Max` | Source pixel coordinate, centre and normalized pixel square |
| `positionWorld` | Unoffset world position, present only when `positionKnown` |
| `geometricNormal`, `shadingNormal`, `tangentWorld` | Separate geometry-derived and source-interpolated fields |
| `normalOrientationKnown`, `shadingNormalKnown` | Numerical availability/orientation, independent of confidence |
| `areaWorldSquared`, `coveredFraction` | Effective covered surface area and UV fraction |
| `areaErrorEstimate`, `mappingErrorBoundWorld`, `basisErrorWorld` | Numerical mapping/integration diagnostics |
| `materialIndex`, pinned `BakedSurface::material` | Stable original shader identity, not a runtime shader handle or measured reflectance |
| `colour` | Raw source RGB and encoding, clipping/quantization/transfer/calibration metadata from the canonical decoder |
| `coverage`, `issues`, `confidenceKnown` | Coverage/ambiguity diagnostics; confidence remains unknown rather than an invented probability |

The image vector ordinal `image` is only a lookup into a pinned snapshot. It can
change when an external alternative is added. Stable image identity uses its
logical index, role, encoding, source path, dimensions and source hash; stable
chart identity uses the original map/surface/channel. A source content/layout
change creates a different identity scope. A chart can have disconnected UV
islands; no artificial physical connection is inferred between them.

LDR and external HDR at one logical index are **alternative representations**,
not additive observations. A caller selects one image. The core does not create
an independent energy measurement per triangle, reference or rendering atlas
page. Raw values remain uncalibrated; `DecodeLuxel` and
`ReadSurfaceObservation` use the explicit [photometric decoder](rend2-observation-decoding.md).
Unknown source transfer/scale stays unknown. Geometry mapping never treats
lightmap RGB as lux, divides it by texture colour or changes renderer colours.

## Coordinates, centres and footprints

BSP XYZ is preserved in world units with +Z up. No metre conversion, model
translation, camera origin or atlas offset is applied. World area is in squared
world units; a later consumer can divide by `worldUnitsPerMeter^2` when an
explicit unit policy is supplied.

For a selected original image of width W and height H:

```text
texel centre = ((x + 0.5) / W, (y + 0.5) / H)
texel footprint = [x/W, (x+1)/W] x [y/H, (y+1)/H]
```

Rows use the original CPU decoder's row order, exactly as the unflipped renderer
upload input. There is no extra V flip. Pixel centres are used for point mapping;
the full pixel square is clipped against source UV geometry for coverage/area.
Internal 128x128 pages and external images with different, including rectangular,
dimensions use their own W/H. UVs remain normalized coordinates from their BSP.
External files must conform to that BSP's logical UV contract, as the renderer
also requires. An arbitrary repack without matching UV metadata cannot be
inferred from image colours; no undocumented remapping is guessed.

## Faces and triangle soups

Each source triangle supplies an affine UV-to-world map. A UV determinant below
`1e-14`, world cross-product magnitude below `1e-12`, invalid indices, invalid
positions or invalid active UVs are explicitly diagnosed. Inactive lightmap
slots may contain garbage/NaN in real BSPs: their original values are retained,
but they do not invalidate the active channel. Missing/non-finite shading
normals do not destroy otherwise valid position/geometry information.

Barycentric interpolation at the texel centre gives position. UV derivatives
give geometric normal and tangent. The UV triangle is clipped against the pixel
square, so planar surface area is the clipped UV area times
`|dP/du cross dP/dv|`. This handles irregular UV charts and partially covered
texels without filling a surface's bounding rectangle with invented geometry.

For planar faces, the original basis is also checked against **every** source
vertex. The two documented centre conventions tested are:

```text
P = lightmapOrigin
  + basis[0] * (128*u - chartX - half)
  + basis[1] * (128*v - chartY - half), half in {0, 0.5}
```

128 is the original JA BSP page basis, not the dimensions of an external
replacement. The basis is used only when its maximum vertex residual and its
sample agreement with triangulation are within `positionToleranceWorld`
(default 0.002). Otherwise the triangulated source geometry remains authoritative
and `LuxelBasisMismatch` records the discrepancy. Stock basis fields do not
universally satisfy this interpretation; large rejected residuals are reported,
not used as positions. No ill-conditioned inverse or arbitrary repair is applied.

## Patches

Odd 3x3 quadratic control blocks are read from the original control grid. For
blocks whose control UVs describe an invertible affine parameter map (verified
at all nine controls within `uvTolerance=1e-7`), that UV map is inverted exactly.
The position and derivatives are evaluated on the tensor-product quadratic
Bezier surface, rather than on a flat face or current renderer tessellation:

```text
B(t) = ((1-t)^2, 2*t*(1-t), t^2)
P(s,t) = sum_i,j control[i,j] * B_i(s) * B_j(t)
```

Positions are evaluated relative to a control-point anchor, and derivatives use
first differences of adjacent controls. This avoids cancellation when a patch
is far from the world origin. Collapsed control edges/poles have zero derivatives
and are explicitly ambiguous; floating-point residue is never normalized into
an invented normal. Translated and collapsed-edge fixtures cover this case.

Two UV triangles cover a block's parallelogram for clipping. Their world
evaluation remains the analytic curved surface. Area uses positive degree-5
seven-point Dunavant quadrature, comparing coarse/refined integrals and
subdividing deterministically to a configured depth. `LuxelAreaApproximate` and
the estimated integration delta remain explicit; this is not a certified error
bound for arbitrary pathological surfaces.

**Non-affine, folded or singular patch UV blocks are currently Unsupported.**
Their conservative control-UV bounds mark affected texels `Unsupported`, including
texels that might otherwise receive a competing valid reference. No flat-plane
fallback, uncontrolled Newton root or approximate world position is fabricated.
This limitation is visible in the per-image report and coverage export.
The task's Definition of Done permits explicitly reported unsupported surface
cases; full non-affine UV inversion is future work.

The mapping never calls `R_SubdividePatchToGrid`, uses `r_subdivisions`, stitching,
runtime LOD or a camera. The runtime test uses fine renderer subdivision only
to make the displayed mesh approximate the independently mapped analytic curve.

## Coverage, shared references and normals

`LuxelCoverage` states are:

- `Uncovered`: no eligible footprint (padding or an excluded/unlightmapped
  region); no position. It does not certify the physical absence of geometry.
- `FootprintOnly`: positive covered area, but the centre is outside the geometry;
  area/coverage remain available, no invented centre position or normal.
- `Mapped`: finite, unambiguous centre and geometric normal.
- `Ambiguous`: positive-area overlap, mixed incompatible physical charts, a
  discontinuous normal at a shared centre, degeneracy or insufficient float
  storage precision; no usable position/area weight.
- `Unsupported`: the possible footprint of unsupported geometry/UV; no position.

Canonicalized triangle keys remove duplicate/reversed references before area
accumulation. Shared edges contribute zero overlap area, while any positive
overlap beyond `1e-7` of a texel's UV area is ambiguous even when the total covered
fraction is below one. Each texel is returned once. Adjacent coplanar chart
references may share that single observation; incompatible local maps/normals
are rejected with `LuxelMixedFootprint`/`LuxelNormalSeam`. No area is counted twice.
Edge incidence within each source chart identifies borders, including full
pixels along chart boundaries. Internal triangulation edges are not chart borders.

Geometric normals come from world-space UV derivatives. Their sign is aligned
with finite source shading normals, or the source planar normal when shading
is missing. The interpolated shading field is stored separately: triangle
barycentric or patch Bezier interpolation of original BSP normal attributes,
normalized when available. For patches this is a **source shading candidate**,
not a claim to reproduce normals regenerated by every renderer LOD.
`LuxelWindingReversed` records a UV-orientation sign correction.
`LuxelOrientationUnknown` prevents assuming an outward hemisphere when no
orientation evidence exists. Unknown/invalid transformed fields are not NaN
lighting values; raw source fields remain inspectable in the geometry snapshot.

`LuxelRayOrigin` offsets a **copy**, default 0.05 world units, capped at
`0.01 * sqrt(areaWorldSquared)`. It returns `Unavailable` without a known
position/orientation and rejects invalid offsets/geometry. It never modifies
the observation. The debug renderer uses a separate 0.12-unit drawing separation
and six-unit normal lines solely to avoid marker z-fighting, not to fix mapping.
Numerical validation uses the unoffset observations.

## Explicit computation, bounds and debug tools

There is no permanent per-map luxel matrix. `MapLuxels` allocates only its requested
tile and bounded scratch. Default limits are 65,536 output texels, 262,144 triangles,
1,048,576 clipped pieces, 128 MiB conservatively accounted scratch and 33,554,432
work units. `ReadContext.maxBytes` separately bounds output records and supports
injected cancellation. Errors clear the result; no partial tile is published.
Scratch accounting uses 64 bytes per tile location, 2,048 per assembled triangle
and 1,024 per clipped piece, deliberately exceeding the small implementation
records. These are algorithm budgets, not measurements of process RSS.

Raw geometry capture defaults to 64 MiB accounted records/payload, 1,048,576
vertices, 4,194,304 indices, 131,072 surfaces and 4,096 bytes per material/path.
Allocator overhead/vector spare capacity, existing renderer memory and separately
owned image/grid snapshots are not included in the diagnostic byte estimate.
No hunk/STL allocator ownership is mixed. Temporary BSP reread scratch is still
released before surface parsing; mapping scratch is released when its call returns.

`r_lightReconInfo` adds `luxel-geometry=...` with counts, exclusions and accounted
memory. The SP/MP console command is explicitly opt-in:

```text
r_lightReconLuxels list
r_lightReconLuxels logical 4 2
r_lightReconLuxels logical 16 1 319
r_lightReconLuxels <image-vector-ordinal> <stride 1..64> [surface-ordinal]
r_lightReconLuxels off
```

`logical` selects the first nonmissing lighting representation of that original
logical page; use the image ordinal to choose another representation explicitly.
Mapping is independent of the camera; debug selection alone keeps up to 512
nearby markers. Green indicates faces/soups, cyan patches, orange chart borders
and red lines geometric normals. Markers closer than 32 or farther than 2,048
world units from the view are culled for readable rendering.

The same command writes `lightrecon/luxel-image-N.obj` (world points/normal lines,
with comments for invalid/uncovered records) and `.ppm` (the coverage mask) through
the engine's per-user writable filesystem. Mask colours: green mapped, dark
no eligible coverage, blue footprint-only, orange border, magenta ambiguous,
red unsupported.
No world positions are fabricated to draw padding or invalid regions.

`R_LuxelDebugDraw` returns immediately with no explicit markers: no shader
registration, source queries, draw submission, lighting, shadow or GPU resources
are activated by normal/default rendering. Explicit debug use reuses the existing
white-image vertex-colour polygon path with **depth testing enabled**; markers
behind world geometry are occluded. It introduces no lighting shader defines
or samplers. The regression detector also rejects a depth-disabled 2D debug shader.

## Reproducible tests and acceptance

Run from the repository root with CMake/Python on PATH:

```powershell
cmake -S tools/lightrecon_test -B build/lightrecon-standalone -G "Visual Studio 16 2019" -A x64
cmake --build build/lightrecon-standalone --config Release --parallel 4
ctest --test-dir build/lightrecon-standalone -C Release --output-on-failure -V

# Existing configured renderer build used locally:
cmake --build build/lightrecon-core-renderers --config Release --parallel 4 --target rdsp-rend2_x86_64 rd-rend2_x86_64 lightrecon_test lightrecon_api_test lightrecon_core_test lightrecon_control_test lightrecon_disk_test lightrecon_lifecycle_test lightrecon_lightmaps_test lightrecon_decode_test lightrecon_lightgrid_test lightrecon_surfaces_test
ctest --test-dir build/lightrecon-core-renderers -C Release --output-on-failure -V

cmake -S tools/lightrecon_test -B build/lightrecon-disk-standalone -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build/lightrecon-disk-standalone --parallel 4
ctest --test-dir build/lightrecon-disk-standalone --output-on-failure -V
g++ -std=c++11 -Wall -Wextra -Wpedantic -Wconversion -fsyntax-only -Ishared/rd-rend2 shared/rd-rend2/lightrecon/surfaces.cpp tools/lightrecon_test/lightrecon_surfaces_test.cpp

python tools/rend2/test_lightrecon_contract.py
python tools/rend2/lightrecon_regression.py --no-gpu --output build/lightrecon-luxels-shader-contract

python tools/rend2/test_lightrecon_luxels_runtime.py --installation "C:\Program Files (x86)\Steam\steamapps\common\Jedi Academy\GameData" --binaries build/ssr-msvc/RelWithDebInfo --renderer build/lightrecon-core-renderers/Release/rdsp-rend2_x86_64.dll --label final-internal
python tools/rend2/test_lightrecon_luxels_runtime.py --installation "C:\Program Files (x86)\Steam\steamapps\common\Jedi Academy\GameData" --binaries build/ssr-msvc/RelWithDebInfo --renderer build/lightrecon-core-renderers/Release/rdsp-rend2_x86_64.dll --label final-external --external-layout

# Compare stable logical-page exports rather than image-vector ordinals:
python -c "import json; from pathlib import Path; p=Path('build/lightrecon-visual-20261022'); a=json.loads((p/'luxels-final-internal/result.json').read_text()); b=json.loads((p/'luxels-final-external/result.json').read_text()); assert a['passed'] and b['passed']; assert len(a['numerical']['exports'])==38; assert a['numerical']['exports']==b['numerical']['exports']; print('38 identical geometry exports and coverage masks')"
```

Use fresh runtime labels. The runtime harness requires an installed game, SP
executable/game DLL/compiler helper and SDL. It isolates configs and a private
HDR fixture; installed PK3/maps are read-only inputs. No game assets or geometry
exports are committed. The external run replaces one lightmap privately to
exercise the renderer's external layout instead of its internal atlas; the
original BSP observations and geometric correspondence are compared unchanged.

The numerical executable tests planar/rotated quads, both half-texel basis
conventions, arbitrary UV triangles, area conservation, shared chart seams,
duplicate/reversed triangles, partial/full overlaps, sharp normal seams, curved
quadratic patches with closed-form positions/area, collapsed poles and world
translation invariance, explicit unsupported UVs,
different external dimensions, missing/vertex-lit/moving/deformed geometry,
NaN/Inf handling, budgets/cancellation, stable repeated output and lease/reset
lifetime. It links no engine, GL or window library. The runtime additionally
checks every exported mapped point against an independent NumPy solve of the
original BSP and a sparse set of normals against analytic source derivatives.

The accepted runtime records requested **and actual** camera poses, fixed exposure,
disabled temporal effects, fine curve tessellation, paused FX/game and hidden
entities. The actual camera can differ from a requested player teleport because
of SP camera/collision rules. It must be identical in each base/repeat pair.
Each view has base, overlay and repeat screenshots. Base/repeat must match
exactly, and the overlay must visibly change the frame. This protects against
accepting empty or invisible marker screenshots. A fixed set of source images
is scanned, so camera visibility cannot change the mapping statistics.

Synthetic maximum position error is `9.53674316e-7` world units; the curved
fixture's integrated area differs from its closed form by `6.25277607e-13`
squared world units. Actual-map maxima, counts, screenshot hashes, atlas
comparison and build/test totals are recorded in
`tools/rend2/lightrecon_baseline/luxel-validation.json` and described below.

## Recorded results (23 October 2026)

MSVC 2019 x64 built both `rdsp-rend2_x86_64` and `rd-rend2_x86_64`, plus
all ten CPU test executables. Both MSVC and independent GCC 16.2 C++11 runs
passed **10/10 executables, 536,954 assertions** each; the new surface test
accounts for 5,441 assertions. The strict `-Wall -Wextra -Wpedantic -Wconversion`
syntax check passed. The renderer build emits its existing x64 `/arch:SSE2`
option warning. The dependency/disabled-path detector passed 13/13 tests, and
the controlled preprocessed GLSL baseline passed 27 programs / 51 stages with
zero differences (`--no-gpu`).

An actual SP executable loaded stock `hoth3` twice on Intel UHD Graphics:
internal atlas and a private external-HDR layout. Original capture retained
12,462 faces, 365 patches and 30 triangle soups, with 83,772 source vertices
and 159,663 indices. Its accounted geometry payload is 13,017,960 bytes;
129 surfaces are excluded by motion/material facts, and no captured surface
failed geometry validation. All 866 surfaces with a negative primary lightmap
index use `LIGHTMAP_BY_VERTEX` in this BSP, including all 30 soups. Soup luxel
mapping is therefore covered by synthetic tests rather than stock-map samples.

The complete scan covers 38 original 128x128 lighting pages (622,592 texels):

| Coverage | Texels |
| --- | ---: |
| Mapped, finite and unambiguous | 390,285 |
| No eligible footprint (padding or excluded/unlightmapped region) | 164,788 |
| Footprint only, centre outside geometry | 27,310 |
| Ambiguous/degenerate | 15,680 |
| Unsupported UV footprint | 24,529 |

The nonexclusive chart-border count is 164,898. The scan handles 760 affine-UV
patch blocks and explicitly rejects 168 unsupported UV blocks. It removes
12 duplicate triangle references and reports nine degenerate source triangles.
Conservative unsupported control-UV bounds can exclude more texels than the
actual unknown footprint; these counts are coverage diagnostics, not a claim
that all omitted texels lack geometry.

Independent source-BSP evaluation checks every mapped position: 360,296 face
samples and 29,989 patch samples. Maximum observed position error is
**0.0005295006 world units**, below the 0.0021 debug-export acceptance limit
(the core float-storage tolerance is 0.002). Sparse analytic normal checks cover
12,196 samples, with a maximum component error of `4.00016e-5` and maximum
angular error of `0.001945` degrees. Endpoint subtraction from nine-digit OBJ
coordinates contributes rounding to those normal errors.
Large `basis-max` diagnostics describe rejected source-basis interpretations,
not these position errors. A collapsed patch pole discovered by this check is
now explicitly degenerate and has its own CPU regression fixture.

All **38 OBJ exports and 38 coverage masks are byte-identical** across the two
renderer atlas layouts. Logical indices, world geometry, areas, issue flags
and selected original source samples therefore stay independent of atlas packing.
The private HDR override is an alternative image, not another energy observation.

The internal-layout planar camera is recorded at `(3402, 651, 117)`, angles
`(-357, 55, 0)`; the patch camera at `(3534, 740, 113)`, angles `(-359, 171, 0)`.
The external-layout poses are `(3399, 652, 118)` / `(-357, 53, 0)` and
`(3532, 737, 113)` / `(-359, 169, 0)`. All use FOV 80.
These are the actual SP poses, not the requested player teleport positions.
At 960x540, base and disabled-repeat frames match exactly in both views and
layouts. Depth-tested markers sit on the corresponding faces and curved pipe;
normal lines follow the pipe curvature, with occlusion by nearer geometry.
The source chart coverage masks expose borders and unsupported/ambiguous areas.
Screenshot comparisons use matched base/repeat pairs within each run; the
cross-layout comparison uses camera-independent CPU exports. The SP camera
controller's run-to-run pose differences are recorded rather than hidden.

Local screenshots/exports are ignored build artifacts; they require licensed
game assets to regenerate. The versioned JSON stores their hashes, settings,
cameras, numerical maxima, source hashes and commands without including maps:

- [Face overlay](../build/lightrecon-visual-20261022/luxels-final-internal/face-overlay.png)
- [Curved patch overlay](../build/lightrecon-visual-20261022/luxels-final-internal/patch-overlay.png)
- [Face chart coverage](../build/lightrecon-visual-20261022/luxels-final-internal/coverage-logical-4.png)
- [Patch chart coverage](../build/lightrecon-visual-20261022/luxels-final-internal/coverage-logical-16.png)
- [Versioned validation witness](../tools/rend2/lightrecon_baseline/luxel-validation.json)

Changed entry points are the original-BSP capture before `ReleaseSource`,
post-parse material facts in `R_LoadSurfaces`, main-world capture finish/reset,
SP/MP debug-command registration and the opt-in debug call in `RE_BeginScene`.
The shared core CMake target, standalone tests, regression detector and runtime
harness accompany them. No atlas construction, `ParseFace/ParseMesh/ParseTriSurf`
geometry, patch subdivision, shader lighting or texture upload math is changed.

| Changed files | Responsibility |
| --- | --- |
| `shared/rd-rend2/lightrecon/surfaces.h/.cpp`, `input.h`, core `CMakeLists.txt` | Canonical snapshots, bounded mapping/colour join/export and shared CPU linkage |
| `shared/rd-rend2/tr_lightrecon.h/.cpp`, `tr_bsp.cpp` | Original BSP capture, material/motion facts, world lifetime, diagnostics and export command |
| `shared/rd-rend2/tr_local.h`, `tr_scene.cpp`, SP/MP `tr_init.cpp` | Opt-in marker drawing and command registration |
| `tools/lightrecon_test/lightrecon_surfaces_test.cpp`, test CMake/README | Dependency-free numerical/lifetime fixtures |
| `tools/rend2/test_lightrecon_luxels_runtime.py` | Real SP captures, independent source position/normal validation |
| `tools/rend2/lightrecon_regression.py`, `test_lightrecon_contract.py` | Disabled-path dependency fence and debug-depth mutation checks |
| `tools/rend2/lightrecon_baseline/luxel-validation.json`, baseline README | Small versioned validation witness; no game assets |
| This document and linked core/lightmap/lifecycle documents | Contracts, commands, ownership and requirement audit |

## Requirement audit and scope

| Prompt requirement | Implementation / evidence |
| --- | --- |
| 1: canonical identity, geometry, RGB, coverage/quality | `DecodedSurfaceObservation`, pinned raw image/geometry/recipe, explicit flags and area |
| 2–4: original UV, internal/external layouts, centres | Pre-atlas capture, normalized per-image mapping, square clipping, external-size fixtures and actual atlas comparison |
| 5: planar basis and triangle validation | All-vertex residual check; triangulated geometry remains authoritative on mismatch |
| 6: actual curved patches | Analytic quadratic Bezier geometry; non-affine/folded/singular UV explicitly Unsupported |
| 7: soup UV degeneracy and overlap | Source triangles, determinant/world-area checks, positive-area overlap detection |
| 8: padding and chart borders | Coverage enum/mask, footprint-only state, per-chart edge incidence |
| 9–10: shared references and area weighting | Duplicate removal, one pixel record, clipped planar area / curved quadrature, conservative mixed-footprint exclusion |
| 11: static/moving/animated distinction | Original model ownership and parsed deformation exclusions |
| 12–13: separate normals and offset policy | Geometry derivatives vs source shading candidate; explicit orientation and nonmutating bounded ray offset |
| 14: missing/vertex-lit/malformed | Explicit unavailable/excluded/unsupported results; no fake dense positions |
| 15: deterministic/camera/LOD/atlas independent | Pure original-input core, ordered loops, repeat tests and cross-layout exports |
| 16: spatial and invalid-region visualization | Explicit native world markers/normals, OBJ export and coloured coverage mask |
| CPU tests, SP/MP and visual runtime validation | Shared standalone tests, both DLL builds, actual SP screenshots and independent source-ground-truth checks |

Remaining cases are explicit: full non-affine patch UV inversion, arbitrary
external repacks without matching UV metadata, reconstruction of shader-deformed
or moving geometry, exact bake-tool footprint/filter history, full material
reflectance/classification, deluxe interpretation, complete cache fingerprints
and any inverse-lighting solver. Stock triangle soups with no baked images are
retained/excluded; their geometric mapping is tested synthetically. There is no
claim of MP gameplay validation or a new full-matrix GPU shader compilation;
the controlled GLSL comparison uses `--no-gpu`, while the SP runtime uses the
actual driver. Legacy lighting/shader calculations are unchanged.
