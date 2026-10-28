# X05 benchmark witness

Reference/admission self-check: **Passed**. No production evaluator or inverse solver was run.

| Fixture | Contract | Held-out | Blocks | Positive | Black interval RMSE | Reference RMSE | Admission / reference |
|---|---|---:|---:|---:|---:|---:|---|
| x05_doorway_pair_v1 | doorway_through | 23 | 3 | 9 | 8.992751 | 0.000000 | InsufficientEvidence / Failed |
| x05_doorway_pair_v1 | doorway_blocked | 13 | 2 | 0 | 0.000000 | 0.000000 | Admitted / Passed |
| x05_doorway_pair_v2 | doorway_through | 12 | 2 | 0 | 2.943920 | 0.000000 | InsufficientEvidence / Failed |
| x05_doorway_pair_v2 | doorway_blocked | 24 | 3 | 0 | 0.000000 | 0.000000 | Admitted / Passed |
| x05_doorway_pair_v3 | doorway_through | 23 | 3 | 20 | 37.335727 | 0.000000 | Admitted / Passed |
| x05_doorway_pair_v3 | doorway_blocked | 13 | 2 | 0 | 0.000000 | 0.000000 | Admitted / Passed |
| x05_panel_positive_v1 | strong_area | 96 | 11 | 96 | 59.951022 | 0.000000 | Admitted / Passed |
| x05_sky_aperture_v1 | positive_sky | 96 | 11 | 29 | 6.386835 | 0.000000 | Admitted / Passed |
| x05_sky_sealed_v1 | sealed_sky_negative | 96 | 11 | 0 | 0.000000 | 0.000000 | Admitted / Passed |
| x05_sun_positive_v1 | strong_sun | 96 | 11 | 96 | 120.000000 | 0.000000 | Admitted / Passed |
| x05_wall_pair_v1 | wall_illuminated | 60 | 7 | 53 | 18.942897 | 0.000000 | Admitted / Passed |
| x05_wall_pair_v1 | wall_blocked | 36 | 4 | 0 | 0.000000 | 0.000000 | Admitted / Passed |

Failed doorway v1/v2 controls are retained diagnostics. Required v3 strengthens the declared primary and spatial receiver domain; numerical gates are unchanged.

## Historical controlled fixture census

