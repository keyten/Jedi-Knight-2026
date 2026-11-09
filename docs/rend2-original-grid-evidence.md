# ASR12: original lightgrid direction evidence

`lightrecon_grid_evidence_tool` extracts original grid observations from existing
`.jkobs` captures. Its tool-only library is `tools/lightrecon_test/original_grid_evidence.*`.
It never substitutes generated P02 probes, fits lamps, or changes the core snapshot,
canonical archive/asset codecs, historical solver, renderer, or world shading.
The summary identifies synthetic fixtures and the archive's declared compiler/profile;
that declaration is not a stock compiler recognizer.

## Source audit (2026-11-09)

The inspected local sources match all seven relevant LF-normalized SHA256 entries
in the existing [source lock](../tools/rend2/bake_model/source-lock.json): original
id q3map `light.c`, and both GtkRadiant and NetRadiant `light.c`, `mathlib.c`, and
`bspfile_rbsp.c`. The original raw-capture implementation in
`shared/rd-rend2/lightrecon/lightgrid.cpp` remains correct for its stated **consumer**
convention and preservation contract. Its `/256` vector is not the exact inverse
of the inspected q3map2 writer. No replacement of that established API is needed.

| Evidence | Inspected behavior and consequence |
| --- | --- |
| [id q3map `TraceGrid`, 1591–1737](https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/q3map/light.c#L1591) | Samples the x-fastest world lattice; forms a norm-RGB-weighted direction over local lights and sun. Splits incoming contributions into directed and ambient, then adds a separate `0.25 * directed` ambient fudge. This eight-byte ancestor is not a JA RBSP writer. |
| [GtkRadiant `TraceGrid`, 1257–1455](https://github.com/id-Software/GtkRadiant/blob/0991a5ce8b8ccb86a5b0d54c089369d84f53dc10/tools/quake3/q3map2/light.c#L1257) | Four styles share one aggregate direction. Style overflow falls back into slot zero. Its non-bounce ambient fudge is `0.125 * directed`. Ambient is not a universally invertible residual. |
| [NetRadiant `TraceGrid`, 1520–1799](https://github.com/xonotic/netradiant/blob/d9338309ea8fab75027b7382efaabe034302b862/tools/quake3/q3map2/light.c#L1520) | Forms one aggregate before style separation. Maps dot products through configurable ambient/directed directionality, clamps negative weights, and adds `0.25*(1-q)*RGB` to ambient. Explicit ambient, min-grid light, grid scale, floodlight, bounce history and color encoding affect the result. The older ambient fudge is disabled. Solid/unclustered samples may be randomly nudged; their actual bake positions are not recorded. |
| [NetRadiant `NormalToLatLong`, 452–481](https://github.com/xonotic/netradiant/blob/d9338309ea8fab75027b7382efaabe034302b862/libs/mathlib/mathlib.c#L452) and locked GtkRadiant helper | Write polar and azimuth using **255**, truncate toward zero, then mask to a byte. North pole is `(0,0)`; south pole is `(128,0)`. Negative azimuth must be unwrapped as a signed integer before `/255` inversion. Treating byte 255 as positive 255 loses the one-bin negative angle. |
| [NetRadiant RBSP `AddLightGridLumps`, 84–199](https://github.com/xonotic/netradiant/blob/d9338309ea8fab75027b7382efaabe034302b862/tools/quake3/q3map2/bspfile_rbsp.c#L84) and locked GtkRadiant writer | Thirty-byte records: 12 ambient RGB bytes, 12 directed, four exact style bytes, two angles. Dense uint16 little-endian indices can repeat. The first approximate record matches each RGB difference `<=4`, angle difference `<=4` **or `>=251`**. The latter admits a **five-bin** circular seam gap. Repeated references are correlated evidence, not independent measurements. |
| Current SP/MP `tr_light.cpp`, shared `tr_bsp.cpp::R_LoadLightGrid` | Consumer uses byte 1 for azimuth, byte 0 for polar, `/256` math (legacy entity path uses a `/1023` sine table). Layout is snapped world-model bounds with authored spacing, no half-cell offset. HDR is a separate headerless six-float tuple per logical cell: ambient RGB, directed RGB. It has no per-style direction/color contract. This establishes Rend2's sidecar interpretation, not a verified historical compiler HDR writer. |

The [forward-model investigation](rend2-bake-model.md) contains the fuller compiler
differences and references. None identifies Raven's stock production compiler.
The five-map earlier inventory and the two-map audit below remain unknown-profile
stock evidence. Source compatibility is not source identity.

## Observations and calibration

Every logical cell retains its ordinal, integer xyz, unchanged BSP world position,
raw source-record index, raw RGB/styles/angles, source issues and optional HDR.
Coordinates follow `ordinal=x+Nx*(y+Ny*z)` and `world=origin+xyz*spacing`.
Record zero is valid. Missing/out-of-range indices remain unavailable, never a
black lighting measurement or a solid-space sentinel. Summaries count reference
reuse and byte-identical extra records separately. JSONL rows report the source
record's total logical reference count.

Both direction interpretations point **from the receiver toward aggregate incoming
light**; propagation has the opposite sign. World Z is up. `consumer256` reproduces
the established ideal consumer math. `writer255_hypothesis` inverts the signed
truncation convention nominally, with the south-pole special case. It is conditional
on the inspected compiler family. `writer255_code_possible=false` identifies codes
that its exact writer cannot produce; the corresponding inverse/bound is not a
supported historical interpretation. Compatibility of other codes does not identify
that compiler. The vector is shared by all styles, not an independent per-style
source direction, emitter position, or individual lamp identity.

Consumer encoding-only uncertainty retains the existing `2*(2*pi/256)+0.02` bound
(about 3.958 degrees, including a conservative legacy-table allowance). The writer
hypothesis uses a whole truncation bin per angle, `2*(2*pi/255)` (2.824 degrees).
`--q3map2-merge4` adds a separate `10*(2*pi/255)` angular merge allowance (14.118
degrees) and four RGB codes per endpoint. These are conservative conditional
bounds, not estimated errors, covariance, or measurement confidence. Default
compression uncertainty is **unknown**, represented by a null merge bound.
Multiple lights, opposing-light cancellation, sun/local mixtures, compiler sample
nudges, bounce and interpolation error cannot be bounded by angle quantization.
`direction_reliability_known` and `compiler_sample_nudge_known` remain false.

Default colors/gradients use encoded `RGB8/255`; these are explicitly encoded
relative values. `--source-relative` requests the archive's explicit decoder
profile. Unknown transfer produces unavailable colors/gradients. Assumed profiles
retain decoder flags; neither mode establishes calibrated linear irradiance or SI
units. Current renderer brightness is never used to infer bake calibration.
Ambient/directed components and style IDs stay separate. A 255 style sentinel
ends active interpretation; raw slots after it remain visible. Duplicate active
style IDs prevent arbitrary gradient matching.

HDR is an independent **unstyled alternative**, indexed by logical cell rather
than source record. It never replaces, adds to, or calibrates LDR. JSONL exposes
its raw tuple and separately decoded source-relative ambient/directed values.
Nonfinite or negative components are unavailable through the existing decoder.
Nonfinite raw JSON numbers are null; exact original float bits stay in the source
archive and the HDR hash covers those bits. HDR measurement bounds and HDR spatial
gradients are not established by this diagnostic.

## Geometry and gradients

The original `GridCell.geometry` contract remains Unknown. A separate diagnostic
location comes from captured `BspRegionDomain` classification, never RGB, style,
record index or direction. Free, solid, unknown, sky, exterior, outside and boundary
facts remain distinct. Missing geometry stays unknown. Grid/domain/geometry source
identities are checked where available. This classification concerns the original
static BSP geometry; dynamic occupancy is separately uncertain.

For each axis, a central finite difference is available only when center and both
immediate lattice neighbors have records and are known free and away from boundaries,
their slightly outward-expanded segment boxes are certified free, both optical
segment queries are clear/geometric without issues, and moving-model occupancy is
supported. No boundary extrapolation, one-sided fallback, smoothing, interpolation,
flood fill or source inference occurs. Unknown travel extents cannot certify any
bake pose; that conservatively withholds gradients throughout that captured scene.
Known swept bounds invalidate overlapping segments. Six immediate-neighbor paths
are the maximum per-cell ray count; per-ray work is bounded.

Style IDs are matched across neighbors even when compressed slots reorder. Ambient
and directed RGB get independent component gradients. Intensity means arithmetic
RGB mean, **not luminance**. Each nominal component is `(right-left)/(2*spacing)`
in selected relative color units per world unit. `code_bounds` use interval
subtraction after monotone decoding of endpoints widened by one code for unknown
rounding, plus four codes for the named merge hypothesis. Saturated or potentially
clipped endpoints have no finite bound. Bounds are on code finite differences,
not on the derivative of a continuous physical field: unknown compression, color
normalization history, interpolation, sub-grid curvature and sample nudges remain
separate uncertainties. A constant compressed neighborhood is not proof of zero
physical gradient.

The grid has no receiving surface normal or BRDF. Its values cannot be used as
material-independent surface illuminance by assigning a nearby wall material.
Compiler bounce, colored filters, ambient floors and directional reduction can
still influence colors. Geometry support only certifies the stated paths/domain;
it does not certify the historical tracing recipe.

## Reproduction

From repository root, with CMake and Python 3 available:

```powershell
cmake -S tools/lightrecon_test -B build/lightrecon-standalone -G "Visual Studio 16 2019" -A x64
cmake --build build/lightrecon-standalone --config Release --parallel 4 --target lightrecon_grid_evidence_tool lightrecon_grid_evidence_test lightrecon_lightgrid_test
ctest --test-dir build/lightrecon-standalone -C Release -R "^(lightrecon_grid_evidence|lightrecon_lightgrid)$" --output-on-failure -V

$tool = Resolve-Path build/lightrecon-standalone/Release/lightrecon_grid_evidence_tool.exe
& $tool original.jkobs report.json --cells cells.jsonl
# Conditional compiler profile only for known controlled fixture captures:
python tools/rend2/grid_evidence/run.py --tool $tool --inputs build/q3map2-runtime-verified/inputs/point_room.jkobs build/q3map2-runtime-verified/inputs/two_colour.jkobs build/q3map2-runtime-verified/inputs/overlap_whites.jkobs build/q3map2-runtime-verified/inputs/sun_sky.jkobs build/q3map2-runtime-verified/inputs/door_occlusion.jkobs build/q3map2-runtime-verified/inputs/wall_leak.jkobs --output build/asr12/compiled --q3map2-merge4 --source-relative --cells
python tools/rend2/grid_evidence/verify_compiled.py --suite build/q3map2-bake-verified --diagnostics build/asr12/compiled --output build/asr12/compiled-checks.json

# Existing private licensed-content captures, default unknown compression/encoded color:
$stock = 'C:/Users/Keyten/AppData/Local/Temp/jk-lightrecon-all-stock-20261031/runs'
python tools/rend2/grid_evidence/run.py --tool $tool --inputs "$stock/hoth2/all-pages.jkobs" "$stock/hoth3/all-pages.jkobs" --output build/asr12/stock-final
```

The local session used CMake/GCC from `../OpenJK/build/toolchain/w64devkit/bin`
and `py -3` because CMake was not on PATH. Captures and known compiler fixtures
are prerequisites; [the existing capture recipe](rend2-q3map2-validation.md)
recreates those private artifacts. This task reuses verified bakes and does not
claim a fresh compiler invocation, runtime session, GPU check, or all-stock census.
Reports and optional JSONL are deterministic; `run.py` runs each input twice and
compares SHA256, plus count and no-probe/no-absolute-calibration invariants. JSONL
is interpreted with its paired report/profile. Large stock JSONL exports are opt-in.
Only compact diagnostic results are versioned; stock assets and raw rows are private.

## Validation results

[Versioned results](../tools/rend2/grid_evidence/results/validation.json) record
input/tool hashes, repeat identity, reports and independent compiled checks.
Six focused MSVC CPU test executables pass (decode, raw lightgrid, ASR12, visibility,
regions, offline), as do 15 Python contract checks. New C++ sources also pass GCC
C++11 `-Wall -Wextra -Werror -pedantic` syntax checks. The ASR12 executable has
32,584 checks; the unchanged raw-grid executable has 459,733 checks, including all
65,536 angle pairs against the source-extracted SP/MP consumer oracle.

Controlled tests cover axes/signs/poles, signed wraparound and impossible writer
codes, weighted multi-light and sun/local mixtures, opposing-light cancellation,
weak directed signal, anisotropic RGB/intensity gradients, slot-reordered styles,
compression bounds, censoring, occlusion, unknown spans/domains/optics, solid and
boundary cells, dynamic uncertainty, missing/invalid indices, duplicate records,
style gaps/duplicate IDs, independent HDR indexing and invalid HDR components.
The sampled writer/consumer angular maxima are 0.0337612/0.0446190 radians.

Six existing **actual q3map2** baked captures contain 630 original logical cells;
all grid/index bytes, world layouts and extracted raw slots match their source
BSPs. Repeated reports and JSONL are byte-identical. Independent checks verify 134
ambient/directed gradient axes. Forty-eight known single-point-light directions
have maximum writer-hypothesis error 0.0299847 radians. Evaluator-only source
answers never enter diagnostic inputs. Supported directed gradient axes are
14 each for point/two-color/overlapping-white/sun-sky scenes, 7 for door occlusion,
and 4 for the partition-wall scene.

| Stock map | Logical cells | Source records | Free | Solid | Unknown | Sky |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| hoth2 | 1,044,126 | 4,001 | 590,876 | 255,483 | 177,556 | 20,211 |
| hoth3 | 229,068 | 11,553 | 138,519 | 2,501 | 88,048 | 0 |

The record column is the compressed source-record count, not the number of
records with multiple references (1,281 and 1,678 respectively). There are no
invalid/missing indices, style gaps or all-255 style sets in these two captures.
All active styles are zero. Reference reuse is 1,040,125 and 217,515 cells.
Directed signal is weak at the stated max-channel `<=2` code threshold for
708,090 and 215,156 style observations. Both stock scenes contain moving models
with unknown travel extents, so their supported gradient count is **zero** under
this conservative policy. Their raw observations remain available. Both repeat
reports are byte-identical. All their raw angle codes are compatible with the
inspected writer's range; that alone does not identify the compiler.

These are evidence diagnostics for aesthetic source discovery. They neither
certify exact historical lamps nor establish reconstruction quality.
