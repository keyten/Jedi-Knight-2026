# Physically constrained indirect lighting for baked JA maps

The subsequent [S10 implementation](rend2-indirect-fitting-s10.md) implements
the selected passive one-bounce CPU stage and a separate bounded unresolved
appearance fallback. This document preserves the original research evidence;
its compiler controls do not certify physical radiosity or a stock Raven profile.

The subsequent [DDGI field comparison](rend2-ddgi-field-research.md) examines
probe placement and distance moments for future field generation. It keeps
approximate interpolation weights separate from exact inverse-fitting visibility.

Research and experiments: 2026-10-27; repository baseline `7840feb44`,
`rend2-unified-wip`. This is an engineering decision and bounded CPU research,
not a production inverse solver. No renderer, consumer, cvar, shader, canonical
asset schema or map-load implementation changes in this work.

**Decision:** start with **one passive diffuse bounce**, represented by sparse,
visibility-aware patch transfer, with fixed or bounded bake-reflectance hypotheses.
Keep direct local emitters, direct sun, sky, legacy ambient and model discrepancy
separate. Add a second bounce only after an explicit validation gate. For an
unknown bake profile or unsupported optical/material paths, publish conditional
bounds or **unresolved transport**, not recovered physical GI or new point lights.

## 1. Existing evidence and primary sources

This extends the existing [inverse-lighting design](rend2-inverse-lighting-research.md),
[bake-model specification](rend2-bake-model.md),
[localization research](rend2-light-localization-research.md),
[q3map2 validation](rend2-q3map2-validation.md) and
[X04 footprint work](rend2-shadow-footprints-x04.md). It does not repeat their compiler
history, source-position optimization or geometry capture implementation.

The full mathematical sections, implementation, relevant results and limitations
of the following papers were read. Page numbers below are **PDF pages**, except
where marked printed. Download versions/hashes are pinned in
[sources.json](../tools/rend2/inverse_gi/sources.json); PDFs remain outside Git.
None of the authors' optimization systems was installed or reproduced.

