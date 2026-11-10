# ASR23 frozen blind benchmark

| Method | Recall | Precision | Proposal matches | False positives | Blind multisource successes | Mean held-out / black MSE | Fit seconds |
|---|---:|---:|---:|---:|---:|---:|---:|
| adaptive_off | 0.619 | 0.7222222222222222 | 17/21 | 5 | 1 | 0.34076 | 11.24 |
| all_cues | 0.619 | 0.7647058823529411 | 17/21 | 4 | 0 | 0.33691 | 11.06 |
| area_off | 0.619 | 0.7647058823529411 | 17/21 | 4 | 0 | 0.33691 | 10.26 |
| fixture_only | 0.000 | Unavailable | 2/21 | 0 | 0 | 0.81329 | 0.02 |
| grid_lightmap | 0.619 | 0.7647058823529411 | 17/21 | 4 | 0 | 0.33691 | 10.87 |
| grid_only | 0.524 | 0.9166666666666666 | 17/21 | 1 | 1 | 0.34589 | 0.58 |
| ja_baseline | 0.286 | 0.3333333333333333 | 7/21 | 12 | 0 | 0.50676 | 10.18 |
| lightmap_only | 0.333 | 0.4666666666666667 | 7/21 | 8 | 0 | 0.53304 | 7.51 |
| sun_off | 0.619 | 0.7647058823529411 | 17/21 | 4 | 0 | 0.33691 | 10.75 |

| Gate | Status |
|---|---|
| blind_multisource | Fail |
| recall_improves | Pass |
| heldout_improves | Pass |
| geometry | Pass |
| negative_local_sources | Fail |
| stock_source_positions | Unavailable |
