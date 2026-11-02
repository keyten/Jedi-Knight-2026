# P03: directional field representations and the quantity boundary

Decision date: 2026-11-02. Input revision `9787e7945`, `rend2-unified-wip`.
The [versioned witnesses](../tools/rend2/field_representation/results/v1/manifest.json)
pin measured implementation/executable hashes. No stock pixels, maps or recovered
stock lighting are used or published. These are synthetic relative-linear Rec.709
tests, without absolute JA photometric calibration.

**Decision:** retain explicit physical emitters and direct sun as source records.
Use SH L2 as the initial **diffuse irradiance response** codec for supported
low-frequency indirect/environment fields, with separate geometry support,
membership and approximation error. Offer L4 when independent normal/position
validation demonstrates useful improvement. Do not use L2, L4 or irradiance-only
data as a general incoming-radiance replacement. Fitted SG plus a separately
declared low-frequency component is an optional incoming-radiance tier for smooth
lobes; it is not the default diffuse codec or a representation of exact point
sources. Sharp apertures/specular distributions require explicit geometry/light
evaluation or a separately validated angular map. Multiple encodings may coexist.

This implements mathematical encoders/decoders and comparisons, **not** a stock
field generator, source estimator, `Snapshot` field backend or renderer adapter.
The canonical schema remains 7. No shader, game-lighting or runtime-consumer path
changes. `SampleIndirect` and `SampleIncomingRadiance` retain quantity-based APIs.

## Existing contracts and sources inspected

- `TransportField` already declares `FieldQuantity`, units, calibration, dataset,
  bounds and `TransportMembership`; field metadata does not prove sampling support.
  P06 root/order/configuration rules and `Attribution::CanAdd` remain authoritative.
- `Snapshot` distinguishes `IrradianceQuery.normalWorld` from
  `RadianceQuery.directionToSource`; missing prepared data remain Unavailable.
- `Photometric` already distinguishes radiance, irradiance, relative compiler Q,
  physical SI and scene-equivalent calibration. A mathematical angular basis
  introduces no new colour calibration or legacy-RGB inversion.
- The [RBDOOM study](rend2-rbdoom-field-research.md) and its hash-pinned source were
  reused. [`SphericalHarmonics.h`][rbsh] supplies independently checked convention/
  convolution details. [`RenderWorld_lightgrid.cpp`][rbgrid] L934–1027 projects L4
  radiance but normally writes an octahedral **E/pi** atlas; `STORE_LIGHTGRID_SHDATA`
  is 0. Its ordinary grid is not a serialized SH25 radiance field.
- The [DDGI study](rend2-ddgi-field-research.md), pinned
  [`Irradiance.hlsl`][ddgi] and existing geometry tests were reused. Distance moments
  and nonlinear irradiance lookup are consumer approximations, not exact visibility
  or new inverse observations. No SDK implementation is copied or linked.
- The existing renderer has no reusable CPU SH/SG angular codec discovered by the
  source search. Existing Python `rbdoom_research/field_basis.py` supplies an
  independent old three-lobe reference, not production encoding or fair SG fitting.

## Quantities and equations

At a spatial support x, w points **from receiver toward the incoming source** in
the fixed world frame. Photon travel is -w. All basis directions/normals must be
finite unit vectors; APIs reject zero/nonunit vectors instead of guessing a frame.

```
L(x,w)             incoming radiance per steradian
J(x,Omega) = integral_Omega L(x,w) dw
E(x,n)     = integral_sphere L(x,w) max(0,n.w) dw
Lo_diffuse = rho * E / pi
```

J is solid-angle integrated radiance, not E unless the cosine response is
explicitly included. Visibility-weighted L includes a declared source/geometry
operator V(x,w). Angular encoding cannot recover a missing blocker. RGB is linear
Rec.709 here. Luminance metrics use (.2126,.7152,.0722); on stock UnknownProfile,
unknown primaries/transfer must still block this interpretation. Do not divide
original BSP colours by current PBR albedo.

The units in this experiment are synthetic relative radiance and its derived
relative irradiance. A physically calibrated parent would use Nits/Lux under the
existing canonical contract. No source Q is converted into candela. A point light
at one position gives an angular delta with weight I/r²; a parallel directional
source gives a delta weighted by normal illuminance. Their finite irradiance is
well defined, while pointwise radiance RMSE is **NotComparable**, never zero.

