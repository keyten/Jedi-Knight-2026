# ASR19 — choose a modern fixture representation

This follows [ASR18 mounted admission](rend2-mounted-admission-asr18.md).

`ShapeSelection::{Rank,FitAndValidate,AuditHeldout}` adds an **offline, geometry-led
representation ranking** over the existing point, spot, rectangle and tube types.
It is independent of the historical candidate scheduler and does not publish an
asset or runtime light. The existing canonical schema, evidence extraction,
source-support predicates, historical transport mathematics and renderer remain
unchanged.

The objective is a plausible modern source hypothesis. A low error against the
old bake does not identify a historical lamp or justify collapsing a visible
panel into a point. Geometry preference, placement, model error and uncertainty
remain separate fields.

## Inputs and ranking

The request pins the original `SurfaceGeometry`, visual BVH, retained BSP domain,
fixture identity and original surface ordinals. Planar geometry uses
`SourceSupport::FromSurfaces`, including captured outward-side interpretation,
exact triangle leases, connectedness and coplanarity. Width, height, projected
area, aspect ratio and rectangle coverage derive from actual vertices. The
existing support check still decides whether each rival is admissible. It does
not infer a stand-off, remove a wall or turn unknown geometry into empty space.

| Geometry cue | Preferred modern representation | Limits retained in the output |
|---|---|---|
| Pin-sized emitting aperture | Point proxy | Ideal point; actual finite aperture retained in the result |
| Small aperture inside a supported hood, or an independent cone cue | Spot proxy | Smooth angular distribution is assumed, not measured photometry |
| Flat rectangular panel | Rectangle with original emitting triangles | Lambertian distribution and uniform amplitude are hypotheses |
| Long, flat luminous strip | Rectangle | Aspect ratio alone cannot establish a cylindrical body |
| Explicit measured/author-attested cylindrical body and named mount | Tube | Radius, centreline and stand-off come from the body witness, never from a sheet offset |
| Irregular connected or disconnected emitting regions | Unknown primitive | Exact connected union or bounded original surface references are retained; no broad rectangle fills holes/gaps |

Default policy is a maximum point extent of **4 world units**, strip aspect
**6**, and rectangular coverage within **1e-5**. These are explicit aesthetic
prior thresholds, not calibrated physical confidence. Coverage gates use a
bounding frame for measurement; prediction and admission retain the original
triangle union. The wrapper never replaces it with a broad unsupported quad.

Hood surfaces must be explicitly named static planar geometry. A clear axial
probe and four opaque lateral probe hits on the named hood supply a **sparse
direction cue**. Unknown alpha, missing geometry or an unknown ray cannot supply
that cue. The angle comes from hood dimensions, with an explicitly assumed
smooth transition. This is not a proof of reflector material, a full angular
profile or a continuous visibility certificate. An optional cone cue needs
separate provenance and must be derived independently of withheld targets.

Tube bodies need positive finite dimensions, a valid frame, provenance and a
pinned `Cylinder` mounting contract. Their occupied support goes through the
unchanged X14/ASR18 checks. A planar luminous strip does not supply tube radius,
body geometry or a cavity. A point proxy that alone passes placement while
collapsing a large visible emitter leaves `primitiveKnown=false`.

Each result contains a stable key and rank order, actual geometry/bounds, numeric
ordinal preference, `Supported`/`Approximation`/`Unknown`/`Rejected` status,
reason codes, uncertainties and placement results. The key includes geometry,
fixture, policy, emitted side, dimensions, support and placement-policy identity.
Preference is **not a probability or a penalty added to baked RGB error**.

## Luminous mask boundary

Exact emitting geometry requires `fullSurfaceEmissionAttested` plus provenance.
Without it, the source face is a bounded emission approximation: `MaskUnknown`
is recorded and `primitiveKnown=false`. Glow or a successful fit does not fill
in an unavailable texture mask.

The original `.jkobs` evidence stores sampled mask blobs and fitted bounds, but
does not retain an exact clipped triangle lease for each luminous blob. The
stock audit therefore ranks the immutable whole-face bounds conditionally; it
does not claim to have recovered that mask. Existing source-side orientation and
geometric admission remain useful, but are separate evidence.

## Fit, validation and held-out audit

`FitAndValidate` reads a sealed training partition and a separate validation
partition, snapshots their bounded immutable rows, and fits **RGB amplitudes
only** through the existing `Shape::Fit`. Positions, frames, dimensions and
cones are frozen by the geometry prior. Each rival sees the exact same rows and
weights. Receiver material identity and comparable material response must be
explicit; sample IDs and spatial blocks cannot overlap across partitions.

The caller chooses either attested relative-linear targets or a declared
historical encoded-RGB8 model. Encoded RGB8 cannot enter the modern integral as
calibrated linear irradiance. The old exact-contact historical point/spot guard
remains `Unavailable` rather than returning black or fabricating an endpoint
trace.

The validation-equivalent set retains fully covered models that improve on a
black prediction and fall within explicit MSE slack plus squared quadrature
discrepancy. An unavailable rival is not disproved. Numerical discrepancy is a
nested-rule diagnostic, not a certified confidence interval.

