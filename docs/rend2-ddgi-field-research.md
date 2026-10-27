# DDGI geometry and irradiance-field techniques for JA reconstruction

Research date: 2026-10-27. JA baseline: `7cb86c04a`, `rend2-unified-wip`.
Reference: RTXGI-DDGI 1.3, pinned commit
[`f33e496ca31b3f0eec1c4e2cbaa8bb620e337fa6`](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/tree/f33e496ca31b3f0eec1c4e2cbaa8bb620e337fa6).
The [source manifest](../tools/rend2/ddgi_research/sources.json) pins 24 selected
documentation/source files, their URLs and SHA-256 hashes. The 285,535 bytes of
reference material remain in ignored `build/ddgi-research/reference/`, not vendored
into the engine. This work adds research and independent scalar CPU fixtures only.

**Decision:** use geometry-aware support selection, deterministic ray diagnostics
and separately generated irradiance probes. Keep exact three-state CPU visibility
for physical inverse constraints. Directional distance moments may accelerate or
improve **approximate field interpolation**, but are neither a physical visibility
oracle, calibrated confidence nor an original-light estimator. Do not relocate
historical lightgrid samples while retaining their baked RGB.

This complements the existing [GI decision](rend2-inverse-gi-research.md),
[static visibility](rend2-static-visibility.md),
[lighting regions](rend2-lighting-regions.md) and
[observation-decoding contract](rend2-observation-decoding.md). No RTXGI runtime,
hardware ray tracing, new renderer dependency, shader path or canonical schema
change is introduced.

## 1. Actual implementation inspected

The relevant sources are pinned, not links to a changing `main` branch:

| Reference | Inspected responsibilities |
|---|---|
| [Algorithms][alg] / [Integration][integration] | Application traces and shades rays; SDK filters probes and supplies queries. This is forward GI from known scene/light inputs, not inverse inference. |
| [DDGIVolume reference][volume] and [descriptor][desc] | Grid/volume layout, optional movement, active states, bounds, parameters and storage. |
| [ProbeRayCommon.hlsl][rays] L20–74, L113–134 | Miss/front/back-face encoding; spherical-Fibonacci directions; fixed versus rotated rays. |
| [ProbeCommon.hlsl][position] L28–111 | World positions from lattice/volume transform plus optional offsets. |
| [ProbeRelocationCS.hlsl][relocate] L150–233 | Geometry-based relocation candidates and bounded normalized offsets. |
| [ProbeClassificationCS.hlsl][classify] L135–217 | Back-face heuristic and nearby-geometry relevance classification. |
| [ProbeBlendingCS.hlsl][blend] L403–567 | Angular filtering, moments, capped distances, normalization, history and nonlinear radiance encoding. L232–277 handles octahedral border updates. |
| [Irradiance.hlsl][query] L28–31, L65–203 | Query bias, eight-probe selection, approximate visibility and nonlinear readout. |
| [ProbeOctahedral.hlsl][oct] | Direction ↔ folded-square parameterization; not SH or a single light direction. |
| [Test Harness ProbeTraceRGS][trace] L53–200 | Application-specific tracing/shading, sky misses, recursive irradiance, albedo clamp and radiance saturation. These sample policies are not universal DDGI physics. |

Host `DDGIVolume.cpp` updates ray rotation and scrolling state; descriptor/GPU
packing, probe-data helpers, sample material/hit/miss code and the public math guide
were also examined. No SDK shaders were compiled or executed. Documentation
occasionally uses older property names; the pinned code is authoritative here.

## 2. Probe placement, classification and relocation

### 2.1 What DDGI places and traces

A volume contains a regular 3-D lattice. Without scrolling, its positions are
the rotated, centred lattice translated to the volume origin. Offsets optionally
move each support point. This differs from JA's original `GridLayout`, whose
origin is the **first sample centre**, not a centred volume origin. Converting
between layouts must be explicit; do not introduce a half-cell shift.

