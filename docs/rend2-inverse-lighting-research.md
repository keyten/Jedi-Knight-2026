# Inverse baked-light reconstruction: research and a JA solver design

Research date: 2026-10-26. Repository baseline: `447e2ce22`.
This document proposes an offline algorithm; it does not implement reconstruction,
change rendering, or certify any recovered light. A valid observation dataset,
emitter candidate set, or empty output asset is not a successful reconstruction.

The useful result of inverse rendering is generally a **family of plausible
explanations**, not proof of the original source configuration. Appearance error,
geometric source error, calibration, and suitability for a particular consumer
must be evaluated separately.

## 1. Sources, scope and reproducibility

Primary reference **[T]**: Mathias Schwengerer, *Inverse Method for Baked Lighting*,
TU Wien, bachelor thesis, August 2023:
[publication](https://www.cg.tuwien.ac.at/research/publications/2023/Schwengerer_Mathias-2022-BGL/),
[full PDF](https://www.cg.tuwien.ac.at/research/publications/2023/Schwengerer_Mathias-2022-BGL/Schwengerer_Mathias-2022-BGL-Thesis.pdf).
Page references below are the **printed Arabic pages**. PDF viewer page = printed
page + 16; for example, Eq. 3.7 is printed p. 15, PDF page 31. The PDF has 67 pages,
4,753,811 bytes, SHA256
`23f15080f41828f4930628fcf3cd9058f6ce4314f8a843e3e9f1212e4f6fd148`.

The complete English technical text was read: abstract, Chapters 1–6, references
and format appendix. Its PDF text layer has broken character mappings. All pages
were rendered at 140 DPI and OCRed with Windows `en-US` OCR; equations 3.1–3.7,
Algorithm 4.1, Table 5.1 and the key evaluation figures were also checked against
the page images. OCR is a reading aid, not authority for mathematical symbols.
No thesis images or proprietary game maps are redistributed here.

Supplement **[A]**: Lipp, Hahn, Ecormier-Nocca, Rist and Wimmer,
*View-Independent Adjoint Light Tracing for Lighting Design Optimization*,
ACM TOG 43(3), article 35, 2024, DOI
[10.1145/3662180](https://doi.org/10.1145/3662180).
The [versioned author manuscript, v2, 2024-05-06](https://arxiv.org/html/2310.02043v2)
provides explicit objective and gradient equations. [T]'s bibliography instead
lists its framework as **[Pri23], private communication**, p. 46. The 2024 paper
clarifies the method family; its settings and experiments must not be silently
attributed to the 2023 thesis.

Additional primary sources:

- **[Q]** id Software's original Q3 source at commit
  `dbe4ddb10315479fc00086f08e25d968b4b43c49`:
  [BSP definitions](https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/qcommon/qfiles.h),
  [`TraceGrid` and lighting](https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/q3map/light.c),
  [lightmap construction](https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/q3map/lightmaps.c).
- **[N]** NetRadiant q3map2 at commit
  `c00084bc741a16438e9257dc5a64da989085532f`:
  [`TraceGrid`](https://github.com/xonotic/netradiant/blob/c00084bc741a16438e9257dc5a64da989085532f/tools/quake3/q3map2/light.c).
  This inspected source snapshot is **not** evidence that stock JA or our 2022
  compiler binary used the same defaults.
- **[E]** Li et al., *Differentiable Monte Carlo Ray Tracing through Edge Sampling*,
  2018: [author project and paper](https://cseweb.ucsd.edu/~tzli/diffrt/).
  Used here for the distinction between interior and visibility-boundary
  derivatives, not as a reproduced experiment.

File hashes and source locators are in
[`sources.json`](../tools/rend2/inverse_lighting_research/sources.json).
Statements marked **[T]** or **[A]** describe those sources; the proposed equations
**J1–J9**, algorithms and acceptance gates below are recommendations for this
repository. No author implementation or optimizer benchmark was reproduced.

## 2. What the thesis actually optimizes

### 2.1 Problem and forward model

**[T, §§3.1–3.3, pp. 11–16]** Inputs are static scene geometry, material textures
and baked RGB lightmaps. Unknowns are the **position, colour and intensity of a
fixed set of point lights**. The task is matching the baked appearance using a
physically based lighting configuration, not recovering a known original light
list. There is no demonstrated spot/rectangle/tube/source-count inference.

The forward model is the rendering equation [T, Eq. 2.1, p. 6]:

\[
L_o(x,\omega_o)=L_e(x,\omega_o)+
\int_{\mathcal H^2} f_r(x,\omega_i,\omega_o)
 L_i(x,\omega_i)(n\cdot\omega_i)\,d\omega_i.
\]

The framework uses **light tracing from sources**, accumulating a view-independent
surface radiance representation, and **adjoint light tracing** for parameter
gradients [T, §3.1]. It can represent directional exitant radiance with
hemispherical harmonics (HSH), including specular effects. Final evaluation
images also use path tracing. This is not a direct-only inverse-square fitter,
nor is its training objective a collection of camera screenshots.

**What is not specified by [T]:** the complete fitting objective formula,
regularization coefficients, BRDF parameters, maximum bounce depth, exact
derivative estimator, parameter constraints, optimizer tolerances/betas/history
size, radiometric calibration and all sampling/filtering settings. In particular,
**Eqs. 3.1–3.7 derive the target projection, not the light-fitting loss**.
A clearance penalty and changing source count are future work (p. 38), not
implemented regularizers to copy.

For an explicit mathematical reconstruction of the framework family, **[A, §3,
Eqs. 1–3]** gives:

\[
 O(L)=\frac12\int_\Omega\frac1{2\pi}\int_{\mathcal H^2}
 \alpha(x,\omega_o)[L(x,\omega_o)-L^*(x,\omega_o)]^2
 \,d\omega_o\,dx,\qquad
 p^*=\arg\min_p O(L(p)),
\]

subject to the rendering equation. RGB adds a channel sum. This is a weighted
surface/directional squared error. [A, §4.1, Eqs. 4–6] uses piecewise-linear
spatial basis functions and HSH coefficients \(L_{klm}\):

\[
 O=\tfrac12\sum_{klm} A_k\alpha_k\alpha_l
 (L_{klm}-L^*_{klm})^2,\qquad
 A_k=\tfrac13\sum_{t\ni k}|t|.
\]

Order zero suffices for diffuse exitance; glossy transport can still participate.
Its chain rule is [A, Eq. 9]
\(\nabla_p O^T=\sum_{klm}(\partial O/\partial L_{klm})
(dL_{klm}/dp)\), evaluated by the adjoint pass rather than storing every primal
path's full differentiation graph. These equations are the **2024 clarification**,
not undocumented exact numerical settings of [T].

[A, §§4.3–4.4, Eqs. 10–15] accumulates adjoint sensitivity to each light path's
emitted flux and first-interaction BRDF, then differentiates the primary
light-to-surface geometric factor with the first surface hit held fixed. For a
position update its Eq. 14 is
\(d\Phi_r/dp=\tilde I_e\,d[(n\cdot\omega)/r^2]/dp\), with a per-ray virtual
intensity chosen from the primal flux. This avoids the naive differentiation
of sample hits through triangle interpolation, which can give systematically
wrong descent directions; simply applying automatic differentiation to a
light tracer is insufficient. Primal/adjoint random seeds can be decorrelated.

The later framework's positive intensity parameterization is
\(I_e=\tfrac12p_j^2\); its constant-intensity chromaticity constraint uses
\(r+g+b=1\) [A, §4.4.1]. Neither is evidence of measured units in [T]. In JA,
canonical colour instead requires **luminance Y=1**. A squared amplitude also has
zero derivative at zero, making dormant-source activation difficult. The
proposed first solver therefore uses explicit nonnegative amplitudes and birth
proposals, rather than copying that parameterization unexamined.

### 2.2 Geometry, samples and area-weighted target

**[T, Ch. 4, pp. 17–24, Appendix A, pp. 49–51]** reads Quake III `IBSP` version 46:
17 lumps; RGB8 128×128 lightmap pages; vertices with position, material UV,
lightmap UV, normal and colour; indexed polygon/mesh surfaces and quadratic
Bezier patches. Mesh indices are offsets from the surface's first vertex.
Adjacent 3×3 patch controls are tessellated; the implementation chooses four
vertices in each parameter direction, 16 points per patch. This choice is not
proof that all Q3/JA renderers have identical patch tessellation or runtime LOD.

**[T, §3.2, pp. 12–15]** forms a scalar/vector surface function
\(r(x)=T(x)\odot B(x)\), texture times lightmap, and projects it onto vertex
shape functions \(\varphi_i\). The derivation is:

1. Partition of unity: \(\sum_j\varphi_j(x)=1\), Eq. 3.1.
2. Interpolant: \(\tilde r(x)=\sum_jr_j\varphi_j(x)\), Eq. 3.2.
3. Minimize \(\tfrac12\|\tilde r-r\|_{L^2(\Omega)}^2\), giving
   \(\int_\Omega(\tilde r-r)\varphi_i\,dx=0\), Eq. 3.3.
4. The consistent mass system is
   \(\sum_jr_j\int\varphi_j\varphi_i=\int r\varphi_i\), Eq. 3.4.
5. Mass lumping replaces the coupled mass matrix with its row sums:
   \(r_i=(\int r\varphi_i)/(\int\varphi_i)\), Eq. 3.5.
6. Associated vertex area is
   \(A_i=\int\varphi_i=\tfrac13\sum_{t\in N(i)}A_t\), Eq. 3.6.
7. With \(n\) uniform samples per triangle,
   \[
   r_i\simeq\sum_{t\in N(i)}\frac{A_t}{A_i n}
   \sum_{s=1}^{n}r(x_s)\varphi_i(x_s),\tag{T3.7}
   \]
   Eq. 3.7, p. 15. Algorithm 4.1, p. 24 implements this in a compute shader.

This preserves a constant field and surface-integrated energy under exact
quadrature. It **does not preserve sharp spatial features**, or even an input
linear field's original nodal peaks after mass lumping. For one triangle with
nodal values `(0,0,1)`, exact degree-two quadrature yields `(0.25,0.25,0.5)`.
Our small numerical check reproduces this. Coarse original BSP vertices can
therefore erase the very shadow boundaries needed to locate lights. [T, p. 38]
acknowledges this loss and proposes mesh subdivision as future work.

Lightmap sampling filter, mip level, transfer function, source gamma,
overbright history, censoring and texel coverage are not fully prescribed in
[T]. A product of two historical texture samples is an **appearance/radiance-like
target**, not automatically a measurement in radiance units. For a known diffuse
receiver, \(L_o=\rho E/\pi+L_e\); mapping lightmap values to irradiance requires
knowing the bake/shader multiplier, transfer, reflectance and emission. A
lightmap can already encode an irradiance-like multiplier with constants folded
in. Multiplying or dividing by texture RGB without this distinction corrupts
the inference. [T] demonstrates an appearance match, not calibrated lux.

### 2.3 Initialization and evaluated optimization

**[T, §3.3, pp. 15–16; §5.2, p. 30]** initializes uniformly spaced, dim white
point lights in the scene AABB; initial intensity is `1.0` in the framework's
unspecified units. The thesis uses **n×n grid labels**, illustrated in Fig. 3.3;
its exact vertical layering is not specified. Do not substitute the later
paper's `9×9×2` OpenArena example for this experiment. Source count stays fixed;
no source is created or deleted by the optimizer.

**[T, Ch. 5, pp. 25–35]** evaluates `q3dm1` (Arena Gate), `q3dm2` (House of Pain)
and `q3dm3` (Arena of Death), with three runs per configuration and approximately
100,000 rays per light per rendering evaluation:

| Experiment | Actual tested settings and result |
|---|---|
| Adam step sweep, Figs. 5.1–5.2, p. 26 | 11×11 grid; steps 1, .8, .6, .4, .2, .1, .08, .06, .04, .02; 400-evaluation cap. Smaller steps generally improve the eventual match but can be too slow under that cap; .1 is selected as a compromise. |
| L-BFGS sweep, Figs. 5.3–5.4, p. 27 | Steps 1, .8, .6, .4, .2, .1; nominal cap 10,000, observed termination below 200; higher run variance and no clear best step. |
| Grid sizes, Figs. 5.5–5.8, pp. 28–29 | 5×5, 7×7, 9×9, 11×11, 13×13; Adam .1 / L-BFGS 1. More lights reduce error at increasing tracing cost and with diminishing returns. |
| Scheduling, Figs. 5.9–5.11, Table 5.1, pp. 30–33 | **Map 1 only**, 9×9 grid, three runs; five schedules below. |

Table 5.1's schedules, with `C/I` meaning colour and intensity:

| Scheme | Phases in order |
|---|---|
| 1 | Adam .1 C/I → Adam .1 position → Adam .1 all |
| 2 | Adam .1 position → Adam .1 C/I → Adam .1 all |
| 3 | L-BFGS 1 all → Adam .1 all |
| 4 | Adam .1 all → Adam .1 all → Adam .1 all |
| 5 | L-BFGS 1 all → L-BFGS 1 all → L-BFGS 1 all |

Scheme 4 produces the best reported objective; scheme 3 trades some quality for
time. “Adam never terminates” in the discussion means their configured stopping
rule was not reached before the cap; it is not a general property of Adam.
An L-BFGS step can contain multiple render evaluations. Count evaluations, rays
and wall time separately rather than comparing optimizer step counts directly.
The graphs retain only improvements/running minima, hiding intermediate
regressions [T, pp. 25–27]. There are no original-source ground-truth position,
intensity, count or confidence metrics.

### 2.4 Shadows, materials, indirect light and failure cases

The physically based framework transports light through scene geometry and
includes indirect illumination. Thus it should not be described as ignoring
occlusion or fitting bounce light solely with point falloff. However, [T] does
not specify a reproduction-complete material model, optical treatment of alpha
textures/windows, JA shader effects, bake filtering or all transport settings.

**[A, §§2.2, 6.4, 7] explicitly does not differentiate moving-shadow boundaries.**
The gradient holds ray-surface intersection positions fixed while a source
moves; visibility discontinuities are omitted. Forward shadows exist, but the
gradient is incomplete at their boundaries. Figures 20–21 show resulting
failures, including a light becoming trapped inside solid geometry. Adam's
momentum sometimes escapes cases where gradient descent/L-BFGS do not. This is
not a visibility guarantee. [A, §3] also limits the presented formulation to
reflective transport, with no volumetric scattering; transmission is an
extension, not demonstrated JA grate/window handling.

Fixed grids waste sources outside rooms, allocate too many sources to one real
fixture, and can create multiple shadows/hotspots. A source occluded from every
receiver has essentially zero photometric gradient: colour/intensity fitting
cannot bring it into a useful room. Section 3.3 initially dismisses such lights
as wasted compute; the evaluation and conclusion acknowledge placement and
near-surface artifacts [T, pp. 32–35, 38; Figs. 5.12–5.14]. Manual movement/removal,
better initialization, adaptive source count and clearance penalties are
suggested improvements, not completed automatic recovery. A source pushed close
to one triangle can fit a local peak with implausible inverse-square energy.

**Adopt:** camera-independent objectives, area weighting, real geometry/occlusion,
multiple parameter schedules and explicit evaluation of initialization.
**Do not adopt as guarantees:** fixed AABB point grids, coarse nodal targets,
unqualified physical units, lowest training error as proof of source recovery,
or smooth gradients through hard visibility events.

## 3. Mapping the method onto the current repository

### 3.1 Q3 is not JA's on-disk schema

| Responsibility | Original Q3 [Q] | JA / current extraction |
|---|---|---|
| BSP | `IBSP` 46, 17 lumps | `RBSP` 1, 18 lumps, including light-array lump 17 |
| Vertex | 44 bytes; one lightmap UV and RGBA | 80 bytes; four lightmap UVs and RGBA style channels |
| Surface | 104 bytes; one lightmap reference | 148 bytes; four references/offsets plus lightmap and vertex style arrays |
| Lightgrid | 8-byte ambient RGB / directed RGB / lat-long record | 30-byte record with four ambient/directed RGB style slots, four styles and one direction; separate logical-cell `uint16` indirection |
| Lightmaps | Embedded RGB8 128×128 | Embedded pages plus external LDR/HDR, alternative encodings, possible deluxe pairing |
| Shader interpretation | Thesis texture × lightmap target | Multiple stages, styles, glow, sky, alpha, deformation and material uncertainty need explicit classification |

JA definitions are in [`code/qcommon/qfiles.h`](../code/qcommon/qfiles.h) and
[`codemp/qcommon/qfiles.h`](../codemp/qcommon/qfiles.h). Other Q3-family games and
compiler extensions can differ again. Do not port thesis appendix offsets into
the JA reader. Runtime entity lighting is also not the inverse of the bake
compiler's grid-packing operator.

Even original Q3 is not an exact physical oracle: [Q, `LightContributionToPoint`]
supports linear versus inverse-square attenuation, clamps distance at 16 world
units and discards weak contributions. A physically plausible replacement need
not recover a physically authored bake, because the original bake may not have
been one. Keep a **compiler-emulation observation model** separate from the
physical emitter model, and report their residual disagreement. Modern q3map2
options add more profiles; the selected source snapshot is not a universal
description of JA's historical pipeline.

### 3.2 Available interfaces and limits

| Data / contract | Current location | Consequence for solver design |
|---|---|---|
| Canonical output | [`tr_lightrecon_types.h`](../shared/rd-rend2/tr_lightrecon_types.h), [asset contract](rend2-reconstructed-lighting.md) | Point/spot/rectangle/tube/directional types, hypotheses, explicit uncertainty, calibration and separate transport/environment. A candidate is not a resolved physical source. |
| CPU session/input | [`lightrecon/input.h`](../shared/rd-rend2/lightrecon/input.h), [`session.h`](../shared/rd-rend2/lightrecon/session.h) | Bounded leased chunks, injected cancellation/visibility, no GL/world globals. The default algorithm is unavailable; no solver exists. |
| Raw/decoded colours | [`decode.h`](../shared/rd-rend2/lightrecon/decode.h), [decoding contract](rend2-observation-decoding.md) | Raw → source-relative → renderer-compatible → optional scene-equivalent stages. Unknown transfer/primaries/scale remain explicit; source values are not lux. |
| Raw lightmap images | [`lightmaps.h`](../shared/rd-rend2/lightrecon/lightmaps.h), [capture](rend2-lightmap-observations.md) | Original logical pages before atlas and renderer colour transforms; RGB8/RGBA8 or decoded HDR floats. HDR and LDR alternatives are not two independent energy measurements. |
| Luxel geometry | [`surfaces.h`](../shared/rd-rend2/lightrecon/surfaces.h), [mapping](rend2-luxel-observations.md) | Pixel centres, coverage/footprint area, original UVs, geometric/shading normals; deterministic curved patch mapping where supported. Padding/overlap/unsupported geometry cannot become fabricated targets. |
| Styles/material/deluxe | [`classification.h`](../shared/rd-rend2/lightrecon/classification.h), [classification](rend2-observation-classification.md) | Distinct vertex observations; animated styles excluded from static sums; paired world-space deluxe evidence; uncertain bake-time albedo. |
| Lightgrid | [`lightgrid.h`](../shared/rd-rend2/lightrecon/lightgrid.h), [grid observations](rend2-lightgrid-observations.md) | Every logical cell and record link preserved; HDR inconsistencies explicit; style/brightness does not certify free space. |
| Static visibility | [`visibility.h`](../shared/rd-rend2/lightrecon/visibility.h) | Immutable CPU BVH; visible/blocked/unknown, sky/material policy, bias and reliability. No-hit and PVS do not prove sky visibility. |
| Regions | [`regions.h`](../shared/rd-rend2/lightrecon/regions.h) | Separate adjacency, LOS, transport possibility and semantic grouping. Conservative/partial graph is not guaranteed room segmentation. |
| Emitter evidence | [`evidence.h`](../shared/rd-rend2/lightrecon/evidence.h), [`emission_geometry.h`](../shared/rd-rend2/lightrecon/emission_geometry.h) | Surface masks/geometry, authored JSON, retained entities, shader sun/sky, stable provenance; no physical certification of raw glow brightness. |
| Offline input | [`offline.h`](../shared/rd-rend2/lightrecon/offline.h), [dataset workflow](rend2-offline-observation-datasets.md) | `.jkobs`, bounded chunks, hashes, explicit partial coverage, blind benchmark hints and spatial splits. Solver should consume these observations without game startup. |
| Output disk/lifecycle | [`disk.h`](../shared/rd-rend2/lightrecon/disk.h), [`cache.h`](../shared/rd-rend2/lightrecon/cache.h), [`lifecycle.h`](../shared/rd-rend2/lightrecon/lifecycle.h) | Validated immutable results, optional capability sections, complete-key requirement. Production full input fingerprint / early cache preflight is still unwired; an incomplete key remains `NotCacheable`. |

Offline derived observations default to a finite mapped-image prefix and bounded
region ROI. **Partial is not full-map training coverage.** Use explicit all-page
export or stream source images/UV mappings; missing tiles must never turn into
black observations. Stock-map region witnesses can contain no certified regions;
the optimizer must support conservative local geometry/unknown topology rather
than require successful room grouping as a precondition.

### 3.3 Physical and colour semantics

The authority is [`rend2-physical-pbr.md`](rend2-physical-pbr.md) and
[`tr_pbr_contract.cpp`](../shared/rd-rend2/tr_pbr_contract.cpp):

- JA world is right-handed, +Z up. \(r_m=r_{world}/u\), where
  \(u=r\_pbrWorldUnitsPerMeter\), default 33.3333333. Do not assume one inch per unit.
- Point/spot intensity is candela; area/tube emission is luminance (cd/m², nits);
  directional sun is normal-incidence illuminance (lux). These are photometric
  units, not interchangeable scalars or necessarily spectral/radiometric values.
- Linear RGB uses Rec.709/D65 when that assumption is justified. Canonical
  chromaticity has \(Y=.2126R+.7152G+.0722B=1\), so saturated colour components
  may exceed 1. Intensity is the Y magnitude. Black has unknown chromaticity.
- `r_pbrPhysicalToScene`, default `1e-4`, is physical-to-canonical presentation;
  it and geometry scale do not reveal historical bake brightness. They are not
  camera exposure, source decode gamma, or calibration measurements.
- Distinguish raw encoded values, linear relative observations, calibrated
  scene equivalents and genuinely physically authored values. Legacy q3map
  `light`, `surfacelight` and shader sun numbers are not automatically cd/nits/lux.
- Receiver direction \(\omega\) points **toward** the light. Canonical spot/sun
  `Frame.forward` points with **photon propagation**, so sun-toward direction is
  `-forward`. Rectangle normal emits along `forward`; tube axis is `right`.

Keep photometric scale separate from observability. A position can be well
constrained while intensity scale is unknown; authored candela can be known
while its relationship to a particular baked highlight is uncertain.

### 3.4 Why grid and deluxe need their own observation operators

For original Q3 **[Q, `TraceGrid`]**, let \(C_j\) be a source's RGB contribution at
a grid point and \(\omega_j\) the unit direction toward it. Before byte conversion:

\[
 u=\operatorname{normalize}\sum_j\|C_j\|_2\omega_j,\quad
 d_j=\max(0,\omega_j\cdot u),\quad D=\sum_jd_jC_j,
\]
\[
 A=A_0+\tfrac14\sum_j(1-d_j)C_j+\tfrac14D
   =A_0+\tfrac14\sum_jC_j.
\]

Thus even one entirely direct source contributes to **both** packed A and D.
The single direction is RGB-magnitude weighted, not luminance weighted, and is
undefined under exact moment cancellation. Quantization/normalization changes
the stored values again. Some solid sample positions are nudged before baking;
the logical cell centre need not be the actual historical sample location.

**[N, `TraceGrid`]** has different directionality parameters, per-style packing,
separate ambient contributions, optional flood/bounce/minimum-light operations
and byte conversion. Its older ambient-addition code is disabled. This is a
concrete counterexample to assuming one universal grid formula. Neither snapshot
identifies the stock JA bake toolchain conclusively. The current renderer's
sampling or HDR sum rules cannot reconstruct missing compiler settings.

Use a **profile-specific \(H_g^\psi\)**; if unsupported, reduce/disable grid
photometric constraints with an explicit reason. Never assert `ambient = GI`,
`directed = direct irradiance`, or `A+D = calibrated irradiance` universally.
Quantized lat/long points receiver-to-source according to entity conventions;
angular uncertainty and unknown position nudging must be retained. Cells inside
walls remain observations awaiting geometry classification, not seeds of rooms.

Current deluxe decoding supports paired world-space encoding
\(d=\operatorname{normalize}(RGB/255-0.5)\). A midpoint/near-zero vector is unreliable;
the compiler's weighted directional moment must still be identified before
predicting it. A deluxe vector is not a tangent-space normal or necessarily a
single source axis. Encoding modes beyond the supported profile are unavailable,
not silently converted. Multiple styles share limited directional information;
do not manufacture one independent direction per style.

## 4. Recommended mathematical model (proposed, not implemented)

### 4.1 Unknowns and forward prediction

Use a finite geometry-aware candidate set with an adjustable active subset:

\[
 \theta=(K,\{\tau_j,p_j,Q_j,G_j,q_j\}_{j=1}^K,
 s_\odot,e_{sky},b,\psi,g).\tag{J1}
\]

Here \(\tau\) is source type; \(p,Q,G\) are position, frame and dimensions;
\(q_j\ge0\) is RGB amplitude in the **type's own unit**; \(s_\odot\) and
\(e_{sky}\) are distinct direct-sun and sky parameters; \(b\) describes residual
indirect transport; \(\psi\) is a small set of transfer/compiler hypotheses;
\(g\) contains explicitly scoped calibration gains. Do not optimize redundant
intensity and RGB scale: solve RGB amplitudes, then split into Y and Y=1 colour.
Source-count changes are discrete proposals, not a differentiable type switch.

For a far-field point source and a supported spot angular profile \(a_j\):

\[
 E_i^{direct}=\sum_j V_{ij}\frac{q_j}{r_{ij,m}^2}
 (n_i\cdot\omega_{ij})_+a_j(-\omega_{ij}),\quad
 \omega_{ij}=\frac{p_j-x_i}{\|p_j-x_i\|}.\tag{J2}
\]

An isotropic point has \(a=1\). A spot axis is photon-forward; its cone tests
\(f_j\cdot(-\omega_{ij})\). Use the contract's smooth cone / solid-angle conversion
when converting flux, not an unlabelled cone multiplier. RGB J2 is a tristimulus
irradiance proxy whose Y has lux semantics under the physical calibration model.
The point approximation is invalid in its near field. **Do not fit a singular
point against a texel using an arbitrary attenuation clamp as physical evidence.**

For finite luminance geometry, integrate instead:

\[
 E_i^{area}=\int_{A_j}L_{e,j}^{rgb}
 V(x_i,y)\frac{(n_i\cdot\omega)_+(n_y\cdot(-\omega))_+}{r_m^2}
 \,dA_m^2.\tag{J3}
\]

Use deterministic area quadrature with refinement tests. Tube normals vary
around the finite cylinder; a line with no radius is not finite emitting area.
Sun contributes \(q_\odot(n_i\cdot s)_+V_{sky}(x_i,s)\), where \(s\) points
toward the sun. Sky is a separate hemispherical radiance integral, optionally
with a low-order nonnegative basis; a broad sky term is not a guessed sun.

For known diffuse materials, predict \(L_i=\rho_i(E_i^{direct}+E_i^{indirect})/\pi
+L_{e,i}\). For irradiance-like lightmaps, compare an appropriate irradiance
multiplier instead. The selected **measurement operator**, not the same formula
for every RGB stream, determines which quantity the target represents.
Use geometric normals for sidedness/ray bias, shading normals only under a
declared receiver model. Texture glow supplies shape/colour priors, not known
incident illumination or automatic nits.

### 4.2 Objective and censored measurements

Define a source/bake operator \(H_s^\psi\) including the identified transfer,
gain, lightstyle, footprint/filtering and quantization history. Preserve ordering
of nonlinear operations; average forward lighting over the footprint before
applying the known bake/filter operator. Do not reapply sRGB to already decoded
HDR. Renderer presentation transforms are useful for appearance validation,
not a replacement for source-domain fitting.

For each available channel, decoding provides an interval \([l,u]\), not always
an exact value. Define a signed interval residual:

\[
 r(z;[l,u])=\begin{cases}z-l&z<l\\0&l\le z\le u\\z-u&z>u.\end{cases}
 \quad u=+\infty\text{ for a known saturated lower bound}.\tag{J4}
\]

The interval must come from the decode profile; unknown historical colour
normalization can make a simple monotone channel bound unsupported. RGB8=255
does not determine an upper intensity. Missing/non-finite channels have no
brightness residual; black has a quantization interval, not proof of no light.
For reliable bounds, apply a robust loss to \(r/\sigma\), with model uncertainty
in addition to quantization. Near-black samples need an absolute noise floor;
pure relative error would overweight noise there.

The proposed objective separates evidence:

\[
\begin{split}
 \mathcal J(\theta)={}&w_s\mathcal L_s+w_g\mathcal L_g+w_d\mathcal L_d
 +w_v\mathcal L_v+\lambda_q\mathcal R_{sources}\\
 &+\lambda_b\mathcal R_{indirect}+\lambda_c\mathcal R_{calibration}
 +\mathcal I_{geometry}+\mathcal R_{authored}.\tag{J5}
\end{split}
\]

| Term | Definition / constraints |
|---|---|
| Surface \(\mathcal L_s\) | Robust interval residuals between \(H_s^\psi[L,E]\) and source-decoded observations; weights proportional to covered world area, confidence and independent footprint support. Normalize by sampled area per stream/region. Do not count duplicated style/UV references or base/deluxe pairs as independent RGB energy. Vertex samples stay a distinct stream with their own footprint uncertainty. |
| Grid \(\mathcal L_g\) | Compare predicted incoming illumination through the supported compiler operator \(H_g^\psi\) to ambient/directed components and style records. No invented receiver normal; no assumption that grid and lightmap have equal absolute or relative scale. Unknown compiler profile can leave this term unavailable. |
| Direction \(\mathcal L_d\) | Cone/quantization-aware discrepancy of deluxe/grid directions from their predicted compiler moments, e.g. robust \(1-d\cdot\widehat M\). Only evaluate for significant \(\|M\|\); near cancellation has weak/unknown direction. Different directional datasets use different moment definitions and weights. |
| Visibility \(\mathcal L_v\) | Consistency of reliably identified direct shadow transitions and trace constraints; unknown alpha/transmission is masked or evaluated as explicit bounded scenarios. Geometry gates every direct-light prediction even when no independent shadow-edge term is available. |
| Sources \(\mathcal R_{sources}\) | Group sparsity over RGB amplitudes and an active-source complexity cost, plus shape/position priors from actual fixtures. Use normalized **response columns** or declared per-type flux/reference scales; summing cd+nits+lux in one L1 penalty is dimensionally wrong. |
| Indirect \(\mathcal R_{indirect}\) | Restricted low-frequency nonnegative residual, with energy/magnitude priors and smoothing only across certified transport-compatible space. Or a fixed-material multiple-bounce transport operator. No free independent residual at every texel. |
| Calibration \(\mathcal R_{calibration}\) | Bounded gains/transfer alternatives with declared reference and common domain. Fix a reference gain to remove scale gauge; unknown primaries/transfer cannot be solved by silently relabelling data. |
| Geometry \(\mathcal I_{geometry}\) | Hard feasible emitting geometry/free-space/mount constraints, nonzero dimensions and swept/trust-region collision checks. PVS is not optical visibility; open/no-hit is not a sky portal. Dynamic doors induce scenario uncertainty. |
| Authored \(\mathcal R_{authored}\) | Explicitly typed priors with provenance. Locked authored JSON remains locked; test an alternative hypothesis rather than silently changing its intensity or geometry. |

One form of the surface term is
\[
 \mathcal L_s=\frac1{\sum_i a_iw_i}\sum_i a_iw_i
 \sum_c\rho_\delta\left(\frac{r(\widehat y_{ic};[l_{ic},u_{ic}])}
 {\sigma_{ic}}\right),\quad
 \widehat y_i=H_s^\psi(\theta;x_i,\text{footprint}_i).\tag{J6}
\]

Treat the term weights as explicit experiment settings, not universal constants.
Fit supported static style channels separately or model known animation bases;
never add arbitrary animated lightstyles into one recovered static source.

### 4.3 Direct, bounce and environment must remain identifiable components

A bright doorway is an aperture/transport observation, not a physical emitter.
An indirect residual should not become a point/rectangle just because that
decreases surface error. Use two development modes:

1. **Diagnostic direct mode:** direct sources plus a deliberately limited,
   region-local smooth residual, separate sky and known ambient context. This
   mode identifies direct evidence and reports model discrepancy; it does not
   claim accurate GI or all-source recovery.
2. **Fixed-material transport mode:** deterministic diffuse transport or a
   converged light tracer, with the same unknown primary emitters. For fixed
   geometry/reflectance, transport remains linear in emitted RGB:
   \(L=(I-\mathcal K)^{-1}L_e\), where \(\mathcal K\) is the reflection/transport
   operator with energy-conserving materials. This schematic inverse requires
   convergent transport, not arbitrary inversion of a singular matrix.

A completely free indirect field explains every positive observation with zero
physical lights. Conversely, forcing indirect to zero turns bounce into fake
direct sources. Quantify this ambiguity with ablations, held-out regions and
constrained model complexity. Do not optimize albedo, indirect and intensity
freely together in the first implementation.

### 4.4 Visibility, bias and feasible source positions

The existing BVH is authoritative for supported static visual triangles;
collision/BSP solids help classify volume feasibility. Disagreement, unsupported
patch/material or moving brush geometry means uncertainty. Windows and grates
cannot be assumed opaque or clear merely from collision contents. Use material
scenario bounds or exclude affected evidence, preserving the reason.

Bias is a **query construction policy**, never a change to the stored luxel
position. Start from the documented geometric-normal offset/tolerance, check
sensitivity at half/double bias, and reject unstable samples. Source mounting
constraints must allow a real panel on a wall while preventing a point hidden
inside it. Checking proposal endpoints alone is insufficient if an optimizer
step crosses a wall; require a swept/segment feasibility check or bounded
component-local proposals. Unknown free-space classification is not “free”.

Keep visibility queries exact in the final accepted model. A smoothed shadow
surrogate may guide a continuation phase only if the final hard/finite-area
visibility objective is recomputed. Finite differences with a tiny step can also
miss a discontinuity; they are not an automatic cure for omitted boundary terms.

## 5. Observability and uncertainty

With fixed positions/materials/profile, write the forward approximation as

\[
 y\simeq g\,[A(p,Q,G)q+B b+e_{sky}],\qquad q,b\ge0.\tag{J7}
\]

The local Jacobian \(J=\partial\widehat y/\partial\theta\), appropriately scaled
and whitened by measurement uncertainty, diagnoses **local** rank loss. Small
singular values / nearly collinear response columns mean an unresolved
combination of parameters; a local Hessian is not proof of global uniqueness.
Inspect profile likelihoods or bounded perturbation ensembles as well.

| Ambiguity | Consequence / treatment |
|---|---|
| Global gain versus emission | \(gq=(ag)(q/a)\). Fix a reference; retain relative calibration. Scene presentation scale does not remove the missing physical measurement. |
| World/metre scale | For fixed world positions, J2 contains \(Iu^2/r_{world}^2\). Changing units-per-metre by \(a\) and intensity by \(1/a^2\) preserves the observation. Geometry units must be fixed before fitting. |
| Reflectance versus light | In diffuse reflection, \(\rho I\) is ambiguous without known reflectance. Do not infer irradiance by dividing dark/unknown textures. |
| Coincident / permuted sources | Two collocated identical sources have only their summed intensity observable. Source permutations give K! equivalent parameterizations. Use stable provenance IDs and evaluator matching, not arbitrary vector order. |
| Too few receivers / one plane | One sample cannot distinguish distance and intensity; even a plane can provide weak depth/orientation constraints for distant sources. Require geometrically diverse receivers and hold-out probes. |
| Occlusion | A source invisible to all available receivers has no measured intensity or position gradient. It is unsupported, not a confidently dark recovered source. |
| Far-field equivalence | Distant points, directional lights and small area fixtures can be indistinguishable over a local ROI. Preserve type/position alternatives and wide intervals. |
| Saturation / filtering | Saturation supplies a lower bound, not an upper scale. Coarse filtered charts erase source/shadow detail; increasing optimizer iterations cannot recover absent information. |
| Indirect / sky / broad direct | Each can explain smooth diffuse illumination. Constrain component capacity and seek directional/free-space evidence; do not manufacture sources from smooth residuals. |
| Artistic bake operations | Minimum light, negative lights, painted/normalized values or missing moving occluders may violate a nonnegative physical forward model. Report model discrepancy; do not issue negative physical intensity or certify a bad fit. |

Source position uncertainty, source existence, intensity uncertainty, transport
confidence and exact-light suitability are separate output quantities. A small
residual cannot certify all five. Disagreement across plausible decode/material/
door-state hypotheses must widen uncertainty, not be averaged into a fictitious
precise emitter. Bootstrapping adjacent luxels independently understates error;
resample spatial blocks/fixtures instead.

## 6. Candidate generation and an initial optimization algorithm

### 6.1 Candidates from evidence and residuals

Start with existing unified evidence, independently of LTC/fog/Forward+ switches:

- Authored JSON retains exact geometry/intensity and units. Preserved BSP light
  entities give legacy-unit priors, styles and target directions where present.
- Connected emissive masks and actual surface shape propose rectangles, finite
  tubes, large area emitters or point-like fixtures. Separate adjacent lamps;
  common colour is not evidence that they are one fixture.
- A spot needs directional fixture/target evidence; a single grid/deluxe vector
  cannot establish position plus cone. Test point/spot alternatives with a
  complexity penalty and held-out evidence.
- Shader sun declarations seed a separate directional hypothesis; sky materials
  seed environment support. Missing hints leave sun unknown, never the renderer's
  default sun vector. Estimate a solar direction only with sufficiently diverse
  exposure/shadow/directional evidence.
- In regions with unexplained **localized direct evidence**, propose bounded
  free-space points or fixture-relative positions. Use deterministic adaptive
  geometry coverage / coarse-to-fine residual sensitivity, not sparse-grid
  gradients promoted to “physical lights”. Do not propose a light at every bright
  indirect doorway or every high-RGB cell.

Group/deduplicate by stable surface/entity cross-references and geometric fixture
connectivity, preserving all evidence records. Keep alternative hypotheses for
ambiguous shapes. No global area-first `PROXY_MAX` budget. Use explicit memory/
work limits and response-column screening, with diagnostics if the budget stops
search. Candidate counts measure search coverage, not reconstruction success.

### 6.2 Linear amplitudes, constrained nonlinear geometry

For fixed geometry, visibility, material and transfer hypotheses, transport is
linear in nonnegative emitted RGB. Solve amplitudes and restricted indirect
coefficients with NNLS / group-sparse constrained least squares (or the matching
robust interval formulation), then update geometry:

\[
 q^*(p),b^*(p)=\arg\min_{q,b\ge0}\mathcal J(p,q,b),\qquad
 p^*=\arg\min_{p\in\mathcal F}\mathcal J(p,q^*(p),b^*(p)).\tag{J8}
\]

This is block optimization / variable projection, not a claim that NNLS is smooth
everywhere. Changing active sets, clipping intervals or visibility invalidates a
naive smooth reduced Hessian. Away from those events, envelope derivatives are
useful; near them use trust regions, retraced finite differences/multiscale
proposals and accept only the actual recomputed objective. Scale position,
angles, dimensions and amplitude blocks separately. A thesis step size of .1 is
not .1 metres or a safe universal learning rate.

For the smooth lit side of one point contribution, with
\(d=p-x\), \(r=\|d\|\), the analytic world-coordinate derivative is
\[
 \frac{\partial E}{\partial p}=Iu^2
 \left(\frac{n}{r^3}-\frac{3(n\cdot d)d}{r^5}\right).\tag{J9}
\]
This omits visibility-boundary, footprint and spot-profile derivatives and is
only a checked building block. A minimal block update, before adding robust
intervals/group penalties, is:

```text
r = A*q + B*b - y
q_trial = max(0, q - eta_q * transpose(A) * W * r)  # fixed geometry, quadratic case
p_trial = bounded_feasible_proposal(p - eta_p * scaled_geometry_gradient)
if swept_geometry_check(p, p_trial) succeeds:
    retrace(p_trial); refit_amplitudes(); accept_only_actual_loss_improvement()
```

Use a converged constrained amplitude solve in real experiments, not one
projected step mistaken for convergence. For censored/robust data, its gradient
uses the derivative of J4 and the robust influence function rather than the raw
quadratic residual. A failed feasibility or visibility query is an explicit
rejected/uncertain proposal, not a zero residual.

Use deterministic restarts and early coarse surface sampling. Reuse immutable
BVH/transport data, not a permanently resident observation×candidate matrix.
Stream tiles and accumulate normal equations/gradients or bounded sparse
response blocks; explicitly control ill-conditioning and numerical precision.
Do not form \(A^TA\) blindly when it squares a poor condition number: use
rank-revealing solves/preconditioning or matrix-free methods as appropriate.

After amplitude fitting, propose source birth/death, type alternatives and
fixture-aware split/merge. Select by validation improvement and a declared
complexity/MDL penalty, not training residual alone. Merge only when geometry/
response evidence supports it. Reassign stable IDs by provenance and explicit
revision mapping; never use an optimizer array index as source identity.

```text
dataset = open_validated_jkobs(limits)
admit_only_supported_static_observations(dataset)
for profile in declared_decode_and_material_hypotheses:
    geometry = immutable_visibility_and_feasibility(dataset)
    candidates = evidence_candidates_plus_bounded_geometry_proposals(dataset)
    for deterministic_restart in seeds:
        state = initialize_candidates(candidates, profile)
        repeat within work/cancellation limits:
            stream_train_tiles_and_build_response_operator(state, geometry)
            solve_nonnegative_amplitudes_and_restricted_indirect(state)
            propose_feasible_geometry_steps(state)
            retrace_and_accept_using_actual_objective(state)
            periodically_test_source_birth_death_on_validation_blocks(state)
        keep_hypothesis_and_uncertainty(state)  # no emitter publication yet
evaluate_spatial_holdout_without_reading_answer_file()
publish_only_validated_components_or_report_unavailable_or_ambiguous()
```

```text
for observation in bounded_tile:
    if missing_or_unsupported(observation): record_exclusion(); continue
    direct, sky, indirect = forward_components(observation, candidates, visibility)
    prediction = measurement_profile(direct, sky, indirect, observation.footprint)
    loss += area_weight * robust(interval_residual(prediction, decoded_bounds) / sigma)
# Grid and directional terms use their own operators, never the surface formula.
```

L-BFGS is suitable for a deterministic smooth local polish with stable active
sets; reset history after topology/visibility/model changes. Adam can help noisy
Monte Carlo refinement, but neither method fixes invisible sources or missing
boundary derivatives. Record **accepted and rejected** losses, validation,
component energy, rays and wall time, not only running minima.

## 7. Analytical versus differentiable transport

| Approach | Worth using for | Cost / limitations |
|---|---|---|
| Deterministic J2/J3 + BVH | Initial candidate screening, direct-light identifiability, unit and visibility debugging, fast amplitude solves | Material/bounce mismatch remains explicit; finite-area quadrature must converge; hard visibility is nonsmooth. Cannot call its residual field recovered GI. |
| Fixed-material diffuse transport / response basis | Bounce-aware amplitude fitting and coloured-wall tests | Precompute/storage can be large; use streamed/compressed response blocks and bounded basis. Wrong bake-time albedo biases primary sources. |
| View-independent adjoint light tracing [T,A] | Expensive global multi-bounce refinement, many parameters and broad surface objectives | Monte Carlo noise, source sampling, spatial target resolution, material uncertainty; [A]'s moving-shadow gradient omissions remain material. Requires a substantial verified implementation. |
| Differentiable path tracing with boundary treatment [E] | Cases where occlusion/silhouette motion is the dominant identifying signal, or final targeted refinement | More difficult sampling/derivatives, noise and compute; camera-only objectives omit invisible-to-camera surfaces unless a surface receiver strategy is implemented. Not a free automatic-differentiation wrapper. |

Start with the deterministic model because it exposes data/model errors cheaply.
Advance to diffuse transport after the synthetic direct tests establish units,
geometry and parameter recovery. Use full differentiable GI only when measured
residuals show transport complexity justifies it. Reuse common random numbers for
nearby stochastic proposal comparisons, but evaluate with independent validation
rays/seeds and report confidence intervals; a frozen noisy sample set can itself
be overfit. Full path tracing is not required merely to produce a cached light
list, and a low surface loss alone does not justify its cost.

## 8. Offline execution, caching and consumer independence

Normal game map loading should load a validated result, not execute this loop.
Offline runs may take minutes/hours with cancellation, progress and checkpoint
metadata. Checkpoints are optimizer state, not publishable assets. Identity must
include BSP and external image contents, material/shader interpretation, geometry
tessellation policy, authored hints, decoder/units/calibration, algorithm,
settings and seed. Include model version, exclusion/coverage report and validation
metrics. An incomplete fingerprint cannot create a valid cache hit. Existing
codec support is groundwork; complete production key plumbing remains work.

The immutable canonical result separates emitters, sun/sky, indirect fields,
visibility/region references and contributions. Persist uncertainty and unresolved
hypotheses. Geometry, amplitudes and diagnostics are shared CPU data; shadow
atlases, froxels, GPU handles and LTC/Forward+ resources stay in adapters.

Publication gates apply **per component and per intended use**. A calibrated
appearance-equivalent source is not a physically measured original source.
Relative results keep relative units/calibration. An empty valid asset, an
unfinished fit, or only emitter evidence must not be labelled reconstructed.
Failed validation returns unavailable/draft diagnostics, not approximate lights
silently promoted to exact lighting.

Consumers independently require master-use, their own flag, asset capability
and implemented feature availability. NPC/Ghoul2 access cannot depend on fog;
sun cannot enable Forward+; observation export/build does not enable rendering.
Surface matching does not certify incoming angular radiance for specular/fog.
World diffuse must preserve the original bake or explicitly separate/replace
its contributions; adding a full recovered solution over the unchanged bake
would double lighting. Direct shadowed sun and diffuse sky/bounce must not be
counted twice. These are future integration requirements, not implemented here.

## 9. Verification plan and measurable gates

The following are **proposed initial engineering gates**, not measured solver
results. Fix renderer/bake profile, sampling, exposure and spatial splits before
choosing thresholds; keep a held-out suite for the final decision.

### 9.1 Assumptions that require explicit synthetic verification

| Assumption / scene | Required experiment and provisional gate |
|---|---|
| Geometry and filtering | Planar/rotated/curved charts, seams, external dimensions, uncovered texels. Analytic fixtures: position error <1e-5 world units; approximate curves: report error relative to footprint, target <0.1 luxel. No padding, overlaps or movable surfaces admitted as exact samples. |
| Absolute physical units | Independent analytical point/spot/area fixtures with known metres, Y-normalized RGB and photometric units. Forward error <1e-6 relative away from boundaries; finite-area refinement changes <0.5%. This does not calibrate q3map's legacy source numbers. |
| One point / two coloured / overlapping white | Spatially diverse receivers. On well-conditioned direct fixtures, held-out linear NRMSE <3%, emitter position <5% of room span, intensity <10% **only when absolute calibration is known**. Use optimal source matching and report count precision/recall; permit declared non-identifiability for coincident lights. |
| Spot, panel and tube | Recover orientation (<5°), physical dimensions (<10%) and compare alternate source types; far-field type ambiguity must be reported. q3map emissive prisms are not ground-truth analytic cylinders. |
| Wall / doorway / closed door / skylight | No direct contribution through an opaque wall; CPU visibility must agree with independent reference. Hold out the occluded room. Door-state uncertainty cannot become certain permanent connectivity. No-hit alone does not recover sun. |
| Outdoor sun and sky | Separate angular direct and diffuse terms; known-profile sun angle <3° with diverse exposed/occluded normals. A sky-only scene must not gain a fabricated directional source. |
| Bounce and coloured materials | Compare no-bounce versus bounce using identical geometry/primary lights; direct-only model must report structured discrepancy. Additional point lights must not be the mechanism for declaring GI solved. |
| Raw / linear / renderer presentation | Repeat linear, assumed sRGB and unknown-transfer profiles, RGB8/HDR alternatives, saturated channels and overbright histories. Unknown transfer stays unavailable; renderer presentation agreement does not certify source-domain calibration. |
| Grid and deluxe operators | Single/multiple lights, opposing directions, all styles, solid/nudged cells; compare encoded outputs with the **identified compiler**, including rounding. Unknown JA stock compiler remains lower-confidence evidence, not a failing physical identity test. |
| Observability and robustness | Degenerate one-plane/one-sample/collocated lights, missing hints, bad albedo, weak sources. Confidence intervals should include known truth or report unresolved; never narrow merely because the residual is small. |
| Reproducibility / independence | Same content/profile/seed yields identical deterministic bytes or declared stochastic tolerances. Train/validation/held-out are spatially distinct, with a gap at least the filter footprint; answers never reach the solver. Consumer switches cannot affect the CPU fit. |

Define held-out error on valid uncensored linear channels as
\[
 \mathrm{NRMSE}=\sqrt{\frac{\sum_i a_i\|\widehat y_i-y_i\|_2^2}
 {\sum_i a_i\|y_i\|_2^2+\epsilon}},\tag{J10}
\]
with a declared dataset-level black-scene floor \(\epsilon\). Report censored
violations separately, not as exact RGB error. Also report p95 absolute/relative
error, shadow-boundary displacement, per-region energy/colour bias, missing-area
coverage, grid angular error, source-count errors and uncertainty calibration.
For real stock maps without source truth, report appearance and consistency;
do not invent position/intensity accuracy. Fixed camera/exposure image checks
are complementary presentation tests, not substitutes for global surface metrics.

### 9.2 Existing fixtures and evidence that can be reused

[`tools/rend2/datasets/`](../tools/rend2/datasets/README.md) already contains
11 self-contained analytical scenes and a q3map2 workflow covering point,
two-colour, overlapping whites, spot, panel, tube, sun/sky, skylight, doorway,
coloured bounce and wall leakage. The actual compiler suite adds a no-bounce
control: 12 BSPs. Ground-truth answer files stay outside solver inputs;
`BlindBenchmark` removes retained light entities/authored light answers while
keeping legitimate visible fixture geometry. The evaluator must also withhold
held-out observation RGB: partition tags inside an archive are not an access
control boundary.

[The existing compiler witness](rend2-q3map2-validation.md) records NetRadiant
`2.5.17n-git-d9338309e` (2022-06-28), bake flags, source hashes, 72 compiler-stage
invocations and SP exports of the unmodified 12 BSPs. The linear recipe uses
gamma 1, compensation 1, exposure 0, world-space deluxe mode 0; Rec.709 and common
grid/lightmap scale remain assumptions. Source values such as `light=300` and
surface-light 100/2000 are legacy tool units. These fixtures test compiler
consistency and geometry, **not photometric source-intensity recovery**.
Whole-BSP baking has documented nondeterminism; repeated exports of one fixed
BSP are the determinism boundary. Do not conceal it by normalizing observations.

Existing planar correspondence error up to `3.18e-14` world units is a fixture
witness, not an error bound for stock curved patches. Existing runtime renders
and exports also do not demonstrate a solver, since none exists.

### 9.3 Experiments executed for this research

The standard-library-only diagnostic in
[`tools/rend2/inverse_lighting_research/`](../tools/rend2/inverse_lighting_research/README.md)
tests target projection, inverse-square units/direction derivatives, ambiguity,
censoring and the original Q3 grid packing counterexample. It does not read maps,
recover multiple sources, write assets or implement a production optimizer.

```powershell
python -m unittest discover -s tools/rend2/inverse_lighting_research -p 'test_*.py' -v
python tools/rend2/inverse_lighting_research/math_checks.py --output build/inverse-lighting-research/math-report.json
```

Exact measured results and the CPU contract check are recorded in that tool's
README. No new q3map bake, source-fitting benchmark, GPU experiment or gameplay
visual comparison was performed for this research. SP/MP code and build lists
are unchanged; the earlier witnesses are explicitly distinguished from fresh
tests. The entire relevant thesis was read; the referenced optimizer was not run.

## 10. Implementation order and unresolved decisions

1. Freeze a small identified bake profile and blind direct-only fixtures. Audit
   source-domain decoding, footprints, visibility and units before optimization.
2. Implement a deterministic tiled direct forward operator and derivative/
   finite-difference checks; separate emitter shape models and source identity.
3. Add fixed-geometry RGB amplitude fitting, observability diagnostics and
   constrained position/shape updates; then type/count selection on validation.
4. Add supported deluxe/grid operators as supplementary terms after matching the
   compiler. Keep unsupported profiles unavailable.
5. Add fixed-material bounce/sky estimation and only then expensive adjoint GI,
   if held-out residuals warrant it. Compare against the cheap model by equal
   work/wall-time, not only best-case training loss.
6. Gate publication by calibration/uncertainty/consumer suitability, complete the
   content fingerprint plumbing, and integrate consumers independently later.

Unresolved: stock JA bake toolchain and transfer histories; reliable bake-time
albedo; incidence weighting of every deluxe mode; probe nudging reconstruction;
optical alpha/transmission/dynamic-door states; how much geometry/energy is absent
from the exported static model; acceptable model discrepancy for artistic
lighting; external physical calibration anchors; source-count thresholds and
confidence calibration. These are experiments and explicit output limitations,
not reasons to substitute heuristic physical lights for missing evidence.