SH uses a real orthonormal Condon–Shortley basis, z polar and index l*l+l+m:

```
integral Y_lm Y_jk dw = delta_lj delta_mk
Y00 = 1/sqrt(4*pi)
Y1,-1 = -sqrt(3/(4*pi))*y
Y1,0  =  sqrt(3/(4*pi))*z
Y1,1  = -sqrt(3/(4*pi))*x
c_lm = integral L(w) Y_lm(w) dw
Lhat(w) = sum c_lm Y_lm(w)
Ehat(n) = sum A_l c_lm Y_lm(n)
A_l = 2*pi*integral_0^1 t P_l(t) dt
A_0..4 = pi, 2*pi/3, pi/4, 0, -pi/24
```

Maximum degree L means (L+1)² **RGB coefficients**: 4,9,16,25,81 for
L1,L2,L3,L4,L8. L3 adds radiance information but no diffuse response beyond L2.
Odd degrees above 1 are in the diffuse operator's null space. Already convolved
irradiance coefficients are labelled `DiffuseIrradiance`; querying them as
incoming radiance returns Unsupported. This cannot be fixed by dividing by pi.

SH reconstruction remains signed. Negative values, negative integral, false dark
illumination and energy bias before/after a hypothetical channel clamp are
reported. The evaluator **does not clamp**. A future nonnegative `LightingSample`
adapter must return unsupported/approximate-with-a-declared-policy where needed;
it must not turn ringing into apparently conserved physical energy.

The fitted SG uses unit-integral lobes:

```
g(w,a,lambda) = lambda / [2*pi*(1-exp(-2*lambda))]
                * exp(lambda*(a.w-1))
g(w,a,0) = 1/(4*pi)
Lhat(w) = q0/(4*pi) + sum_j qj*g(w,aj,lambdaj), qj >= 0
```

Each qj is RGB **integrated radiance**, not peak radiance, source intensity or a
physical emitter. Stable `expm1` normalization avoids cancellation. The reference
SG diffuse evaluation integrates clamped cosine independently of SH: with
u=a.w, beta=acos(a.n), A=u*cos(beta), B=sqrt(1-u²)*sin(beta), average over azimuth is
0 if A<=-B, A if A>=B, otherwise
`[A*acos(-A/B)+B*sqrt(1-(A/B)^2)]/pi`. Integrate against the normalized exponential
in u. A t=lambda*(1-u) substitution, split grazing transitions, and a cosine
change of variables stabilize quadrature. The exp(-40) discarded tail is tiny,
but quadrature error is still tested separately; determinism is not proof of a
correct physical derivative. No source-motion derivatives are advertised here.

## Benchmark protocol

[C++ reference](../shared/rd-rend2/lightrecon/field_basis.h),
[native tests](../tools/lightrecon_test/lightrecon_field_basis_test.cpp) and
[comparison executable](../tools/lightrecon_test/lightrecon_field_basis_tool.cpp)
are CPU-only C++11, shared by standalone/SP/MP builds. No new external dependency.
Python only controls processes, exact geometry checks, JSON/CSV and hashes.

Main comparison: 3,200 Gauss–Legendre × azimuth training directions, 8,192
independent directions with a different order/azimuth offset, 129 independent
Fibonacci receiver normals. There is no random initialization or hidden-axis input
to SG fitting. Parameters are estimated only from training directional RGB.

18 owned distributions cover hemisphere ambient; finite directional disk; opposing
directions; red/blue sides; three coloured point deltas; narrow/broad finite spot
apertures; rectangle; finite cylindrical tube; a sheet seen from its emitting back
side under an explicit two-sided rule; sun+soft sky; smooth indirect; narrow bright
window; finite wall occlusion; a sharp lobe with weak fill; the prior three-lobe
reference; ideal parallel delta; and 1e6 versus 5e-5 RGB dynamic range. The 6-degree
disk is a declared angular stress test, not the measured Sun or a JA sky recipe.
Point/spot aperture outgoing emission and incoming angular extent are distinct.

Irradiance truth is independent of the tested low-order basis:

- Convex rectangles/windows and blocked-wall projected solid angles use analytic
  spherical polygon vector area after clipping to the receiver hemisphere.
- Constant hemisphere and low-order diffuse fields have analytic responses.
- Known smooth lobes use independent one-dimensional integrals, cross-checked
  against dense full-sphere integration in native tests.
