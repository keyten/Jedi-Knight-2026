# X06 aggregate witness

No inverse solver was run. All licensed payloads remain in the recorded private temporary directory.

| Map | Raw colour pages | Mapped / raw texels | Ambiguous | Unsupported | Censored samples | Train / validation / held-out |
|---|---:|---:|---:|---:|---:|---|
| vjun2 | 42 | 448,811 / 688,128 | 40,046 | 2,310 | 62,755 | 133926 / 50916 / 47944 |
| kor1 | 24 | 235,843 / 393,216 | 18,978 | 3,824 | 43,552 | 13558 / 4855 / 5546 |
| yavin1b | 10 | 92,167 / 163,840 | 5,959 | 11,481 | 22,174 | 11763 / 3145 / 3594 |
| t2_trip | 14 | 101,098 / 229,376 | 23,694 | 326 | 30,021 | 6038 / 2320 / 2125 |
| mp/ffa1 | 22 | 226,573 / 360,448 | 1,987 | 1,295 | 9,445 | 37855 / 14043 / 12000 |
| mp/ctf1 | unavailable | Unavailable: Section chunk limit | — | — | — | — |
| mp/duel1 | 46 | 486,958 / 753,664 | 4,433 | 29,647 | 78,851 | 140729 / 41105 / 17609 |

Counts of ambiguous/unsupported/uncovered are separate. The known-footprint area denominator is not total world receiver area. See JSON for area-based predicates, normals, styles, per-page/height distributions, stages and hashes.

## Direct-light attempt

| Map | Outcome | ROIs | Rays | Visible / blocked / unknown |
|---|---|---:|---:|---|
| vjun2 | NoAdmittedRegionWithinBudget | 8 | 7952 | 5 / 2270 / 5677 |
| kor1 | InsufficientLegitimateGlowPriors | 0 | 0 | 0 / 0 / 0 |
| yavin1b | NoAdmittedRegionWithinBudget | 8 | 0 | 0 / 0 / 0 |
| t2_trip | InsufficientLegitimateGlowPriors | 0 | 0 | 0 / 0 / 0 |
| mp/ffa1 | NoAdmittedRegionWithinBudget | 8 | 8992 | 0 / 2595 / 6397 |
| mp/ctf1 | Unavailable | 0 | 0 | 0 / 0 / 0 |
| mp/duel1 | InsufficientLegitimateGlowPriors | 0 | 0 | 0 / 0 / 0 |

No new stock region was admitted by the frozen common-visible/multi-normal/spatial-fold rules. This is a bounded candidate-selection result, not a conclusion that stock direct lights cannot be identified.

## Independent geometry ROIs

