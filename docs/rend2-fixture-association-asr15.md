# ASR15: associate physical fixtures with illumination structures

ASR15 adds a standalone offline association stage in
[`tools/rend2/fixture_association`](../tools/rend2/fixture_association/README.md).
It consumes visible-fixture hypotheses and independently extracted lighting
observations. It does not change the historical solver, shared renderer/core,
canonical asset schemas, cache/storage contracts or world shading.

The purpose is aesthetic source discovery. A selected fixture plausibly explains
part of an illumination structure under explicit assumptions. Its existence as
a historical baked source, original wattage and exact historical identity remain
unknown. Every report and link has `historical_source_certified: false`.

## Evidence and compatibility

Read-only adapters use ASR09 masked BSP luminous footprints, ASR11 MD3 world
placements, ASR14 mapped lightmap descriptors and the ASR12/13 original-grid and
native-static-geometry bridge. No generated probes or reconstructed source
positions replace original grid observations. Fixture, patch and geometry map
identities must agree. Optional original rows must match the hash pinned by the
patch descriptor. Unplaced BSP inline models remain ineligible; repeated model
asset IDs never collapse different physical placements into one source.

Each fixture has a centroid, conditional outward direction, relative colour
prior and bounding radius derived from its luminous footprint. Unknown or
two-sided orientation stays unknown. Unresolved layer composition withholds
colour. The radius describes spatial extent, not energy, and only softens spatial
and angular compatibility. It does not replace extended-emitter integration.

Each observation retains its original support IDs. ASR14 tangent gradients are
transformed with the original world tangent frame; they never become 3D incoming
light vectors. Supported ASR14 normal-line constraints provide a conditional
projection test without inventing source depth. Grid supports use aggregate
incoming directions with explicit decoder interpretation and mixture uncertainty.

Nine bounded terms contribute to a support score:

| Term | Weight | Interpretation |
|---|---:|---|
| Proximity | .08 | Soft distance to bounded luminous support, with 256 WU scale. |
| Fixture orientation | .08 | Cosine to the declared emitting face. |
| Receiver side | .06 | Geometric receiver half-space/cosine. |
| Colour | .10 | Relative chromatic cosine affinity; transfer and transport unresolved. |
| Gradient | .18 | Tangent gradient points toward the conditional source projection. |
| Grid direction | .18 | Aggregate incoming direction agrees within a 45 degree gate. |
| Projection | .12 | Distance to an ASR14 conditional normal line. |
| Visibility | .12 | Known static clear/blocked segment; unknown remains null. |
| Nearby dark receiver | .08 | Contradictory exposure ordering on matched material/style/normal controls. |

Scores average only known terms and report their `known_weight`. Each term has
a value or null, a state and a reason. The full-weight score bounds let unknown
terms vary freely over [0,1]; they are neither statistical confidence intervals
nor physical error bounds. Pair scores aggregate the known weighted contributions.
A completely unknown support/pair has a null score, not a zero measurement.
Rank follows compatibility score with stable fixture/observation ID tie-breaking;
the separate selection/status fields must also be inspected.

Dark-receiver checks compare uncensored bright codes against nearby dark codes
under a hypothetical cosine/inverse-square *ordering* and known visibility.
Different materials, styles, normals or unknown visibility cannot establish a
contradiction. This never inverts RGB8 or treats it as calibrated irradiance.
Unknown albedo variation, bake filtering and mixed contributions remain caveats.
Only the direction of a supported gradient is used; its magnitude supplies no
source height, brightness or photometric calibration.

Known occlusion and wrong emitting/receiver half-spaces veto a direct link on
that support. Unknown visibility forbids selection on that support. A positive
gradient, grid-direction or projection term is required in addition to a score
of at least .65; proximity and colour alone cannot activate a source. Partial
links retain all supported, contradictory and unavailable rows. An occluded
direct link can still have an indirect explanation.

## Many-to-many assignment and uncertainty

The bounded greedy facility-coverage objective is:

