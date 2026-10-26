# Baked observation decoding

`lightrecon/decode.h/.cpp` is a C++11 CPU conversion API for original baked
observations. It consumes immutable raw lightmap snapshots or individual RGB8,
RGBA8 and already-decoded linear float RGB samples. It does not change texture
loading, lightgrid loading, shaders, cvars, PBR calibration or runtime lighting.
No fitting, disk access, GPU allocation or OpenJK startup is involved.

## API and ownership

The API is in `Rend2LightRecon::Core::Decode`:

| Entry point | Contract |
| --- | --- |
| `ValidateProfile` | Validate version, enums, finite ranges, provenance and calibration recipe |
| `Decoder(Profile)`, `Validation`, `Metadata` | One immutable owned recipe per stream/interpretation/hypothesis; metadata can outlive the decoder |
| `Decoder::Run(Raw, Observation&)` | Copy the raw sample and produce separately labelled stages/quality; no source mutation |
| `Lightmap` | Read 3/4 original components through a bounded CPU lease and decode one pixel; pin image/map/source identity |
| `ForcedLinear` | Reproduce setup conditions and recorded HDR-discovery state, without reading/changing cvars |
| `Compare` | Identify an explicitly shared relative domain or common calibrated scene reference; otherwise `Incomparable` |
| `CanonicalIssues` | Map channel censoring/quantization/missing/unreliable conditions to canonical aggregate issue flags |

`Observation` contains the original `Raw`, an immutable shared `Profile` lease,
three channel issue masks, overall uncertainty, resolved transfer/primaries and
four `Value` stages. Each stage has explicit state/reason/space, finite nonnegative
RGB when available, optional channel bounds and censoring bits. Zero slots in
an unavailable/invalid stage are **not** black-light observations. `Run` always
replaces its output with raw data and diagnostics, even on failure. A failed
conversion publishes no available transformed stage. Ordinary absence of an
optional stage can coexist with successful decoding of other stages.

`LightmapSample` additionally pins `LightmapObservations`, original image ordinal
and pixel ordinal. Its owner exposes original logical index, role, source path,
pixel hash and source BSP SHA-256. Source pixels and recipes are never copied
as whole datasets during a query. Per-sample outputs are caller-owned values;
there is no permanently resident decoded image or per-pixel metadata table.
Instances with a valid immutable recipe support independent concurrent calls
with separate output objects. `ReadContext` bounds/cancellation apply to image
reads; metadata/query operations never start reconstruction or perform I/O.

## Distinct values and assumptions

| Stage | Meaning |
| --- | --- |
| `raw` | Original RGB8/RGBA8 codes or exact available decoded HDR float RGB, including invalid float bits for diagnostics; alpha retained separately |
| `encoded` | Bytes normalized by 255, or unchanged available HDR representation; no claim of linearity for byte codes |
| `source` | Source-decoded **linear relative RGB candidate**, under a documented transfer or explicit assumption; source primaries/scale may remain unknown |
| `renderer` | The existing loader's transformed texel/component value, with explicit renderer space and lossy-operation flags; independent of source interpretation |
| `scene` | Optional Rec.709/D65 **scene-linear equivalent**, using an explicitly supplied reference/gain/provenance; never a measured physical quantity |

Default LDR transfer is `Unknown`; the normalized codes remain accessible, but
`source` reports `Unavailable/UnknownTransfer`. Neither `r_linearLighting` nor
`r_pbrPhysical` establishes how a historical map was baked. To test a hypothesis:

```cpp
namespace D = Rend2LightRecon::Core::Decode;
D::Profile profile;
profile.identity = "stock-map/srgb-hypothesis/1";
profile.source.assumedTransfer = D::Transfer::SRgb;
profile.source.assumeRec709 = true;
profile.source.assumptionRationale = "Explicit hypothesis; original bake settings absent";
D::Decoder decoder(profile);
D::Raw raw;
raw.bytes[0] = raw.bytes[1] = raw.bytes[2] = 128;
D::Observation decoded;
auto error = decoder.Run(raw, decoded);
// decoded.source ~= (0.2158605, ...), transfer/primaries assumption flags set.
// decoded.absoluteScale remains Unknown; decoded.scene is unavailable.
```

A documented transfer requires provenance, and cannot be combined with an
alternative assumed transfer in the same recipe. Separate recipes can represent
competing hypotheses over the same raw data. Known/assumed primaries are separate
from transfer. Unknown primaries do not prevent a relative source RGB candidate,
but prohibit canonical scene RGB until Rec.709/D65 is established or explicitly
assumed. No other-primary-to-Rec.709 matrix is invented.