Ray directions use spherical Fibonacci sampling. With relocation or classification
enabled, 32 fixed directions remain unrotated for stable geometry diagnostics;
other directions receive the volume rotation and exclude that fixed subset from
irradiance/distance blending. Inactive probes still trace fixed rays in the sample
application so that they can be reactivated. A descriptor seed of zero uses a
time-based default; offline JA work must use recorded deterministic inputs instead.
These details follow [ray helpers][rays] and [volume state][volume].

The application supplies acceleration structures, material handling and outgoing
surface radiance. DDGI does not derive those from old baked observations. The
sample ray generator computes reflected direct light and recursively sampled GI;
a miss receives sky radiance. It also clamps recursive albedo and saturates final
radiance. Those are **sample application choices**, not a suitable JA HDR/physical
forward model. JA sky queries require an actual sky boundary, and unknown/missing
geometry must not become a sky miss. [Application reference][trace]

### 2.2 Classification is relevance, not a solid/free certificate

The pinned classifier first compares the fraction of fixed rays hitting back
faces with a threshold. If it passes that heuristic, it tests whether any front
hit lies before the relevant axis plane at one probe spacing. Otherwise it marks
the probe inactive. The descriptor's fixed-ray threshold is .25; the random-ray
blending threshold is separately .1. [Classifier][classify], [descriptor][desc]

An inactive probe can mean likely inside geometry **or no nearby receiver geometry**.
It does not mean physically solid, unlit, irrelevant to volumetrics or lacking
illumination. Conversely, an active probe is not proof of accessible free space.
Open meshes, two-sided sheets, inconsistent winding and unsampled thin structures
break a simple back-face occupancy interpretation. Fixed finite rays cannot
certify all directions or the whole cell.

For JA, use this separation:

| Question | Evidence / status |
|---|---|
| Is the original sample's point in a solid world brush? | Existing BSP/region domain classification; preserve Free/Solid/Boundary/Unknown. |
| Does an optical segment encounter a retained blocker? | `StaticVisibility::Query` with its geometry/material/precision issues. |
| Is there sufficient certified neighbourhood for a new field support? | Local domain plus geometry/overlap tests and explicit clearance/coverage. |
| Is a support worth updating for a particular diffuse consumer? | A separate relevance/coverage policy; DDGI-style classification can help. |

`GridPresence`, styles, nonzero RGB and encoded direction never prove free space.
`GridGeometry::Unknown` remains appropriate in raw capture. Record new geometry
classification as a derived association; retain all original records and reasons.

### 2.3 Relocation is useful only with fresh field evaluation

The relocation shader uses the fixed rays to find nearby front/back faces. A
likely-inside probe is moved beyond a nearby back-face hit; a too-close probe
gets a candidate displacement away from the nearest face, and well-separated
probes can move back toward their lattice position. The final test is an
**ellipsoid** `sum_k(offset_k/spacing_k)^2 < .45^2`, not three independently
clamped .45-spacing components. [Relocation code][relocate]

The sample stores a back-face distance as `-.2*t`; relocation restores full
distance by multiplying by `-5`, while distance blending uses its shortened
absolute value. This deliberately suppresses problematic supports. It is not an
exact geometric first-hit representation. Keep full distance and facing/material
tags in any JA geometric dataset. [Ray encoding][rays]

For new JA field supports, a bounded clearance search can reuse these ideas:
candidate positions from certified free region members, geometry diagnostics,
restricted moves, exact/local certification of the candidate, then **regenerate
lighting and geometry statistics at that position**. Do not let a candidate cross
a wall or change surface side merely because its final offset fits an ellipsoid.
Unknown geometry leaves the support uncertified; no automatic move is required.

For historical lightgrid, relocation while preserving ambient/directed RGB is
invalid: it changes the observation operator. A simple authored 1-cd point at
`x=1 m` gives 1 lux at `x=0`, but 1.5625 lux at `x=.2`. Carrying the old value
to the new position creates **36% relative error**, even before unknown bake rules.
Keep raw observation position/data immutable; a new probe needs its own identity,
origin/provenance and evaluated value. Relocation cannot recover a valid sample
that the historical compiler never supplied.