- Finite disk, spot-aperture and cylindrical sources use independently integrated
  source geometry, not SH/SG fitted to themselves. 96 versus 128 quadrature order
  is reported. Largest relative irradiance change is about 0.047% for the tube;
  the remaining tested scenes are smaller. This is a resolution check, not an
  unconditional error bound for arbitrary geometry.

All representations use the same main angular observations. A **separate** SH
projection sensitivity control increases training to 12,800 directions. It is
labelled as extra work, not a fair SG comparison. For the rectangle, L4 error falls
from 6.24% to 2.69%, and L8 from 5.87% to 1.67%: finite angular sampling error must
not be incorrectly attributed entirely to basis truncation.

SG settings: 1/3/6 nonconstant lobes plus a fitted nonnegative constant. Initialize
from 64 deterministic axis candidates and widths {2,8,32,128,512}; then alternate
nonnegative RGB coordinate fitting and tangent-axis/log-width pattern search.
32 passes, four amplitude sweeps, at most 100 million kernel evaluations; widths
bounded to [.05,4096] during refinement. Report initial/final loss, iterations,
inactive lobes, maximum response-column cosine, stagnation and iteration limits.
The fit is local: reaching the iteration cap is not called convergence or proof
of unique lobe parameters. Held-out directions never initialize or select lobes.
The smooth SG-generated scenes deliberately test a matched family; reference SG
irradiance uses the generative parameters and higher-order integration, not the
fitted parameters. The same generic integral is used at different quadrature
orders, with independent full-sphere and existing Python azimuth-integral controls.
This explains why the smooth-lobe success must be read alongside the much poorer
rectangle/window results.

Hybrid has two meanings in this report:

1. `Explicit_SH_L2`: supplied, correct explicit source geometry/functions plus SH2
   of the independently labelled residual. This is a representation-capacity
   control, **not source discovery**. Point/spot/rectangle/tube/sun roots retain
   their identity; apertures are not converted into fictitious point emitters.
2. `SG_3_SH_L2`: fitted SG3 from exactly the same training data, followed by SH2
   projection of its signed approximation residual. This residual is mathematical
   representation error, not automatically positive physical GI.

The previous known-axis SG capacity oracle is kept in the prior study; it is not
used as this experiment's fitted score. Existing P06 canonical `CanAdd` tests are
also exercised directly: direct plus disjoint first bounce is legal, total field
plus its already-included direct root is not, and unknown membership cannot prove
addition safe. The benchmark uses declared analytic component membership; it
does not publish new canonical fields or certify unknown reconstructed source roots.

## Quality results

Numbers are relative RGB RMS over independent directions/normals, **percent**.
Complete per-scene metrics, channel errors, ringing, integrated energy, peak and
dominant directions are in [quality.csv](../tools/rend2/field_representation/results/v1/quality.csv)
and [quality.json](../tools/rend2/field_representation/results/v1/quality.json).
172 rows: delta scenes omit fitted SG because there is no finite pointwise
radiance objective. Dominant direction is unavailable for symmetric opposing
energy; multiple equal peaks are flagged, not arbitrarily called source truth.

| Three-coloured smooth lobes | Radiance error | Diffuse irradiance error |
|---|---:|---:|
| SH L1 | 92.94% | 28.45% |
| SH L2 | 86.15% | 5.42% |
| SH L3 | 77.04% | 5.42% |
| SH L4 | 66.80% | 1.75% |
| SH L8 | 27.74% | 0.219% |
| Fitted SG3 | 0.235% | 0.0496% |
| Fitted SG6 | 1.36% | 0.158% |

SG3's recovered axes differ by .024/.044/.037 degrees from generating axes,
matched only after fitting. SG6 retains extra, correlated lobes (maximum response
cosine .951) and is worse under the same pass policy. More capacity is not a
guarantee of better estimation. Lobe identifiability and local fitting cost remain
separate from representation capacity.

