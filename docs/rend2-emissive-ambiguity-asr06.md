# ASR06: emissive surfaces, reflected light and false-source controls

Research and bounded offline prototype, 2026-11-09. Inspected baseline
`8f8875008878a0d1dc6a0572862bf13546771f0a`, branch `rend2-unified-wip`.
The supplied checkout's remote is `keyten/BetterJK`; the task names
`keyten/Jedi-Knight-2026`. Work stays in this checkout. Existing localization,
ASR05 and IDF edits are preserved. Input identities are recorded in
[sources.json](../tools/rend2/emissive_ambiguity/sources.json).

**Decision:** screen proposed surface lamps against the existing fixed-root,
passive one-bounce model before assigning additional emission. Brightness,
glow, a good free-source fit and a coherent incoming direction do not establish
an independent energy origin. Preserve ambiguity when the relevant transport,
material, geometry or observation operator is unavailable.

The [prototype](../tools/rend2/emissive_ambiguity/study.py) is a separate analytical
diagnostic. Canonical assets, storage, material/source provenance, the historical
solver and runtime shading are unchanged. It publishes no physical emitters.

## Paper review and missing supervision

Reviewed Jeong, Koo, Zhang and Kim, *ESR-NeRF*, CVPR 2024, using the complete
[author preprint v2, 7 June 2024](https://arxiv.org/html/2404.15707v2),
especially sections 3–4, 5.1, 6 and appendix 8.2/8.4/8.10.

ESR-NeRF trains on posed LDR views in two states: **all sources on and all
sources off**, not one lamp at a time. It learns geometry, BRDF, emission,
environment and outgoing radiance; these are not supplied ground truth.
Rendering loss uses a learned tone mapper plus a fixed clipping/gamma path.
Light-transport-segment consistency is model supervision, not a separate
physical measurement. It still produces false emitting regions without
progressive reflection discovery; reflection rays receive emission suppression
(equations 11–16). Synthetic scenes have 200 training images, split equally
on/off. Emission masks and linear EXR references score identification/intensity;
they are evaluation truth, not emission-mask training labels. Real poses use
COLMAP. Single-state separation is future work; unobserved paths limit editing.
[Paper sections 4–6 and appendix](https://arxiv.org/html/2404.15707v2).

Our applicability assessment:

| Required distinction | What one JA bake provides or lacks |
|---|---|
| Controlled illumination intervention | No independently captured all-on/all-off states. Light styles, surface/grid slots and reconstructed screenshots are not automatically such interventions. |
| Outgoing appearance across known views | A lightmap is a baked material multiplier under a compiler operator. It is not a set of posed outgoing-radiance photographs. New views of that bake add no independent lighting state. |
| Reflectance versus incident energy | Current textures/PBR values do not identify historical bake reflectance. Do not divide receiver lightmap bytes by texture RGB. |
| Camera response or bake response | JA transfer, primaries, clipping and shared-channel operations require explicit hypotheses. The paper's tone mapper does not identify Raven's compiler or calibrate RGB8 to lux. |
| Independent source truth | Shader/entity facts support priors; hypothetical sources remain reconstructed, conditional hypotheses. No stock emission masks or linear radiance truth are available. |
| Complete transport geometry | Original BSP support is useful, but moving, masked, deformed, omitted or unsupported paths must remain Unknown. An absent triangle is not proof of free space. |

We do not train or reproduce ESR-NeRF. The transferable idea is to test a
reflection explanation before allocating local emission. Its neural losses or
ray partitions cannot manufacture the missing intervention in JA.

## Existing code and provenance boundary

The [inverse-GI research](rend2-inverse-gi-research.md), particularly G1–G8,
sections 4–5 and 8–10, already separates source-rooted passive transport from
unresolved discrepancy. [S10](rend2-indirect-fitting-s10.md) implements this in
`lightrecon/indirect.{h,cpp}`: fixed roots, bounded material groups, passive
transfer, unknown-path issues, separate observation contracts, frozen evaluation
and transport-field output. `AppendFields` does not append physical emitters.

The [localization research](rend2-light-localization-research.md), sections
4.1–4.3 and 7, requires the selected observation operator for gradients and
ratios, treats grid directions as mixtures, and distinguishes existence evidence
from location/visibility compatibility. In the actual code:

* `evidence.cpp::Surfaces` extracts original geometry from `MaterialGlow` or
  positive `legacySurfaceLight`, retains raw material identity and uncertainty,
  and keeps `LegacyBakeTool` intensity hints uncalibrated. Glow is useful evidence
  even when it is a display-only effect.
* `candidate_pool.h` has `PhysicalCandidate`, `AmbiguousSourceOrTransport` and
  `TransportEvidence`. These are hypotheses/scheduling inputs, not certification.
  `Basic` protects authored evidence; this study does not demote or remove it.
* `emitter_support.{h,cpp}` checks actual occupied/emitting support independently
  of receiver rays. A visible aperture is not an original emitting polygon.
* `indirect.h` keeps `UnknownMaterial`, `UnknownOptics`, `MissingPrimary`,
  `UnknownProfile`, omitted links and model discrepancy explicit. Our Python
  fixture reuses the existing research `transport.Transfer` power guards; it is
  not a new implementation of the production C++ adapter.

No new provenance kind, material rule, canonical field, wire version, source
class or runtime consumer is introduced. A legacy shader directive is not
automatically physically authored nits, watts or calibrated irradiance.

## Why even a coherent bright patch can be a reflector

For a diffuse surface in an explicitly linear model,

```text
Lo(y) = Le(y) + rho(y) Eprimary(y) / pi
Esecondary(x) = integral_surface V(x,y) Lo(y) cos_x cos_y / r² dAy
```

A reflector with `rho=(.8,.08,.04)` and uniform incident `(20,20,20)` has the
same outgoing field as emission `(16,1.6,.8)/pi`. Receivers and directional
moments of this field cannot determine its energy origin. The counterexample
independently integrates the reflective and emissive hypotheses: receiver and
direction differences are zero, and encoded samples are identical. Color and
spatial gradients of identical observations also coincide.

This is our analytical non-identifiability example, not an assertion that JA
lightmaps measure `Lo`. The correct JA observation operator still surrounds the
transport prediction. A geometric/directional fit can establish the apparent
last scattering region while leaving the primary source elsewhere.

| Evidence signal | Useful constraint | False-source control |
|---|---|---|
| Shader glow / surface-light declaration | Authored appearance or bake-intent prior; original material identity | Glow alone never earns emission. A bake hint still needs independent receiver support and supported geometry. |
| Actual world geometry | Original emitting surface, orientation, area and side | Do not fit a rectangle across a doorway and call it occupied emitter support. No surface is a rejection of this **surface hypothesis**, not of every possible hidden lamp. |
| Directed lightgrid | Profile-specific aggregate incident direction | The last reflector can yield the same direction as a lamp there. Test isolated-source assumptions; mixed/cancelled lobes and quantization weaken localization. |
| Source-to-receiver visibility | Whether a source hypothesis could affect other receivers; dark receivers can constrain amplitude | Optical compatibility is necessary for the fitted direct contribution but does not prove energy origin. Trace both links of a bounce. Unknown paths cause abstention. |
| Color / spatial gradient | Shared-material, shared-style response shape under the chosen observation model | Red bleed, normal/chart/material seams and clipping can imitate emission or source boundaries. Keep them separate from a texture preview. |
| Predicted one bounce | Existing primary energy transported through bounded reflectance and actual visibility | No independent patch brightness gain. A plausible passive explanation avoids requiring another lamp; failure of an incomplete model does not establish emission. |

The directional experiment reports an **ideal radiance-weighted moment proxy**,
not decoded JA directed RGB/angles and not a compiler grid replay. The exact
equivalence persists even with this stronger ideal signal. The lit-doorway proxy
aligns with the proposed opening direction at cosine `0.972–1.000`, despite no
emitting polygon at that opening. These measurements are evaluation diagnostics,
not training labels or hard source-existence gates.

## Deterministic fixtures and diagnostic policy

Four small scenes are constructed in code, each at source/intensity scales
`0.5, 1, 2`. The intensity changes are **separate tests**; a diagnostic receives
only one state. All coordinates are metres, with a declared synthetic operator.
There are six training receiver supports at X=1/1.6 and six held-out supports at
X=3/4. Each support has area 0.04 m². Held-out receivers include a different normal;
the minimum train/test centre separation exceeds the 0.2 m footprint width.

| Scene | Construction and proposed false lamp |
|---|---|
| Genuinely emissive panel | Original 0.8x0.8 m rectangle at `(0,0,2)`, facing +X; emission `(12,8,4)` plus reflection, glow and a bake-emission hint. An existing primary at `(2,0,3.5)` is fixed. |
| Red reflected wall | Same original support, **zero emission**, reflectance `(.8,.08,.04)`, existing white primary intensity `(160,160,160)`. Propose emission on the bright red patch. |
| Lit doorway | Screen at X=0 with opening `abs(Y)<.65, 0<Z<2.8`; original reflector behind it at X=-2 and primary at `(-1,0,3.5)`. The opening proposal has no occupied surface. Both direct transmission and reflection are retained. |
| Texture-only decorative glow | Original panel and glow-stage appearance, **zero illumination from the glow**, no bake-emission hint. Ordinary diffuse reflection remains possible. Preview brightness is independent of the receiver lighting target. |

Primary/source units and material reflectance are synthetic declarations, not
recovered JA quantities. Black receiving patches terminate transport. Sparse
absence in this complete, declared toy network means no edge; it does not
establish zero transfer in an incomplete exported map.

Sender integration uses a 24x24 reference versus 8x8 fitting quadrature. The
receiver operator uses the same explicit midpoint approximation; there is no
claim of exact finite-footprint integration or compiler reproduction. Reciprocal
links and nonnegative reflectance pass the existing `Transfer` outgoing-power
guards. Geometry has a deterministic segment/aperture test with opaque edges.

Synthetic receiver observations use `Q(E)=min(255,floor(12 E+.5))` with declared
linear primaries. Fit distance is to quantization intervals; 255 has no finite
upper bound. The separate display preview uses `255 L/(1+L)` and never supplies
an irradiance target. This does **not** decode encoded JA RGB8 as linear light.

Frozen policy, declared before running the fixtures:

1. Missing geometry, material, primary or encoding returns
   `AmbiguousSourceOrTransport`, with no invented numeric prediction.
2. Compare direct-only, direct plus free candidate emission, fixed-root passive
   transport, and passive transport plus additional emission on **training** RGB.
   Reflectance/root amplitudes stay fixed; only the proposed emission RGB is fit,
   nonnegatively and bounded by 64 in synthetic radiance units.
3. Absent original surface support returns `ApertureOrUnsupportedSurface`.
   Passive interval RMSE <=0.75 byte returns `PassiveTransportPlausible`.
4. A `ConditionalEmitterCandidate` additionally requires a bake-emission hint,
   >=2 independent receiver responses exceeding 2 bytes, >=2-byte training RMSE
   improvement, and combined interval RMSE <=0.75 byte. Otherwise abstain.
5. Freeze the decision, then score held-out RGB and directional diagnostics.
   Private emission labels, held-out RGB and grid proxies are outside the fit
   interface. No threshold or parameter is selected on held-out values.

Thresholds are preset research settings, not learned confidence levels or
production defaults. `PassiveTransportPlausible` means another lamp is not
required by this model; it does not prove the surface cannot emit. The source
case can simultaneously reflect and emit. No result certifies exact history.

## Measured false-source rate and limitations

[results.json](../tools/rend2/emissive_ambiguity/results.json) contains all
per-scene errors, fitted hypothesis amplitudes, color ranges, tangent gradients,
direction alignment, provenance and abstentions. The unit of scoring is **one
proposed surface lamp**, excluding the already supplied primary lamp.

| Detector / diagnostic | False emitting proposals / 9 nonemitters | False-source rate | Retained / 3 genuine panel variants |
|---|---:|---:|---:|
| Preview peak >=180 RGB8 | 9/9 | 100% | 3/3 |
| Glow stage alone | 3/9 | 33.3% | 3/3 |
| Actual surface plus preview threshold | 6/9 | 66.7% | 3/3 |
| Conservative passive-screening diagnostic | **0/9** | **0%** | **2/3** |

Three intensity variants per family are correlated controls, not nine independent
maps or a calibrated deployment error estimate. Abstentions count as missed
sources in recall; they are not silently removed from its denominator. The dim
panel's passive training RMSE is 1.7102 bytes and does not meet the 2-byte
improvement gate; it remains ambiguous. The retained panels' selected held-out
interval RMSEs are 0.05398 and 0.00318 bytes. All nine nonemitters' passive
held-out interval errors are zero at this quantization, not claims of zero
continuous physical-model error.

The red wall is the decisive control: direct plus a freely fitted emitter gives
training interval RMSE below `1e-10`, with spurious red emission amplitudes
`2.5086, 4.9104, 8.4074`. Fixed-root passive reflection explains the same targets
without that lamp. Smooth, visible, color-consistent source fitting therefore
cannot replace an origin test. No production emitter is created by either fit.

Additional controls withhold geometry, bake reflectance, primary support,
encoding, or the usable emission hint; all abstain. Censoring remains one-sided.
Tests change held-out answers, emission truth and directional proxies without
changing fitted emission or decisions. Appearance-only changes likewise cannot
change the photometric fit. Quadrature refinement reduces the red-reflector
error below 0.01 encoded byte at the fitting order against the 24x24 reference.

This small suite does not measure BSP extraction quality, stock-map false-source
rate, unknown-profile coverage, specular/transmissive transport, arbitrary source
assemblies or multiple bounce orders. The one-bounce model is a screening
hypothesis, not historical bake replay. Unknown roots/materials or substantial
residuals must remain unresolved; they are not reasons to add a physical lamp.

## Validation record and implementation recommendation

Reproduction commands are in the [fixture README](../tools/rend2/emissive_ambiguity/README.md).
Validation on this checkout:

* 15 ASR06 tests pass, including a frozen result and identical repeated runs.
* 33 existing localization tests pass.
* Rebuilt native `lightrecon_indirect`, `lightrecon_candidates`,
  `lightrecon_emitter_support` and `lightrecon_evidence`: 4/4 pass.
* Existing inverse-GI tests: 14/15 pass. `test_repeat_witness` fails against its
  unchanged tracked `results/analytic.json`: the witness records sealed-wall
  direct visibility `[1,1]`, while the existing code returns `[0,0]`, plus small
  floating-point differences elsewhere. The same-process repeat is stable.
  Neither the older witness nor its solver/research code is edited here.

For a future real-ROI adapter, run existing `Indirect::PredictRoot/Fit` under
stated bake/material hypotheses at frozen primary settings, then compare
additional-emitter ablations on separated receivers with the selected surface
and grid operators. Preserve root membership, omitted-link uncertainty and
signed model discrepancy. Keep plausible reflection as transport evidence or
`AmbiguousSourceOrTransport`; keep authored evidence even when it is not needed
to explain the bake. If every supported explanation fails, record unresolved
transport and expand hypotheses deliberately. Do not turn a residual patch,
glowing reflection or doorway into a physical lamp automatically.