## 3. Irradiance and distance representations

### 3.1 A directional irradiance map is not incoming radiance

At a probe `p`, the irradiance for receiver normal `n` is

```text
(D1) E(p,n) = integral_sphere Li(p,w) max(0,n·w) dw
```

`w` points from the receiver/probe **toward the previous radiance source**. The
irradiance map stores this cosine-convolved function over normal directions using
octahedral coordinates. It does not store `Li(p,w)` itself. Distance lookup instead
uses the direction from a probe **toward the query position**. Source propagation
is opposite the incoming direction convention. [Math guide][math], [octahedral][oct]

For `M` uniform sphere rays, an integral estimate is
`E ~= (4*pi/M) sum_r Lr*max(0,n·wr)`. DDGI uses a self-normalized, lower-variance
quantity before encoding:

```text
(D2) c_r = max(0,n·w_r)
     Q(n) = sum_r L_r c_r / (2 sum_r c_r)
     E_readout = 2*pi*Q(n)
```

For uniform radiance `L`, `Q=L/2`, so `E=pi*L`. The factor 2 is intentional.
The full-sphere cosine sum has expectation `M/4`; equivalently a hemisphere's
`N ~= M/2` rays have cosine sum `N/2`. Do not lose another factor of two by mixing
these sample-count conventions. Ratios of random sums are not generally unbiased
at finite count; lower variance is not an exact-integral guarantee. [Math guide][math]

The canonical JA field should expose decoded scene-linear **E**, with stated
Relative/Lux calibration. `SampleIncomingRadiance` needs a separate representation
and evaluator; an irradiance field does not automatically supply angular light
for fog scattering or sharp specular reflection. Raw lightgrid ambient/directed
and deluxe directions are different operators, not DDGI ray sets.

### 3.2 Directional moments in the actual implementation

For distance texel direction `u`, the sample weights are proportional to
`max(0,u·wr)^kappa`. The pinned descriptor starts `kappa=50`. Distances are capped
at `1.5*length(probeSpacing)` and back-face samples use the shortened representation
described above. Weighted first/second moments are

```text
(D3) mu(u) = sum_r a_r d_r / sum_r a_r
     m2(u) = sum_r a_r d_r² / sum_r a_r
     variance = m2 - mu²
```

The shader stores **`(mu/2,m2/2)`**, then multiplies both sampled channels by 2.
These are not moments of `d/2`, whose second moment would be `m2/4`. Values are
also spatially filtered and temporally accumulated. A miss initially encodes a
large finite sentinel; capped moment construction loses the distinction between
far hit, out-of-range and true sky unless separately retained.
[Blending][blend] L466–502; [ray helper][rays] L20–29

Our CPU proposal stores actual typed moments, with distance unit, square-distance
unit, cap, angular kernel/count/coverage, position/generation, facing and optical
policy. Opaque hit, explicit sky, range-censored and unsupported/unknown rays need
separate tags or masks. Missing bins are unavailable, not automatically large
distance. Do not serialize the GPU sentinel or shader-specific half-scale as if
they were canonical geometry.

## 4. What the variance visibility weight really means

### 4.1 One-sided bound, not a segment probability

For a random angular hit distance `D`, mean `mu`, variance `v`, and `t>mu`,
the one-sided Chebyshev/Cantelli inequality gives:

```text
(D4) P(D >= t) <= v / (v + (t-mu)^2)
     C(t) = 1                         for t <= mu
          = v/(v+(t-mu)^2)           otherwise
```

One derivation applies Markov to `(D-mu+b)^2`, bounds the event by
`(v+b²)/(t-mu+b)²`, and minimizes at `b=v/(t-mu)`. This needs the **true moments
of that angular distribution**, not noisy or stale estimates of unrelated rays.
The shader calls the resulting expression a Chebyshev weight. [Lookup][query] L138–152

