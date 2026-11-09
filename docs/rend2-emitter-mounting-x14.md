# X14 — occupied source support, emitting geometry and mounting

Follow-up: [ASR17 stock placement diagnosis](rend2-placement-diagnosis-asr17.md)
reproduces the frozen X14 witnesses and traces the current failures without
changing admission semantics.

The CPU source checker now distinguishes an emitting support from its enclosing
vacuum cube. `SourceSupport` is reused by production `Shape::Fit` and `Joint`
placement. Existing `PointRefine::Check` world-solid and displacement predicates
are preserved. No renderer, LTC, fog, lightmap upload or gameplay mover code is
changed by X14.

## Contracts and numerical scope

| Contract | Occupied/emitting support | Mounting and limitations |
|---|---|---|
| LegacyVacuum | Existing enclosing-clearance checks | Unchanged callers and frozen regression baseline; no inferred mounting |
| FreePoint / FreeSpot | Ideal zero-volume point; point/spot angular model remains separate | Nonzero effective source radius is unsupported; no implicit empty sphere |
| MountedPoint / MountedSpot | Ideal emission point on the named outward side | Named original fixture, plane, outward normal and explicit maximum stand-off; spot axis must point outward |
| Rectangle | Exact planar quadrilateral, zero thickness, one Lambertian emitting side | Optional named support; actual polygon must be covered by its triangles |
| TwoSidedRectangle | Same quadrilateral with explicit emission on both sides | Requires side evidence and independent world-solid admissibility on the back side; not a licence to shine through a backing wall |
| Cylinder | Closed occupied finite cylinder; only its curved lateral surface emits | No glowing end caps; mounting tangent line must lie on the named support; intersection with unrelated visual triangle AABBs is conservatively Unknown |
| OriginalSurface | Exact immutable planar triangle union, with per-triangle surface identity | No bounding-rectangle replacement; disjoint/multi-part, overlapping, curved, unknown-optics or inconsistent-side assemblies stay unsupported |
| RecessedRectangle | Actual panel plus a caller-declared empty forward prism | Requires an actual cavity in the brush/visual geometry; conditional fixture/bake interpretation remains explicit |
| UnsupportedAssembly | No fabricated body or luminous area | Explicit unavailable geometry |

`frontClearanceWorld` describes a geometric region to check, not a source push.
`maximumStandOffWorld` is a declared fixture dimension, defaults to exact contact,
and cannot be inferred from a successful RGB fit. Fixture identity is upstream
evidence, never spatial proximity, material name or similar colour. Different
nearby lamps retain different IDs. Connected original geometry is obtained from
named BSP surface ordinals; transport patches, observation folds and GPU atlas
indices do not define its shape.

New support contracts currently require a fresh certificate for a changed pose.
The bounded fitter cannot carry a mount-contact certificate across XYZ/frame
motion. Existing legacy refinement remains available. Original triangle unions
are fixed geometry; their physical quadrature and explicit historical triangle
expansion share one root RGB amplitude, not one invented lamp per triangle.

## Predicates and independent visibility

`RegionDomain::ClassifySupport` has a conservative box fallback. The retained
`BspRegionDomain` uses actual support functions against original BSP planes:

```text
H(n,d) = [min_v n·v-d, max_v n·v-d]                 (convex hull)
H(n,d) = n·c-d ± (h |n·a| + r sqrt(|n|²-(n·a)²)) (finite cylinder)
```

Brushes remain present. A named outward contact plane can separate the source
from a brush only when the minimum signed distance is nonnegative and matches
the declared plane within arithmetic roundoff. A negative penetration is never
clamped to zero. Planar support clipping can prove penetration; inconclusive
brush/cylinder/corner cases remain Unknown. The support's actual world bounds
must also remain inside the retained map bounds. A named brush-boundary contact
may be assigned to an unclustered BSP leaf by the tree's equality convention;
that does not make the zero-thickness support an occupied volume. Only an actual
matching brush contact can admit this case. A no-contact unclustered support
stays Unknown, and all other intersecting brushes are still tested. The owned
negative-leaf fixture checks a flush panel, an unmounted boundary point and a
point inside the solid separately.