| Scene / metric | SH L2 | SH L4 | Fitted SG3 | Explicit + SH2 |
|---|---:|---:|---:|---:|
| Rectangle: irradiance | 8.48% | 6.24% | 16.16% | <1e-9% |
| Rectangle: radiance | 91.53% | 80.06% | 41.68% | 0% |
| Narrow window: irradiance | 7.61% | 5.23% | 16.81% | <1e-9% |
| Narrow window: radiance | 93.96% | 86.24% | 45.85% | 0% |
| Sun + soft sky: irradiance | 5.06% | 4.71% | 8.46% | 0.117% |
| Sun + soft sky: radiance | 92.45% | 90.93% | 85.43% | 6.35% |
| Point-source mixture: irradiance | 8.55% | 4.14% | NotComparable | <1e-9% |
| Point-source mixture: radiance | NotComparable | NotComparable | NotComparable | NotComparable |
| Smooth diffuse indirect: irradiance | <1e-9% | <1e-9% | 0.458% | <1e-9% |

Hybrid's near-zero rows assume the correct direct function has been supplied; its
exact reference source integration is charged separately. It cannot validate
that stock reconstructed geometry/amplitudes are correct. For opposing or
three-coloured smooth lobes with **no explicit source record**, `Explicit_SH_L2`
is simply L2 and gains nothing. The fitted SG+SH2 model can improve diffuse
prediction, but signed correction and extra complexity are visible in the receipt.

Raw SH rings and can illuminate nominally dark/opposite directions. Clamping
changes energy, hence both signed and hypothetically clamped energy biases are
retained; no residual is normalized to force agreement. A small global RGB error
can hide wrong colour allocation or weak-source loss, so channel-wise compression
errors and chromaticity errors are also reported. Unknown/zero lighting is tested
separately: unavailable data are not black.

## Spatial support is a different error

[spatial.json](../tools/rend2/field_representation/results/v1/spatial.json) reuses
the existing owned triangle segment reference, independent of angular encoding.
For supports (100,0) across a sealed wall, naive equal interpolation gives 50;
certified segment support yields 0 from the valid dark support. This result is
unchanged for SH L1/L2/L4/L8. With a real doorway both selected segments are clear
and the declared interpolation is 50. This is a supported interpolation result,
not measured ground-truth radiance at the doorway. With every support blocked the
result is **Unavailable**, not 0. Alpha-unknown/missing geometry remains Unknown.

No area/region identifier is treated as an opaque boundary. Unknown region
membership cannot override a certified local segment. Neither higher SH order
nor more SG lobes repairs missing spatial samples, thin-wall support errors or
incorrect door state. Moment weighting remains an optional approximate consumer
optimization; it cannot replace these visibility decisions for physical fitting.

## Storage, performance and compression

Prototype angular block v1: little-endian magic/version, explicit quantity,
storage kind, degree/counts, bounded coefficients/lobes and FNV-1a corruption
checksum (not cryptographic authentication). Maximum decode size 8,192 bytes;
degree <=8, lobes <=8. Owning dataset still must carry units/calibration, world
frame, support identity/geometry, root/path/style/configuration, uncertainty and
reconstruction generation. This experimental block is **not registered as a new
production Dataset encoding**; no canonical schema or existing S13/P06 payload is
replaced. Any later registration needs its own stable namespaced codec/version.

An eventual GL3.2/legacy adapter can decode to ordinary FP32 coefficients and
evaluate a fixed finite basis, without requiring native half arithmetic, compute
shaders, hardware tracing or modern packed HDR textures. SH2 requires nine RGB
weighted terms plus its basis; L4 requires 25. SG3 needs three directional dots,
three exponentials and RGB accumulation plus a constant. These are operation
counts, **not measured GPU cycles**. Device limits, actual shader compilation,
texture layouts and interpolation must be tested by that adapter. Octahedral E
can use four bilinear taps; the prototype performs them explicitly on CPU. Nothing
in the canonical dataset must name GL textures, shadow slots or froxel formats.

| Encoding | RGB coefficients / stored parameters | Raw FP32 bytes | Block FP32 bytes | Block FP16 bytes |
|---|---|---:|---:|---:|
| SH L1 | 4 RGB | 48 | 80 | 56 |
| SH L2 | 9 RGB | 108 | 140 | 86 |
| SH L3 | 16 RGB | 192 | 224 | 128 |
| SH L4 | 25 RGB | 300 | 332 | 182 |
| SH L8 | 81 RGB | 972 | 1,004 | 518 |
| SG3 + constant | 4 stored (axis xyz, width, RGB) tuples | 112 | 144 | 88 |
| SG6 + constant | 7 tuples | 196 | 228 | 130 |
| SG3 + constant + SH2 | 55 scalars | 220 | 252 | 142 |