The actual ray from one candidate emitter to one luxel has a chosen direction;
it is not a random direction drawn from this kernel. Even perfect moments bound
angular survival mass, not whether that particular segment crosses a wall. Nor
is this a posterior over unknown geometry. Finite ray sets, angular/bilinear
filtering, quantization and changing anchor positions add approximation error.

For `t<=mu`, returning 1 is only an uninformative upper bound. It is **not a proof
that the segment is clear**. A large variance often means a doorway/depth edge,
not poor geometric measurement or a noisy baked RGB observation.

### 4.2 Display remaps deliberately sacrifice the bound

The lookup sharpens `C` with a cubic, retains a minimum visibility factor .05,
uses soft normal-facing support, crushes small combined weights, and normalizes
the remaining probe weights. These are interpolation/display controls; cubing an
upper bound does not preserve its inequality, and a positive floor cannot enforce
opaque occlusion. [Lookup][query] L128–172

The independent scalar experiment uses exact discrete distributions:

| Distances | Probabilities | Mean | Second moment | `P(D>=2.5)` |
|---|---|---:|---:|---:|
| `{1,3}` | `{.5,.5}` | 2 | 5 | .5 |
| `{1.5,4}` | `{.8,.2}` | 2 | 5 | .2 |

Both give `C(2.5)=.8` and the same remapped weight despite different angular
visibility. At `t=2.9`, the bound is `.552486`, but its cube is `.168641`, below
the first distribution's actual .5. At `t=1.8`, the shader branch returns 1 while
the survival masses remain .5 and .2. No encoding precision can recover the lost
distribution from two moments alone.

The source uses `abs(mu²-m2)` for numerical robustness. A CPU geometric payload
should instead reject materially inconsistent moments, allowing only a documented
quantization tolerance before `max(0,m2-mu²)`. `abs` can turn malformed input or
overflow into an artificial positive variance, which looks more permissive.

**Inverse-fitting rule:** use moment statistics to prioritize exact queries or
flag angular/depth complexity, never as proof of visibility, physical transmittance,
source confidence, likelihood or a reason to invent a source behind a wall.

## 5. Interpolation, bias and geometric boundaries

The pinned query uses eight lattice neighbours, with selection/trilinear weights
based on the unrelocated grid; actual distances use the relocated positions.
Inactive probes are omitted. Normal-facing support is soft rather than a hard
hemisphere rejection; this helps small surface details retain support but cannot
stand in for geometry. Angular distance statistics suppress probe contributions
across suspected blockers. Octahedral maps use a border to keep filtering across
folded seams consistent. [Position][position], [lookup][query], [octahedral][oct]

The query biases the receiver by normal and view terms. This can reduce
self-occlusion in rendering, but can also move the effective query through a thin
partition or opening. A camera-dependent bias is inappropriate for historical
observations or deterministic inverse targets. Keep the canonical position fixed;
the existing JA `RayBias` records a bounded, scale-aware geometric offset separately.
Choose the side from known geometry, not a viewing camera.

The irradiance codec also matters. Blending applies `1/gamma` encoding; lookup
leaves a square-root-like curve during interpolation and squares the normalized
result. For decoded energies `Ei`, the ideal scalar effect is approximately

```text
(D5) E_interpolated = [sum_i wi sqrt(Ei) / sum_i wi]^2
```

This differs from a linear weighted mean: equal support for 0 and 100 gives **25**
instead of **50**. The pinned integer-format lookup adds a separate precision
correction. These presentation choices and temporal hysteresis are not legacy
bake gamma, source transfer or physical calibration. Use linear quantities for
canonical fitting and explicitly tag any nonlinear consumer approximation.
[Blending][blend] L510–550; [lookup][query] L183–201

### 5.1 Boundary cases and recommended JA treatment

