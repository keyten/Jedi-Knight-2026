# ASR23 — measured source discovery

16 owned compiled scenes; 9 common-budget configurations; 21 local source roots.
RGB8 errors are conditional encoded appearance, not calibrated irradiance.
The baseline uses unchanged strict JA Hidden proposals with the same benchmark fitter.

| Method | Proposal recall | Proposal precision | Selected recall | Selected precision | False sources | Held-out / black MSE | Blind multisource successes |
|---|---:|---:|---:|---:|---:|---:|---:|
| adaptive_off | 0.857 | 0.126 | 0.667 | 0.737 | 5 | 0.285 | 1 |
| all_cues | 0.857 | 0.133 | 0.667 | 0.737 | 5 | 0.263 | 0 |
| area_off | 0.857 | 0.133 | 0.667 | 0.737 | 5 | 0.263 | 0 |
| fixture_only | 0.095 | 1.000 | 0.000 | Unavailable | 0 | 0.813 | 0 |
| grid_lightmap | 0.857 | 0.135 | 0.667 | 0.778 | 4 | 0.303 | 0 |
| grid_only | 0.810 | 0.486 | 0.524 | 0.917 | 1 | 0.346 | 1 |
| ja_baseline | 0.381 | 0.075 | 0.333 | 0.350 | 13 | 0.476 | 0 |
| lightmap_only | 0.381 | 0.082 | 0.381 | 0.500 | 8 | 0.500 | 0 |
| sun_off | 0.857 | 0.133 | 0.667 | 0.778 | 4 | 0.303 | 0 |

The all-cues improvement is real for these frozen scenes, but its full acceptance gate fails:
five false local sources remain, and its two-color model includes a duplicate source.
Grid-only and adaptive-off pass the complete two-color blind multisource gate.

| Scene | All-cues matched / true | Local + solar sources | Held-out / black MSE | Source positions and types |
|---|---:|---:|---:|---|
| point_room | 1 / 1 | 1 + 0 | 0.00069 | Point (-93.8, 32.2, 182.5) |
| spot | 0 / 1 | 0 + 0 | 0.84591 | None |
| panel | 0 / 1 | 0 + 0 | 0.34230 | None |
| tube | 0 / 1 | 0 + 0 | 0.87012 | None |
| two_colour | 2 / 2 | 3 + 0 | 0.05264 | Point (90.7, -30.1, 162.2); Point (-95.3, 18.0, 179.9); Point (-104.0, 72.0, 200.0) |
| overlap_whites | 2 / 2 | 3 + 0 | 0.04263 | Point (-28.1, -54.0, 176.0); Point (91.4, -30.6, 162.1); Point (-74.0, 20.9, 174.5) |
| sun_sky | 0 / 0 | 2 + 2 | 0.47580 | Point (52.0, 36.0, 18.0); Point (26.0, 18.0, 35.0) |
| skylight | 0 / 0 | 0 + 0 | 1.00000 | None |
| bounce_colour | 1 / 1 | 2 + 0 | 0.00223 | Point (100.7, 0.1, 161.7); Point (98.3, -7.1, 146.0) |
| bounce_control | 1 / 1 | 1 + 0 | 0.00019 | Point (100.7, -0.1, 161.8) |
| door_occlusion | 1 / 1 | 1 + 0 | 0.00044 | Point (-94.3, 32.2, 182.5) |
| wall_leak | 1 / 1 | 1 + 0 | 0.00047 | Point (-94.9, 0.4, 182.2) |
| empty | 0 / 0 | 0 + 0 | Unavailable | None |
| mixed_sun_local | 1 / 1 | 1 + 2 | 0.10744 | Point (-68.1, 18.0, 171.8) |
| bridge_direct | 2 / 4 | 2 + 0 | 0.12113 | Point (-469.1, -303.8, 204.0); Point (452.2, -319.5, 196.8) |
| bridge_passive_bounce1 | 2 / 4 | 2 + 0 | 0.07771 | Point (451.7, -318.9, 197.3); Point (-453.5, -293.5, 218.5) |

| Numerical gate | Status |
|---|---|
| all_cues_blind_multisource | Fail |
| false_selected_sources | Fail |
| geometry | Pass |
| heldout_improves | Pass |
| negative_local_sources | Fail |
| recall_improves | Pass |
| stock_source_positions | Unavailable |
| suite_blind_multisource | Pass |
| sun_ablation_coverage | Pass |
| wall_leaks | Pass |

Proposal/selected precision uses maximum-cardinality one-to-one matching at 32 WU.
Types are scored separately. A blind multisource success additionally requires every type
correct, every true root matched, zero false sources, held-out/black MSE <= 0.10 and zero
geometry violations. Unknown/zero-energy scores and missing stock truth stay Unavailable.

| Method | Model fits | Metered RGB row-channel trials | Fit seconds |
|---|---:|---:|---:|
| adaptive_off | 487 | 36910620 | 11.95 |
| all_cues | 440 | 31863276 | 10.05 |
| area_off | 435 | 30299628 | 9.60 |
| fixture_only | 18 | 28344 | 0.02 |
| grid_lightmap | 381 | 28823532 | 9.28 |
| grid_only | 97 | 1600776 | 0.54 |
| ja_baseline | 298 | 30018456 | 9.49 |
| lightmap_only | 261 | 21119028 | 6.85 |
| sun_off | 391 | 30301932 | 9.71 |

Proposal/native geometry work is shared across configurations and recorded per scene in results.json.
Totals must not be added to each configuration as though independent experiments had been timed.
The common limits are caps, not promises of equal spent work. No wall-clock early stopping is used.