Linear float RGB is already source-decoded by representation. Applying an sRGB
or gamma recipe to it is rejected, preventing a second decode. HDR origin
distinguishes Radiance RGBE (existing `.hdr` loader), lightgrid raw float32 and
unknown float provenance. This API does not parse image containers; original
[lightmap capture](rend2-lightmap-observations.md) supplies the decoded HDR/LDR
representations from Rend2's existing loaders, including TGA/PNG/JPEG fallback.

## Source formulas

For an LDR code `b`, normalized value `e = b / 255`. For float RGB, `e` is the
original decoded component. With positive explicit gains `Ge`, `Gl`:

```text
z = Ge * e
Linear(z)       = z
sRGB(z)         = z / 12.92                              if z <= 0.04045
                  ((z + 0.055) / 1.055)^2.4             otherwise
PowerGamma(z)   = z^gamma
source          = transfer(z) * Gl * 2^(-preExposureStops)
```

The exposure factor applies only when source pre-exposure is documented or
explicitly assumed. Unknown source exposure applies no invented correction and
sets `UnknownExposure`. A positive known stop count means the source had already
been multiplied by `2^stops`, which is undone in linear space. Renderer camera
exposure/auto-exposure/compensation are recorded separately and never applied
to these pre-display values.

The sRGB function has an explicit positive extension above one; it does not
silently clip HDR range. Extension is flagged. Source colour is never normalized
to unit chromaticity, divided by its brightest channel, floored to grey or
arbitrarily desaturated. Nonlinear transfer legitimately changes encoded ratios;
linear transfer and scalar gains preserve relative ratios within float precision.

Original bake overbright knowledge/bits are recorded independently of the
current renderer's settings. Bake bits are context, not an instruction to guess
a correction domain. If documentation establishes a scale, the host must choose
`encodedGain` or `linearGain` explicitly. There is no inversion of historical
clipping, colour normalization, gamma-space presentation or quantization.

## Censoring and uncertainty

Channel masks distinguish quantization, raw black, near-zero, exact maximum
code, near-saturation, missing, negative HDR and non-finite HDR. Defaults classify
codes 0/1 as near-zero and 254/255 as near-saturated. A maximum code 255 is
conservatively **censored**, not certified exact radiance. The per-channel mask
survives even when other channels are unsaturated; all three maximum codes also
set `FullySaturated`. HDR above one is not LDR clipping.

Quantization bounds are code-bin/relative-model bounds, not statistical error
bars or guarantees about pre-compression/pre-normalization physical radiance:

| Policy | Lower encoded bound | Upper encoded bound (unless b=255) |
| --- | --- | --- |
| Unknown final quantizer | `max(0,(b-1)/255)` | `min(1,(b+1)/255)` |
| Round nearest | `max(0,(b-0.5)/255)` | `min(1,(b+0.5)/255)` |
| Truncate | `b/255` | `min(1,(b+1)/255)` |

For 255, upper is explicitly unknown. Positive monotone source transfer/gains
and calibration propagate these bounds and censoring; they never reconstruct
an upper bound. The reported point remains a convenient candidate, not an exact
fit target. JPEG loss, dithering, undocumented bake processing and chromaticity
normalization can introduce errors beyond final code bins. Default
`MagnitudeHistoryUnknown` records this missing information; a fitter must retain
the assumption and cannot treat all unsaturated channels as known radiance.

RGBE's shared-exponent quantization is labelled, but original exponent/bake
precision is not reconstructed from float values. HDR bounds remain unspecified;
`HdrPrecisionUnknown` is explicit. Negative/NaN/Inf HDR components reject the
sample and retain channel flags and raw bits. Missing input returns `Unavailable`
with all channels marked missing. Black is valid data, distinct from missing.
Deluxe directions return `Unavailable/Unsupported` as photometric observations.
Non-finite/negative converted values, float overflow or positive values
underflowing to zero return `InvalidInput`, preserving the raw sample.

## Renderer-compatible stage

`RendererSettings.known` must be explicitly supplied. The recipe records requested
and effective overbright bits, current map-overbright bits, linear/PBR/HDR/tone
flags, effective float-lightmap support, HDR-lightmap discovery, display gamma,
exposure context and debug mode. Requested overbright is not substituted for
effective `tr.overbrightBits` (currently clamped to 0..2). A renderer snapshot
must reflect the **sample's conversion point**; HDR discovery can change forced
linearization within external-map loading. No cvar is queried or changed here.

Setup parity is `(linearLighting || physicalPbr) && hdr && toneMap &&
effectiveOverbrightBits == 0`, with forced LDR decode disabled after HDR-lightmap
discovery. HDR samples bypass LDR transfer/overbright operations.

Let `delta = mapOverbrightBits - effectiveOverbrightBits`:

