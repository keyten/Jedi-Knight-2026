# Full IDF follow-up measurements

All source positions are research hypotheses; no canonical physical source is published.

| Scene | Variant | State | Active sources | TP/FP/FN | Held-out relative RMSE |
|---|---|---|---:|---|---:|
| compiled_point_room | full | NoRemainingVotePeaks | 2 | 0/2/1 | 52.2538% |
| compiled_point_room | one_pass_fit | Partial | 3 | 0/3/1 | 79.8436% |
| compiled_point_room | proposal_only | ProposalsOnly | 4 | 0/4/1 | Not evaluated |
| compiled_two_colour | full | NegativeResidualFailure | 5 | 0/5/2 | 14555.5% |
| compiled_two_colour | one_pass_fit | Partial | 1 | 0/1/2 | 725.212% |
| compiled_two_colour | proposal_only | ProposalsOnly | 1 | 0/1/2 | Not evaluated |
| linear_fill_control | full | Budget | 1 | 0/1/0 | 36.8043% |
| linear_fill_control | one_pass_fit | Partial | 1 | 0/1/0 | 36.8043% |
| linear_fill_control | proposal_only | ProposalsOnly | 1 | 0/1/0 | Not evaluated |
| linear_four | full | Converged | 4 | 4/0/0 | 4.93239e-07% |
| linear_four | one_pass_fit | Partial | 1 | 0/1/4 | 28.5663% |
| linear_four | proposal_only | ProposalsOnly | 1 | 0/1/4 | Not evaluated |
| linear_two | full | Converged | 2 | 2/0/0 | 6.77036e-11% |
| linear_two | one_pass_fit | Converged | 2 | 2/0/0 | 6.77036e-11% |
| linear_two | proposal_only | ProposalsOnly | 3 | 0/3/2 | Not evaluated |

Source matching: 5% of room width on authored linear controls, 32 WU on compiler cases.
Active means >1% of strongest fitted source and >1e-6 normalized amplitude; raw models retain negligible extra slots.
All positive-source truth and held-out RGB are used only after every model freezes.

## Work

| Scene | Variant | New position queries | IDF transforms | Coefficient visits |
|---|---|---:|---:|---:|
| compiled_point_room | full | 52591 | 38 | 60436992 |
| compiled_point_room | one_pass_fit | 9084 | 1 | 1502592 |
| compiled_point_room | proposal_only | 8944 | 1 | 1502592 |
| compiled_two_colour | full | 12271 | 26 | 29922816 |
| compiled_two_colour | one_pass_fit | 2251 | 1 | 1067136 |
| compiled_two_colour | proposal_only | 2236 | 1 | 1067136 |
| linear_fill_control | full | 2990 | 160 | 960422400 |
| linear_fill_control | one_pass_fit | 2259 | 1 | 6002640 |
| linear_fill_control | proposal_only | 2236 | 1 | 6002640 |
| linear_four | full | 24999 | 68 | 410628960 |
| linear_four | one_pass_fit | 2315 | 1 | 6002640 |
| linear_four | proposal_only | 2236 | 1 | 6002640 |
| linear_two | full | 7062 | 1 | 7635600 |
| linear_two | one_pass_fit | 7062 | 1 | 7635600 |
| linear_two | proposal_only | 6708 | 1 | 7635600 |

The shared geometry-only cache preparation costs another 14,749 positions per scene.
Local refinements have a soft query cap; optimizer queries remain separately counted.
