# AR-41 — measured oblique contact-plane refusal

**2026-11-10 (Australia/Sydney). Investigation Done; decision
Blocked-SourceSupport; AR-35 integration unauthorized; aesthetic gate Unknown.**
Send this negative receipt directly to **AR-36**. The immediate refusal has a
measured numerical cause. No justified replacement oblique-contact contract has
been established within this diagnosis, so a production repair is not authorized.
No original-target fit or appearance score exists for this pilot.

Inspected and completion HEAD: `89a40b0fc027cc5e3e1e1cce5204cc48b526c013`,
branch `rend2-unified-wip`, origin `git@github.com:keyten/BetterJK.git`. The
supplied checkout is authoritative despite the older project name. The initial
worktree/index were clean; no applicable repository/ancestor `AGENTS.md` was
found. Newer AR-35 work was inspected and reused; it had correctly stopped before
integration. No production files, staged files or commits were changed here.

The [versioned receipt](../../tools/rend2/aesthetic_relighting/results/ar41-v1/evidence.json)
and [manifest](../../tools/rend2/aesthetic_relighting/results/ar41-v1/manifest.json)
pin the evidence. Exact original XYZ, normals, triangle/primitive identities,
anchors, barycentrics and all diagnostic inputs/outputs remain in ignored
`build/ar41-v1/`. The [AR-34 protocol](../../tools/rend2/aesthetic_relighting/policies/viability-v1.json)
and every earlier result remain immutable.

## Reproduction and input identities

Rehashed the original duel10 BSP member, capture, catalogue, compressed row file,
source metadata, selected scope and all six frozen native executables. All
150 ASR25 implementation-file hashes match. The audit records 223 file-hash
comparisons (217 distinct path strings), including all ten ASR24/25 public
manifest entries, and matching original BSP, source-image and parsed
material-body identities. Public ASR24/25 summaries/checks and AR-34/35 evidence
were read; their reported regression runs were not rerun or claimed as fresh.

Four native probes use the unchanged executables and AR-34 protocols. Every
stdout is byte-identical to AR-34:

| Probe | Measured result |
|---|---|
| `dummy-admission-v2.txt` / `dummy-held-v2.txt` | Both singletons and joint configuration unavailable: `Named mounting surface absent at declared contact plane`; selected index -1; 16,422 fitter work units |
| Individual 394-row geometry responses | 57 first-only, 64 second-only, 199 both known zero, 74 either unavailable, zero both-positive |
| Frozen joint response | 141/141 available; maximum coefficient difference from the sum of individual responses 0 |

All supplied target-channel tokens were verified to be literal zeros. Dummy
baseline scores are not original-lighting quality measurements. Neither the
compressed original row values nor original final/reserve target packets were
parsed, displayed, fitted or scored. One technical boundary is explicit: the
unchanged `Offline::Dataset::Load` used by the frozen CLI deserializes archive
image buffers. The diagnostic also uses that loader, immediately clears vertex
colours as the CLI does, and never indexes, prints or evaluates receiver-image
values. This is a controller non-use boundary, not an assertion that no RGB bytes
entered process memory. No final appearance informed this decision.

`--coverage` goes through `MakePlacement` / `WithOccupancy`, `Shape::Prepare` and
unit `Block::Evaluate`. It does **not** call `SourceSupport::Check`.
`aesthetic_fit.cpp::Prepare` calls that stronger check before preparing a source
column. Each refused source makes every containing configuration unavailable;
the joint refusal is not evidence that only one housing is bad. Successful unit
transport preparation cannot certify a named mount.

## Measured causal chain

The private read-only diagnostic links the pre-existing
`build/s06-gcc/core/librend2_lightrecon_core.a`; its archive/compiler/executable
hashes are recorded now. It calls the existing `FromSurfaces`, `ExtractBox`,
`Check`, domain and occupancy providers. It changes no admission predicate.
Its source-orientation header matches the frozen implementation. The separate
Python measurement uses exact rational arithmetic on the original binary32
coordinates and compares only the two original source records with their BSP
geometry fields; it never parses the BSP lightmap lump.