| Case | DDGI idea / limitation | JA fitting and future field requirement |
|---|---|---|
| Thin wall / corner | Angular bins mix near and far depths; soft facing and positive fallback can leak. | Trace the actual segment. Restrict support by surface side/local geometry; refine the field if no nearby usable probe. |
| Open doorway/window | Large variance and mixed visibility; an opening must not be mistaken for a light. | Preserve aperture/path metadata. Allow certified cross-region support through the opening rather than forbidding all different region IDs. |
| Sky/outdoor | Sample treats a ray miss as sky; classifier can deactivate geometry-distant probes. | Require explicit sky evidence; geometry-distant free space is not solid or unlit. Surface and volumetric support have different relevance. |
| Moving door | Stored moments/history can describe the wrong state. | Dynamic/swept bounds and a geometry/state generation; unknown runtime/bake state stays conditional. No old moments proving a newly closed path clear. |
| Sparse old grid | Too few directions/supports for the DDGI operator. | Keep original RGB/styles/direction. Derive independent point validity; never fabricate an angular map or relocate RGB. |
| Unknown alpha/transmission | Renderer-specific hit shaders define optical behaviour. | Keep `Unknown` or stated policy; no statistical .5-transmission guess. |
| Missing patch/bounds | Unobserved blockers may be absent from every sample ray. | Local certification where possible, unbounded omissions still limit clear queries. Missing geometry does not become transparent. |
| Highly localized light | Low-frequency irradiance and coarse probes blur gradients/shadows. | Keep explicit lights independent. Increase geometric/field resolution locally; do not extract lamps from probe maxima. |

Region IDs accelerate support searches and provide useful sides/boundaries, but
are not direct visibility. PVS connectivity is not optical visibility. A disconnected
sample graph need not prove a wall; unknown region membership must not forbid an
otherwise valid source→luxel ray evaluation. Reuse `FilterPair`'s actual-ray result.

Near-wall observations can be especially informative for shadows/localization.
Do not downweight them merely because a distance-bin variance is high. Use actual
coverage/normal/footprint uncertainty, malformed source flags and local optical
support. Refine difficult rays or retain alternative hypotheses rather than erase
the very evidence needed to locate sources.

## 6. What transfers to each stage

| Technique | Offline inverse reconstruction | Later irradiance-field consumers |
|---|---|---|
| Deterministic sphere ray sets | Geometry/clearance diagnostics, reproducible missing-support tests. Not an occupancy proof. | Fresh transport integration and field validation. |
| Relocation | Never change an observation. Generate separate certified support positions. | Improve sampling coverage, then reevaluate data at the new anchor. |
| Classification | Separate domain truth from heuristic relevance; exclude certified-solid cells from physical free-space fitting with reasons. | Skip unhelpful support work, retain explicit availability. |
| Directional first/second distance moments | Optional prioritization/error indicators with recorded geometry/kernel; no direct-light constraint replacement. | Cheap approximate occlusion/support weights after leak/error validation. |
| Octahedral directional maps | Compact representation with known convolution and sample directions. | Candidate encoding for `E(p,n)`; seams/precision are adapter concerns. |
| Exact CPU geometry | Required source-shadow constraints under declared optical policy. | Offline truth/validation and optional precise support selection. |
| Temporal history/convergence | No camera/frame dependence; deterministic batch convergence diagnostics only. | A runtime design choice, independent of solver validity. |

Explicitly irrelevant to this task: DXR/Vulkan RT setup, GPU descriptor heaps,
UAV/SRV textures, shader permutations, ray-generation dispatch, NVAPI reordering,
infinite scrolling camera volumes, UE integration, per-frame barriers and history
resource management. No reason to adopt those architectures to use a mathematical
distance statistic. No hardware RT is required for the existing CPU BVH.

## 7. Why these probes do not recover original lights

Irradiance is a cosine convolution. At one location, a point light 1 m away with
1 cd and one 2 m away with 4 cd yield the same `I/r²` and same normal-dependent
irradiance. Source type/extent and indirect/environment components introduce more
ambiguities. More probe locations can add parallax and visibility constraints,
but DDGI's update does not perform that inverse optimization.

Even knowing the entire continuous `E(n)` loses angular information: the clamped
cosine kernel has zero coefficients for odd spherical-harmonic orders above one
(for example `integral_0^1 u*P3(u) du = 0`). A coarse angular map loses still more.
An irradiance lobe cannot simply be converted into a unique incoming radiance lobe,
point light, spot cone or sun. Geometry moment data describe surfaces, not emitter
existence, intensity or calibrated source confidence.

