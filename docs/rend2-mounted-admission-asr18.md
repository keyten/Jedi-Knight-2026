# ASR18 — geometrically admissible mounted source hypotheses

This follows the [ASR17 source-side correction](rend2-source-orientation-asr17.md).
It changes offline `SourceSupport::Check`, its numerical certificate key, and
geometry-only diagnostics/tests. It preserves the original emitter positions,
frames, dimensions, triangle unions and fixture identities. No observation RGB,
stock fitting, renderer, GPU consumer or historical transport mathematics enters
the change. Geometric support is evidence for a plausible source hypothesis;
it does not establish that a lamp existed or should be fitted.

## Placement and rear contact

The X14 distinction between an ideal point and a finite vacuum sphere remains:
point/spot support is a point, rectangles are sheets (or exact original triangle
unions), and tubes have a closed finite cylindrical occupied body. Every world
brush still participates in the unchanged `BspRegionDomain::ClassifySupport`.
Named original mounting triangles must remain present, exact, opaque, compatible
with the captured emitted side, and cover the projected source support.

The stock area failures after ASR17 frequently come from a different geometry:
an independent opaque fixture edge that ends exactly at the luminous plane from
behind. The old visual intersection test rejects this closed-set contact as if
it crossed the emitting side. The new exception requires all of:

- An existing named mounting contract and one-sided emission.
- An exactly axial unit mounting normal, so equality needs no numerical repair.
- An independent exact opaque triangle with maximum signed distance **exactly
  zero** and minimum signed distance **strictly negative**.
- The whole occupied support on or in front of that plane.

These two closed halfspaces meet only on their common boundary. The triangle
cannot occupy the open emitting side. No independent surface ID is excluded
from the BVH, brush domain or receiver visibility. A crossing wall (even a tiny
positive extension), coplanar overlay, alpha/translucent/unsupported contact or
patch error envelope retains its rejection. Two-sided sources have no such
non-emitting rear exception.

The exception deliberately does not interpret quantized oblique planes. Existing
negative plane residuals, ambiguous brush boundaries, unknown geometry and
disconnected source assemblies retain their conservative behavior. Unknown
space is never promoted to empty space.

For tubes, a tangent body with explicitly supplied radius/centre can pass; a
positive gap beyond `maximumStandOffWorld` now fails `Mount`. The gap refers to
the nearest occupied cylinder boundary, rather than its centreline. A centreline
on a luminous sheet still puts a finite-radius tube through its support and
fails `Solid`. No producer radius offset or cavity is invented.

## Front clearance

An explicit `frontClearanceWorld` extrudes the actual point or polygon support
along the declared mounting normal. The visual box query is a broad phase;
returned triangles now undergo a separating-projection test before rejection.
Exact opaque intersections report `Clearance`, and intersecting unknown optics
remain `UnknownGeometry`. Incomplete local geometry and patch envelopes are
still unavailable, even when a tighter future predicate might resolve them.

Tubes previously had no polygon entry and silently skipped this check. Their
front is now conservatively enclosed by the occupied cylinder's outer box and
its outward extrusion. This can overreject an oblique tube near a corner, but
does not grant clearance from an inner approximation. Translated float vertices
are enclosed rather than rounded inward. Two-sided sheets check both directions.
Independent bake/model occupancy also covers the requested clearance volume;
it no longer checks only the original source bounds.

The support contract schema stays **1**. Its numerical key interpretation moves
from **3 to 4**, invalidating prepared Shape/Joint/Producer identities that already
include `SourceSupport::Key`. Canonical asset and `.jkobs` storage stay unchanged.
Historical exact-contact point/spot restrictions and reversed-winding expansion
guards remain unchanged.

## Validation and stock comparison

Release CPU build passed, with **115/115 owned native assertions**, **54/54
CTest tests**, and **5/5 independent diagnosis tests**. `checks.json` and
`ctest.log` retain the validation receipts. The native repeat is identical.

The reproducible outputs are in
[`tools/rend2/emitter_support/results/asr18-v1`](../tools/rend2/emitter_support/results/asr18-v1).
The runner freezes hashes before evaluation and reuses the exact ASR17 cohort.
It checks full-precision occupied geometry, captured normals, anchors, source and
fixture IDs; compares states **and rejection reasons**; checks the current
instrumented/uninstrumented runs; repeats `mp/duel4`; and replays the frozen ASR17
binary on that map. Licensed geometry stays in the private dataset directory.

Every newly admitted sheet is also checked with independent rational arithmetic:
all original vertices remain exactly on the mount, every intersecting unnamed
triangle is an exact opaque rear contact, and no admitted source has an exact
retained-brush penetration witness. Public samples contain only stable IDs,
material names and static-world classification, with no coordinates or triangles.

| Stock map | Candidates | ASR17 admitted | ASR18 admitted | New original sheets | Unchanged Unknown |
|---|---:|---:|---:|---:|---:|
| `mp/duel4` | 108 | 20 Free | 48 Free | 28 | 35 |
| `mp/siege_desert` | 256 | 9 Conditional | 231 Conditional | 222 | 18 |
| `hoth3` | 256 | 11 Conditional | 89 Conditional | 78 | 115 |
| Total | 620 | 40 | 368 | 328 | 168 |

All 328 transitions are from `Clearance` to Free/Conditional, explained by
**3,374 exact independent rear contacts**. All **59** previously known `Solid`
rejections and **17** `Mount` rejections retain their states and reasons; the
remaining **8** independent visual intersections retain `Clearance`. No Unknown
is promoted. The original 40 point admissions are retained; the new admissions
are exact original triangle unions, not enlarged bounding panels or finite tubes.
The 320 Conditional results keep their independent model/bake occupancy caveats.

The previously rejected X14 rectangle `378495349532519697`, linked by ASR17 to
current source **`8734946737344204972`**, is now Free. It is the original static
world surface **1030**, using **`textures/mp/s_tracklight2`**, fixture
`9237484051810397922` in `mp/duel4`. Its original triangle union and pose are
unchanged. Further static samples include `textures/desert/light_yellow` and
`textures/hoth/light_01b`; their conditional occupancy is retained. This fulfills
the stock mounted-fixture admission witness without assigning a historical lamp
answer. The original X14 tube through its mount remains rejected.

Stock sources retain their declared clearance of zero; the nonzero front-volume
changes are tested separately with owned geometry. Those controls cover all four
source kinds, a real recess, a broad-phase overlap that misses the front prism,
opaque and translucent front blockers, independent solids/model occupancy,
two-sided back blockers, wrong directions, stale support, cancellation/work
limits, undeclared gaps, through-mount tubes and disconnected supports.

```powershell
$env:PATH = 'C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;' + $env:PATH
cmake --build build/s06-gcc -j 2
ctest --test-dir build/s06-gcc --output-on-failure -j 2
python -X utf8 -m unittest tools.rend2.emitter_support.test_diagnosis -v
python -X utf8 tools/rend2/emitter_support/run_admission.py --private "$env:TEMP/jk-lightrecon-all-stock-20261031" --out tools/rend2/emitter_support/results/asr18-v1 --before-tool build/asr18-before-map-solver.exe --before-test build/asr18-before-support-test.exe
```

The before binaries were copied before editing and match the hashes in
`results/asr17-side-v2/protocol.json`. Reproduction requires these binaries and
the licensed private input/witness directories recorded by that protocol; a new
run must use a fresh output name. Conditional model/bake occupancy remains
conditional. Material glow is a fixture cue, not calibrated linear irradiance
or proof of a particular historical source.