* Byte lightmap and LDR grid component: integer `b << max(0,delta)`. If the
  maximum exceeds 255, each channel becomes integer `channel*255/max`, including
  truncation. Normalize by 255. Lightmap sRGB texture sampling is represented
  by one sRGB decode only when forced linear is effective. Grid **stored
  components** remain encoded; consumer-specific interpolation/linearization is
  excluded.
* Float LDR lightmap: `max(b,0.499)` per channel. If RGB sum is below 12, replace
  RGB with `sum*0.3333` exactly as current renderer presentation. Multiply by
  `(1/255)*2^delta`, permitting negative delta. Apply the renderer's sRGB macro
  only when forced linear is effective.
* HDR lightmap/grid: unchanged float RGB, without LDR overbright or transfer.
  HDR grid storage needs HDR enabled, but not float-lightmap support.

Renderer normalization, integer quantization, black floor, dim grayscale and
extended transfer are flagged. Renderer bounds are deliberately absent: coupled
normalization/grayscale cannot preserve the source's independent inequalities.
The representation is pre-half-storage and a nearest texel/component value,
not a filtered framebuffer or screenshot. Values exceeding half-float's 65504
are flagged, while retaining finite float32. GPU filtering, half rounding,
tone mapping, gamma-ramp presentation, style animation, entity minimum/clamp,
lightgrid component merging and debug false-colour mode are not fabricated.
`r_lightmap 2` and unavailable HDR-lightmap storage report unsupported stages.

Supported map-overbright range is -16..15, effective overbright 0..2. Outside
this range the CPU reference rejects unsafe metadata instead of reproducing
undefined signed byte-shift/integer-product overflow. This restriction does
not change any renderer cvar. Display gamma/exposure remain context only.

## Calibration and dataset comparison

Optional `SceneCalibration` requires a positive explicit `relativeToScene` gain,
reference ID, method and provenance. With resolved Rec.709/D65:

```text
sceneEquivalent = sourceLinearRelative * relativeToScene
```

Its `Units` records which geometry/presentation policy the supplied gain belongs
to. `r_pbrWorldUnitsPerMeter` changes geometry scale, not source brightness.
`r_pbrPhysicalToScene` defines canonical presentation for physical values; it
does not establish a conversion from historical baked RGB to physical units.
Changing that context without supplying a newly appropriate gain does not
silently multiply raw observations. Scene-equivalent recipes with different
`physicalToScene` policies cannot be compared as the same scene reference.

Current `R_PBRCalibrateLegacyMap` derives a bounded presentation gain from a grid
percentile (`clamp(1/p95,0.25,4)`, with its existing invalid/black fallback). It
uses already interpreted grid values and is not a physical measurement. A host
may record such a method as a scene-equivalent reference; this decoder neither
calls that heuristic nor invents its result.

Absolute scale remains `Unknown`, even for a calibrated scene equivalent. Invalid
recipes mark it `Invalid`. An asserted lux/candela/nits unit is rejected; this
API provides no physical-reference measurement certification. Authored physical
lights still use the existing [PBR contract](rend2-physical-pbr.md).

Lightmap and lightgrid ambient/directed samples use the same explicit source
conversion API. Components remain separate: no ambient+direct sum/max is assumed.
`Compare` requires either two available scene equivalents with the same explicit
reference and presentation scale, or two available source candidates whose host
has attested the same nonempty `relativeDomain` and a known matching colour basis.
Two `Unknown` primaries values do not establish that basis. Default profiles,
identical filenames/BSP hashes and matching world-units/physical-to-scene settings
alone do not establish comparability. Comparison identifies a common basis; it
does not erase bounds, uncertainty or competing transfer hypotheses.

## Tests and integration limits

The standalone numerical test uses deterministic black/mid-grey/white, single
channel saturation, near-saturation, HDR >1, invalid float, missing/deluxe,
overbright and calibration fixtures, including immutable raw lightmap leases.
`decode_reference.cmake` extracts the **current** four renderer conversion/setup
functions and the actual float-LDR preconditioning block into a generated test
header. Tiny CPU stubs provide their cvars/state. The test matrix compares
independent decoder output with those original functions for byte/float paths,
linear/PBR setup, tone mapping prerequisite, HDR discovery, overbright and grid
components. It requires no GL or game engine. Source and renderer results are
asserted independently; metadata/provenance and error handling are tested too.

Numerical parity tolerance is relative/absolute `2e-6 * max(1,abs(reference))`;
analytic source sRGB fixtures use `1e-7` or `1e-6`. Masks, state, raw bits,
identity and repeated calls compare exactly. This is controlled CPU formula
parity, not a claim about GPU half rounding, texture filtering or screenshot
identity. The original renderer source is not edited to create the oracle.