The visual BVH supplies a bounded local triangle set. The checker verifies
opaque, exact named support, projected area coverage, no duplicate mounting area
and correct outward side. The original emission lease must match the pinned BVH
triangles. Unrelated opaque polygon intersections still reject placement;
unknown alpha/translucency and incomplete geometry do not become empty space.
Conservative front-prism visual overlap is Unknown, not proof of collision.
All checks have work/memory/cancellation limits and retain reasons.

`PointRefine::CheckOccupancy` then applies the separate X13 bake-state policy to
the actual support bounds. It does not grant a static collision or ray exemption.
Unknown model states remain conditional. Incomplete region graphs are not a
mandatory failure when local world/BVH geometry supports the query.

Emission and receiver visibility remain different predicates. Physical finite
quadrature traces every contributing source/receiver pair. Its endpoint
exclusion names only the originating source surface and uses coordinate-derived
float roundoff, not a universal shadow dilation. Other blockers are retained.
An ideal point exactly on an opaque mount emits only into the declared outward
side. Two-sided/back-side emission needs its own support; a positive centre
certificate cannot mark all receiver rays clear.

## Historical versus physical prediction

Physical evaluation remains the explicitly declared irradiance integral:

```text
E(x,n) = integral_A Le(y) max(0,n·u) max(0,ny·(-u)) V(x,y) / |x-y|² dA(y)
```

Two-sided free sheets use the absolute source cosine. Original triangle unions
are subdivided into equal-area triangular cells; the refined rule doubles the
subdivision density. Triangle/rectangle quadrature comparison measures numerical
discretization, not agreement with Raven's baker. The full integral is not
substituted for compiler point sampling.

Historical point/spot recipes and S01 feature-scoped profile validation are
unchanged. Exact point/spot-on-mount historical tracing is **Unavailable**: X14
does not invent source endpoint rules for an unverified compiler case. A positive
stand-off uses the existing historical operator. Finite historical sources require
the explicit polygon-expansion hypothesis and its declared compiler source push;
that push is not used to manufacture physical source clearance. A triangle union
is expanded as those triangles, not as its bounding rectangle. Uniform emission
over the full named geometric surface is an explicit numerical hypothesis;
original shader-mask support is not recovered or certified by this construction. Native historical
tube emission, original shader masks/amplitudes and Raven production settings
remain unverified. Relative Q is never candela/nits.

## Integration, identity and publication

`Shape::Emitter::support` contains a versioned contract, fixture and surface IDs,
map/geometry hashes, side/stand-off/prism assumptions, provenance and an immutable
triangle lease. Contract schema is 1, numerical geometry interpretation is 2.
`SourceSupport::Key` enters Shape, Joint and Producer identities.
Versions: SourceSupport 1, Shape 3, PointRefine 3, Joint 6, Producer 6. A changed
geometry/contract invalidates prepared evaluation and scene-provider bindings.

Supplying `SceneInput::originalSurfaces` opts Producer's original-surface evidence
conversion into these contracts. It must be the exact lease used to build the
visibility provider and must contain neither present nor hidden vertex-colour
target bytes. A negative test verifies rejection of this alternate observation
route. Existing callers without this lease retain the old declared
clearance policy. Unsupported assemblies become explicit unsupported hypotheses,
not a wide panel. No source type is inferred from low error alone.

Joint receipts/assumptions preserve the support result and its occupancy limits.
Canonical point/spot/rectangle/tube provenance includes the support key. A numeric
OriginalSurface fit cannot yet publish a canonical mesh-source payload: Producer
returns Unavailable rather than serialize its bounding rectangle. No disk asset
schema was silently changed. Publishing exact mesh emitters requires a future
versioned source-geometry payload and consumer support. Conditional mounting or a
converged encoded-RGB fit supplies no exact-light/replacement certificate.

## Reproduction and evidence