SG axes have two independent degrees of freedom but three stored coordinates.
The prototype stores a constant as a full tuple; a registered codec could save its
fixed axis/width. Root/geometry metadata, spatial indices and explicit emitter
records are additional. A hybrid's field byte count does not include those source
records. At 100,000 supports raw SH2 is 10.8 MB versus L4 30 MB, excluding metadata;
separate per-support prototype headers add 3.2 MB. Production chunk headers should
be amortized. Reading eight uncached FP32 SH2 supports requires at least 864 bytes,
L4 2,400 bytes, plus indices/metadata. These are traffic calculations, not measured
DRAM/GPU bandwidth.

An additional [octahedral capacity control](../tools/rend2/field_representation/results/v1/octahedral.json)
stores **E**, with explicit FP32/FP16 RGB texels and CPU virtual seam folding.
It receives exact irradiance at texel centres, so it is an atlas interpolation/
quantization capacity test, not a fair unknown-field estimator against the main
SH/SG training protocol and not an RBDOOM shader replay.

| FP16 angular atlas | Bytes, no metadata/borders | Three-lobe E error | Hemisphere E error | Delta E error |
|---|---:|---:|---:|---:|
| 8x8 | 384 | 6.47% | 3.97% | 8.93% |
| 16x16 | 1,536 | 1.97% | 1.24% | 2.83% |
| 32x32 | 6,144 | 0.597% | 0.373% | 0.931% |

This supports a possible diffuse atlas adapter but does not justify it as the
default CPU codec: the tested SH4 diffuse reference is smaller, and an irradiance
atlas still cannot supply incoming radiance. Texture borders, padding and hardware
filtering are extra; only the explicit CPU fold/interpolation and rounding were
tested. Exact-reference atlas samples provide more direct E information than the
main angular training data, so the table is not a claim of superior reconstruction.

The measured generic CPU path is deliberately validation/recurrence based rather
than hand-expanded SIMD. Typical costs from the first frozen run are approximately
0.5 microseconds per SH2 quantity evaluation, 1.1 for SH4 and 4 for SH8. SG3
radiance is about 0.5 microseconds, but its independently integrated diffuse
response is roughly 90 microseconds; eight evaluated neighbour values cost about
eight times that. Exact measured values for both repeats, including build time
and process RSS, are in [performance.json](../tools/rend2/field_representation/results/v1/performance.json).
Do not present the SG diffuse reference integrator as a cheap renderer recipe.
Generate a diffuse SH response offline if that consumer needs it.
The full 172-row native processes take 64.985 and 59.734 seconds with peak process
RSS 8,810,496 and 9,338,880 bytes (about 8.40/8.91 MiB). Native mathematical tests
take .312 seconds, peak about 4.75 MiB. Times include all fitted alternatives,
reference work and quantization checks; other work on the shared machine can
affect wall time. Numerical semantics match exactly across the two runs.

Explicit-source irradiance reference work is separately timed and is expensive
for finite-source quadrature. Incoming-radiance hybrid timings include the source
function. Eight-neighbour tests use identical in-cache supports and value
blending; they measure arithmetic, not irregular-grid memory throughput. SH
coefficients may be linearly blended only across compatible frames/quantities/
membership. SG axes/sharpness cannot be blended as linear coefficients: evaluate
and blend values or explicitly refit a lobe mixture and charge its error.

FP32 is the reference storage; computation/accumulation is double. FP16 uses
nearest-even rounding, explicit overflow rejection and normalized decoded SG
axes. Block-int16 uses a declared per-block scale, preserves signs and includes
that scale in its error measurement. It is supported for SH only: one shared
quantizer across SG axis, width and energy would be inappropriate.

The HDR scene exposes two hazards: FP16 rejects large all-light coefficients,
while a common signed-int16 scale can erase the entire 5e-5 blue signal beside
1e6 red energy. Small global RMSE does not excuse that loss. Hybrid separation
can leave a small residual representable in FP16, but only if the root split is
supported. Never silently clamp to 65,504, infer calibration from coefficient
range or promise BC6H quality: no BC6H, RGB9E5, GPU codec or device was tested.

## Consumer policy