For **each** original surface 987/988:

- Captured vertices, indices, vertex normals and face basis equal the original
  BSP fields exactly. Source orientation resolves consistently on both triangles.
  The contract anchor is the first original triangle vertex. Surface IDs are
  correctly ordinal + 1; no identity-offset error was found.
- `Check`'s exact point-to-anchor query returns 11 triangles, including both
  original named triangles unchanged. A box enclosing the complete mount returns
  30 triangles and still both named triangles. Both extractions report complete
  coverage. The named triangles are opaque with zero geometric approximation
  error. The overall issue mask remains **16 (`OpticalUnknown`)**, including three
  alpha-tested triangles in the smaller query; completeness does not clear them.
- The exact rational plane determinant is zero at all six original triangle
  vertices. `FromSurfaces`' resolved double normal also gives zero residual here.
  The emitted point is on the declared positive side within the existing 8 WU
  stand-off bound. Both exact-plane and current rounded projections lie inside
  the named triangle union. These are diagnostics, not source relocation.

The incompatible representations appear between the two functions:

1. `FromSurfaces` uses the signed, double-precision triangle cross-product normal
   returned by `SourceOrientation::Resolve` for its coplanarity proof. It then
   stores `normal.Float()` in `Contract::outward` (a `Vec3`).
2. `Check` promotes that binary32 normal back to double and defines its plane as
   `dot(outward, anchor)`. It requires **every vertex** of a named triangle to
   satisfy `abs(dot(outward, vertex) - plane) <= error`, where
   `error = 128 * epsilon_double * (1 + scale)`.
3. At least one vertex of **each** named triangle has the nonzero residual below.
   Both triangles are discarded from `mount`, leading to the exact refusal.

| Measurement | Surface 987 | Surface 988 |
|---|---:|---:|
| Original / extracted named triangles | 2 / 2 | 2 / 2 |
| Maximum resolved-double-normal residual, WU | 0 | 0 |
| Maximum stored-float-normal residual, WU | 2.384185791015625e-7 | 2.384185791015625e-7 |
| Existing `Check` error bound, WU | 1.624448436864759e-11 | 1.6244664421924223e-11 |
| Residual / existing bound | 14676.894 | 14676.732 |
| Normal quantization difference, L2 | 6.289596039418816e-9 | 6.289596039418816e-9 |
| Native source check work / rays | 136 / 0 | 153 / 0 |

The nonzero residual is exactly **1/4194304 WU** using rational arithmetic on
the stored normal and coordinate differences. It also equals the native
absolute-dot residual. Thus it is not merely cancellation in subtracting two
large double dot products: using anchor-relative subtraction leaves the same
error. Renormalizing the float normal leaves approximately `2.384185782e-7` WU.
Using the pre-cast double normal makes this diagnostic plane predicate pass
all vertices. None of these counterfactual arithmetic evaluations changes or
reruns admission with a modified contract.

Independent existing-provider queries classify each fixed point as `Free` in
the retained BSP domain and its point occupancy query as `Free`. They do not
certify all later visual/contact/clearance checks. The native check exits before
those checks; unknown optical support remains unknown. The measured cause is
the immediate plane rejection, not a proof that every downstream gate would pass.

## Causal controls

Eight separately labelled **owned synthetic numerical controls**, using the same
unmodified native functions and a declared, fully known control BSP domain,
exercise the predicate. They supply no replacement stock evidence or appearance
experiment. Original source poses/geometry were never altered.

| Control | Native outcome |
|---|---|
| Exact axial planar mount | Admitted, source side certified |
| Same axial plane, anchor one binary32 ULP off plane | Named-contact-plane refusal |
| Oblique planar mount with equal nonzero normal components | Admitted, source side certified |
| Same oblique plane, anchor one binary32 ULP off plane | Named-contact-plane refusal |
| Exactly planar oblique mount with unequal component ratio reproducing the original quantization mechanism | Named-contact-plane refusal |
| Axial declared wrong side | Mount/stand-off refusal |
| Oblique source behind the declared side | Mount/stand-off refusal |
| Oblique absent named surface identity | Named-contact-plane refusal |