| Fixture | Eligible | Train / validation / held-out | Surfaces | Normals | Visible / blocked representative pairs |
|---|---:|---|---:|---:|---|
| compiled12/bounce_colour | 2278 | 352 / 176 / 176 | 6 | 6 | 195 / 0 |
| compiled12/bounce_control | 2278 | 352 / 176 / 176 | 6 | 6 | 195 / 0 |
| compiled12/door_occlusion | 2539 | 352 / 176 / 176 | 9 | 6 | 141 / 87 |
| compiled12/overlap_whites | 2278 | 352 / 176 / 176 | 6 | 6 | 390 / 0 |
| compiled12/panel | 2278 | 352 / 176 / 176 | 6 | 6 | 0 / 0 |
| compiled12/point_room | 2278 | 352 / 176 / 176 | 6 | 6 | 195 / 0 |
| compiled12/skylight | 1893 | 352 / 176 / 176 | 9 | 5 | 48 / 2808 |
| compiled12/spot | 1139 | 66 / 176 / 110 | 3 | 3 | 95 / 0 |
| compiled12/sun_sky | 1853 | 352 / 176 / 176 | 5 | 5 | 559 / 2161 |
| compiled12/tube | 2278 | 352 / 176 / 176 | 6 | 6 | 0 / 0 |
| compiled12/two_colour | 2278 | 352 / 176 / 176 | 6 | 6 | 390 / 0 |
| compiled12/wall_leak | 2278 | 286 / 176 / 110 | 6 | 6 | 113 / 82 |
| compiled22/ambient | 1225 | 66 / 57 / 33 | 1 | 1 | 0 / 0 |
| compiled22/angle_scale | 1225 | 66 / 57 / 33 | 1 | 1 | 100 / 0 |
| compiled22/area | 1225 | 66 / 57 / 33 | 1 | 1 | 0 / 0 |
| compiled22/bounce1 | 1225 | 66 / 57 / 33 | 1 | 1 | 100 / 0 |
| compiled22/bouncegrid1 | 1225 | 66 / 57 / 33 | 1 | 1 | 100 / 0 |
| compiled22/colour_clip | 1225 | 66 / 57 / 33 | 1 | 1 | 100 / 0 |
| compiled22/colour_gamma | 1225 | 66 / 57 / 33 | 1 | 1 | 100 / 0 |
| compiled22/extra_distance | 1225 | 66 / 57 / 33 | 1 | 1 | 100 / 0 |
| compiled22/half_lambert | 1225 | 66 / 57 / 33 | 1 | 1 | 100 / 0 |
| compiled22/inverse_square | 1225 | 66 / 57 / 33 | 1 | 1 | 100 / 0 |
| compiled22/linear | 1225 | 66 / 57 / 33 | 1 | 1 | 100 / 0 |
| compiled22/linear_angle | 1225 | 66 / 57 / 33 | 1 | 1 | 100 / 0 |
| compiled22/linear_fade | 1225 | 66 / 57 / 33 | 1 | 1 | 100 / 0 |
| compiled22/multi_sun | 1225 | 66 / 57 / 33 | 1 | 1 | 458 / 1242 |
| compiled22/no_angle | 1225 | 66 / 57 / 33 | 1 | 1 | 100 / 0 |
| compiled22/pointscale_half | 1225 | 66 / 57 / 33 | 1 | 1 | 100 / 0 |
| compiled22/retained_light | 1225 | 66 / 57 / 33 | 1 | 1 | 100 / 0 |
| compiled22/skylight | 1225 | 66 / 57 / 33 | 1 | 1 | 450 / 1250 |
| compiled22/spot | 1225 | 66 / 57 / 33 | 1 | 1 | 100 / 0 |
| compiled22/sun | 1225 | 66 / 57 / 33 | 1 | 1 | 459 / 1241 |
| compiled22/wolf_inverse_square | 1225 | 66 / 57 / 33 | 1 | 1 | 100 / 0 |
| compiled22/wolf_linear | 1225 | 66 / 57 / 33 | 1 | 1 | 100 / 0 |

The twelve original dataset folds remain frozen. The twenty-two kernel cases use their original planar-floor verification domain. Counts are not a whole-map readiness claim.

## Fair fitted-profile controls

| Domain | Alternative | Train-only scalar | Validation RMSE | Held-out RMSE | Outcome |
|---|---|---:|---:|---:|---|
| broad | isq | 298.979089 | 0.207790 | 0.046360 | Passed |
| broad | linear | 243.401810 | 30.150915 | 22.943279 | Passed |
| broad | noangle | 212.818923 | 14.232873 | 10.704386 | Passed |
| broad | spot | 370.685858 | 95.111947 | 75.333923 | Passed |
| broad_floor | isq | 299.549567 | 0.000000 | 0.000000 | Passed |
| broad_floor | linear | 287.386663 | 13.467441 | 13.802245 | Passed |
| broad_floor | noangle | 245.090839 | 3.611057 | 4.219440 | Passed |
| broad_floor | spot | 312.491698 | 18.145763 | 13.401448 | Passed |
| narrow | isq | 291.571812 | 0.000000 | 0.000000 | Ambiguous |
| narrow | linear | 294.574009 | 0.000000 | 0.000000 | Ambiguous |

All controls fit one bounded non-negative legacy-relative scalar with fixed colour and geometry. Validation selects; held-out confirms. No posterior or stock compiler identity is inferred.

All 35 X04 boundary texels / 105 channels remain evaluated; old max error 28, restricted source-locked replay max error 0.

See summary.json for per-control coverage/errors, normal/distance/brightness distributions, explicit unknowns and derivative cusp status; audit.json contains all 34 historical cases.

Controller wall time: 24.651 s; Python process peak working set: 34795520 bytes. Child processes are not included in that RSS.