The reproducible runner is `tools/rend2/emitter_support/run.py`; raw commercial
geometry witnesses stay in the private stock dataset directory. It freezes map,
dataset and native executable hashes before collecting the named stock results,
then repeats the native witness and small MP map. The final manifest records
source hashes, which are verified against the completed worktree. Only aggregate
classification, reason counts, hashes and resource measurements are published.
Previous X12/X13 witnesses are preserved.

The stock audit is bounded to 256 stable-ID-ordered named-surface models and 16M
geometry work per map, with a 300-second/1-GiB process safety limit. It reads no
observed RGB, chooses no favourable model from held-out scores and performs no
joint fitting. This isolates admission changes from source inference. Full
receiver coverage and frozen independent validation remain required for any later
accepted fit; no receiver is dropped to make these geometry tests succeed.

Results and exact commands are recorded alongside the versioned machine-readable
protocol and native/stock reports under `tools/rend2/emitter_support/results/v3`.


## Controlled numerical results

The final native witness has **52 assertions, zero failures**, with identical
semantic output on repeat. Geometry includes real retained BSP brush planes,
not only a mocked Free classifier. It covers near-ceiling ideal points, forward
and backwards mounted spots, flush panels, a real recessed cavity, tangent and
penetrating tubes, an independent opaque wall, unknown alpha support, separate
lamp identities, malformed/stale leases, cancellation, work exhaustion, exact
L-shaped support and the solid-leaf equality case. No constant collision radius
or minimum inverse-square distance was introduced.

| Measurement | Result | Scope |
|---|---:|---|
| Panel emitting area | 2 world-unit² | Exact quadrilateral |
| L-shaped union area | 12 world-unit² | Missing corner retained; disconnected union rejected |
| Positive panel red-channel prediction | 0.237759575 | Relative finite Lambertian integral, amplitude (2,3,4), receiver above source |
| Same prediction behind opaque wall | 0 | Wall remains an independent blocker |
| Triangle-union vs rectangle relative difference | 0.00597276% | Two independently partitioned quadrature rules |
| Fitted RGB relative error | 0.0115132% | Fixed supplied geometry; initial RGB (0.1,0.1,0.1), not truth |
| Frozen held-out interval MSE | 1.44098e-9 | Eight held-out vs eight training receivers; all available; black baseline fails |
| Reference value-check rays | 1,282 | Panel/mesh/blocker/point value checks only |
| Reference value-check geometry work | 4,499 | Same restricted counter scope |
| Whole native process | 0.328 / 0.500 s | First/repeat, includes fitting and Joint smoke test |
| Peak process RSS | 5.08 / 5.09 MiB | Whole native process |

The production fixed-geometry Shape fitter strictly reevaluates the flush-panel
fit; every held-out receiver stays present. Joint also executes actual production
forward calls with an explicit ideal-point support contract. This is a scoped
numeric/admission test, not source discovery, type identification or compiler
parity. Fitting/Joint work counters remain available through their production
results; the compact native witness does not serialize their full operation
counts. The reference ray total above must not be read as the complete fitter
ray cost.

## Stock regression: exact outcomes

The final `v3` protocol preserves archive/BSP/dataset hashes and stock
UnknownProfile. Catalogues contain 3,219 / 1,410 / 746 models, respectively. The
comparison considers only convertible named-surface hypotheses, in frozen ID
order; it is not exhaustive search of every source or type.

| Map | Tested | Old collision / unknown | New collision / unknown / invalid | Newly admitted | Process seconds | Peak RSS MiB |
|---|---:|---:|---:|---:|---:|---:|
| mp/duel4 | 108 | 108 / 0 | 17 / 87 / 4 | 0 | 3.407 | 32.32 |
| mp/siege_desert | 256 | 256 / 0 | 4 / 252 / 0 | 0 | 11.140 | 51.74 |
| hoth3 | 256 | 230 / 26 | 31 / 209 / 16 | 0 | 15.047 | 85.51 |

No proven or conditional stock placement was published. Geometry work was
58,830 / 149,011 / 159,238; admission-loop durations were 0.907 / 0.479 / 0.246 s.
Process time/RSS also include dataset loading, geometry/BVH preparation and the
catalogue. These runs overlapped other builds/tests; they are bounded resource
measurements, not isolated CPU throughput claims. The small MP repeat has equal
semantic JSON and byte-identical private geometric witnesses.

