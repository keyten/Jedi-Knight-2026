# ASR17 — stock source placement diagnosis

This is an offline geometry diagnosis, not a change to source admission or a
claim about historical lamps. The reproducible protocol, classifications,
numerical residuals, input hashes and resource measurements are in
[`tools/rend2/emitter_support/results/asr17-v1`](../tools/rend2/emitter_support/results/asr17-v1).
Exact stock points, triangle vertices, planes and rational penetration witnesses
remain in the licensed private dataset directory. No observation targets enter
the stock diagnosis, no stock fit is run, and encoded RGB8 is not interpreted as
irradiance. The existing owned native test retains its separate synthetic fitting
and Joint smoke checks.

## Reproduction and identity

The checkout is `249bd480fb52a97df5cb2464cea6bcfc8c98f87b` on
`rend2-unified-wip`; its configured origin is `git@github.com:keyten/BetterJK.git`,
rather than the repository name in the request. Work was performed in the
supplied checkout. The input datasets match X14 v3 byte for byte.

Two comparisons answer different questions:

1. The **frozen X14 v3 executable** reproduces all three historical admission
   reports and byte-identical ordered `mounting-witnesses.tsv` files.
2. The **current executable** is run with and without `mounting-diagnosis`.
   Admission reports (excluding elapsed time) and ordered witness TSVs agree
   byte for byte. The extra diagnostic queries cannot grant admission.

Current candidate identities changed after X14: `Candidates::kVersion` is now 2,
and `candidate_pool.cpp` hashes it into catalogue/bundle identities. The bounded
first-256 selection therefore changes on the two larger maps. This is not a
placement improvement/regression measurement. The six-significant-digit X14
geometry signature matches 108/108 `mp/duel4`, 54/256 `mp/siege_desert`, and
129/256 `hoth3` rows in the current samples; every unique match retains its state
and reason. This signature is a link to the archived witness precision, not a
new full-precision identity contract.

| Region/map | Frozen X14 collision / Unknown / invalid | Current collision / Unknown / invalid | Current admitted |
|---|---:|---:|---:|
| `mp/duel4` | 17 / 87 / 4 | 17 / 87 / 4 | 0 |
| `mp/siege_desert` | 4 / 252 / 0 | 2 / 252 / 2 | 0 |
| `hoth3` | 31 / 209 / 16 | 37 / 208 / 11 | 0 |

`Collision` is the existing classification of `Solid`; `invalid` is the existing
classification of `Mount`. None of these aggregate names certifies a historical
source. Current IDs are stable within the frozen current protocol and the small
map repeat. `protocol.json` pins source and executable hashes before evaluation.

## Prioritized causes

### P0: raw winding is being used as the emitted side

`SurfaceEvidence` in `shared/rd-rend2/lightrecon/evidence.cpp:796` computes a
triangle cross product and stores its area-weighted direction as `e.outward`
at line 908. `SourceSupport::FromSurfaces` in `emitter_support.cpp:769` repeats
the raw cross-product interpretation and saves it in `c.outward` at line 812.
`Producer::Convert` (called by `MakeCandidate`) in `producer.cpp:1205` converts
this into the mounting contract without independent side interpretation.

The receiver path already handles the relevant convention:
`MapLuxels` in `surfaces.cpp:1027` compares the geometric normal with
captured vertex normals (or the face lightmap basis) and flips an opposing
winding, recording `LuxelWindingReversed`. The source path does not do this.
`GL_Cull` in `shared/rd-rend2/tr_backend.cpp:139` also shows why a raw winding
convention must not simply be equated with the visible side.

For **620/620** candidates, every referenced captured vertex normal opposes
`c.outward` (normalized dot below −0.999). All 620 named surfaces carry
`SurfaceCull::Front`. The representative axis-aligned witnesses have dot exactly
−1; an oblique `hoth3` witness has dot −0.9999999828857289. This comparison uses
only captured geometry normals, not lightmap values or a hidden source answer.

Geometry-only probes at **+0.125 world unit along the declared outward normal
are inside retained world solid for all 620 sampled current candidates**. At
−0.125, 618 are Free and two are Solid. These are samples of the retained BSP,
not certificates for a whole emitter or a proposed relocation. The corresponding
ray probes retain their conservative Unknown results when their origin is in
solid; they are not failed visibility coverage proofs.

This is a systematic side-convention defect in the supplied hypotheses. It does
not establish that reversing every source would make it admissible. Actual
penetration, two-sided emission, patch envelopes, fixture dimensions and every
other brush still require their existing checks.

**Next engineering change:** establish a versioned source-side interpretation
using captured normal/basis/cull evidence plus independent retained-brush support
checks. Share the established receiver orientation convention where applicable;
retain Unknown for contradictory or absent side evidence. Re-extract/rederive
archived source evidence under that interpretation: editing the extraction
function alone does not replace evidence already stored in `.jkobs`. Revalidate
the full support, not only the centre. ASR17 applies none of these fixes.