| Consumer | Recommended request/encoding | Accuracy / cost evidence | Limits and uncertainty |
|---|---|---|---|
| Ghoul2/entity diffuse | E(n), SH2 first; SH4 optional | 108/300 raw FP32 bytes; smooth diffuse exact here, three-lobe error 5.42/1.75%; small dot-product-style evaluation | Local geometry support and negative reconstruction policy required; source uncertainty persists |
| Environment diffuse | Same E(n); quality tier by independent normals/positions | SH3 adds no diffuse information; SH4 improves selected angular cases at ~2.8x storage | No inverse conversion to radiance, no current-albedo division |
| Volumetric incoming lighting | Explicit sources + separately available incoming radiance; fitted SG+low-frequency SH optional | SG3 handles smooth three-lobe radiance at .235%; sharp aperture still ~46% | Irradiance-only request is Unsupported; phase-function-weighted accuracy needs an additional benchmark; no froxel dependency |
| Environment specular | Explicit sharp sources; independently validated high-resolution radiance map/reference where needed | SH4 still 66.8% radiance error on smooth three-lobe case and ~80% panel; SG is useful only for suitable smooth lobes | No exact glossy/high-frequency claim; angular map/prefilter codec not implemented by this task |
| Recovered physical direct lights | Canonical emitter/sun records, strict selected physical/bake operator | Given correct records preserve delta/finite-source detail instead of low-order projection | Type/position/amplitude/profile confidence comes from solver, not representation fit; stock UnknownProfile stays conditional |
| Lowest-cost diffuse tier | SH1 only when declared error is acceptable | 48 bytes but 28.45% three-lobe diffuse error | Not a universal fallback; no fake confident colour/direction |
| Optional high quality | SH4 diffuse; SG+SH incoming; L8 offline diagnostic | L8 three-lobe E .219% but L still27.74%; SG fitting costs much more than projection | Higher order does not solve spatial leaks, unknown optics or source identity |

The first implementation decision is bounded: register a geometry-aware sparse
**irradiance-only SH2** field codec only after spatial support generation and
held-out position tests are supplied, preserving all P06 membership metadata.
Keep raw FP32 on CPU; permit FP16 or signed scaled coefficients only after
per-field/per-channel error admission. Upgrade individual field regions to SH4
when measured improvement justifies storage/query cost. Return Unavailable if no
supported probe exists; return Unsupported for radiance from E-only storage.

Do not lock the canonical asset to SH9. The descriptor must select quantity,
codec/version, frame/phase/indexing, units, calibration, support/membership and
error. A field may have diffuse and incoming-radiance encodings representing the
same root/path set; these are **alternative evaluations**, not additive energy.
P06 composition checks reject duplicate root/order coverage and exclusive source
or profile configurations. An unresolved field with unknown root membership may
be useful as approximate illumination but cannot claim safe exact-light addition.

## Verification and remaining work

Native tests include SH signs/orthogonality, convolution/null space, RGB
independence, SG normalization, independent irradiance integration, unknown versus
zero, signed ringing, unknown-axis fitting, deterministic repetition, finite/HDR
checks, quantization, corruption, work exhaustion, E-only radiance rejection and
canonical legal/illegal root composition. **970 checks pass**. Full CPU CTest:
**37/37 pass**. Existing DDGI 12 and RBDOOM 13 tests pass; SP/MP MSVC Release builds
pass. No game was launched. Existing `/arch:SSE2` warning on the x64 MSVC build is
unrelated. [Exact commands](../tools/rend2/field_representation/README.md) and
versioned checks preserve the environment/resource witnesses.
Ten new Python tests validate the frozen receipts, wall/doorway/unknown support,
octahedral direction round trips, seams, quantities, overflow and determinism.

Both complete comparison runs produce identical numerical semantic output after
separating timing/RSS. SG local fitting, sharp-boundary angular sampling and
spatial interpolation remain distinct limitations. Missing evidence is not
improved by re-encoding. This work does not establish stock incoming radiance,
accurate specular/phase-function convolution, legacy GPU performance, runtime
interpolation, an installed Snapshot field backend or safe replacement of baked
world lighting. Those require separate generation/admission/consumer work.

[rbsh]: https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5/neo/idlib/math/SphericalHarmonics.h#L196
[rbgrid]: https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5/neo/renderer/RenderWorld_lightgrid.cpp#L934
[ddgi]: https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/f33e496ca31b3f0eec1c4e2cbaa8bb620e337fa6/rtxgi-sdk/shaders/ddgi/Irradiance.hlsl#L140
