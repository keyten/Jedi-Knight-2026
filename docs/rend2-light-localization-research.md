# Identifiability, shadow constraints and emitter localization

Research date: 2026-10-26. Code baseline: `ff02fa5d4`.
This extends the [inverse-lighting design](rend2-inverse-lighting-research.md)
using the supplied [bake-model investigation](rend2-bake-model.md). It proposes
constraints and uncertainty reporting, with small synthetic demonstrations.
No production solver, renderer consumer or canonical schema is changed.

**A low RGB error is evidence of predictive agreement, not of a uniquely
localized physical lamp.** Geometry, transfer/attenuation profile, materials,
component separation and observation coverage determine what can be identified.
An inferred incoming direction can be correct even when no physical emitter's
position, type or existence is identifiable.

## 1. Evidence reviewed and what it establishes

The following are primary sources. Page numbers for author PDFs mean **PDF page**
unless explicitly marked printed. Hashes, versions, read sections and local
input hashes are in the [source manifest](../tools/rend2/light_localization_research/sources.json).
Source results and the proposed JA constraints below are distinguished; none of
the authors' optimizers was reproduced in this task.

| Reference | Reviewed method / limits and relevance |
|---|---|
| **[T]** Schwengerer, *Inverse Method for Baked Lighting*, TU Wien, 2023: [thesis](https://www.cg.tuwien.ac.at/research/publications/2023/Schwengerer_Mathias-2022-BGL/Schwengerer_Mathias-2022-BGL-Thesis.pdf) | Previously read in full; revisited printed pp. 15–16, 25–35, 38. Fixed point-light grids, target projection and adjoint fitting reproduce appearance but can place lights outside rooms/against surfaces. No original-source localization/confidence benchmark. Printed page +16 gives PDF page. |
| **[A]** Lipp et al., *View-Independent Adjoint Light Tracing for Lighting Design Optimization*, 2024: [versioned manuscript](https://arxiv.org/html/2310.02043v2) | §§2.2, 4.3–4.4, 6.4, 7: differentiated direct geometric factors with fixed surface hits; **moving-shadow boundary derivatives omitted**. Forward shadowing is not a correct boundary gradient. Figures 20–21 expose failures, including a light trapped in solids. |
| **[S03]** I. Sato, Y. Sato, K. Ikeuchi, *Illumination from Shadows*, TPAMI 25(3), 290–300, 2003: [author preprint](https://research.nii.ac.jp/~imarik/resources/papers/TPAMI-IlluminationFromShadows.pdf) | §§II–V, PDF pp. 1–7: visibility-weighted angular illumination, known geometry, calibrated image response; Eqs. 5–8 give a linear nonnegative radiance solve. **Distant illumination**, no strong caster/receiver interreflection; estimates directions/distribution, not finite lamp distance. Unknown textured reflectance cancellation needs an additional image without the occluder (Eqs. 10–11), absent from a single JA bake. Author preprint is numbered 0–10, so use PDF pages. |
| **[S01]** Same authors, *Stability Issues in Recovering Illumination Distribution from Brightness in Shadows*, CVPR 2001: [paper](https://research.nii.ac.jp/~imarik/resources/papers/CVPR01-StabilityIssue.pdf) | §§2–5, PDF pp. 2–7: response-matrix rank, conditioning, missing shadow regions and limited resolution. Eq. 3 is `A L=P`; Eq. 4 is `sigma_max/sigma_min`. Similar columns require coarser **angular illumination representation**; not proof that two actual lamps are one. §4.2 samples extended angular bins for fractional occlusion; §4.3 samples diverse shadow patterns. |
| **[Z18]** Zhang, Cohen, Curless, *Discovering Point Lights with Intensity Distance Fields*, CVPR 2018: [paper](https://grail.cs.washington.edu/projects/lightlocalization/lightlocalization.pdf), [supplement](https://grail.cs.washington.edu/projects/lightlocalization/lightlocalization_supplementary.pdf) | Main §§3–6 and supplement §§1–3: known geometry/BRDF, direct radiance, isotropic inverse-square points. Eqs. 3–8 derive intensity upper bounds and limiter/voting proposals. Refinement fits intensities then nonlinear positions; it can confuse nearby sources and generate occlusion-induced proposal peaks. Main Table 1 explicitly separates low-error fits from ground-truth recovery: noisy five-light cases have 84% low-error versus 38% ground-truth recovery. These are their constrained 2D tests, not JA results. |
| **[P20]** Santo, Waechter, Lin, Sugano, Matsushita, *Light Structure from Pin Motion: Geometric Point Light Source Calibration*, IJCV 128, 1889–1912, 2020: [full article](https://doi.org/10.1007/s11263-020-01312-3) | §3.1 Eqs. 1–4 derive known caster–shadow collinearity and a unified near/distant model. §4 Eq. 17 jointly refines light/caster parameters with robust estimation. Their moving calibrated target and known correspondences are additional measurements, not available automatically in BSPs. JA does supply metric 3D geometry, which removes some single-image projective ambiguities. |
| **[W01]** Weber, Cipolla, *A Practical Method for Estimation of Point Light-Sources*, BMVC 2001: [paper](https://bmva-archive.org.uk/bmvc/2001/papers/117/accepted_117.pdf) | §§3–6, PDF pp. 2–6: finite-distance shading model and Gauss–Newton with known homogeneous diffuse geometry/response; background can be cancelled with source-on/source-off images. JA normally supplies neither those images nor verified bake-time reflectance. |
| **[E18]** Li, Aittala, Durand, Lehtinen, *Differentiable Monte Carlo Ray Tracing through Edge Sampling*, 2018: [author project](https://cseweb.ucsd.edu/~tzli/diffrt/) | Visibility derivatives contain boundary distributions; sampling smooth paths alone misses them. Used for the interior/boundary distinction; no implementation or performance reproduction is claimed. |
| **[R09]** Raue et al., *Structural and practical identifiability analysis … by exploiting the profile likelihood*, 2009: [DOI](https://doi.org/10.1093/bioinformatics/btp358), [primary indexed abstract](https://pubmed.ncbi.nlm.nih.gov/19505944/) | General profile-likelihood motivation: distinguish structural parameter relationships from insufficient data. Only the indexed abstract was accessible here; the formulas and cautions in §8 below are our specified statistical design, not a claim to reproduce that paper's algorithm. |

[S03] includes diffuse reflections elsewhere in the environment as incoming
illumination while excluding strong interreflection involving the known caster
and receiver. This distinction matters: recovering an angular radiance lobe from
a reflected wall is not proof of a primary emitter at that direction. [Z18]'s
ideal direct-light subtraction assumes rich surface radiance/BRDF coverage; its
real-scene experiment instead uses an ambient approximation. JA RGB lightmaps
do not provide the complete outgoing directional light field needed to perform
that subtraction exactly.

## 2. Repository contracts and the new bake-model evidence

Responsibilities inspected:

- [`tr_lightrecon_types.h`](../shared/rd-rend2/tr_lightrecon_types.h): physical
  emitters, separate transport/environment, hypothesis groups, explicit units,
  calibration, positional sigma and independent emitter/transport evidence.
- [`lightrecon/surfaces.h`](../shared/rd-rend2/lightrecon/surfaces.h): original
  luxel identities/UVs, coverage, footprint/area, geometric versus shading normals,
  mapping error bounds and unchanged canonical positions.
- [`visibility.h`](../shared/rd-rend2/lightrecon/visibility.h) and
  [visibility contract](rend2-static-visibility.md): immutable BVH, structured
  `Visible/Blocked/Unknown`, optical policy, point-domain diagnostics, hit IDs,
  patch approximation, bias and errors. A successful query may still be unknown.
- [`regions.h`](../shared/rd-rend2/lightrecon/regions.h): certified free/solid/
  unknown cells, sampled topology and conditional dynamic boundaries. Cluster,
  adjacency, LOS, transport possibility and room semantics are different facts.
- [`evidence.h`](../shared/rd-rend2/lightrecon/evidence.h): source hints and
  provenance; geometric mask coverage is explicitly not physical confidence.
- [lightmap/grid/style decoding](rend2-observation-decoding.md),
  [classification](rend2-observation-classification.md),
  [offline datasets](rend2-offline-observation-datasets.md) and
  [ground-truth workflow](rend2-q3map2-validation.md).

The supplied bake-model result narrows what may safely become a localization
constraint:

1. Correct controlled point/linear kernels match bytes, but **five inspected
   stock maps remain `UnknownProfile`**. Do not select inverse square merely
   because a fit can hide a wrong law behind a brightness scale.
2. Coupled RGB-peak normalization, exposure, clipping, grid merging and source
   sampling make independent per-channel Gaussian measurements inappropriate
   in general. A saturated channel can invalidate absolute bounds for the other
   channels too. Fit the known coupled forward operator or retain uncertainty.
3. Grid records can be approximately merged, sampled at nudged positions and
   share one aggregate direction across styles. Compiler 255-step angle encoding
   and the observation API's 256-step/entity-table conventions are different
   stages. Neither is an exact ray to a particular lamp. Use raw bytes/profile
   uncertainty, not a universal angular sigma.
4. The multiple-sun comparison leaves **35 boundary texels / 105 RGB channels**
   unchecked within a two-world-unit band, with discrepancy up to 28 bytes.
   This is specifically the signal a shadow localizer would exploit. Interior
   byte agreement is **not** validation of boundary position. Full trace/sample/
   epsilon replay or an explicitly conservative boundary error model is required.
5. Full bounce, stock Raven behavior, historical materials and several optical/
   patch sampling paths remain unresolved. Such profile uncertainty must enter
   source uncertainty, not be converted into apparent positional precision.

The current `GridDirection.errorBoundRadians` is a decoder policy, not a complete
error model for unknown producer sampling, 255/256 convention differences and
lossy shared-record merging. A future localizer must replay a supported profile
or add its justified uncertainty; that field alone cannot certify a source cone.

Coordinates remain right-handed JA world, +Z up. Receivers look **toward** a
source; canonical spot/sun frame forward is photon propagation. Convert distances
to metres once using `worldUnitsPerMeter`. Point/spot use cd, area/tube nits,
sun normal-incidence lux **only under explicit calibration**; legacy bake-tool
numbers and raw RGB are not these units. `physicalToScene` is presentation,
not a measurement of original bake brightness.

## 3. What “identifiable” means

Let \(F_m(\theta,\eta)\) predict admitted observations for model/profile \(m\).
\(\theta\) contains source count, type, positions, frames, extents and amplitudes;
\(\eta\) contains nuisance transfer, gains, allowed indirect light, material and
geometry uncertainty. Observations are quantized/censored sets \(\mathcal Y\),
not necessarily exact linear RGB.

**Structural identifiability** asks whether different physically distinct
parameters can produce exactly the same ideal admissible observations, after
removing source permutation and declared scale gauges. **Practical
identifiability** asks whether the available finite-resolution, uncertain
observations distinguish them at useful tolerances. Optimizer convergence is
neither condition. Uniqueness is always conditional on model class, source-count
limits, geometry/material knowledge and coverage; a local full-rank Jacobian
does not prove global uniqueness.

A useful nonprobabilistic output is a set of explanations:
\[
 \Theta_{m,\epsilon}=\{(\theta,\eta)\in\mathcal F_m:
    D(F_m(\theta,\eta),\mathcal Y)\le D_{m,min}+\epsilon\}.\tag{L1}
\]
Here \(\epsilon\) is an explicitly justified error budget, not a confidence
percentage. Include separate validation criteria; do not change them after
seeing answers. If material/profile alternatives also pass, preserve their
union or multiple modes instead of averaging their source locations.

### 3.1 Ambiguity catalogue

| Ambiguity | Why it exists / what may disambiguate it | Required uncertain outcome |
|---|---|---|
| Two nearly identical emitters | Nearly parallel response columns; coincident sources of the same type have only summed RGB observable. Different occlusion signatures, diverse receiver baselines or distinct colours can help. | Correlated amplitudes and positions; count may remain unresolved. |
| One bright versus several dim | Colocated lights are exactly additive; separated ones can match after filtering, noise or restricted coverage. A sparsity prior chooses a compact explanation, not necessarily the original. | Competing complete configurations, including one-versus-many. |
| Distance versus intensity | One normal-incidence sample fixes only \(I/r^2\); small flat patches provide weak distance information. Spatial curvature, parallax or matched shadows can help. | Long position/intensity ridge, possibly no finite upper distance. |
| Broad spot versus point | If every observed direction lies inside a spot's flat inner cone, its axis/cone can change without any photometric effect. | Point/spot alternatives; unconstrained cone/axis until a cone edge or independent fixture cue is observed. |
| Lamp versus reflection/doorway | A bright transport boundary or mirror can reproduce a localized incoming lobe. A virtual reflection source can lie behind solids. | Transport-direction/indirect explanation without physical-emitter certification; explicit primary-light alternative if supported. |
| Sun versus distant local source | Over baseline B, parallax is approximately B/R, and distance-dependent variation may be below encoding error. Common direction is not proof of astronomical sun. | Direction constrained but inverse distance compatible with zero; sun/local alternatives and unknown finite position. |
| Illumination versus albedo | Exitant \(L=\rho E/\pi\) has a reflectance/lighting gauge. Texture colour need not equal bake-time albedo; chromatic bounce complicates ratios. | Joint scale/material uncertainty; do not divide raw lightmap RGB by texture RGB. |
| Invisible/off-map hypotheses | No illuminated admitted receiver means zero data sensitivity; missing exterior geometry prevents reliable occlusion/sky conclusions. | Source existence/intensity/position unsupported. A prior cannot turn invisibility into evidence of absence. |
| Direct versus smooth indirect/sky | Smooth lighting can be explained by several components. A per-texel free residual explains all data without sources. | Component attribution unresolved unless indirect capacity or transport is independently constrained. |
| Profile/geometry versus position | Wrong attenuation, missing occluders, bake nudges or patch precision can be compensated by moving sources. | Model discrepancy and between-profile positional alternatives, rather than tiny conditional error bars. |

World/metre scale, source intensity/colour normalization and global photometric
gain are separate gauges. Fix units and normalize chromaticity to canonical
Y=1. With unknown gain g, \(gI=(ag)(I/a)\); geometry/shadows can constrain
position while leaving physical intensity unknowable. More RGB channels do not
add three independent geometric constraints when they are all proportional to
one source colour.

Source-count recovery also needs a declared detection limit. Zero-intensity
sources add unidentifiable parameters, and arbitrarily weak extra sources can
stay within quantization/model error. Count the **detectable, supported** sources
under a stated search domain and minimum contribution, rather than claim an
exhaustive census of every originally authored lamp.

## 4. Constraints supplied by each JA observation

### 4.1 Gradients, ratios, normals and colour

For an isolated isotropic point in a lit, unoccluded region with a known physical
or explicitly relative inverse-square profile, define \(d=p-x\), \(r=\|d\|\):
\[
 E(x)=I\frac{n\cdot d}{r^3},\qquad
 \nabla_p E=I\left(\frac n{r^3}-\frac{3(n\cdot d)d}{r^5}\right).\tag{L2}
\]
Distances here are metres; use the corresponding units factor if differentiating
world coordinates. For constant planar n and tangent t,
\[
 \partial_{x,t}\log E=3\frac{d\cdot t}{r^2}.\tag{L3}
\]
The intensity cancels, so broad spatial gradients/curvature constrain source
projection and depth. For spatially varying normals an additional normal-
derivative term enters. For multiple sources the log gradient is an
illumination-weighted mixture, not a direction to a source. L2–L3 do not apply
unchanged to linear falloff, disabled cosine, spot cones, bake clipping,
near-field clamping or filtered shadows; differentiate the selected bake
operator instead.

Ratios \(E_i/E_k=a_i(p)/a_k(p)\) cancel an isolated source's amplitude only
when the streams share a known gain/profile and their material dependence is
correctly accounted for. Unknown reflectance adds \(\rho_i/\rho_k\). Ratios
near zero are unstable; use a joint likelihood/interval residual rather than
divide tiny RGB values.

For distant single-direction diffuse lighting, \(E_i=n_i^T v\) on lit normals,
with \(v=Is\). Three independent normals can constrain v under that model;
parallel normals cannot. Local-light position also requires spatial diversity.
Sample different wall/floor/ceiling normals and separated positions, not many
adjacent texels of one face as if they were independent geometric experiments.

Distinct source colours can separate overlapping contributions if the decode
and materials support linear additivity. A sharp same-material colour change
across a shadow can suggest the colour of a disappearing component. It cannot
prove source count under unknown colour bleeding, shared-peak compression or
animated styles. Mask material/chart/normal seams and compare alternative
explanations before treating any RGB gradient as a shadow.

### 4.2 Directional observations

Under the **additional** hypothesis of one dominant isolated source, a direction
measurement at x defines a cone around \(p=x+t d\), \(t>0\). Multiple nonparallel
cones can localize; nearly parallel cones yield large depth uncertainty.
The actual grid direction is a profile-specific weighted aggregate; deluxe can
include a normal bias and normalized mixture. Opposing contributions cancel,
and near-isotropic light has unstable direction. Do not feed these aggregate
vectors directly into geometric ray triangulation without testing the isolated-
source hypothesis and applying quantization/compression/profile uncertainty.

For sun-versus-local classification, a baseline B with angular uncertainty
\(\epsilon_\omega\) cannot resolve a radius R when \(B/R\ll\epsilon_\omega\).
Use direction plus inverse distance \(\kappa=1/R\); if an admissible interval
includes zero, finite source distance is unresolved. Sky intersections, shader
sun declarations and repeated hard-shadow directions strengthen a solar
hypothesis but do not substitute for calibration or prove its historical origin.

### 4.3 Response-rank and negative evidence

For fixed source shapes/positions and supported linear transport,
\(y=Aq+Bb\), with nonnegative source RGB q and restricted indirect b.
After whitening and nuisance projection, near-identical columns indicate
indistinguishable source contributions. For two unit response columns with
correlation c, their Gram eigenvalues are \(1\pm c\). Shadows can reduce c by
turning one source off at receivers where the other remains visible. Repeating
the same occlusion pattern does not add rank [S01, §§3–4].

[Z18, Eqs. 3–6] derives a useful **candidate screening bound**:
\[
 I_j\le\min_{i:a_{ij}>0}\frac{y_i}{a_{ij}}.\tag{L4}
\]
It requires additive nonnegative illumination, known response and an appropriate
linear data domain. For interval data use a justified finite upper observation
bound; a saturated channel with no upper bound supplies no such cap. If
\(a_{ij}=0\), that observation supplies no intensity constraint on j. Known
dark receivers can strongly limit an unoccluded bright candidate; unseen
receivers cannot. Indirect subtraction must include its uncertainty.

Do not compute L4 on raw peak-normalized RGB or declare a low IDF value a
confidence. A single outlier can dominate the minimum; robust quantile variants
are soft scores and lose the strict bound. [Z18, supplement §2] also shows false
voting peaks near occluders. Use sparse adaptive proposals and actual held-out
forward tests rather than allocate a huge global distance field or certify its
peaks as emitters.

## 5. Geometry of cast-shadow localization

### 5.1 Known caster point versus matched mesh edge

With a known 3D caster q and its corresponding receiver shadow point x, a point
source satisfies
\[
 p=x+t(q-x),\quad t>1,\qquad
 (q-x)\times(p-x)=0.\tag{L5}
\]
This is [P20, Eq. 1] with ray ordering made explicit. One correspondence gives
a line, **not a unique 3D position**. Two nonparallel, consistent lines can
intersect uniquely in the ideal exact single-source model. Nearly parallel or
noisy/skew lines need a conditioned fit and uncertainty, not a fictitious exact
intersection. The nearest caster must actually block the ray; another nearer
wall invalidates the correspondence.

An arbitrary occluder **edge** is weaker evidence than a known caster point.
For an edge with endpoints a,b and receiver boundary point x,
\[
 m=\operatorname{normalize}[(b-a)\times(x-a)],\qquad
 m^T(p-a)=0.\tag{L6}
\]
The source lies in a plane. A ray from x through p must meet the finite edge at
an allowed q and with x–q–p ordering; the correct incident faces must form a
silhouette for that source. Coplanarity alone does not establish any of these.
Three independent matched edge planes can constrain a point; several samples
from the same straight edge and planar shadow boundary often supply the **same
plane**, not independent constraints. Degenerate/parallel planes leave an
unbounded subspace. Curved silhouette correspondence depends on p and must be
updated, not frozen to a convenient triangle vertex.

In practice, boundary points are bands with mapping, filter, geometry and
profile error. Replace L6 with a residual/band such as
\(|m^T(p-a)|\le\epsilon_m(p)\), deriving the bound from those uncertainties.
Do not attach an arbitrary constant “high confidence” to all edge intersections.
If edge identity is unknown, enumerate plausible associations or use a robust
latent assignment/outlier model, retaining competing consistent groups.

### 5.2 Receiver geometry and source-distance sensitivity

For a floor z=0, point source \((l_x,l_y,H)\) and known caster
\((q_x,q_y,z)\), \(H>z>0\), [P20, §3.1] gives
\[
 s_{xy}=\frac{Hq_{xy}-z l_{xy}}{H-z}.\tag{L7}
\]
The direction, caster height and observed displacement constrain source location
independently of intensity. Noncoplanar receivers/occluder heights add different
baselines. When \(H\) is very large, projection approaches parallel rays and
depth becomes poorly determined. When \(H\approx z\), tiny geometry errors can
produce enormous shadow displacements; a numerically sharp edge is not a
well-calibrated depth constraint.

In the prototype's restricted vertical-axis example, q=(1,0,1), p=(0,0,H):
\[
 s=\frac H{H-1},\quad H=\frac s{s-1},\quad
 \frac{dH}{ds}=-\frac1{(s-1)^2}.\tag{L8}
\]
At H=4, a boundary uncertainty of 0.01 m yields roughly 0.09 m height uncertainty.
If the boundary interval reaches s=1, there is **no finite upper height bound**.
This is an informative unresolved result, not a failed optimizer to retry until
it emits a precise light.

### 5.3 Penumbrae, source extent and multiple lights

For a horizontal finite source with full width D at height H, a straight edge at
height z and a parallel floor, geometric full penumbra support has width
\[
 w=D\frac z{H-z}.\tag{L9}
\]
It constrains **extent-to-distance**, not either independently. With known
source height, support estimates extent; with two known caster heights and a
common uniform source, the two widths can separate them. Width means geometric
support between extremal source rays, not an arbitrary 10–90% RGB threshold.
Area orientation, radiance profile, finite occluder geometry, multiple sources,
nonparallel receivers and exact cosine/falloff weighting affect the observed
profile. The prototype tests the support geometry, not a full area-light fit.

Bake filtering, supersampling, jitter, luxel footprints and geometry approximation
can also blur an edge. They must be forward modeled jointly with source extent.
A Gaussian variance sum is valid only under an explicitly Gaussian approximation,
not as a universal subtraction of lightmap blur. Unknown PSF means an unresolved
extent interval. A truly tiny emitter plus a bake filter can be indistinguishable
from a larger emitter without independent filter information.

Several lights can cast several shifted hard shadows, appearing like one soft
edge after quantization/filtering. Distinct colours and occluder baselines help;
shared colours and sub-luxel separations may not. Full-dark versus partial-dark
labels describe aggregate light, not per-source visibility. Under multiple
sources, L5/L6 belong to an assigned component or a likelihood over assignments;
one apparent boundary cannot be assigned to every source simultaneously.

### 5.4 Detecting usable shadow evidence in JA

Use source observations, not rendered screenshots or the GPU atlas. Require a
mapped, static, same-style receiving chart; identify seams, padding, folds,
normals, material boundaries, alpha/transmission and uncertain patch regions.
Generate candidate boundaries from local encoded/decoded contrasts under the
selected profile. Compare a smooth local-background-plus-shadow model with
material/style/transport alternatives. Use known geometry to propose plausible
occluder edges, then require forward visibility and the predicted lit side.

A black texel is not a shadow label, and glow is not incident light. A doorway
may frame a source or an indirect transport lobe. Do not convert it into a
portal emitter. If bake visibility differs from the static visual mesh, keep
the mismatch/profile uncertainty rather than optimize an implausible source
against the wrong shadow. Until the documented boundary-band discrepancy is
resolved, shadow residuals for those profiles must remain soft/uncertain.

## 6. Discontinuous visibility and optimization

For a point sample, binary visibility V is piecewise constant in p and jumps at
tangency. Its derivative is zero almost everywhere and undefined at the jump.
A renderer can trace the correct shadow while an optimizer receives no direction
to move across it. Finite-difference steps smaller than the boundary distance
also give zero; steps crossing it give a step-size-dependent quotient.
This explains why choosing Adam instead of L-BFGS is not a mathematical fix.

For an integrated receiver footprint, the correct derivative has a boundary
term. With visible side \(g(x,p)>0\), smooth contribution a, and footprint D,
\[
 F(p)=\int_D a(x,p)\,\mathsf H(g(x,p))\,dx,
\]
\[
 \partial_pF=\int_{D\cap\{g>0\}}\partial_pa\,dx+
 \int_{D\cap\{g=0\}}a\frac{\partial_pg}{\|\nabla_Dg\|}\,d\ell.\tag{L10}
\]
Here \(\nabla_D\) is the receiver-surface gradient and \(d\ell\) the boundary
curve measure. This assumes a regular boundary branch and differentiable integrands; more
general topology/silhouette events require corresponding boundary treatment.
The second term is lost by freezing visibility or hits. [E18] addresses this
class with boundary sampling; [A] explicitly omits moving-shadow terms. A finite
fixed set of footprint samples reintroduces discrete jumps and is not the same
as analytically differentiating the footprint integral.

| Technique | Appropriate role / limitation |
|---|---|
| Smooth analytic gradients / L-BFGS or LM | Photometry and geometry within a stable visibility/correspondence branch, with scaled parameters and trust regions. Reset history at model/active-set changes; accept on retraced actual objective. |
| Matched-edge reprojection residual | Gives continuous geometry information within an association even when intensity samples are flat. Breaks at silhouette/association changes; use bands and robust alternatives. |
| Multiscale finite differences | Diagnostic and bounded local proposals with full retracing. Check step-size stability; a small step misses edges and a huge step can cross walls or jump to a different source. |
| Adam | Can tolerate stochastic smooth transport and sometimes escape a bad basin through momentum. No correctness/feasibility guarantee; all proposed motion still needs geometry checks. |
| Stochastic restarts/search | Can cross visibility basins; bounded, seeded, explicit work limits. The frequency of optimizer convergence is not a probability of source existence. |
| Discrete visibility cells / edge-association search | Tangency planes partition source space for planar occluders/point receivers; constrained amplitude solves can compare cells. Combinatorics require room/fixture proposals and pruning, not global exhaustive subdivision. |
| Soft visibility continuation | A proposal surrogate only. Use known finite-source/footprint integration when possible; arbitrary smoothing cannot become final leaking light. Re-evaluate hard/physical visibility and uncertainty. |
| Boundary-aware differentiable transport | Later refinement when shadow movement is central and the data warrant it. Requires validated sampling/model; adding automatic differentiation to existing traces is insufficient. |

Do not use the same inferred RGB edge twice as statistically independent data
in both a photometric likelihood and an edge likelihood. Use edge constraints
for initialization with final RGB validation, or model their dependence / use
disjoint observations. Likewise, several boundary samples within one filter
footprint are correlated.

## 7. Actionable source-placement strategy

### 7.1 Hard constraints versus priors

Hard rejection is justified for non-finite/degenerate geometry, an emitting
domain certified inside opaque solid under the chosen model, inconsistent
source/caster/receiver ordering, or exact violated measurements under verified
bounded errors. Most JA evidence is conditional, so identify which assumption
makes a rejection hard. `Unknown` is not clear or solid. PVS membership and
region proximity are neither optical proof nor a solid-volume certificate.

Soft priors can prefer plausible ceiling/fixture volumes, conservative room
membership, emitter alignment, simpler source sets and stable authored hints.
They must not prohibit all emitters without visible glow geometry: hidden bulbs,
recesses, omitted source entities and exterior sources are normal possibilities.
A hard search-domain cutoff is a computational limit, not evidence that sources
outside it do not exist. Sun has no finite map position. Off-map local illumination
must be supported/uncertain through actual boundary geometry, not rejected merely
because p lies outside the world AABB.

Use separate feasible emitting-domain models:

- **Free-space point/spot:** source centre in certified free volume, finite
  emitting radius/clearance consistent with fixture dimensions, and a path-safe
  trust region. Near-surface hotspots do not justify shrinking radius to zero.
- **Mounted rectangle/tube:** luminous geometry tied to the observed fixture,
  allowed to touch its support surface. Check emitting side/support and valid
  dimensions; do not impose an isotropic centre clearance that excludes every
  real wall panel. Canonical source geometry remains physically meaningful.
- **Recessed source:** connected free cavity/aperture with explicit occlusion
  and a fixture/beam prior. Do not move a point through a wall to make it visible;
  if the cavity is not represented, report unsupported geometry.
- **Exterior/directional:** boundary/sky evidence and geometry coverage assessed
  separately. A ray with no retained triangle does not prove sky or an exterior
  source's visibility.

A clearance penalty such as
\(\lambda\,[\max(0,r_{emit}+\epsilon-d_{solid}(p))]^2\) is a **soft prior**
only when a reliable signed distance exists. The current BVH does not expose a
general signed distance; do not pretend a nearest triangle or region AABB is
one. Use the injected point classifier plus conservative box/geometry tests or
a future validated SDF. A swept emitting shape, not just two legal endpoints,
must remain feasible during an update. Exact authored geometry stays locked
unless an explicit alternative hypothesis is being evaluated.

### 7.2 Initialization and refinement

1. Validate dataset/profile/coverage and create immutable visibility/domain
   snapshots. Keep unexplained regions/unsupported materials visible in reports.
2. Seed all eligible authored and geometric evidence, preserving provenance and
   type alternatives; retain separate sun and sky hypotheses. No area-first
   proxy budget and no physical certification from mask brightness.
3. Propose matched caster lines/edge planes from stable direct-shadow evidence.
   Intersect well-conditioned independent constraints within feasible domains;
   use bounded ray/cone intersections only for justified isolated directional
   observations. Reject wrong lit-side/ordering/occlusion and retain ambiguous
   associations as alternatives. Use robust deterministic subsets/consensus;
   a consensus score is not a calibrated probability.
4. Cover unexplained plausible free space adaptively, including hidden/recessed
   fixture volumes. Use multi-surface gradient/IDF-like bounds for screening only
   under a supported profile. Avoid allocating a map-wide dense source grid or
   treating topology gaps as certainties.
5. Fit RGB amplitudes and restricted indirect/sky nuisance terms for each fixed
   geometry hypothesis, then update positions/shape with geometry-safe trust
   regions and actual retraced residuals. Candidate count/type/edge association
   changes are discrete moves. A weak source plus a free residual must not create
   a falsely precise location.
6. Split/merge/prune only with validation and an explicit model-complexity prior.
   If one and two lamps have indistinguishable evidence, preserve that ambiguity;
   sparsity selecting one does not prove only one existed.
7. Profile/local-rank-check the finalists, evaluate held-out regions and model
   perturbations, and publish only supported component properties. No candidate-
   only/empty result or fit to indirect transport becomes a reconstructed map.

## 8. Precision, uncertainty and alternative configurations

### 8.1 Ray bias and geometry error are part of the experiment

Keep the original mapped position. `RayBias` defaults include absolute/relative
tolerance with a 0.05-world-unit cap and optional footprint cap; the luxel-origin
helper is a separate offset policy. Neither is a bake sample-position oracle.
Test half/default/double bias, local geometry perturbations and patch refinement;
mark evidence unstable if its source estimate shifts beyond the declared error.
Use geometric normals for sidedness/offset, not an unknown smoothed normal map.
Target-surface suppression must be limited to that ID and endpoint tolerance,
not skip an entire wall or turn a recessed fixture into unoccluded light.

For L7 with receiver plane shifted upward by \(\delta z_r\), its shadow moves
by \(-a\,\delta z_r/(H-z)\) in the axis example. A 0.03 m unintended receiver
shift at H=4,z=1,a=1 moves the boundary by 0.01 m, enough to induce roughly
0.09 m apparent height error. This toy uses metres, **not the API's default
0.05 world units**. Geometry/sample offsets cannot be tuned to hide residuals.

Use double arithmetic for optimization and local coordinates where useful, but
input float geometry has already lost information. Around world coordinate
100000, float32 cannot distinguish a 0.001-unit displacement; double fitting
cannot recover it. Propagate mapping bounds, patch chord bounds, input precision,
sample-nudge and boundary-filter uncertainty. Do not shrink source error bars
below those limits because an internal linear solve has a tiny residual.

### 8.2 Data information, nuisance parameters and confidence

For an explicitly specified differentiable observation/noise model, whiten the
Jacobian and separate parameters of interest p from nuisance \(\eta\):
\[
 \widetilde J=\Sigma_y^{-1/2}J,\quad
 P_\eta=\widetilde J_\eta\widetilde J_\eta^+,\quad
 \mathcal I_{p|\eta}=\widetilde J_p^T(I-P_\eta)\widetilde J_p.\tag{L11}
\]
Scale parameter blocks explicitly before conditioning diagnostics. With regular
Gaussian noise, sufficient local rank and a correct model, inverse information
approximates local covariance. Profiling nuisance gains/albedo/indirect generally
removes apparent precision. Prior curvature must be reported separately from
data information. Quantization bounds and epistemic errors are not automatically
Gaussian \(\Sigma_y\).

If information is singular, its pseudoinverse's zero in the null direction is
**not zero uncertainty**: retain the nullspace/unbounded directions. A local
ellipsoid also cannot represent distant modes, source permutation or alternative
counts. Use a profile objective for each parameter/function,
\[
 D_k(t)=\min_{\theta,\eta:\theta_k=t}D(F_m(\theta,\eta),\mathcal Y),\tag{L12}
\]
and check other starts/visibility branches. Likelihood-ratio thresholds require
a declared stochastic model and regularity; do not label a handpicked loss delta
“95% confidence”. For bounded but nonstatistical errors, report projected
feasible intervals/sets from L1 instead. Profile searches can miss modes; record
their explored domain/work limit.

Bootstrap uncertainty should resample independent **spatial blocks/occluders**
and perturb validated noise/model assumptions. Resampling neighboring luxels as
independent measurements fabricates precision. Report conditional within-profile
uncertainty and between-profile/geometry alternatives separately. Expected
coverage must be checked against synthetic truth over many fixed-seed repeats
before putting calibrated probabilities in production metadata.

### 8.3 Representation recommendations

| Property | Recommended representation / expected underconstraint |
|---|---|
| Position | Conditional 3×3 covariance only in a regular local mode, plus bounded region/cone/ray or ensemble of modes. Long/unbounded depth directions remain explicit. Current `Position.sigmaWorld` is isotropic and insufficient; keep it unknown when misleading, and use a future versioned diagnostic dataset for anisotropy/nullspaces. |
| Intensity | Profile interval jointly with gain, colour and indirect; relative versus calibrated units retained. Saturation can leave a one-sided lower bound without an upper bound. Canonical `Estimate` requires finite stored payloads; do not serialize infinity or an invented huge upper endpoint. Represent missing bounds explicitly in a future uncertainty payload. |
| Angular axis / cone / extent | Tangent-plane/angular credible or feasible regions per type; intervals for cone/extent. If all receivers lie in a spot's plateau, axis/cone remain broad/unknown. Penumbra/filter ambiguity stays correlated with source distance/size. |
| Type | Scores or probabilities over point/spot/area/tube/directional/transport hypotheses, conditional on profile/prior and compared data. Do not average an area and a point into fake geometry. |
| Existence / count | Explicit null-source / one / multiple configurations with held-out evidence. Bootstrap selection frequency measures algorithm stability, not posterior existence probability. Bayesian model weights require proper priors, likelihood and coverage of alternatives. |
| Transport reliability | Geometry/material/profile/model-discrepancy records independent of source existence. An accurate transport direction can coexist with completely unknown emitter position. |

At intensity zero the source position/type are unidentifiable, and source-count
tests are nonregular mixture/boundary problems. Standard fixed-degree chi-square
likelihood-ratio rules or BIC cannot automatically certify existence. Validate
null-versus-source tests with synthetic null simulations/parametric bootstrap
under declared noise and model-discrepancy assumptions, plus held-out prediction
and prior sensitivity. No product of “direction confidence × fit quality ×
fixture score” is a probability without an independently validated model.

Canonical `HypothesisGroup` supports individual mutually exclusive source
alternatives. A **one-lamp versus two-lamp configuration** needs correlated
membership across several sources and a null option; do not misuse per-source
weights as if each source were independently selectable. Recommend a future
versioned configuration-level hypothesis/diagnostic dataset containing member
source IDs, exclusions, provenance, fit/validation scores, uncertainty and
conditional weights. Preserve candidate lineage IDs across hypotheses/rebuilds.
Incomplete alternatives do not imply weights sum to one. Shared consumers must
not render every competing explanation at once.

## 9. Held-out tests and expected uncertainty

Partition by spatial blocks and leave a guard band at least as large as the bake
filter/footprint support. Also hold out whole receiver orientations, occluders,
fixture neighborhoods and, where possible, rooms. A random split of adjacent
luxels permits interpolation leakage. Keep answers, original light entities and
source parameters out of blind solver input; visible fixture geometry remains
legitimate evidence in the observed-prior protocol.

Evaluate:

- Predicted shadow placement on **held-out geometry/occluder correspondences**,
  and both lit and dark sides with the correct source/profile. A source fitted
  to one hotspot should not invent shadows elsewhere.
- Encoded/linear residuals in their declared domains, with censored channels
  and missing coverage separate. Report direct, sky and indirect attribution;
  no arbitrary rescaling to rescue wrong falloff.
- Source position/count/type against private synthetic truth using permutation-
  invariant matching. Report sums for unresolved clusters and distinguish
  “fits observations” from “recovers original sources”.
- Stability under sampler resolution, half/double bias, geometry/profile/material
  alternatives and blind-versus-hint initialization. Stable optimization under
  one wrong profile is not accuracy.
- Uncertainty coverage and false-positive source rate on null/indirect-only
  scenes. Narrow intervals excluding truth are a failure even with low RGB error.

Extra held-out observations cannot distinguish **exactly equivalent** source
configurations that predict all accessible data identically. They can reject
overfit explanations, not establish impossible uniqueness. Prefer an unresolved
equivalence class to a fictitiously exact light.

### 9.1 Synthetic scene specifications

The existing [dataset scenes](../tools/rend2/datasets/README.md) supply geometry,
point/spot/panel/tube/sun/sky, doorway, bounce and wall tests. Add the following
controlled variations; these are **test specifications**, not newly baked BSPs:

| Scene / change | Observation and expected inference / uncertainty |
|---|---|
| Coincident equal-colour pair versus one | Identical summed direct responses under all receivers. Count remains unresolved even with shadows; estimate total amplitude, not separate exact intensities. |
| Nearly coincident separated pair | Sweep separation versus luxel/filter scale; one and two lights become distinguishable only when held-out spatial/occlusion evidence exceeds uncertainty. Position intervals must widen below resolution. |
| Small flat patch / height-amplitude family | Three RGB8 samples can agree across a wide source-height range. Add a known wall edge: depth interval shrinks but stays finite-width; distant-limit case stays upper-unbounded. Implemented below. |
| Same pair with independent normals/baselines | Add walls/ceiling progressively; data Jacobian/profile should gain information only from independent observations, not repeated copies. Compare known and unknown albedo. |
| Symmetric points, one finite blocker | Without blocker the intensity columns coincide on selected receivers; asymmetric shadows separate them. Implemented ideal two-amplitude case. |
| Point versus broad spot | All probes inside plateau: type/axis/cone unresolved. Add off-axis or cone-edge receivers: wrong point predicts measurable light there. Include compiler cone profile mismatch. |
| Reflection / lit doorway, no local emitter | Fit known bounce or mirror/transport alternatives; no physical source should be certified merely at the transport lobe. Direct-only model must report discrepancy. |
| Far point versus sun | Sweep R/B and direction quantization; inverse-distance interval includes zero below parallax resolution. A finite location must not be invented. |
| Texture/albedo edge versus cast shadow | Same RGB contrast with/without a geometric occluder; receiver material/UV seams must not create matched shadow constraints. Include unknown transfer and coloured bounce. |
| Invisible source / missing exterior | Source affects no admitted receiver or lies behind unsupported geometry. Existence remains unknown; no confident zero or precise outside location. |
| Known pins / mesh edges | One caster line unbounded, two nonparallel known-point rays localize; one edge plane or repeated edge remains underconstrained; three independent planes localize. Perturb associations/add outliers and check rejection/broadening. Implemented exact cases. |
| Area extent versus distance/filter | One penumbra admits multiple (D,H) combinations; two caster heights recover ideal support geometry. Unknown PSF or multiple lights reopens ambiguity. Implemented geometric widths, not radiance inversion. |
| Recessed / wall panel | Mounted emitting face is legal at support; free-space point inside solid rejected; omitted cavity remains unsupported rather than relocating a lamp through a wall. |
| Offset / precision / thin wall | Vary ray bias, large coordinates, patch chord error and wall thickness. Report unstable/unknown constraints before asserting sub-geometry position precision. Offset/float32 demonstrations implemented. |
| Saturated/clipped styles/grid | Coupled peak normalization, record merging, shared directions, animated style. Intervals broaden / unavailable evidence stays unavailable; no false exact intensity or grid triangulation. |

Provisional numerical gates: exact synthetic collinearity/plane examples below
should recover within `1e-11 m`; perturbations must be reflected in feasible sets,
not maintain that tolerance artificially. For noisy identifiable scenes, specify
95% interval coverage only after at least 200 independent fixed-seed scene/noise
realizations and a binomial coverage report. Select acceptable false-positive
rate and localization tolerance before tuning. Those statistical experiments,
new compiled-map shadow bands and stock-map localization are **not executed here**.

## 10. Executed prototypes and results

The dependency-free [prototype and commands](../tools/rend2/light_localization_research/README.md)
use ideal inverse-square emitters, known synthetic geometry and explicitly
provided correspondences. Units are metres, not inferred JA units. Its nearest-
half-up RGB8 quantizer is declared **not q3map `ColorToBytes`**. There is no asset
writer, candidate extraction from real maps, general optimization or renderer API.

### A. Quantized intensity ambiguity reduced by a shadow

True source `(0,0,4)`, I=`(128/255)*16` cd; floor receivers at
`(-.05,0,0)`, `(0,0,0)`, `(.05,0,0)`. Over H=2…8 m at 0.02 m spacing, choose
\(I(H)=(128/255)H^2\). **All 301 hypotheses** produce the same `[128,128,128]`
scalar/grey RGB8 observations. The small patch cannot resolve the distance.
The target lies at a quantization-bin centre, avoiding a floating-point boundary
at exactly half a code.

An opaque vertical wall at x=1, height 1, creates boundary `s=4/3`.
With measured boundary tolerance ±0.01 m:

- Only **9** of those 301 sampled heights remain.
- The continuous admissible interval is **[3.9126213592, 4.0927835052] m**;
  the discrete survivor list must not masquerade as exact uncertainty.
- Held-out floor x=`[1.2,1.4,1.6]` has blocked masks `[T,T,T]` at H=2,
  `[T,F,F]` at H=4 and `[F,F,F]` at H=8, independently checked by segment/wall
  intersection rather than the boundary formula.
- A boundary band `1.005±.01` gives H≥67.6666667 m with **no upper bound**.

The source's vertical axis is a known fixture prior in this experiment. Removing
that prior restores additional lateral freedom: one measured edge alone does
not determine a 3D lamp. Shadow constraints provide geometric evidence; they
do not solve absolute intensity calibration.

### B. Known correspondences and two-source separation

For truth `(0.5,-0.25,4)` m, two known caster–shadow lines recover it with
Euclidean error **2.70e-15 m**; three independent matched edge planes recover
with **1.99e-15 m**. These are floating-point arithmetic checks on exact supplied
correspondences, not realistic localization accuracy. One line, parallel lines,
or the same edge plane repeated three times is rejected as underdetermined.

Two sources `(-1,0,3)` and `(1,0,3)` have identical intensity response columns
at floor receivers `(0,0,0)` and `(0,2,0)`. A finite wall at x=.5, z=0…2,
|y|≤.5 blocks only one source at one receiver. The matrix becomes full rank
and recovers the supplied **2 and 5 cd** amplitudes. Positions are fixed in
this test; it demonstrates contribution separation, not two-source discovery.

### C. Penumbra and precision counterexamples

A .6 m wide source at H=4 gives support widths .2 and .6 m for caster heights
1 and 2 m. Their pair recovers `(H,D)=(4,.6)` algebraically. The single .2 m
width is also explained by H=7,D=1.2. The distant-limit width ratio returns
unavailable, not an enormous precise height. A receiver-plane shift and float32
example demonstrate why bias/geometry errors bound usable localization precision.

### D. A missing visibility-boundary derivative

For the same wall, integrate just the visibility factor over floor interval
`[1.3,1.4]`. At H=4 the normalized integral is `2/3`, and its analytic derivative
with respect to H is `1.1111111111 / m`. Central differences of the **exact
integral** agree. Replacing it by five frozen point samples produces derivative
**zero** at the same step `1e-5 m`. Thus even adding footprint samples does not
by itself implement the boundary term in L10. This experiment isolates visibility,
not the separate changing falloff of full irradiance.

**33 tests passed.** Repeated reports are deterministic on the recorded
interpreter; cross-platform floating-point tests use tolerances. The previous
25 inverse-lighting research checks and seven bake-model numerical tests were
also run; exact results/commands are in the prototype README. The supplied
22-BSP bake witness was read, not rerun or extended. No new game/GL validation,
SP/MP rebuild, full solver run or statistical coverage experiment was performed.

## 11. Implementation recommendation and remaining risks

Begin with supported bake/profile hypotheses, leased offline observations and
immutable geometry. Combine fixture priors with independent shadow planes/cones,
multi-surface photometry and restricted indirect/sky models. Fit amplitudes
inside each geometry branch, explore competing placements/types/counts, then
test independent receiver/occluder predictions. Compute data information and
profile/feasible uncertainty only after nuisance parameters and model mismatch
are included. Publish conditional alternatives and unavailable properties rather
than a single overconfident source set.

The strongest localization signal is **independent known geometric baselines
with differing visibility**, not the number of RGB texels. The hardest remaining
risks are unknown stock bake/shadow sampling, automatic occluder–shadow
association, indirect/material separation, alpha/window optics and incomplete
geometry. A validated angular transport field can be useful even when these
prevent physical emitter recovery. Consumers must continue reading the shared
asset independently and require their own capability/suitability; none is
implemented or enabled by this research.

[X04 shadow-footprint validation](rend2-shadow-footprints-x04.md) now tests the
moving-boundary term against independent exact solid-angle derivatives, retains
fixed-sample/frozen-visibility failures, and adds scoped compiler trace replay.
Finite-source quadrature derivatives and general real shadow correspondence
remain explicitly unverified.