```
sum over observations (
    mean over correlation groups (
        mean over supports (maximum selected-link utility)
    )
) - .04 * active fixtures - .03 * active source hypotheses - .02 * selected links
```

Utility is `known_weight * max(0, (score - .65) / .35)` on eligible supports.
Unknown and vetoed supports have no assignment utility; their evidence values
remain unavailable or contradictory in the report. Each physical fixture
currently offers one conditional emission-source hypothesis, so fixture/source
activation costs add together. ASR15 does not search several alternative emission
lobes or source shapes within a fixture.

The greatest positive marginal gain is added, with stable-ID tie-breaking.
Existing support is credited only its maximum utility, so a redundant lamp gains
no extra coverage. One fixture can explain several observations; multiple
fixtures can improve distinct supports within one observation. No region is
required to have a lamp. Shared grid records and all samples in one patch share
normalized correlation groups; they do not become independent statistical votes.
Different extracted structures can still be correlated, and no posterior or
dataset-wide statistical independence is claimed.

The solver is a deterministic approximation, not a global optimum. Selected links
report their marginal gain and a local single-link replacement margin. Equal or
near-equal replacements (margin at most .04) keep the selected link ambiguous.
That margin does not assess every joint alternative or establish uniqueness.
Rejected and unselected compatible links remain in the ranked output. Explicit
unassociated fixtures, unmatched observations and unexplained support IDs remain.

## Validation, 2026-11-09

The checked controlled report in
[`results/v1/validation.json`](../tools/rend2/fixture_association/results/v1/validation.json)
freezes policy, implementation hashes, complete reasons and all alternatives.
Eight owned analytical scenes cover one-to-one, one-to-many, many-to-one,
wrong-colour, nearby separate same-colour fixtures (96 WU separation), occlusion,
coloured-transport perturbation and unexplained colour-only evidence. Ground-truth
pair labels enter evaluation only. No new compiler execution is claimed.

| Method | Correct links | False links | Missed links | Micro F1 |
|---|---:|---:|---:|---:|
| ASR15 penalized association | 10 | 0 | 0 | 1.00 |
| Nearest fixture | 4 | 6 | 6 | .40 |
| Nearest plus colour | 5 | 5 | 5 | .50 |
| Colour only | 7 | 3 | 3 | .70 |

These simple baselines choose one link per observation and do not model
occlusion, directional structure or multiple contributors. They are reference
baselines, not competitive optimized solvers. The coloured-transport control
perturbs receiver chroma while preserving independent direct spatial/directional
cues; a nearby red decoy loses to the white source. This is a tint/bleeding
adversary, not a solved full-GI scene. Colour-only evidence remains unmatched.
The measurements are finite controlled-candidate results, not held-out stock-map
precision/recall or identification of exact historical lights.

Fourteen unit tests cover these assignments plus input-order determinism,
immutable evidence, null unknown scores, unknown/blocked geometry, opposite
orientation, unknown colour/extent, nearby dark controls, source penalties,
correlated duplicates, invalid/budget failure, original-grid guards, catalogue
placement/instance identity and actual ASR14 descriptor adaptation.

A read-only integration run uses the existing owned compiled panel BSP,
ASR09's six fixture hypotheses, original mapped lightmap rows, original lightgrid
ROI and native geometry. Twelve fixture/observation links survive in the report:
nine ambiguous, three rejected, zero selected. Its 264 pair-support visibility
queries report 161 unknown and 103 blocked states. The tool retains both
unmatched regions; it never promotes surface-origin or centroid-ray uncertainty
to empty space. This verifies adapter/geometry behavior, not fixture recovery
accuracy. The associated manifest pins the private input and output hashes.

Two complete controlled runs and two complete compiled-panel runs are byte
identical. The new CTest suite and existing original-grid evidence, triangulation,
visibility, evidence, offline loading and BSP capture suites pass. The existing
15 ASR14, 14 ASR09 and 13 ASR11 Python tests also pass. Full owned descriptors
remain under `build/asr15`; checked results contain analytical controls and
integration metadata. No licensed stock geometry or images are redistributed.
