# ASR17 follow-up — correct the captured source side

The source-side mismatch found in the [ASR17 diagnosis](rend2-placement-diagnosis-asr17.md)
is fixed for new geometry-derived evidence and for old `.jkobs` priors converted
against their exact named geometry. No source is relocated, enlarged or assigned
an invented fixture dimension. This changes a faulty source hypothesis; it does
not relax collision, visual coverage, optical or conditional occupancy checks.

The frozen before/after protocol and residual witnesses are under
[`tools/rend2/emitter_support/results/asr17-side-v2`](../tools/rend2/emitter_support/results/asr17-side-v2).
The earlier X14 and ASR17 results remain intact. Licensed points, planes and
triangles remain private. No stock RGB targets, fits or historical source answers
are used in the admission comparison.

## Source-side interpretation

`SourceOrientation::Resolve` in
`shared/rd-rend2/lightrecon/source_orientation.h` chooses the **sign of the actual
triangle's geometric normal**, using the captured vertex normals and the planar
face basis. It does not substitute a smoothed vertex normal for the plane.
Missing vertex orientation requires a valid face basis. Contradictory records,
tangential orientation, degenerate geometry and a conflicting captured cull side
are unavailable. Raw winding alone does not establish an emitted side.

Both `SurfaceEvidence` (`evidence.cpp`) and `SourceSupport::FromSurfaces`
(`emitter_support.cpp`) use this interpretation. Unsupported source sides remain
explicit unsupported hypotheses. Triangle positions, winding, unions, identities,
fixture membership and emitting area are retained.

`Producer::Convert` handles archived GlowSurface/SurfaceLight frames. If the
archived forward direction is parallel but opposite to the newly established
named surface direction, it negates forward and up together, retaining the
right-handed frame, tangent axis, position and dimensions. A nonparallel or
missing archived frame stays unsupported. Explicit authored frames and receiver
observations are not reinterpreted as visible fixtures. The archived evidence
objects and original files remain immutable; converted priors record the
correction in their provenance.

The mount audit now checks the corrected hypothesis at its own fixed pose.
Its legacy vacuum control is a separate hypothesis, not a swept transition from
the old frame into the corrected frame. Production motion checks are unchanged.

## Physical evaluation and historical limits

Physical OriginalSurface quadrature in `emitter_shape.cpp::Nodes` now uses the
contract's captured emitted side. Areas and quadrature positions still derive
from the original triangles. An owned panel with reversed raw winding must
produce the same positive physical response as the same panel with aligned
winding; checking placement alone would miss this third consumer of the old
direction.

The historical point/spot and polygon expansion mathematics are unchanged.
Historical OriginalSurface expansion whose raw polygon winding opposes the
captured side now returns **Unavailable**, rather than applying the unverified
historical source push on the wrong side. No compiler endpoint or winding rule
is invented. The existing exact-contact point/spot historical restriction also
remains in force.

SourceSupport keeps contract schema **1** and advances its numerical key
interpretation from **2 to 3**. Shape advances **3 to 4**, Producer **8 to 9**.
Fresh surface evidence IDs include the source-side interpretation **2**; archived
record IDs remain unchanged during conversion. These changes invalidate prepared
models/provider bindings without changing canonical asset or `.jkobs` layouts.

## Same-cohort stock comparison

The comparison uses the same three licensed archives and the same **620 candidate
and fixture IDs** as ASR17 v1. Full-precision candidate positions and occupied
support geometry are checked for equality; it does not rely only on the older
six-significant-digit TSV coordinates. Newly admitted sources are checked against
the independent rational penetration analysis.

| Map | Candidates | Before admitted | After admitted | After Unknown |
|---|---:|---:|---:|---:|
| `mp/duel4` | 108 | 0 | 20 Free | 35 |
| `mp/siege_desert` | 256 | 0 | 9 Conditional | 18 |
| `hoth3` | 256 | 0 | 11 Conditional | 115 |
| Total | 620 | 0 | 40 | 168 |

The 20 Conditional placements retain independent unknown model/bake occupancy;
they are not unconditional free-space certificates. All 40 admissions are ideal
point hypotheses. No stock finite mesh is published or certified by this result.
The source direction now agrees with every referenced captured vertex normal in
the sampled cohort. The retained BSP, visual BVH and SourceSupport admission
predicate implementations are verified against their before-run versions.

The remaining 168 Unknown results are bounded brush-boundary or patch-envelope
limitations. Tubes through their mounts, undeclared stand-off and actual retained
penetration still fail. The corrected side also lets later visual intersection
checks run: their existing `Clearance` state is a placement rejection, and should
not be reported as proof of positive-volume brush penetration. No contact brush
is removed, negative distance clamped, patch error discarded, or unknown optics
treated as transparent.

## Validation and reproduction

Owned controls cover fresh evidence with reversed winding, face-basis fallback,
absent/conflicting normals and cull metadata, archived point/rectangle/tube
conversion, unchanged source position/radius, positive physical mesh prediction,
independent solids and the historical Unavailable guard. The existing negative
alpha/translucency, stand-off, stale lease and work/cancellation controls remain.

`checks.json` records the native assertions, full CPU test suite and independent
Python checks. The stock runner checks a current uninstrumented control for every
map, replays the frozen X14 binary, and repeats the small map byte for byte.
`comparison.json` records the same-cohort transitions and confirms that no admitted
source has an independent exact penetration witness.

```powershell
$env:PATH = 'C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;' + $env:PATH
cmake --build build/s06-gcc -j 2
ctest --test-dir build/s06-gcc --output-on-failure -j 2
python -X utf8 -m unittest tools.rend2.emitter_support.test_diagnosis -v
python -X utf8 tools/rend2/emitter_support/run_diagnosis.py --private "$env:TEMP/jk-lightrecon-all-stock-20261031" --out tools/rend2/emitter_support/results/asr17-side-v3 --source-side-fix
python -X utf8 tools/rend2/emitter_support/compare_orientation.py --before tools/rend2/emitter_support/results/asr17-v1 --after tools/rend2/emitter_support/results/asr17-side-v3 --out tools/rend2/emitter_support/results/asr17-side-v3/comparison.json
```

Use a fresh output name and retain previous results. No runtime lighting consumer,
world shading, GPU code or baked/direct replacement behavior is implemented or
enabled. Plausible placement remains a source-discovery hypothesis, not exact
historical-lamp identification or calibrated photometry.