### P1: BSP boundary Unknown is not a penetration verdict

`BspRegionDomain::ClassifySupport` in `regions_support.cpp:102` only separates a
touching brush via a contact whose normal/distance match the declared mount.
`Contact` at line 74 forbids negative penetration repair. An exact support range
`[0,0]` on the **opposite** brush face cannot match that declared normal, so the
brush falls through to Unknown at line 223. A valid contact with another brush
does not erase this unresolved brush.

| First diagnosed failure | `mp/duel4` | `mp/siege_desert` | `hoth3` |
|---|---:|---:|---:|
| Opposite coplanar brush boundary | 54 | 252 | 147 |
| Other brush-plane boundary | 0 | 0 | 33 |
| Independent exact penetration witness | 2 | 2 | 26 |
| Cylinder body through mount | 15 | 0 | 11 |
| Stand-off/side violation | 4 | 2 | 11 |
| Local patch approximation envelope | 33 | 0 | 28 |

These are exclusive first-failure categories. `facets` in `stock.json` records
overlapping secondary evidence, including unclustered leaves without a matching
contact. It would be incorrect to sum the facets as independent rejected lamps.
Private `classification.json` records every tested support, traversed leaf,
unseparated brush and exact plane residual; multiple supports/brushes can explain
one candidate. Primary classification follows the first failing production
support, while secondary queries inspect all pieces even after an early return.

**Next engineering change:** resolve the emitted-side interpretation first.
Then distinguish a proved nonpenetrating edge contact from an inconclusive
brush intersection with an exact bounded support/brush test and explicit side
evidence. Keep an unnamed or opposite-side boundary unresolved until that proof
exists. Do not replace `lo < 0` with a tolerance clamp or exempt the named brush.

### P1: “Incomplete local visual support” is patch scope, not absent stock triangles

All 61 current first failures with this message have `PatchApproximation` in
`LocalGeometry::issues`. `StaticVisibility::ExtractBox` in `visibility.cpp:691`
exports conservative triangle AABB overlap. At lines 742–745 **any** overlapping
triangle with `errorWorld > 0` sets `coverageComplete=false`.
`SourceSupport::Check` in `emitter_support.cpp:397` immediately rejects that
result, before examining mount identity, actual polygon intersection or brush
support.

The three BVHs have complete spatial coverage, zero unbounded omissions and zero
unsupported bounds. “Incomplete” here means that a nearby curved-patch error
envelope prevents an exact local certificate. It is not evidence that licensed
render triangles are unavailable, or that the panel collided with that patch.
For example, current `mp/duel4` source `997137809388226990` encounters local
patch error bounds of 4.688804316633247–4.688810992353462 world units. The archived
offline metadata defaults to an 8-unit patch tolerance (`offline.h:32`); the
stock runner restores the dataset's declared tolerance, not the visibility
library's default 0.01. An exact opaque-support checker consequently cannot
pretend these retained approximations have zero error. No one-plane separation
proof removes all approximate triangles for any of the 61 early patch failures;
a tighter local geometric test would still need to establish separation.

**Next engineering change:** preserve the patch envelope, but test whether it
can actually intersect the occupied support using a bounded separating-plane or
distance proof. Refine relevant patch error only if necessary. Exact opaque
mount coverage and independent optical blockers must remain mandatory.

### P2: unsupported tube volume and centre/plane roundoff are upstream hypotheses

`SourceSupport::Check` in `emitter_support.cpp:491` computes the occupied
cylinder's lowest signed plane distance. All 26 rejected tubes genuinely extend
behind their declared mount. Their fitted centreline lies on/near a luminous
surface, which does not supply a finite-radius body, recess or stand-off.
`producer.cpp:1210` changes the support kind to `Cylinder` without inventing that
missing body relationship. These proposals should stay rejected. A future
fixture-specific cylinder needs independent body/cavity geometry.

At `emitter_support.cpp:362`, a negative signed source position or an unprovided
positive stand-off is rejected even when tiny. `Emission::FitRectangle` in
`emission_geometry.cpp:86` creates a float centre by centroid/frame arithmetic;
that centre need not remain exactly on the original plane. The private witness
retains the exact signed ranges, and the public numerical witness records the
residuals. One `mp/duel4` point is **−1/4096 = −0.000244140625 world unit** behind
the declared plane. This is a representational inconsistency, not permission to
push the point out of solid or assume a fixture dimension.

The 17 cases include 12 ideal-point centres and five original triangle unions.
All five unions are on `hoth3`; `FromSurfaces` verifies coplanarity with its
double normal before saving a float normal. The later strict signed-plane check
then sees ranges such as [−6.258487701416016e−7, 0]. Thus centre construction and
normal quantization are distinct upstream paths to this rejection.

**Next engineering change:** preserve an exact/parameterized planar support and
carry an explicit bounded numerical interpretation through candidate creation.
Keep physical stand-off separate from arithmetic uncertainty. Do not infer a
mount from nearby geometry or a successful fit.