For the exact frozen X12 `mp/duel4` source IDs and unchanged XYZ:

| Model / source ID | Previous | X14 | Meaning |
|---|---|---|---|
| Rectangle `378495349532519697` | Clearance | UnknownGeometry | Its actual emitting support is not certified outside retained world solids; this is not proof that the historical lamp was impossible |
| Tube `17291380550458404209` | Clearance | Solid | The proposed finite cylindrical body penetrates its mounting plane; no automatic radius offset or invented cavity was applied |

Across `mp/duel4`, reasons are: 15 penetrating cylinders, 4 unsupported stand-off/
side placements, 33 incomplete local visual supports and 56 world-support
classification failures. Of the last 56, two are known collisions and 54 remain
Unknown. `siege_desert` has 4 world collisions and 252 unknown supports. `hoth3`
has 10 penetrating cylinders, 16 stand-off/side failures, 33 incomplete visual
supports, 21 other known world collisions and 176 other unknown world supports.

Earlier v1/v2 witnesses remain frozen. The independent solid-leaf correction
improves its owned positive boundary test but does not change the observed stock
admission totals. No threshold was relaxed to obtain positive stock statistics.
There is **no new stock joint fit or held-out improvement claimed by X14**.

## Checks and remaining limitations

No game was started. The runtime stays opt-in/disabled. Full-map physical emitter
recovery, historical source counts, Raven profile identity and absolute SI scale
remain unresolved. Required next evidence is a supported mount/brush relationship
or bounded geometric-error interpretation for the remaining world-boundary
supports, actual cavity/assembly geometry for recessed tubes, mask-aware emission
support and independent positive receiver predictions under explicit profiles.
Unknown area must not be converted into empty space or discarded receivers.

Exact commands (PowerShell; Python path may be replaced by the same interpreter):

```powershell
$env:PATH = 'C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;' + $env:PATH
cmake -S tools/lightrecon_test -B build/s06-gcc
cmake --build build/s06-gcc -j 2
ctest --test-dir build/s06-gcc --output-on-failure -j 2
python -X utf8 tools/rend2/emitter_support/run.py --private "$env:TEMP/jk-lightrecon-all-stock-20261031" --out tools/rend2/emitter_support/results/v3
python -X utf8 tools/rend2/emitter_support/compare_witnesses.py --before "$env:TEMP/jk-lightrecon-all-stock-20261031/runs-x12-a/mp_duel4/isq-candidate-placement.tsv" --after "$env:TEMP/jk-lightrecon-all-stock-20261031/runs-x14-v3/mp_duel4/mounting-witnesses.tsv" --out tools/rend2/emitter_support/results/v3/historical-witnesses.json
$env:PYTHONPATH = 'C:/Users/Keyten/Desktop/projects/OpenJK-rend2/tools/rend2/datasets'
python -X utf8 -m unittest tools.rend2.datasets.test_blind_views tools.rend2.producer.test_controller
$env:CL = '/MP2'
cmake --build build/s13-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 2
```

Preserve the published result directories: a fresh reproduction must use a new
revision/output path. GCC 16.2 / CMake 4.4 build the CPU tools; MSVC 2019 Release
builds SP/MP. The final manifest records the exact executable/source hashes.
An intermediate unrelated SP compile error for `R_WaterWavesOn` is preserved in
`build/x14-final-sp-mp-build.log`; its declaration was supplied by concurrent
renderer work. X14 did not edit water code or suppress that compiler error.


Final verification: **39/39 CPU tests** (535.93 seconds under concurrent build
load), **52/52 final X14 assertions**, **11/11 blind/controller Python tests**,
and **SP + MP Release passed**. The final X14 test also appears in the full CTest
log with 52 assertions. `checks.json` records commands/log hashes; native and
small-MP repeats match. The three stock tests perform admission only. No game
process, renderer consumer or baked-direct replacement was enabled.