| Source | Demonstrated method and relevant limits |
|---|---|
| [Azinović et al., CVPR 2019, Inverse Path Tracing](https://arxiv.org/pdf/1903.07145) | §3, pp.3–4, Eqs.1–3: path-integral image prediction and L1 fitting; independently sampled colour/gradient estimates avoid product correlation. §4, pp.4–5: known geometry, object materials/emission, emission sparsity, Adam. Figs.9–11, pp.7–8: sampling/convergence and missing-emitter-geometry failures. This is not evidence that unknown JA lamps can be localized from RGB alone. |
| [Wu et al., ICCV 2023, Factorized Inverse Path Tracing](https://arxiv.org/pdf/2304.05669v2) | pp.3–5, Eqs.3–11 factor reflected radiance as `kd*Ld + ks*Ls0 + Ls1`, cache observed outgoing radiance, and trace specular paths to diffuse/cache endpoints. Eqs.12–17 separate an emission mask from BRDF fitting. Emitter extraction assumes nonreflecting emitters. Table3, p.7 reports 44 min average training on a 3090Ti. §6/Fig.14, p.9: geometry errors, unobserved dominant sun, dark occluded materials and transparency limit recovery. These HDR/multi-view assumptions do not hold for ordinary JA lightmaps. |
| [Yu et al., SIGGRAPH 1999, Inverse Global Illumination](https://www.cs.princeton.edu/courses/archive/spring2001/cs598b/papers/yu99.pdf) | §3, printed p.217/PDF p.3, Eq.1 gives the radiosity balance. §5, PDF pp.4–5 uses hierarchical surface links, explicit direct lighting and interpolated indirect samples; Fig.5 alternates transport and BRDF estimation. §7, pp.6–8 validates calibrated lighting/HDR photographs and relighting; geometry/model errors remain. Known light emission and outgoing radiance make material recovery better constrained than JA's unknown historical incident multipliers. |
| [Keller, SIGGRAPH 1997, Instant Radiosity](https://www.cg.tuwien.ac.at/courses/Rendering/InstantRadiosity.pdf) | pp.1–3, Eqs.1–3 and §3.2: transport expansion and weighted path vertices; §3.3/Fig.4: quasi-random walk construction. These secondary samples approximate transported energy, not independently authored lamps. §5, pp.5–6 discusses the near-field transport singularity and sampling artifacts. Its mean-reflectivity path-allocation estimate is not a conservative bound for arbitrary high-contrast JA materials. |

The equations, constraints and decision below are our JA-specific proposal.
The papers' error scores and timings are not JA performance predictions.

## 2. Quantities and decomposition

### 2.1 Physical scene operator

Let `wi` point **from a receiver toward the previous source/vertex**; `wo` points
away from the receiver. Normals are world-space outward geometric normals for
visibility; shading-normal conventions are separately identified. In vacuum:

```text
(G1) Lo(x,wo) = Le(x,wo) + integral_H fr(x,wi,wo) Li(x,wi) (n·wi)+ dwi
(G2) E(x) = integral_H Li(x,wi) (n·wi)+ dwi
     Ldiffuse_out(x) = rho(x) E(x) / pi;  Mdiffuse_out = rho E
```

`E` is irradiance/illuminance; `L` is radiance/luminance; `M` is exitance. Do not
add `E` to `L` without the relevant operator. RGB calculations require stated
linear primaries and calibration. For a calibrated neutral light, these equations
can use lux, cd/m² and lumens channelwise under the canonical RGB/Y convention.
Geometry uses metres in physical kernels, converted once from world units.

For a point source `j`, `Dlocal_j(x)=V(x,pj) Ij(w) (n·wi)+ / r_metres²`.
For a Lambertian finite source, integrate
`Le(y) V(x,y) (nx·wi)+ (ny·(-wi))+ / r² dAy` over its emitting area. Directional
sun has perpendicular illuminance `Esun` and `Dsun=Vsky Esun (n·wsun)+`.
Diffuse sky is a separate angular boundary condition:

```text
(G3) Ssky(x) = integral_H V_to_sky(x,wi) Lsky(wi) (n·wi)+ dwi
```

An open segment is not automatically a sky hit. A bright window can transmit sky,
sun, exterior emitters and reflected light. It is not necessarily an area emitter.

With diffuse reflection, define `K_rho` from surfaces to incident irradiance:

```text
(G4) (K_rho E)(x) = integral_surfaces [rho(y) E(y)/pi]
                         V(x,y) (nx·wi)+ (ny·(-wi))+ / r² dAy
     E0 = Dlocal + Dsun + Ssky
     Eb = K_rho^b E0;  Ephysical = E0 + E1 + E2 + ...
```

One bounce here means **one reflecting vertex after a primary emitter or sky**
before reaching the receiver. The receiver's outgoing reflection is a different
operator. Surface emission is already in `Dlocal`; never also add it as reflected
exitance. A surface may both reflect and emit; do not copy FIPT's nonreflecting
emitter extraction assumption onto glow fixtures.

Keep roots `local`, `sun` and `sky` through each bounce. A sky-lit wall's reflected
energy is a `sky-rooted bounce`, not direct skylight and not a new lamp. In a room
lit only through a doorway, the correct set of **local physical emitters is empty**.

### 2.2 JA observation operators are not interchangeable

The model for observation family `o`, style `s` and bake hypothesis `h` is:

```text
(G5) target_encoded[o,s] in Q( O_h[o,s](theta_local, theta_sun, sky,
                                      reflected_transport, material, nuisance) )
                           + delta_h[o,s] + measurement_error[o,s]
```

`Q` is quantization/censoring; `delta_h` is **model discrepancy in the declared
observation domain**, possibly signed. It is not physical incident energy.
The operator includes receiver placement/footprint/filtering, style processing,
compiler transport approximations, transfer, shared-channel clipping, output
encoding and, for grid data, reduction/compression. Its internal physical fields
may be additive; its final encoded RGB8 usually is not.

| Input | Interpretation and constraint |
|---|---|
| Original RGB8 lightmap | Stored baked multiplier/relative lighting under an identified or assumed compiler operator. It is not automatically measured incident radiance, lux, or outgoing surface radiance. |
| Source-linear candidate | Conditional transfer/primaries assumption recorded by `decode.h`. Unknown transfer/primaries must remain unknown; do not label bytes linear to enable fitting. |
| HDR source | More range, not automatically physical calibration or known bake semantics. |
| Grid ambient/directed | Separate `O_h_grid`. Neither RGB slot universally equals indirect/direct lighting. Do not subtract directed from ambient or assign a universal surface/grid gain. |
| Deluxe direction | Supplementary compiler-weighted directional observation; not a normal map, exact incoming lobe, or source-position measurement. |
| Material texture/PBR value | Current runtime appearance is evidence, not measured bake-time `rho`. Never divide a baked lightmap by current texture RGB to construct a physical target. |

For ordinary q3map lightmapped receivers, the stored lighting modulates material
later; receiver texture colour does not simply multiply its own stored lightmap.
The **reflecting surface's** sampled texture response participates in the next
bounce. Distinguish fitting that incident-like stored quantity from fitting
`rho_receiver * E / pi` in an outgoing-radiance photograph.

The [physical PBR contract](rend2-physical-pbr.md) remains authoritative:
`r_pbrWorldUnitsPerMeter` changes geometric conversion and
`r_pbrPhysicalToScene` changes presentation. Neither recovers historical luminous
intensity. Use the existing `PhysicallyAuthored`, calibrated scene-equivalent and
uncalibrated-relative provenance. No new universal GI calibration is proposed.

### 2.3 Six separate output meanings

1. Direct physical-emitter contribution, conditional on emitter hypothesis.
2. Direct directional sun contribution.
3. Direct sky/environment contribution with explicit sun inclusion/exclusion.
4. Reflected transport generated by a specified source/sky root, geometry,
   reflectance and bounce model, with approximation/conditional status.
5. Identified artist-authored legacy ambient/fill, if a profile provides evidence.
6. Unexplained discrepancy or approximate/unattributed transport. A useful smooth
   field can belong here without being verified physical GI.

No residual field is promoted into `PhysicalEmitter`. A valid empty asset,
candidate set or low training error is not a successful reconstruction.

## 3. What the actual q3map bounce controls establish

The pinned compiler is NetRadiant `d9338309ea8fab75027b7382efaabe034302b862`,
binary SHA-256 `894049bd136e7828b0f068d12c932833153b3ada87b99655dac90e4a60c85ad4`.
This is **not an identified Raven production compiler**.

The already inspected [light_bounce.c](https://github.com/xonotic/netradiant/blob/d9338309ea8fab75027b7382efaabe034302b862/tools/quake3/q3map2/light_bounce.c)
`RadSample`, `RadSampleImage`, `RadCreateDiffuseLights` and the stage loop in
[light.c](https://github.com/xonotic/netradiant/blob/d9338309ea8fab75027b7382efaabe034302b862/tools/quake3/q3map2/light.c)
give an empirical polygon-source bounce algorithm. Previous-pass float lighting
and texture samples generate secondary polygon emission; old primary lights are
removed from subsequent bounce passes, while results accumulate. Material/image
transfer options matter. The fragment sampling and empirical emission scaling
are not a converged energy-conserving Lambertian solution. See the existing
bake-model document for constants, indexing and exact source links.

`-bouncegrid` is independent of `-bounce`. Sky sampling, ambient/flood, styles,
lightmap and grid reduction have their own paths. Legacy ambient may itself
enter an earlier pass and propagate depending on profile; do not assert a
universal no-bounce rule for fill.

### 3.1 Real paired bakes executed here

[compiler.json](../tools/rend2/inverse_gi/results/compiler.json) records inputs,
map/material/output hashes, sample counts and metrics. The existing 22 pinned
forward BSPs were hash-checked and their supported surface/grid/deluxe comparisons
rerun; the existing D09 coloured-bounce pair was also checked. Fourteen additional
bakes/42 compiler stages reused existing D09 maps/materials/primary lights.

New pairs use identical `-super 1 -nocollapse` on **both** sides, in addition to
the D09 recipe. Without it, dark chart collapse can change UV sampling when bounce
is enabled; an initial strict pair check correctly rejected that comparison.
Position, original UVs, normals, indices and surface descriptors must match before
subtraction. The final 16 bytes of RBSP vertices are baked RGBA and are expected
to change; they are excluded from **geometry comparison only**, never altered in
the BSP. Original frozen D09/22-case bytes were not modified.

Each new paired floor measurement uses 425 equal-area interior samples. Source
geometry is planar, 16-world-unit luxels; the mean is a selected floor statistic,
not all-map energy. Values below are **encoded RGB8 differences**, not additive
physical GI. No gain or tolerance was fitted to make them pass.

| Same primary geometry/lighting, parameter changed | Mean `bounce1 - bounce0` RGB8 | Other observation |
|---|---:|---|
| Existing large white room | `(5.33143,5.33143,5.33143)` | 1,225 floor samples; grid identical without bouncegrid, changes with bouncegrid |
| D09 point room, white texture 180 | `(22.22118,22.22118,22.22118)` | Grid identical in recorded pair |
| Same room, white texture 90 | `(13.13412,13.13412,13.13412)` | Zero-bounce lightmap byte-identical to white180; bounce response changes |
| Red-wall room | `(26.60706,20.52706,19.87529)` | Existing D09 pair reproduces these same floor statistics; red excess is measurable |
| Same red room, bouncegrid enabled | Same surface delta | Resolved grid changes |
| Doorway | `(17.75529,17.75529,17.75529)` | Right-side mean delta `3.14118`; indirect can reach through opening |
| Sealed wall | `(18.71529,18.71529,18.71529)` | Lit left side delta `36.5`; dark right side **0** across 170 samples |
| Skylight | `(0,0,0)` | Primary floor mean already `(4.17882,4.10353,3.78824)`; this recipe has no detectable additional encoded floor bounce |

The skylight result is not proof that real skylight cannot bounce. Nor should
white90/white180 differences be used as a measured physical albedo ratio: compiler
fragmentation, thresholds and representation remain involved.

A same-settings doorway repeat preserved the lightmap but changed **12 logical
grid records** (an earlier repeat changed 11). The final zero/one pair happened to have identical grid records;
an earlier pair differed. This agrees with the existing near-solid grid
nondeterminism limitation. Such variation is **not evidence of bouncegrid being
enabled**. Raw hashes/differing cell IDs are retained, not normalized away.

### 3.2 Direct-model mismatch creates supposed GI

On the existing large-room bakes, compare the verified NR inverse-square/cosine
kernel against source-supported NR linear/no-angle. This intentionally changes
the attenuation/angular **profile family**, not just attenuation in isolation.
Both use the same source position, scalar intensity range `[0,600]`, encoding
(`gamma=1`, compensation 1, exposure 0) and optional constant fill `[0,30]`.
Linear intensity is searched through its real changing distance envelope; it
is not treated as a gain on a fixed linear basis.

Training uses floor `y<=-96`, held-out `y>=96`, leaving a 192-world-unit centre
gap. Original samples have 16-unit support. These symmetric receiver sets are
spatially independent but do **not** prove general multi-normal identifiability.
The reported loss is distance to encoded quantization/censoring intervals after
the profile's encoding operator, not RGB bytes versus linear lux.

| Truth | Direct profile | Intensity constraint | Fitted fill | Fitted legacy intensity | Held-out interval RMSE, bytes |
|---|---|---|---:|---:|---:|
| Zero bounce | Correct inverse-square | Fixed 300 | 0 | 300 | 0 |
| Zero bounce | Wrong linear/no-angle | Fixed 300 | 8 | 300 | 9.20374 |
| Zero bounce | Wrong linear/no-angle | Fitted | 14 | 272 | 4.96332 |
| One bounce | Correct inverse-square | Fixed 300 | 5 | 300 | 0.17306 |
| One bounce | Correct inverse-square | Fitted | 4 | 318 | 0.10403 |
| One bounce | Wrong linear/no-angle | Fitted | 19 | 274 | 5.14906 |

Without fill, correct direct-only one-bounce data prefer intensity **375**, not
the authored 300, and held-out error is 1.66484 bytes. This is direct/GI confounding
even with the correct primary profile. Conversely, with **no real bounce**, the
wrong profile prefers substantial fill. Constant fill fits one-bounce appearance
well but cannot certify its physical origin or transport paths.

`UnknownProfile` in this experiment explicitly allows both NR families; its
conditional nuisance-fill range is `[0,14]` for zero-bounce data and `[4,19]` for
one-bounce data. These are finite experiment ranges, **not credible intervals or
stock defaults**. No historical compiler is identified. A future stock experiment
must carry the feature-specific X03 catalogue and include unsupported-family
discrepancy; two simple NR alternatives do not span all Raven possibilities.

## 4. Minimal passive transport model

### 4.1 Patch power before interpolation

For diffuse patches of area `Ai`, let `Fji` be the fraction of power leaving `j`
that reaches `i`. For static opaque diffuse geometry:

```text
(G6) Fji = (1/Aj) integral_Aj integral_Ai
                     V(x,y) cos_j cos_i / (pi r²) dAi dAj
     Fji >= 0; sum_i Fji <= 1; Aj*Fji = Ai*Fij
(G7) Pj[b+1] = Aj * rho_j * Ej[b]
     Ei[b+1] = sum_j Fji * Pj[b+1] / Ai
```

The row bound is on **outgoing fractions**, not unweighted sums of incident
irradiance at unequal-area receivers. Escape/absorption do not turn into new
sources. Validate reciprocity and row budgets against integration error. Do not
normalize a bad quadrature row to 1 and advertise it as physically correct.

For `rho<=rhoMax<1`, reflected total power contracts by at most `rhoMax` each
bounce in the area-weighted power norm. A remaining-power bound after bounce `B`
is `P0*rhoMax^(B+1)/(1-rhoMax)`, before accounting for known escape. It bounds
**total** omitted power; it is not a tight per-luxel irradiance bound or proof
that the first bounce suffices. A perfectly reflecting closed cavity has no such
strict bound. Mean reflectance is not a safe substitute for `rhoMax`.

Near patches, large solid angles, thin apertures and shadow boundaries require
finite-area integration. A small-patch/VPL approximation must carry truncation
error and a minimum solid-angle criterion; arbitrarily clamping `1/r²` changes
the energy model. Cache geometry-only footprints/form factors, not source-dependent
visibility results after moving a source. Root contributions and bounce orders
are propagated separately by the same linear operator at fixed materials.

### 4.2 First implementation boundary

Use existing immutable observations, geometric/shading normals, actual luxel
footprints, material facts, `StaticVisibility`, region lookup and emitter evidence.
Do not rebuild BSP capture. Start on compact certified fixtures/ROIs:

1. Fix candidate geometry, profile hypotheses and RGB amplitude bounds. Establish
   the direct-only, sun and sky operators with the existing decoder. Unavailable
   rules return unavailable; do not produce a guessed colour.
2. Create diffuse receiver/reflector patches grouped by geometry, style and
   material. Preserve surface-side identity; do not merge across thin walls.
3. Build sparse hierarchical transfer links using geometric solid-angle/error
   bounds and actual ray queries. Region IDs/PVS only accelerate search.
4. Evaluate `E1=K_rho E0`. Initially materials are fixed or selected from bounded
   hypotheses, and GI has **no independent per-patch emission amplitude**.
5. Fit the small direct/sky parameter set and compare profile/material hypotheses
   on validation data. Use final hidden held-out views only for reporting. Never
   initialize a radiance cache from validation/held-out RGB or private source answers.
6. Publish physical-model transport only with its conditional assumptions,
   source/root membership and error budget. Otherwise preserve observations and
   optional approximate transport with unresolved attribution.

This operator is a **physical plausibility model**. For a known empirical compiler,
an eventual separate `LegacyBakeReplay` operator can reproduce its polygon-source
algorithm and sampling. Its empirical coefficients are not physical watts/lumens.
Do not force physical radiosity to match all q3map approximations by raising rho
above 1 or adding fictitious emitters. Keep physical prediction error and compiler
model discrepancy distinct. This task does not implement that replay.

### 4.3 Material cases

| Case | Allowed inference |
|---|---|
| A: known bake-time texture/response and compiler | Fit incident-like lightmap values with that operator; transport through specified reflectance. Conditional amplitude/bounce tests can be meaningful. |
| B: partially known reflectance | Bounded `rho`, preferably tied by material rather than one parameter per luxel. Propagate intervals and refit allowed source amplitudes; report correlation and sensitivity. |
| C: unknown bake-time material response | Do not identify a unique material, source intensity or bounce fraction from one RGB bake. Use explicit hypotheses/upper bounds and unresolved transport; do not silently substitute today's PBR albedo. |

For outgoing radiance alone, `L=rho*E/pi` admits the exact ambiguity
`(rho,E)=(.2,6),(.4,3),(.8,1.5)` with `L=.381971863 cd/m²`. Incident intensity
varies **4×**. For an isolated second-bounce receiver, products such as
`I*rho1*rho2` introduce further ambiguity. These demonstrations do not imply
that a JA incident-multiplier lightmap contains its own receiver albedo.

With exact known geometry/material/profile and sufficient direct observations,
one illumination can predict multiple bounces. It does not independently measure
their orders. Unknown direct scale, sky, rho, incomplete surfaces and clipping
can make several bounce histories indistinguishable. Additional independently
known illuminations/material controls are needed to test those histories.

## 5. Objective, regularizers and attribution safeguards

For each hypothesis `h`, prefer the small constrained problem:

```text
(G8) min_theta,rho,sky,eta
       sum_spatial_blocks mean_i [area_i * robust_distance(
          O_h[i](E0(theta,sky) + K_rho E0), target_interval_i)^2 / scale_i²]
       + R_fixture(theta) + R_material(rho) + R_sky(sky) + R_model_complexity

subject to: nonnegative source/sky energy; valid emitter geometry;
            bounded reflectance; passive transfer; separate styles;
            explicit visibility uncertainty; matched calibration provenance.
```

Normalize block/area weights with an explicit denominator so chart oversampling
does not count as independent localization evidence. Robust loss/scale describes
the observed domain, not arbitrary confidence products. Use X04 reference or
surrogate status for source-dependent shadow derivatives. Do not freeze visibility
and claim correct geometry gradients.

Specific constraints for the first implementation:

- Fixture/location/type priors from source evidence, not from bright residuals.
  Broad source-amplitude bounds and a model-count penalty; avoid a sole L1
  intensity penalty competing against an unpenalized GI field.
- Material group bounds `[rhoLow,rhoHigh]` from stated evidence, capped below 1
  for a tail bound. For unknown materials these may be broad; do not make a
  narrow prior merely to obtain a unique result.
- Nonnegative sky lobes with a shared environment root; explicit direct-sun
  exclusion. Penalize unnecessary angular complexity, not every patch equally.
- Passive transfer and source-coupled reflected energy. No positive bounce in
  an entirely unilluminated disconnected component unless a stated external
  boundary/ambient root exists.
- Equal source/material/sky budgets across bake-profile comparisons. Count
  additional fill, emitters and transport flexibility. Validate stability under
  reasonable parameter-budget changes, not only one best training RMSE.
- Keep `delta_h` separate. Initially measure signed, spatially grouped residuals
  after each constrained fit; do not jointly optimize a free discrepancy at every
  observation. That would cause the same source-collapse degeneracy as free GI.

For a provisional geometry-constrained unresolved field `z`, use nonnegative
incoming energy only where the representation calls for it, coarse regional/surface
support, an explicit magnitude budget and a graph Laplacian built **only from
certified compatible paths**. A signed discrepancy field has different semantics.
Cross-fit field parameters without held-out RGB, freeze direct-anchor tests, and
report emitter/no-emitter ablations. Such a field is still **not verified GI**.
Graph smoothness alone neither establishes an energy origin nor preserves lamps.

Do not select regularization on final held-out data. The prototype's `.02` source
penalty, `.01` smoothness and `.3` residual magnitude are declared experimental
settings, not physically derived constants or production defaults.

## 6. Bounded numerical findings

[experiment.py](../tools/rend2/inverse_gi/experiment.py) and
[analytic.json](../tools/rend2/inverse_gi/results/analytic.json) use a 78-patch,
154-edge reciprocal **midpoint approximation** with a known neutral point source
of 3 cd/channel, a red reflector `rho=(.7,.18,.08)` and black receiver patches.
Black receivers isolate first bounce in this scene; a separate two-patch network
tests nonzero later bounces. This is a physically admissible passive network,
not an exact room form-factor solution or an inverse source-discovery benchmark.

Receivers span floor and wall normals. There are 33 training sites, 33 held-out
sites and 11 gap sites; centres are separated by at least .6 m with .2 m receiver
support. Fitting functions only read training RGB; mutating held-out RGB by `1e9`
does not change their result. The test harness owns the truth and evaluation data.

### 6.1 GI stability across profile/material hypotheses

The analytical `linear_alternative=max(0,1-r/6)*cos/.8` is explicitly a simplified
confounder, **not** the verified NR linear formula. Actual NR kernels were tested
separately in §3. The same primary/profile/material hypothesis generates its
direct and source-coupled one-bounce prediction.

| Bounce truth | Direct hypothesis | Reflector assumption | Fitted source cd | Mean predicted held-out GI lux | Held-out RGB RMSE lux |
|---|---|---|---:|---:|---:|
| Zero | Correct inverse-square | Known, bounce off | 3.000000 | 0 | 0 |
| Zero | Wrong linear alternative | Bounce off | 2.238863 | 0 | .657021 |
| Coloured first bounce | Correct inverse-square | Known | 3.000000 | .0035056 | 0 |
| Same fixed bounce truth | Correct inverse-square | Half reflectance | 3.002259 | .0017541 | .0024002 |
| Same fixed bounce truth | Correct inverse-square | Assumed grey .4 | 2.998871 | .0043804 | .0035038 |
| Same fixed bounce truth | Wrong linear alternative | Known | 2.236731 | .0049942 | .6571886 |
| Same fixed bounce truth | Wrong linear alternative | Assumed grey .4 | 2.235005 | .0062379 | .6573604 |

The fixed true bounce has not changed when its estimated attribution changes.
These are conditional physical-model values in self-authored SI scenes. The wrong
profile has unacceptable predictive error; its larger GI is not an improvement.
Unknown bake material permits held-out mean GI `[0,.0109551]` lux at fixed source.
A bounded red-reflectance envelope encloses all **99/99** held-out RGB channels
in the constructed case, but that is a deterministic conditional coverage check,
not a calibrated 100% probability.

The independent nonnegative transport-shaped nuisance in the wrong-profile,
zero-bounce case fits GI gain **0**: not every profile error resembles this
particular bounce basis. A free residual can nevertheless swallow all the light
and fit direct gain 0, mean residual `1.21865` lux. Both outcomes are retained.

### 6.2 Flexible GI suppresses the real source

Red-channel training from the same physical point plus reflector:

| Fit model | Source cd | Training RMSE lux | Held-out RMSE lux | Parameters |
|---|---:|---:|---:|---:|
| Free nonnegative residual/site, sparse source | **0** | **0** | 1.25667 | 34 |
| Smooth residual + sparse source | **0** | .004473 | 1.25974 | 34 |
| Smooth residual, no source penalty | 3.00100 | .0000658 | .002088 | 34 |
| Smooth residual + magnitude penalty | 3.00267 | .001796 | .001920 | 34 |
| Constant unresolved fill | 2.99938 | .004306 | .004254 | 2 |
| Passive source-coupled transport, known material | **3** | **0** | **0** | 1 |

The residual extrapolation is explicitly nearest training position; no hidden RGB
is read. Source recall uses a toy threshold of 1.5 cd (half the known truth), not
a source-existence probability. Constant fill can predict reasonably here yet
has no demonstrated physical origin. Energy coupling is exact only because the
toy truth uses the same controlled transfer; §6.3 tests a separate finite-area
reference rather than claiming this fit validates the approximation.

The smooth-residual problem is a convex nonnegative quadratic program, solved
with an active set to KKT tolerance `1e-10`. A short alternating-descent schedule
had misleadingly stalled without the source penalty; it was replaced before
recording these final results. **Smoothness without that penalty preserves the
source in this scene**, while smoothness plus source sparsity still suppresses it.
Neither smoothness nor a low held-out error proves physical GI attribution.

Allowing a second nearby point candidate with L1 penalties `0,.05,.5` yields
extra amplitudes `0,.00380,.03796` cd, zero extra emitters above the declared
1.5-cd threshold, while true-source amplitude moves `3 -> 2.91989`. More sparsity
does not automatically improve attribution. An exactly duplicate residual/source
basis remains unidentifiable even on held-out observations; validation rejects
some overfits but cannot prove unique physical origin.

### 6.3 Geometry, quadrature, censoring and uncertainty

- Existing X04 triangle queries certify an occluded primary and a visible
  source→reflector→receiver path through a narrow opening. Sealing that opening
  blocks the bounce path. Both rooms have **zero invented local emitters**.
- Unknown alpha masks give visibility `[0,1]`; missing unbounded geometry leaves
  clear paths uncertain, while a known opaque wall still blocks. These intervals
  are not expected transmittances of .5. Sky-only `Lsky=2 cd/m²` gives `pi*Lsky`
  horizontal lux for an unoccluded hemisphere; no nearby point emitter is needed.
  A zenith query by itself is not that hemisphere integration.
- A two-node Euclidean residual smoother leaks **1/3** of a unit signal across
  a sealed thin wall; removing the uncertified smoothing edge keeps dark-side
  value 0. Region proximity is not optical connectivity.
- Six independent finite-rectangle irradiance tests compare midpoint surfels and
  4×4 Gauss quadrature with a 48×48 area reference at constant reflected exitance.
  For a .1-m reflector at 1 m, midpoint relative error is **.3202%**. For a 1-m
  reflector .2 m away it is **479.84%**, versus **15.47%** for 4×4. The 24×24/48×48
  absolute difference there is `1.60e-8 lux`. No general certified integration
  error is inferred from two orders; refine near-field links instead of clamping.
- A sparse five-site scalar-linear quantization control has two clipped samples.
  Censored fitting admits intensity `[2.99462,3.04221]` cd around true 3; treating
  255 as an exact value estimates **1.99994 cd**. A midpoint of the admitted
  interval has held-out interval RMSE `.1860` byte; its propagated interval
  overlaps all 33 held-out targets. If all useful samples clip, no finite upper
  intensity is obtained. This scalar quantizer is **not NR's shared-peak output**.
- The separate passive two-patch network has bounce levels `1,.1,.01,.001` lux
  on alternating receivers and a remaining power bound `.000111111` lumens/channel
  after four bounces. An unknown transfer edge gives `[0,.3]` lux, not certified 0.

These demonstrate energy/attribution failures and conditional uncertainty, not
GI reconstruction quality on stock maps or exact q3map bounce replay.

## 7. Candidate methods and cost decision

| Method | Attribution/accuracy | Work and decision |
|---|---|---|
| Sparse explicit one-bounce radiosity | A source-coupled reflected path has a defined origin; handles coloured surfaces and openings. Material/geometry/bake discrepancy remains. | **First model.** `O(B*edges)` application, bounded hierarchical build; finite-patch refinement near apertures. |
| Second/low-order bounce | Useful for indirect-only rooms and multiple reflections; order may not be observed independently. | Optional after controlled energy, material and held-out tests. No free per-order gain. |
| Sparse surfel/VPL transport | Practical small-patch integration basis, high variance/near-field errors if used as free points. | Internal weighted transport records only; no `PhysicalEmitter`, specular lamp or candela attribution for a VPL. |
| Hierarchical transfer matrix | Reuses geometry/visibility across sources and material hypotheses; clusters distant links. | Prefer over dense all-pairs. Keep escape, omitted-link and unknown-path budgets. |
| Geometry-constrained smooth field | Cheap appearance residual, can leak or absorb direct shadows if overflexible. | Fallback **unresolved transport** with path-side support/magnitude limit and independent validation. |
| Indirect irradiance probes / SH / SG | Compression/reconstruction of a field, not a source or energy-origin test. Low-order SH blurs shadows; SH can be negative; positive SG lobes can still invent energy. | Export approximations only after field semantics established; SH/SG choice cannot resolve attribution. |
| Full/differentiable path tracing | Captures high-order/specular transport with accurate inputs; expensive/noisy geometry derivatives; does not solve unknown-profile/material gauges. | Optional compact reference/refinement, not the first dependency. |
| Learned/neural GI prior | May predict plausible appearance; domain bias, capacity and reproducibility obscure physical identification. | Reject for first implementation: no demonstrated JA benefit or training/uncertainty validation. |

For example, 50,000 patches × 32 **average represented links**, 24 bytes per
directed bounded link, costs 38.4 MB plus about .2 MB row offsets, patch arrays,
geometry/BVH and scratch. Dense 50k² scalar floats alone cost 10 GB. This is a
layout estimate, not an achieved full-map benchmark. A hard 32-nearest cap is not
a legitimate transport truncation: distant surfaces can matter. Use solid-angle/
power error bounds, hierarchical links and report unresolved remainder. Fail or
coarsen explicitly at the memory/work budget rather than silently dropping energy.

Initial proposed limits: 50k patches, 1.6M represented links, 128 MiB transfer/
scratch budget excluding the existing immutable geometry; one bounce. Those are
engineering starting limits requiring actual stock ROI profiling, not enforced
production defaults. Process source observations in existing tiles. Cancel at
bounded patch/link blocks; do not run a solver during normal map loading.

Measured here (Python 3.12.14, Windows 10, three timing repeats, medians):

| Research operation | Seconds | Peak traced Python bytes |
|---|---:|---:|
| Construct 78-patch network | .00201 | 93,120 |
| Profile/material stability suite | .05483 | 261,696 |
| Source/residual competitors | .23869 | 162,032 |
| Six finite-area reference tests | .05509 | 2,808 |

Process peak working set was **26,284,032 bytes**, separately measured with
`GetProcessMemoryInfo`; it is not the traced-allocator number. Frozen compiler
comparison took 1.24 s, deterministic profile search 21.14 s and 42 new compiler
stages 2.68 s in the final recorded run. Tiny-scene timings do not predict full-map
cost. `measure.py` and ignored build timing reports reproduce measurements.

## 8. Incomplete geometry and region policy

Use local visibility truth from the existing [static-visibility API](rend2-static-visibility.md)
and [region graph](rend2-lighting-regions.md), including X01's spatial uncertainty.
Do not require a perfect whole-map region graph before evaluating a certified
compact path, and do not use one region ID to declare mutual visibility.

Opaque triangle hits remain blockers in incomplete scenes. Unknown bounded optical
surfaces affect intersecting links; unknown unbounded possible blockers prevent
unsupported clear-link claims. Alpha/translucent surfaces without a known mask/
transmission operator are interval/unknown paths. Patch approximation envelopes,
moving-door bounds and model state belong in link provenance.

For one positive transfer, certified visible fraction yields `F_low`; possible
visible fraction yields `F_high`, with outgoing power constraints across links.
Propagate nonnegative intervals without assigning their midpoint as truth.
Missing geometry does not bound **missing illumination** by itself: if an unknown
surface could emit, its energy has no finite bound without a source/boundary prior.
Likewise no finite albedo/source/sky bound means no finite GI upper estimate.

Ignore an unsupported path only by excluding its observations from *certified*
attribution and retaining them in diagnostics/unresolved output. Do not delete
its wall, assume transparency, or smooth lighting through it. Unknown region
membership is a status; retain valid source→luxel ray evaluation and explicit
surface-side assignment availability without fabricating a room ID.

Stock `hoth3`/`hoth2` retain incomplete certification, unknown bake profiles and
material/patch optical uncertainty. This work did not perform a new stock-map
GI fit. Existing coverage does not justify publishing a measured bounce fraction
or physical source accuracy. A locally reliable subset can support conditional
tests even when other map components remain unresolved.

## 9. Uncertainty and non-identifiability

| Component | How to represent / correlations |
|---|---|
| Source location/type/existence | Existing alternate emitter hypotheses, positional intervals/covariance only where justified; correlated with intensity and shadow correspondence. |
| Bake profile | Discrete feature-specific family; conditional fits and sensitivity. Wrong falloff/filter can mimic GI. No normalized ranking masquerading as probability. |
| Material/albedo | Group bounds or hypotheses; strongly correlated with source intensity, colour and bounce order. Current PBR texture is not an anchor. |
| Visibility | Certified/blocked/unknown plus optical/geometry reasons and transfer intervals; shared wall errors correlate many luxels. |
| GI approximation | Patch integration envelope, omitted transport/tail budget and model-class discrepancy, kept apart from measurement noise. |
| Quantization/censoring | Target intervals, channel flags and coupled encoding. NR shared-peak normalization loses amplitude jointly; independent channel-clamp inverses are invalid for that profile. |
| Absolute photometric scale | Explicit calibration provenance/gauge. Relative RGB fitting cannot measure SI intensity without an external anchor. |

For calibrated probabilistic models, covariance must include cross terms, or use
the law of total variance across calibrated hypotheses. Without such models use
sets/envelopes and per-hypothesis sensitivity, not arbitrary variance sums or
products of heuristic confidence factors. Our deterministic intervals are not
Bayesian posterior credible intervals. Near-singular Jacobians and material/light
gauges should return unidentifiable directions, not finite confidence ellipsoids.

Fundamental ambiguities remain: direct versus tightly localized first bounce;
unobserved external source versus window/sky transport; amplitude versus material
under outgoing observations; global exposure/physical scale; bounce count versus
reflectance; correlated smooth fill versus a distant source; residuals exactly
parallel to a source response. Even flawless held-out prediction cannot resolve
an exact observational equivalence. Keep alternative explanations and consumer
suitability rather than requiring one physical answer.

## 10. Canonical integration and preventing double counting

The existing `tr_lightrecon_types.h` already separates `PhysicalEmitter`,
`SunSource`, `EnvironmentLighting`, `TransportField` and sparse contribution
references. `TransportComponent` includes indirect/reflected, aperture,
unresolved-direct and unattributed incident quantities. `SunCoverage` is useful
but not sufficient for all GI membership. Use those ownership/API boundaries;
do not add GI samples to the physical emitter list.

Before a future implementation publishes compound fields, add a versioned
**membership/provenance descriptor**, not conventions inferred from array names:

| Required metadata | Meaning |
|---|---|
| Quantity/domain/units/primaries/calibration | Incident irradiance vs directional incoming radiance vs outgoing radiance; relative vs scene-equivalent vs physically authored. |
| Path-root membership | Local source IDs, explicit sun IDs, sky/environment ID, identified legacy ambient root, unknown roots. Each has Included/Excluded/Unknown status. |
| Reflection order | Direct/zero, first, later or a declared union; unknown differs from none. |
| Field role | Modelled reflected transport, empirical compiler replay, approximate unresolved transport or observation-domain discrepancy. A signed byte discrepancy is not a physical field. |
| Hypothesis and generation IDs | Bake profile/formula version, material/geometry/optical policy, source generation, transfer builder/integration settings and content fingerprint. |
| Composition/coverage | Spatial/style/domain mask, exclusivity group and replace/add semantics; unsupported membership must prevent unsafe composition. |
| Error/support | Numerical/tail budgets, missing geometry/material reasons, bounds and provenance. No uncalibrated scalar "physical confidence". |

Example: a field marked `{roots: sky, reflections: >=1, excludes: local-direct,
sun-direct, sky-direct}` may be added to the corresponding direct components.
A total incident field already including direct local/sun/sky must replace those
components in that coverage, not be added again. If membership is Unknown, fail
composition or select one explicitly declared approximation; do not guess from
RGB similarity. Explicit sun may be separated from environment only if its
membership is known. Fog/entities/specular may independently choose supported
components, with no enablement dependencies.

Proposed CPU request/result boundary (specification only):

```cpp
TransportEvaluation EvaluateTransport(
    ImmutableGeometryView, MaterialHypothesisView, SourceSnapshot,
    BakeProfileHypothesis, ObservationTileView, TransportPolicy, WorkBudget);
// status: AvailableConditional / Unavailable / Unsupported / BudgetExceeded /
//         Cancelled / InvalidInput
// result: typed root/order contributions, domains/units, numerical/path bounds,
//         unresolved discrepancies, per-observation support, hypothesis IDs,
//         counters; never invented emitters or renderer resource handles
```

All returned arrays use owned snapshots or documented immutable views. Transfer
chunks can be evicted independently. Rebuild/cache keys include geometry/material
interpretation, source/profile/calibration generations, optical policy, patching,
bounce count and algorithm version. A changed physical/legacy operator or path
membership requires a new version/key; do not reuse a validated cache under new
semantics. **No schema/cache compatibility change is implemented in this research.**

## 11. Upgrade gates and bounded implementation decision

The minimal model requires original source-domain observations, mapped geometric
footprints/normals/areas, style/material group identity, local certified visibility,
known/bounded reflectance hypotheses, an explicit bake/profile family, and
external calibration only if SI publication is claimed. `UnknownProfile` or
unknown albedo does not forbid experimentation; it limits attribution.

Recommended execution:

```text
for h in declared_bake_profile_family:
    require_supported_observation_operator(h)       # unknown != zero
    for m in bounded_material_hypotheses:
        Dlocal, Dsun, Ssky = direct_fields(training_geometry, candidate_sources)
        K, path_bounds = build_or_load_passive_transfer(geometry, m, budget)
        E1 = K * (Dlocal + Dsun + Ssky)              # roots propagated separately
        fit_small_parameters(training_view, O_h, E1, stated_priors)
        validate_prediction_and_source_ablation(validation_controller)
        retain_conditional_solution_and_signed_model_discrepancy()
controller.evaluate_once(hidden_heldout)
publish_only_supported_components_or_unresolved_transport()
```

Add a second bounce when blind coloured/doorway/multi-room controls materially
improve held-out prediction beyond quantization **and** finite-area/tail errors,
while preserving energy/reciprocity and source attribution. Define thresholds in
the chosen operator domain and measure sensitivity; no universal byte threshold
is a physical truth test.

Use a higher-quality CPU reference integrator first for a compact certified scene:
finite-area adaptive integration plus independent path sampling with proper PDFs,
fixed seeds and error measurement. Only consider full inverse path tracing when
validated materials/geometry/profile and persistent high-order/specular residuals
justify it. Compare equal observation sets and fixed wall-time/ray budgets against
one/two-bounce transfer. Reproduce reference paths/visibility derivatives and keep
strict forward values separate from optimizer surrogates. FIPT's outgoing-radiance
cache is usable only if that quantity is genuinely observed on the **training**
side; JA lightmaps cannot be inserted as outgoing HDR cache samples.

Synthetic acceptance/failure tests for a later production implementation:

| Control | Acceptance / explicit failure |
|---|---|
| Known direct, zero bounce; correct/wrong profile | Correct model's GI consistent with zero; wrong-profile residual remains discrepancy. Fail if every positive residual becomes physical GI. |
| Known point + red reflector, A/B/C materials | Recover known conditional amplitudes in A; B bounds cover controls; C reports ambiguity. Never force unique intensity in C. |
| Bright room behind doorway; sealed version | No new local emitter; certified opening path can transport energy. Sealed dark side has zero supported transfer. |
| Sky/sun, with and without nearby sources | Sky root remains independent of sun/local lights; no point created merely to illuminate the room. |
| Free/smooth/energy-bound residual competitors | Report recall/false lamps, source ablations and held-out error. Lowest training loss does not choose attribution. |
| Sparse/255/unknown transfer/primaries | Censored intervals and incomparable domains retained; unbounded intensity allowed. No inferred lux from unknown bytes. |
| Missing patch/alpha/transmission/moving door | Interval/unavailable path status; known opaque wall remains blocked. No fabricated transparency or global all-map failure for a local certified scene. |
| Multiple bounces/high reflectance | Power contraction/tail and quadrature converge within declared budgets; no row renormalization or untracked omitted links. |
| Geometry/source/profile/material generation change | Relevant cached transport invalidated; fixed geometry-only data reused safely. |

Unresolved stock cases should publish **only approximate/unattributed transport**
if its provenance, coverage, component inclusion and independent prediction are
useful. If even quantity/scale/membership cannot be stated, publish observations
and diagnostics with no transport asset. Neither case is failed software; calling
it verified physical reconstruction would be the failure.

## 12. Reproduction and validation record

Exact commands and first-time fixture generation are in the
[tool README](../tools/rend2/inverse_gi/README.md). Stable numerical witnesses are
`results/analytic.json` and `results/compiler.json`; source provenance is in
`sources.json`. Timing/execution logs, full BSPs, downloaded papers and answers
remain ignored under `build/`. No stock assets are redistributed.

Executed successfully:

- 15 new dependency-free attribution/transport tests, including real-compiler
  witness guards and repeated deterministic analytical results.
- 18 existing bake-kernel/profile tests, 33 localization tests, 27 X04 footprint/
  compiler-boundary tests and all 17 CPU CTest targets.
- Existing 22 actual pinned BSPs hash-checked and supported numerical comparisons
  rerun; original D09 coloured pair and 14 new real bakes/42 stages measured.
- SP `rdsp-rend2_x86_64` and MP `rd-rend2_x86_64` Release targets built successfully.
- Local timing, Python allocation peak and process peak RSS measured separately.

During development the strict pair check rejected bounce-dependent collapsed UVs;
both new recipes were explicitly changed to `-nocollapse`. A floating equality
assertion was corrected to a numerical assertion. A sparse censored midpoint
estimate was correctly found not to match every held-out byte interval exactly;
the final test requires coverage by the predicted interval and reports its
nonzero point error instead of claiming perfect recovery. These are not hidden
production-data fixes or increased compiler-matching tolerances.

No renderer output or runtime GI was tested or changed. Full q3map bounce replay,
real stock physical GI attribution, multiple-illumination identifiability,
alpha/transmission integration and production full-map cost remain unverified.
The results justify a bounded first transport model and its uncertainty/fallback
rules, not claims that the inverse GI problem has been solved.