This does not change the [GI attribution decision](rend2-inverse-gi-research.md):
direct local emitters, direct sun, sky, physically modelled reflected transport,
legacy ambient and unexplained discrepancy remain separate. An irradiance field
cannot make a wrong direct bake profile identifiable or turn mismatch into bounce.
Source-free transport and unresolved fields must not produce fake physical lights.

## 8. Canonical JA boundary and intended field generation

Existing `TransportField` is a descriptor with quantity, unit/calibration, bounds,
optional dimensions and an external dataset reference. It is not a GPU volume.
`Dataset` already supports chunked/sparse payloads. `PhysicalEmitter` is separate.
`IndirectField` metadata presence and `IndirectSampling` evaluation are separate
API capabilities; descriptor-only or unsupported sampling correctly remains
`Unavailable`.

Recommended future pipeline:

```text
immutable original observations + geometry + material/profile hypotheses
    -> inverse fitting with exact/unknown CPU visibility and validated footprints
    -> physical emitter hypotheses + separate indirect/environment/unresolved output
    -> generate NEW field support positions, values and geometry statistics
    -> validate field on independent positions/surface sides and strict rays
    -> immutable chunked field snapshot + optional renderer-specific adapters
```

Proposed CPU field payload requirements (specification, not implemented):

- Stable support ID, original lattice anchor if any, actual evaluated world position,
  displacement/provenance, region/surface side and support/coverage status.
- Explicit `E(p,n)` versus `Li(p,w)` quantity, direction frame and angular basis;
  scene-linear primaries, Relative/Lux/Nits and existing calibration ID.
- Direct-local/direct-sun/sky/root and bounce-order membership, including
  Included/Excluded/Unknown. An application can feed different components into
  DDGI-like storage; its array name cannot define double-counting semantics.
- Optional distance-statistics chunks: full decoded moments, unit/cap/kernel,
  sample counts/coverage, optical/facing policy, missing/sky/range tags and geometry
  generation. Absence means unavailable, not zero distance or infinite visibility.
- Separate numerical field error, geometry/visibility uncertainty, material/bake
  assumptions and photometric-scale ambiguity. Angular variance is not their product.
- Content key covering map/geometry/material interpretation, field algorithm,
  source/profile/calibration generation, support positions, optical state, angular
  kernel and encoding version. Relocation or geometry change invalidates affected
  field samples/statistics; immutable historical observations remain unchanged.
- Bounded chunks/leases, no permanent all-rays matrix or GPU handles. Consumers
  remain read-only and independently opt in; no fog dependency for entity lighting.

The current `VisibilityKind` only names segment transmittance, region connectivity
and sky transmittance. **Directional moments must not be disguised as exact
`SegmentTransmittance`**. A future typed distance-statistics kind/capability needs
versioned semantics, or an explicitly typed field-internal geometry payload.
The public exact `QueryVisibility` should never silently switch to a probe-weight
approximation. Sampling must expose Approximate/Unknown/Unavailable as appropriate,
including no-supported-probe cases, rather than report a valid black field.

The minimum useful first step is an offline support validator based on existing
BSP/visual geometry, followed by **linear, exact-support interpolation** as a
reference. Generate fields from the selected GI model separately. Only then test
moments as an optional approximate consumer optimization. If every nearby support
is blocked/unknown, return unavailable or explicitly select an independently
defined fallback; do not normalize nonzero moment floors into a certified result.

## 9. Memory, precision and work budgets

The following is an illustrative resolution choice, **not an SDK default or new
JA allocation**: 6×6 irradiance interior directions, 14×14 distance directions.

