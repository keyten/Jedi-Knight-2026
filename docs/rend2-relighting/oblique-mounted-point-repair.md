# Named oblique mounted points in Aesthetic Relighting

On 2026-11-12 the user reopened the mounting fix and removed AR-42's four-hour
repair limit. The repair covers the fixed isotropic points for original
`mp/duel10` housings 987 and 988. The earlier decision and its frozen evidence
remain the record of the old implementation. [AR-42 decision](oblique-support-contract-decision.md)

The offline `mounted-point` adapter now attaches a version 1 witness when the
float outward vector fails to describe the original triangle plane. Axial and
other compatible mounts retain the existing support path. The witness binds the
source pose, map and visibility generation, original surface and primitive
identities, ordered index and vertex data, and the captured evidence for the
emitting side. The visibility generation alone omits some orientation records,
so the witness hashes those separately and checks the immutable source lease
during admission. Shape preparation includes the witness in its existing support
key. Historical Shape evaluation, the general placement callers, field building
and the shadow cache refuse this witness type.

The plane derives from the original triangle cross product with the sign chosen
by `SourceOrientation`. Admission verifies all named primitives against the
visual geometry, including opacity, zero geometric error and exact vertex
identity. It rejects nonplanarity, degenerate triangles and overlapping triangle
interiors. For point `p`, triangle edge `e` and signed normal `N`, the edge test
`dot(cross(N,e),p-edgeOrigin)` has the same sign at `p` and its orthogonal
projection. The projection therefore needs no rounded `Vec3` coordinate. The
point must have strictly positive stand-off within its declared maximum. A
directed binary64 enclosure handles that distance comparison after exact sign
evaluation. A separate check ensures the existing optical response will treat
the point as a stand-off endpoint.

The exact domain is bounded. Triangle coordinates must lie exactly on the
1/256 WU lattice and the point on the 1/2²⁴ WU lattice. Coordinates are bounded
by ±2²⁰ WU and the original surface span by 512 WU on each axis. A triangle edge
component is at most 2¹⁷ lattice units, so the cross product and the subsequent
edge cross product are bounded by 2³⁵ and 2⁵³, respectively. Checked 64-bit dot
products reject computations beyond their exact integer range. These limits
preserve the original coordinates and refuse unsupported cases as unknown.

The exact witness covers named mounting and projection only. The occupied point
still passes the retained BSP brush classifier and an independent visual overlap
query. Neither uses the mount as an exemption. Independent bake occupancy uses
the same point-to-anchor bounds as before. This contract requires zero requested
front extrusion and one-sided point support. Other emitters and contact shapes
keep their own existing support checks, including the exact axial `BackContact`
restriction. Existing work, memory and ray limits apply. All positions, source
components, folds and optical response inputs for the two housings stay fixed.

## Verification

The Release CPU build at `80301f662c90c94be2277d77bc24d6111cc6d903` passed all
10 focused CTest suites. The emitter-support suite passed 174 assertions,
including the axial and equal-component controls, both directions of one-ULP
anchor shifts, a warp below the old roundoff bound, wrong or stale identity,
penetration, independent visual blockers, unknown optics, translated geometry,
numeric domain limits, cancellation and historical-mode refusal.

The archived raw `mp_duel10_all.jkobs` is still absent. The existing
`build/asr25-v1/rows/mp_duel10.public.jkobs` copy has SHA-256
`f511b2c1e3b1046a7a4d2dbf75b3e2514a4da90783553fcc69b6101d7579de2c`.
On this copy the AR-41 diagnostic reproduces the complete two-housing records,
including original triangles, their identities, visibility generation, both
local geometry queries and the independent point probes. The old native CLI
also reproduces the complete AR-41 dummy result. The repaired CLI admits
**987 alone, 988 alone and both jointly**, with certified source sides and
`Free` occupied placement. `OpticalUnknown` remains 16 for both. A second run
is byte-identical. The dummy fit used literal zeros in every target field and
reported 364,221 work units, including fit and scoring on those dummy inputs.

The two 394-row trim coverage outputs and all 141 frozen joint rows match the
old coefficients, availability and visibility issue flags. Both old axial
configurations still pass admission with identical dummy results. Replaying
their saved parameters gives the same held-out MSE with the source present and
removed, respectively: 95.075599 and 7837.108618 for `duel10-grid-2`, and
1000.383811 and 3923.111111 for `duel7-grid-3`.

From the repository root, `python build/oblique-support-repair/verify.py` replays
these checks using the preserved executables and private input packets. Full
results are in ignored `build/oblique-support-repair/verification.json`,
SHA-256 `d2cae520080fc13650b6a38b9cad834aa537db8a9af55f2c3487d3ac88cd9c3c`.
The repaired executable hash is
`d1ea7a3f4f5dce4581bc79c67840100c7a9ebc6ed1e17104ba24523f4c4cb9bc`.

The mounting defect is repaired and the original fixtures pass the geometry
and native admission gate on the verified copy. The exact raw archive replay
and original-target appearance evaluation are still outstanding. The housings
remain A candidates with D fallback until an appearance decision. Any resumed
pilot fit needs a separate pre-exposure freeze and decision. A final-informed
change still needs adequate independent final evidence beyond the incomplete
v1 reserve. The AR-42 receipt and `viability-v1` stay immutable.
