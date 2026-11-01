# Reconstructed photometric conversion boundary (P04)

## Audit before implementation

Audited baseline: `1fdd75bb7`, semantic schema **5**, retaining schema-4
photometry and contribution domains. S13's subsequent provider receipts account
for the version difference from the task's schema-4 snapshot.

* `tr_pbr_contract.h/.cpp`: Rec.709 D65 Y weights (0.2126, 0.7152, 0.0722),
  default 33.333333 world units/metre and 1e-4 physical-to-scene scale.
  Runtime convenience helpers substitute defaults/clamp invalid input; the
  reusable conversion boundary must instead return failure. These constants
  and cone formulas are the existing photometric model, not a new model.
* `tr_scene.cpp`: physical cd/lm input is Y-normalized and scene-scaled once.
  Legacy `AddLightToScene` retains radius and RGB semantics. Physical-mode
  migration matches a reference at half the old radius using
  `(radiusMetres/2)^2 * legacyReference * Y(inputRGB)`. This is an approximation,
  not recovery of historical candela. Legacy GLSL attenuation is
  `clamp(0.5 * radiusWorld^2 / distanceWorld^2 - 0.5, 0, 1)`; physical GLSL
  uses inverse-square, a finite-radius denominator and a culling window.
* `tr_world.cpp`, `tr_bakedlighting.*`, `lightrecon/decode.*`: raw RGB8/HDR,
  decoder source hypotheses and renderer-compatible presentation are separate.
  Overbright, black-floor, forced sRGB and HDR storage behavior cannot establish
  historical transfer, primaries or absolute scale. Decode's optional scene gain
  is already a relative-to-scene gain: applying physicalToScene again is wrong.
* `tr_pbr_contract.cpp::R_PBRCalibrateLegacyMap`: positive lightgrid
  ambient+direct Y samples, median and p95; runtime gain
  `clamp(1/p95, .25, 4)`. This is a bounded presentation convention, not an SI
  calibration. `lightrecon/lightgrid.cpp` preserves raw/decoded sample domains.
* `tr_arealights.cpp`: authored nits are normalized/scaled at ingestion; LTC
  integrates radiance over solid angle. Rectangle area is 4*halfWidth*halfHeight;
  finite tube area is 4*pi*radius*halfLength (open curved side, no caps).
  Legacy area amplitudes use map/reference presentation gains. The froxel
  projected-area approximation is a consumer policy, not canonical photometry.
* `bake_profile.h`, `amplitude_fit.cpp`, `producer.cpp`: compiler Q is fitted
  in an explicitly named profile/domain. Canonical-to-compiler mapping requires
  independent calibration. Stock Q/max(Q) is not Y-normalized physical colour.
  `LegacyRelativeChannels` is deliberately rejected by general RGB sampling.
* `tr_lightrecon_types.*`: Photometry already records unit, Y interval,
  chromaticity and calibration kind. `SceneEmissionRGB` is an existing untyped
  compatibility multiply, with no receiver geometry, transfer or Q mapping.
  Calibration currently has only method text: no machine-checkable gain,
  reference distribution, domain/profile scope or interval. This is the concrete
  semantic gap requiring a versioned optional calibration extension.
* `tr_camera.h`, `tr_postprocess.cpp`: exposure, pre-exposure, display encoding,
  bloom and fog are presentation/consumer concerns. None belongs in stored Q,
  source chromaticity, emission, or canonical field values.

No shader or runtime consumer is changed by P04. Existing renderer math remains
the compatibility baseline; new strict CPU helpers operate only on explicit
conversion requests.

## API and persistent compatibility

`lightrecon/photometry.h/.cpp` implements the CPU boundary in
`Rend2LightRecon::Photometric`. `Conversion<T>` has a structured code and reason;
values are usable only on `Ok` or explicitly `Approximate`. Unavailable values
are never black-light samples. There is no I/O, renderer state, exposure,
material lookup, GPU storage clamp or implicit default world scale.

The canonical `Photometry`, `Calibration`, `Source`, `SourceUncertainty`,
`TransportMembership` and `ReconstructedLightingAsset` remain authoritative.
`Value` is a transient typed conversion result/receipt, not a second asset or
source authoring model. It distinguishes quantity, unit and domain; all channels
are linear Rec.709 D65 **only after evidence establishes that interpretation**.
`RGBToPhotometry` is explicitly for RGB already in that space, not raw stock RGB8.
`DecodedRelative` takes the source stage of the existing decoder, rejecting
assumed/unknown transfer and primaries. Renderer and decoder scene stages never
become SI input. It retains the immutable decode profile, flags and censoring.

Semantic schema **6** extends the existing schema-5 `Calibration` with optional
`RelativeCalibrationMapping` version 1. The concrete missing semantics were a
machine-checkable input domain, profile/configuration scope, target scene-equivalent
calibration, colour attestation, global gain estimate/bounds, world scale and
reference distribution (identity, sample count and optional p50/p95). Method and
original provenance stay on the existing calibration record/DAG. A zero mapping
version means absent. Existing producers leave it absent by default.
The original relative record remains relative; a mapping refers to a separate
existing-kind `LegacySceneEquivalent` calibration, never `PhysicallyAuthored`.