## Numerical witnesses and controlled cases

`witnesses.json` publishes stable IDs, plane/brush ordinals and residuals without
stock points or plane equations. The exact geometry is in private
`diagnosis-world.json`, `diagnosis.jsonl` and `classification.json`, whose hashes
are published. Hull plane residuals and clipped penetration centroids are
computed independently with Python `Fraction` arithmetic over the stored binary
values. A clipped point stays on the original polygon; rounding a newly computed
centroid off that plane cannot manufacture a penetration witness.

The X14 rectangle witness `378495349532519697` links to current ID
`8734946737344204972`; its complete original triangle support is retained. The
X14 tube `17291380550458404209` links to current ID `140031554626216494` and still
fails its occupied-radius/mount test. The crosswalk records its serialization
precision limitation explicitly.

For the rectangle, brush 312 / plane 638 has exact signed range **[0,0]** and
normal dot **−1** against the contract, while plane 449 has range **[−68,−68]**
and normal dot **+1**. This independently explains why the declared contact
cannot separate brush 312. The tube's occupied plane range is
**[−0.28869059681892395, +0.28869059681892395]**. A separate `siege_desert`
penetration witness has maximum signed distance **−4** over every brush plane;
some other stored-geometry penetrations are much smaller. These are exact
statements about the retained geometry, without asserting the accuracy of the
historical map compiler's geometric representation.

Owned native controls in `lightrecon_emitter_support_test.cpp` include:

- the same planar geometry with aligned versus opposing captured vertex normals
  and raw winding, preserving the present rejection;
- a panel touching another brush at `x=1` (Unknown), a brush beginning at
  `x=1.001` (Clear), and actual overlap beginning at `x=0.5` (Solid);
- a shared opposite brush face (Unknown) with an outward `z=0.125` point in solid;
- a local patch envelope (Unknown) and a distant query with complete coverage;
- opaque versus alpha/translucent mounts, tangent versus penetrating tubes,
  correct versus incorrect stand-off/side, stale leases and work/cancellation
  limits, including the pre-existing X14 positive cases.

Five independent Python numerical controls check exact negative residuals,
opposite-face contact, edge/gap/penetration, rational oblique-plane clipping and
finite-cylinder support. These verify explanations, not a second permissive
admission implementation.

## Evaluator and access limits

All sampled named supports are static BSP geometry. The classifications do not
identify model-instance geometry or stale map/lease checks as first failures.
The static BVH still excludes brush/deformed model geometry and does not import
external MD3 instances. `CaptureBspSpace` (`offline_bsp.cpp:123`) preserves
non-world model bounds as conditional occupancy evidence; it does not supply
their bake pose or full triangles. Because these candidates fail earlier stages,
the results do not certify independent model occupancy or establish that model
coverage is sufficient for future fits. Unknown alpha/translucent optics remain
unknown; no opacity policy is relaxed.

The diagnostic limit is 256 current candidates and 16M additional native work
per map; each ray/support query also has a bounded work budget. Each candidate
gets two rays through/along the first support's interior normal and four point
probes at ±0.125/±4 world units. The original candidate pose is unchanged. These
short probes cannot replace uniform occupied-support or receiver-ray proofs.
The process safety limit is 300 seconds and 1 GiB. Current catalogue selection
is not exhaustive source discovery, and geometric plausibility is not exact
historical-source identification.

Native diagnostic work was 111,963 / 194,997 / 360,755 for the three maps;
independent analysis work was 34,974 / 71,480 / 178,419. Whole diagnostic
processes took 2.782 / 8.907 / 11.172 seconds and peaked at approximately
34.2 / 51.7 / 85.8 MiB. These include dataset loading and BVH/catalogue building;
Python rational-analysis time is outside those native process timings.

Validation: **64/64 native assertions** on both runs, **5/5 independent Python
controls**, and **3/3 targeted CTests** (visibility, regions, emitter support).
The small-map repeat has byte-identical admission, geometry, and classification
witnesses. No renderer build or game launch is needed for these offline-only
changes.

Reproduction (use a new output name; preserve existing result directories):

```powershell
$env:PATH = 'C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;' + $env:PATH
cmake --build build/s06-gcc --target lightrecon_map_solver lightrecon_emitter_support_test lightrecon_regions_test lightrecon_visibility_test -j 2
python -X utf8 -m unittest tools.rend2.emitter_support.test_diagnosis -v
ctest --test-dir build/s06-gcc -R '^(lightrecon_emitter_support|lightrecon_regions|lightrecon_visibility)$' --output-on-failure
python -X utf8 tools/rend2/emitter_support/run_diagnosis.py --private "$env:TEMP/jk-lightrecon-all-stock-20261031" --out tools/rend2/emitter_support/results/asr17-v2
```

No canonical source/storage schema, historical solver, renderer, world shading,
GPU/runtime consumer or double-lighting behavior is changed. Unknown is never
converted to Clear. No source-certification or fitted-light improvement is claimed.