| Representation | Per support | 10,000 supports | Notes |
|---|---:|---:|---|
| RGB FP32 irradiance + two FP32 distance moments + 64-byte metadata | 2,064 bytes | 20.64 MB | CPU values without GPU borders; more validity/indices may be needed. |
| Illustrative R10 irradiance 8×8 + RG16 distance 16×16 + RGBA16 offsets/state | 1,288 bytes | 12.88 MB | Includes one-pixel borders; not canonical photometry or complete SDK resources. |
| Temporary 256 rays, two FP32 words/ray | 2,048 bytes | 20.48 MB | Packed radiance option; can be processed by small chunks. |
| Temporary 256 rays, four FP32 words/ray | 4,096 bytes | 40.96 MB | FP32 radiance/distance option. |

100,000 supports need 206.4 MB for the illustrative CPU record alone; do not assign
one full angular field record to every historical grid cell. Use geometry-aware
sparse support, tiled loading and bounded scratch. Geometry/BVH, allocator overhead,
visibility masks and variability resources are additional costs. No full-map DDGI
throughput or memory use was measured in this task.

At raw JA spacings `(64,64,128)`, the SDK-style capped squared distance divided
by two reaches 27,648, within finite FP16. Doubling spacing raises it to 110,592,
above FP16's 65,504 maximum. Encoding world-unit squared distances in half floats
without validation can overflow. Use explicit units/scales, bounded ranges and
FP32 for CPU reference; quantized codecs must retain their error envelope. The
value `(mu/2,m2/2)` needs the documented two-channel decode, not a guessed unit scale.

Offline cost is approximately `supports × rays × BVH_query_cost` for geometry
sampling, plus angular filtering. A straightforward dense filter costs
`supports × (irradianceBins+distanceBins) × rays`; chunk and reuse geometry-only
results across source hypotheses when anchor/geometry/policy match. Exact eight-
support tests cost up to eight BVH queries per field sample; use them for reference,
boundaries and held-out validation rather than assume a rendering cost target.
Distance moments can be cheaper to query but do not remove their construction cost.

## 10. Numerical fixtures actually executed

The [independent prototype](../tools/rend2/ddgi_research/probe_weights.py) reuses
X04's tiny triangle reference. It reduces moment/display equations to scalar
support weights; it does **not** emulate an eight-neighbour GPU volume, octahedral
bilinear sampling, classification/relocation pipeline, texture quantization or
temporal history. Test supports are active by construction. Values are declared
synthetic field samples (lux), not illumination inferred from a BSP or GPU output.

Angular distance bins use nine deterministic directions, exponent 50 and the
spacing-based cap; every fixture ray hits known opaque geometry. Selected query
rays are independently tested against the actual wall. The two-support reduction
isolates weighting; the all-blocked case models missing/clamped support, not a
claim that one plane can separate an interior point from all corners of a regular
cell containing that point.

| Control | Naive linear | Moment-weighted linear | Moment-weighted perceptual | Strict geometry support |
|---|---:|---:|---:|---:|
| Sealed wall, bright/dark supports `(100,0)` | 50 | .0632412 | .0000399945 | 0 from the visible dark support |
| All supports `(100,100)` blocked | 100 | **100** | **100** | **Unavailable**, not a black-field claim |
| Thin finite partition, mixed angular depths, `(100,0)` | 50 | **41.7949** | **17.4681** | 0 from the visible dark support |

The opaque wall never disappeared from exact geometry. Small positive weights
can mitigate leaks in one case and fail badly in another; normalization cancels
their small magnitude when all supports are blocked. The finite-partition bin has
`mu=.693983`, variance `.363617`, and angular upper weight `.895476` while the
specific bright-support segment is blocked. These are scalar numerical findings,
**not measured NVIDIA GPU shader errors**.

Other executed controls:

- Equal-moment distributions and loss of the bound after contrast remapping (§4).
- A zero-thickness two-sided sheet yields 16 back-face hits of 32 directions
  from an analytically free point. The .25 heuristic would deactivate it; it
  cannot certify solid occupancy or handle all material/winding policies.
- A doorway's selected segment changes from clear to blocked when closed.
  Stale open-state moments can still give weight 1. Geometry/state versions matter.