```powershell
$toolbin = 'C:\Users\Keyten\Desktop\projects\OpenJK\build\toolchain\w64devkit\bin'
$env:PATH = "$toolbin;$env:PATH"
# Existing independent standalone configuration, no OpenJK dependencies:
& "$toolbin\cmake.exe" --build build/lightrecon-disk-standalone --parallel 4
& "$toolbin\ctest.exe" --test-dir build/lightrecon-disk-standalone --output-on-failure -V
# A fresh standalone configuration may use:
# cmake -S tools/lightrecon_test -B build/lightrecon-decode-cpu -G 'MinGW Makefiles' -DCMAKE_CXX_COMPILER=<g++.exe> -DCMAKE_MAKE_PROGRAM=<make.exe>
& "$toolbin\cmake.exe" --build build/lightrecon-core-renderers --config Release --parallel 4 --target rdsp-rend2_x86_64 rd-rend2_x86_64 lightrecon_test lightrecon_api_test lightrecon_core_test lightrecon_control_test lightrecon_disk_test lightrecon_lifecycle_test lightrecon_lightmaps_test lightrecon_decode_test
& "$toolbin\ctest.exe" --test-dir build/lightrecon-core-renderers -C Release --output-on-failure -V
$python = 'C:\Users\Keyten\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
& $python tools/rend2/test_lightrecon_contract.py
& $python tools/rend2/lightrecon_regression.py --no-gpu --output build/lightrecon-decode-shader-contract
```

Profiles are supplied explicitly by CPU callers, not automatically captured from
renderer globals. A future runtime/offline adapter must retain the appropriate
load-time settings/provenance, including mixed HDR/LDR ordering. The subsequent
[original lightgrid capture module](rend2-lightgrid-observations.md) supplies
pre-shift indexed data and explicit `DecodeGridLdr` / `DecodeGridHdr` helpers;
do not pass already colour-shifted `world->lightGridData` as original bytes.
Image parsing, lightgrid styles/geometry
association, bake-tool discovery and physical reference measurements remain
separate tasks. Profiles/results are not automatically serialized into the
canonical output asset or cache; future input-key plumbing must include the
entire effective versioned recipe/provenance, not just `Profile.identity`.

Existing raw lightmap capture still rejects an entire non-finite HDR image before
publishing a snapshot. That policy is unchanged; the generic `Raw` API separately
retains/classifies invalid HDR samples supplied by CPU callers. No corrupt-image
runtime recovery is claimed by the numerical decoder tests.

## Validation result (2026-10-23)

| Check | Result |
| --- | --- |
| SP/MP Rend2, MSVC Release | Both renderer DLL targets compiled against the shared core |
| MSVC C++11 CPU tests | 8/8 passed; 71,780 checks, including 63,928 decoder checks |
| Independent GCC 16.2 C++11 CPU tests | 8/8 passed; same counts |
| Decoder/renderer numerical parity | 576 controlled renderer settings profiles, lightmap and ambient/directed grid components; all comparisons passed at the documented tolerance |
| Raw/encoding fixtures | RGB8, decoded RGBA8 with preserved alpha, linear RGB32F with RGBE provenance, raw float32 HDR grid components, missing/deluxe and negative/NaN/Inf inputs |
| Strict core syntax check | `g++ -std=c++11 -Wall -Wextra -Wpedantic -Wconversion -fsyntax-only shared/rd-rend2/lightrecon/decode.cpp -Ishared/rd-rend2`, clean |
| Existing disabled-path detector | 12/12 tests passed |
| Controlled GLSL/binding baseline | 27 programs / 51 stages, zero differences; `--no-gpu` (full-matrix GPU compile not run) |
| Legacy rendering sources | `tr_bsp.cpp`, `tr_image.cpp`, `tr_light.cpp`, `tr_pbr_contract.cpp` unchanged |

Build/test logs are `build/lightrecon-decode-{msvc,gcc}-{build,tests}.log`;
the shader report is under `build/lightrecon-decode-shader-contract/`. This task
does not repeat a game/screenshot validation: production rendering does not call
the new decoder. Source-format tests operate on original/decoded sample
representations rather than reimplementing image parsers. Existing x64
`/arch:SSE2` ignored-option build warnings remain; the extracted reference's
intentional int-to-byte narrowing warning is suppressed only around that test
header, preserving the production reference code unchanged.

Changed implementation files are `lightrecon/decode.h/.cpp` and the shared core
CMake source list. Test additions are `lightrecon_decode_test.cpp`,
`decode_reference.cmake` and the existing CPU-test CMake target list. Documentation
links from the PBR, raw-lightmap and reconstruction-core contracts were updated.
No map-load hook, rendering formula, cvar registration or shader was changed.