| Map / ROI | Regions | Free / solid / unknown cells | Rays visible / blocked / unknown | Opaque reference hits / false clear | Peak process bytes |
|---|---:|---|---|---|---:|
| vjun2 / multi_normal | 1 | 1053 / 0 / 1818 | 106 / 93 / 57 | 79 / 0 | 80592896 |
| vjun2 / upward_sky_candidate | 12 | 647 / 29 / 3329 | 44 / 175 / 37 | 189 / 0 | 80478208 |
| vjun2 / uncertain_optics_candidate | 1 | 365 / 127 / 2078 | 130 / 125 / 0 | 122 / 0 | 80445440 |
| vjun2 / fixture_neighbourhood | 1 | 71 / 0 / 3257 | 164 / 76 / 15 | 74 / 0 | 80646144 |
| kor1 / multi_normal | 8 | 215 / 2 / 3746 | 96 / 158 / 2 | 158 / 0 | 71954432 |
| kor1 / upward_sky_candidate | 4 | 2097 / 240 / 1395 | 163 / 93 / 0 | 91 / 0 | 72765440 |
| kor1 / uncertain_optics_candidate | 4 | 1364 / 93 / 2464 | 141 / 83 / 32 | 78 / 0 | 72417280 |
| kor1 / fixture_neighbourhood | 4 | 810 / 93 / 2255 | 140 / 89 / 27 | 81 / 0 | 71835648 |
| yavin1b / multi_normal | 7 | 465 / 133 / 957 | 71 / 152 / 33 | 150 / 0 | 44552192 |
| yavin1b / upward_sky_candidate | 1 | 425 / 537 / 789 | 245 / 11 / 0 | 11 / 0 | 44724224 |
| yavin1b / uncertain_optics_candidate | 1 | 320 / 448 / 1984 | 248 / 7 / 0 | 7 / 0 | 45363200 |
| yavin1b / fixture_neighbourhood | 3 | 711 / 131 / 1182 | 72 / 184 / 0 | 183 / 0 | 44855296 |
| t2_trip / multi_normal | 1 | 1069 / 1078 / 885 | 248 / 8 / 0 | 7 / 0 | 66981888 |
| t2_trip / upward_sky_candidate | 1 | 150 / 0 / 2616 | 253 / 2 / 0 | 2 / 0 | 66949120 |
| t2_trip / uncertain_optics_candidate | 2 | 342 / 30 / 2968 | 222 / 32 / 2 | 34 / 0 | 66981888 |
| t2_trip / fixture_neighbourhood | 4 | 1152 / 762 / 1622 | 121 / 135 / 0 | 134 / 0 | 66957312 |
| mp/ffa1 / multi_normal | 2 | 380 / 47 / 3102 | 110 / 123 / 23 | 123 / 0 | 32538624 |
| mp/ffa1 / upward_sky_candidate | 2 | 565 / 106 / 2403 | 110 / 96 / 50 | 102 / 0 | 32612352 |
| mp/ffa1 / uncertain_optics_candidate | 1 | 180 / 463 / 2728 | 135 / 39 / 82 | 31 / 0 | 32325632 |
| mp/ffa1 / fixture_neighbourhood | 1 | 203 / 303 / 2967 | 133 / 58 / 65 | 54 / 0 | 32124928 |
| mp/duel1 / multi_normal | 2 | 482 / 4 / 3232 | 111 / 127 / 18 | 115 / 0 | 18063360 |
| mp/duel1 / upward_sky_candidate | 1 | 387 / 88 / 1521 | 124 / 82 / 49 | 76 / 0 | 17207296 |
| mp/duel1 / uncertain_optics_candidate | 3 | 821 / 0 / 1847 | 64 / 86 / 106 | 85 / 0 | 17870848 |
| mp/duel1 / fixture_neighbourhood | 2 | 342 / 0 / 1094 | 115 / 123 / 18 | 118 / 0 | 16637952 |

Adaptive cell counts have unequal volumes; exact clipped Free/Solid/Unknown volumes and contact errors are in JSON. Unknown is not transparency.

## Controlled new bakes

| Case | Known direct RMSE | Zero RMSE | Fitted isq held-out | Fitted linear held-out | Outcome |
|---|---:|---:|---:|---:|---|
| multi_direct | 0.000000 | 18.996544 | 0.000000 | 11.402418 | ConditionalSingleton |
| multi_bounce | 4.339191 | 23.369236 | 1.751141 | 15.410761 | ConditionalSingleton |
| multi_red_bounce | 3.236138 | 22.174013 | 1.493255 | 13.818738 | ConditionalSingleton |
| spot_oblique | 0.000000 | 11.977251 | 0.000000 | 5.713269 | ConditionalSingleton |
| gamma_clipped | 0.000000 | 154.418394 | 0.000000 | 8.226310 | ConditionalSingleton |
| saturated_white | 0.000000 | 254.727662 | 0.000000 | 0.000000 | Ambiguous |
| thin_door | 0.000000 | 72.332146 | 0.000000 | 17.074158 | ConditionalSingleton |
| sky_roof_v2 | 0.000000 | 25.732894 | 0.000000 | 0.000000 | NotAnAttenuationTest |
| single_coincident | 0.000000 | 26.892040 | 0.000000 | 13.813186 | ConditionalSingleton |
| split_coincident | 0.000000 | 26.892040 | 0.000000 | 12.655753 | ConditionalSingleton |

Encoded intervals and shared-peak operators are profile-specific. Bounce rows deliberately evaluate a direct-only model and do not certify an indirect solver. Co-located split answers produce identical original lightmaps. Fully white saturation preserves competing attenuation hypotheses.

Exact reproduction, failures, decision changes and remaining prerequisites: [technical report](../../../../docs/rend2-inverse-lighting-cross-map-validation.md).