- Alpha-unknown/missing geometry and absent sky boundary remain `[0,1]` in the
  fixture query; explicit sky is distinguished. Unknown geometry aborts moment
  creation rather than becoming a distance/sky sample.
- Constant-radiance normalization restores `E=pi*L`; nonlinear interpolation
  differs from linear; non-finite/inconsistent moments are rejected; unavailable
  support is distinct from zero illumination. Repeated generation is deterministic.

Twelve new tests passed, alongside 15 existing GI prototype tests, 27 X04 tests
and all 17 standalone CPU CTest targets. No shared production source changed,
so no new SP/MP compilation was required or claimed for this research task.
No game, hardware RT, GPU shader execution or stock-map DDGI field was tested.

### Next acceptance tests before adopting a field approximation

Use existing own-content fixtures: thin/diagonal wall, corner, narrow door/window,
skylight, curved patch envelope, moving-door state and localized emitter. Sweep
spacing, field resolution, angular kernel, sample budget, surface bias and source
position. Compare each query against the exact CPU reference at spatially held-out
locations; measure leak energy, dark-side error, unsupported rate, normal dependence,
seam continuity and build/query work separately. Do not optimize for zero unknowns.
Repeat with missing/alpha/uncertain geometry; confidence must not silently improve.

For inverse fitting, require no false clear/blocked certification introduced by
the approximation: moment-only results remain approximate/unavailable. For display
fields, choose a declared leak/prediction tolerance after measuring representative
JA scenes, with a visible fallback policy. A tiny positive weight or low RMSE is
not proof of correct source attribution or physically calibrated irradiance.

Exact commands, source verification and fixture limitations are in the
[tool README](../tools/rend2/ddgi_research/README.md). The small
[results witness](../tools/rend2/ddgi_research/results.json) records all numerical
inputs/results. No physical emitter recovery is implemented or claimed.

[alg]: https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/f33e496ca31b3f0eec1c4e2cbaa8bb620e337fa6/docs/Algorithms.md
[integration]: https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/f33e496ca31b3f0eec1c4e2cbaa8bb620e337fa6/docs/Integration.md
[volume]: https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/f33e496ca31b3f0eec1c4e2cbaa8bb620e337fa6/docs/DDGIVolume.md
[desc]: https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/f33e496ca31b3f0eec1c4e2cbaa8bb620e337fa6/rtxgi-sdk/include/rtxgi/ddgi/DDGIVolume.h#L82
[rays]: https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/f33e496ca31b3f0eec1c4e2cbaa8bb620e337fa6/rtxgi-sdk/shaders/ddgi/include/ProbeRayCommon.hlsl#L20
[position]: https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/f33e496ca31b3f0eec1c4e2cbaa8bb620e337fa6/rtxgi-sdk/shaders/ddgi/include/ProbeCommon.hlsl#L28
[relocate]: https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/f33e496ca31b3f0eec1c4e2cbaa8bb620e337fa6/rtxgi-sdk/shaders/ddgi/ProbeRelocationCS.hlsl#L150
[classify]: https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/f33e496ca31b3f0eec1c4e2cbaa8bb620e337fa6/rtxgi-sdk/shaders/ddgi/ProbeClassificationCS.hlsl#L135
[blend]: https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/f33e496ca31b3f0eec1c4e2cbaa8bb620e337fa6/rtxgi-sdk/shaders/ddgi/ProbeBlendingCS.hlsl#L403
[query]: https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/f33e496ca31b3f0eec1c4e2cbaa8bb620e337fa6/rtxgi-sdk/shaders/ddgi/Irradiance.hlsl#L65
[oct]: https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/f33e496ca31b3f0eec1c4e2cbaa8bb620e337fa6/rtxgi-sdk/shaders/ddgi/include/ProbeOctahedral.hlsl
[trace]: https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/f33e496ca31b3f0eec1c4e2cbaa8bb620e337fa6/samples/test-harness/shaders/ddgi/ProbeTraceRGS.hlsl#L53
[math]: https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/f33e496ca31b3f0eec1c4e2cbaa8bb620e337fa6/docs/Math.md