The disk codec serializes all mapping fields with enum validation. Asset version
and the empty-format golden digest change. Previous semantic versions are
explicitly rejected and must be rebuilt from inputs; bytes are not reinterpreted
or auto-promoted. Producer dependency keys include the new mapping data.
No new disk envelope format or parallel photometry asset is introduced.

## Conversion support matrix

Here radiance means a photometric RGB proxy with Y in cd/m², not spectral W/(m² sr).
Let U be world units/metre, s be the fixed physicalToScene, n the unit receiver
normal, wi a unit receiver-to-light direction, V supplied transmittance in [0,1],
and f the dimensionless angular emission profile.

| Input → output | Prerequisites | Formula | Uncertainty and unavailable cases |
| --- | --- | --- | --- |
| JA distance ↔ metres | finite nonnegative distance, positive U | m=w/U; w=mU | Invalid scale/number, overflow or underflow to zero is rejected. |
| Canonical RGB ↔ chromaticity + Y | attested linear Rec.709 D65 RGB, nonnegative finite channels | Y=.2126R+.7152G+.0722B; c=RGB/Y; RGB=cY | Exact black has known Y=0 and unknown colour. Positive near-black is normalized without a black floor. Unknown colour with positive energy is unavailable. |
| Raw encoded RGB8/HDR → source-relative RGB | existing Decode recipe; documented transfer and primaries | existing transfer decode and explicitly recorded source gains | Decoder's raw values, profile, censoring and bounds survive. Unknown/assumed primaries or transfer cannot pass this boundary. No raw-RGB8-to-candela path exists. |
| Compiler Q / source-linear relative → scene-equivalent canonical quantity | versioned mapping; matching domain, profile, configuration, units, world scale, attested colour/transfer; explicit positive gain g and reference distribution | equivalentRGB=g*relativeRGB | Positive interval multiplication [gLo*xLo,gHi*xHi]; unknown gain bounds yield unknown output bounds. Q requires explicit nonzero configuration and profile. Missing absolute scale remains unavailable to physical-only APIs. |
| Point cd → receiver lux | explicit ideal inverse-square model; position; valid normal and isotropic profile | E=I*max(n·wi,0)*V/d_m² | Intensity intervals propagate monotonically; geometry/source uncertainty stays in receipt. Zero distance is invalid. No range-window/culling cutoff is applied. |
| Spot on-axis cd → receiver lux | point conditions plus right-handed frame and smooth cone co<ci | E=I*f(theta)*max(n·wi,0)*V/d_m²; f=t²(3−2t), t=clamp((cos(theta)−co)/(ci−co),0,1) | Same conditional bounds. Unknown/tabulated angular profiles are incomparable, not flattened to points. |
| Spot lumens → on-axis cd | same cone profile | Ω=2π(1−(ci+co)/2); I=Φ/Ω | Strict inputs, same formula as existing PBR contract; no silent cone clamping. |
| Finite point/spot radius → bounded receiver approximation | explicitly selected `FiniteRadiusBoundedApproximation`; positive known radius | replace d_m² by max(d_m²,r_m²) | Always `Approximate`. This bounds a renderer-style approximation; it is not an exact sphere/cookie radiance integral. Unknown radius is unavailable; no invented 1 cm radius. |
| Rectangle / open tube geometry → emitting m² | positive finite dimensions | rectangle A=4wh/U²; tube A=4πrℓ/U² | w,h,ℓ are half extents. Tube has curved side only, no caps. Extension, zero area and invalid geometry are unsupported/invalid. |
| Rectangle/tube nits → emitted radiance | Lambertian profile; actual outward surface normal; valid direction/frame | Le=L on emitting side (rectangle may be two-sided); zero on back side | Constant Lambertian radiance is not L*cos(theta); cosine enters the integral. Original source/evidence/uncertainty remain intact. Other profiles are incomparable. |
| Rectangle/tube nits → receiver lux | finite geometry and Lambertian profile; explicit quadrature resolution | E=∫A Le*max(ne·(−wi),0)*max(n·wi,0)*V/r_m² dA | Deterministic midpoint quadrature, always `Approximate`, no claimed quadrature error bound. Full finite geometry retained. Supplied V is uniform; no visibility tracing. On-surface receivers are invalid. |
| Directional sun normal-plane lux → receiver lux | parallel profile and known frame | E=E_normal*max(n·(−forward),0)*V | No finite radiance is invented for the delta direction. Source angular-size metadata remains unchanged. |
| Directional sun lux → incoming disk radiance | explicit uniform-disk model, known 0<a<π/2 | Li=E_normal/(π sin²a) within the apparent disk, zero outside | Conditional on this angular model and stated radius, not a measured limb profile. Unknown/zero disk radius is unavailable/invalid. |
| Sky/environment directional sample → incident radiance | decoded directional RGB sample, compatible unit/calibration | identity with explicit `IncidentRadiance` quantity | Never assumes mean sky luminance is isotropic. Dataset/source/provenance and uncertainty retained. |
| Incident radiance → receiver irradiance | compatible Nits/radiance samples, unit directions, positive solid-angle weights, same calibration/domain/root/path/configuration receipt | E=∫hemisphere Li(wi)*max(n·wi,0)*V(wi)dω | Supplied quadrature is `Approximate`. Uniform hemisphere is analytic E=πL. Irradiance input cannot be integrated as radiance. Missing samples are unavailable, not black. |
| Fields/contribution RGB → typed conversion value | canonical field/contribution unit/quantity; explicit source membership | identity; then the same scoped calibration/scaling operations | Copies source, source list, source/root uncertainties, calibration ID, domain, configuration, sample domain, dataset and entire transport membership including reflection order/style. Fractions and `LegacyRelativeChannels` are incomparable. |
| Physical SI / canonical scene equivalent ↔ scene-scaled value | compatible calibration kind, positive fixed s, no prior scale | scene=s*canonical; canonical=scene/s | Bounds scale once. A scale receipt rejects reapplication and inversion under a different scale. Uncalibrated Q/relative and physical-only use of a scene equivalent are unavailable. |
| Reconstructed quantity → original Quake attenuation | no generally valid relationship | no exact conversion | `LegacyAdapter` returns `Incomparable/UnsupportedLegacyAttenuation`; no inverse-square result is relabelled as Quake attenuation. Runtime AddLightToScene is untouched. |