`AuditHeldout` takes a frozen comparison and another disjoint same-material
partition. It uses the frozen models, visibility context and sampling, and cannot
rerank geometry or change the validation-equivalent set. A repeat audit or leaked
sample/block is rejected. No target value enters `Rank`.

## Numerical witnesses

The current reports and protocol are under
[`tools/rend2/shape_selection/results/asr19-v2`](../tools/rend2/shape_selection/results/asr19-v2).
The owned JSON includes actual triangle coordinates, candidate frames/dimensions,
reason codes, uncertainties, conditional MSEs and unavailable-model limitations.
Training, validation and held-out samples occupy distinct receiver regions of
one explicitly comparable owned material.

The final owned run passes **352 assertions across 14 fixtures**, including the
negative controls. The report repeats byte for byte. Validation receipts are in
`checks.json`: **54/54 selected CPU tests**, the final **3/3 targeted checks**,
**5/5 Python diagnosis tests**, and a warning-clean C++11 syntax check. The CPU
suite excludes only the unchanged long-running coordinate-refinement test
already passed in ASR18.

| Owned fixture | Geometry preference | Held-out result |
|---|---|---|
| 0.1 × 0.1 pin aperture | Point approximation | Finite aperture area retained: 0.01 WU² |
| 12 × 8 flat panel | Rectangle | Relative MSE ≈ 0.00185; point ≈ 53.61 |
| 16 × 0.5 flat strip | Rectangle | Tube is Unknown: no body/radius witness |
| Hooded 0.2 × 0.2 aperture | Spot approximation | Spot MSE 0; isotropic point ≈ 0.00141; area remains a conditional photometric alternative |
| Measured length-12, radius-0.25 cylinder | Tube | Relative MSE ≈ 0.0000732; point ≈ 0.5737 |
| Partially blocked panel | Rectangle | Independent blocker remains in predictions; relative MSE ≈ 0.00152 |
| Distant receivers, 200 WU away | Rectangle geometry prior | Point and area retained as photometrically equivalent |
| Irregular L union / disconnected surfaces | Unknown | No filled bounding rectangle or automatic merged lamp |

The **baked-point/visible-panel** witness declares a separate authored historical
point one unit in front of the 12 × 8 panel. Its held-out encoded interval MSE is
**zero**, while it collapses a visible **96 WU²** emitting area. The modern
geometry ranking still prefers the panel. Its historical area-model MSE is
approximately **950.24**. These are conditional encoded-output errors, not SI
irradiance errors or a claim that modern geometry should reproduce old authoring.
The hypothetical exact-contact point rival remains historically Unavailable.

Negative controls cover unknown alpha hoods, optional cone provenance, invalid
tube dimensions, through-mount tubes, a panel intersection where only the point
proxy passes, unknown masks, stale maps, placement-policy keys, cancellation,
work limits, material mismatch, encoded-as-linear use and held-out leakage.

## Stock audit and reproduction

`lightrecon_map_solver ... shape-selection-only` runs the new API without reading
observed target RGB or running a stock fit. Selection is the first 64 stable
original fixture IDs per map; exact surface aliases are collapsed only for this
audit. Exact stock triangles, source frames and bounds stay in private JSONL.
Public reports retain IDs, material names, reasons, aggregate counts and hashes.

The audit covers **8 `mp/duel4`, 64 `mp/siege_desert`, and 64 `hoth3` fixtures**.
All **136** keep the explicit luminous-mask Unknown. Supported whole-face bound
proxies rank as point/rectangle in **4/2**, **9/54**, and **21/14** cases,
respectively; the other cases have no admissible primitive or are irregular.
These counts are not newly admitted lamps. No stock hood identity or cylindrical
body is fabricated.

The runner compares all three existing `mounting-only` reports and ordered
source/rejection-reason TSVs against ASR18: they must match semantically and byte
for byte, respectively. The small-map shape audit and owned numerical reports
repeat byte for byte. Source, executable and original archive hashes are frozen
before evaluation. Existing Shape, evidence, support, BSP and historical solver
files are checked against the frozen ASR18 protocol.

```powershell
$env:PATH = 'C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;' + $env:PATH
cmake -S tools/lightrecon_test -B build/s06-gcc
cmake --build build/s06-gcc -j 2
ctest --test-dir build/s06-gcc --output-on-failure -j 2 -E '^lightrecon_point_refine$'
python -X utf8 tools/rend2/shape_selection/run.py --private "$env:TEMP/jk-lightrecon-all-stock-20261031" --out tools/rend2/shape_selection/results/asr19-v2
```

Use a fresh output directory for another run. The unchanged, long-running
coordinate-refinement test was already covered by ASR18's full 54-test run; this
change runs the other CPU tests, including the new selection test and existing
shape, producer, Joint and mounting regressions. Canonical asset files and the
historical solver are unchanged. No GPU consumer, world shading, double-lighting
behavior or runtime activation is implemented.