The positive oblique case also shows that obliquity alone does not imply refusal.
Together with the exact original identities and rational residual decomposition,
the controls isolate normal representation error from absent extraction,
nonplanarity, incorrect side and a genuine displaced contact declaration.

Scratch failures remain auditable: the first AR-41 integrity check compared the
whole shader-file hash with the catalogue's parsed material-body hash. The
initial audit/script were retained; the corrected body hash matches. Private
diagnostic attempts retain the compiler PATH failure, ambiguous helper-name
compile errors (and a failed shell quoting correction), a missing owned-control
map-byte identity, and an inconsistent negative-control triangle/surface mapping.
Successful versions correct only the harness/metadata, without changing original
inputs or native predicates. The initial diagnostic's `receiver_rgb_read:false`
header was too broad for the archive loader and is superseded by the explicit
buffer-deserialization disclosure above. AR-34's block-ID collision, metadata
admission correction, unsuccessful folds and original native refusals remain
untouched as well.

## Decision and receipt to AR-36

**Blocked-SourceSupport. `Authorize-Bounded-AR35` is not granted.** No production
file or behavior change is permitted by this receipt. AR-41 changes only this
diagnosis, its versioned receipts, the plan queue/logs and private diagnostics.
It adds no engineering allowance.

The precise remaining blocker is an **absent justified numerical plane-witness
contract for original oblique named support**. An existing double orientation
resolver is useful evidence, but the current contract carries a float normal;
substituting a double computation only at this failing comparison would silently
change what its plane means. Projection, visual contact, front clearance,
world-brush contact and semantic keys must agree on the same witnessed plane.
`BackContact` already explicitly restricts its separate equality proof to exact
axial planes and says quantized oblique normals need a separate numerical plane
contract. It cannot serve as the missing oblique proof. The tiny measured
residual supplies neither a general epsilon nor permission to admit arbitrary
nearby geometry. There is no measured one-M, existing-contract adapter repair
whose complete safety follows from this investigation.

AR-36 must consume this receipt and the unchanged frozen policy to issue its
bounded Blocked/Stop/Pivot verdict, without requiring a nonexistent original-data
fit. This finding is not evidence of appearance-model failure and does not itself
justify an appearance/background pivot. Any choice to pursue a numerical repair
needs a separate explicit decision and a justified, versioned modern-only proof
before coding. That decision must identify exact permitted files and preserve
historical behavior, canonical storage/meanings, named triangle identity, signed
side, true penetration refusals, unknown optics/geometry, work/memory limits and
cache/semantic identities. Acceptance would require the original two mounts,
the controls above, additional contract-specific adversarial controls, old axial
stock regressions and full downstream admission; passing the plane predicate
alone is insufficient. These are requirements for a future decision, not an
authorized implementation scope or a promise it fits the allowance.

The remaining ceiling stays **one M integration / eight engineering hours and
at most one M repair / four hours**, before one original-target fit and identical
repeat, then mandatory AR-36. No second scope or roadmap execution is authorized.
Any material pose, geometry/response-support or acceptance change requires a new
explicit freeze before original targets are exposed. After final evidence has
informed a change, it becomes development evidence and fresh adequate independent
final evidence is mandatory. V1's reserve still lacks housing-988 control pairs
and cannot authorize post-final repair fitting.

The housings retain visibly motivated **A-candidate** status and **D/original-data
fallback** outputs. AR-41 adds zero accepted sources, receiver coverage, maps,
B lamps or C assets. It leaves the two old conditional ASR24/25 successes and all
failures intact; it does not prove historical type/count/XYZ/power, calibrated
irradiance, coherent-region viability or whole-map readiness. Known-zero direct
response is not observed darkness; unavailable response is not black. Codex
performed the inspection, exact-arithmetic analysis and automated controls with
no subagents, separate reviewer or new original-appearance review.