`Emission` extracts canonical source photometry. Calibrated relative values can
be passed explicitly as `convertedEmission` to the emitter geometry functions;
the quantity and source identity must match. There is no implicit Q calibration
or profile/configuration selection. `RequirePhysical` is a separate admission
check and rejects all scene equivalents. Numerical success never changes
`exactLight`, physical confidence, admission records or `absoluteScaleKnown`.

All interval propagation is conditional on the supplied geometry, direction,
visibility and angular model. It is **not** a posterior or a bound over uncertain
source configurations; those original uncertainties remain attached. A global
gain is scoped to one quantity/domain/profile/configuration, not a universal
conversion between compiler source Q, baked receiver RGB and every emitter shape.
Different quantities/shapes may use their own independently evidenced mappings.
Unknown historical bake reflectance is never replaced with current PBR base colour.

These are standard photometric and projected-solid-angle integrals; see
[BIPM photometric definitions](https://www.bipm.org/documents/20126/41749107/Principles%20governing%20photometry/8365350c-bf72-3cf2-bf03-324bf5c01b62)
and [PBRT radiometric integrals](https://pbr-book.org/4ed/Radiometry%2C_Spectra%2C_and_Color/Working_with_Radiometric_Integrals).
RGB photometric proxies do not recover a spectrum or radiometric watts.

## Validation and limitations

`lightrecon_photometry_test.cpp` covers world/metre round trips; photometric Y;
RGB decomposition; exact black/near-black; point values and inverse-square ratios;
normal/visibility dependence; independent spot cone integration and PBR formula
parity; rectangle/tube area; analytic rectangle irradiance and tube projected-area
limit; finite-radius approximation status; directional lux and uniform sun disk;
uniform/cosine incident distributions; scaling once and inverse scale identity;
calibrated/uncalibrated relative values across all five emitter types; profile and
configuration incompatibility; unit/geometry/legacy incompatibility; NaN/Inf and
overflow. It also checks decode RGB/censoring invariance across 16 presentation
combinations and propagation of source/root uncertainty and membership receipts.
Disk tests cover optional mapping round trips and the semantic-version golden.

Historical absolute stock-source brightness remains **unidentified** without
independent calibration evidence. Stock primaries, source transfer and bake
profile remain unknown. P04 supplies no such evidence and certifies no stock
source for exact physical rendering. No Forward+, fog, entity, LTC or shadow
consumer is enabled, no shader is modified, and no visual validation is claimed.

Validation on Windows x64 (MSVC 19.27, Release) passed all 32 initial standalone
tests, followed by all six integration tests affected by merging the current
transport-topology changes into this worktree. Final photometry tests passed
251 checks on both MSVC and GCC (strict C++11); contract and disk tests also
passed after the last numerical guards and schema-version rejection cases.
Both `rd-rend2_x86_64` (MP) and `rdsp-rend2_x86_64` (SP) Release targets built.
The final complete merged-worktree CTest run passed **33/33** tests in 462.36 s.

Disabled-path preprocessing against task input `1fdd75bb7` matched all 27
programs / 51 stages and their bindings; production dependency audit passed.
The older global frozen baseline (`380ee0f7...`) already differs in current
foliage/uniform code, so that baseline comparison is not reported as passing.
No GPU compilation or runtime image comparison was performed for P04.
