# ASR02 frozen measurements

Position-only diagnostic matching at 32 WU; proposals are not accepted emitters.

| Case | Method | Proposals | TP / FP / FN | Precision | Recall |
|---|---|---:|---|---:|---:|
| door_occlusion | censored | 8 | 1 / 7 / 0 | 0.125 | 1.000 |
| door_occlusion | geometry | 8 | 1 / 7 / 0 | 0.125 | 1.000 |
| door_occlusion | hidden | 2 | 1 / 1 / 0 | 0.500 | 1.000 |
| door_occlusion | raw | 0 | 0 / 0 / 1 | N/A | 0.000 |
| hoth3_roi | censored | 8 | Unavailable | Unavailable | Unavailable |
| hoth3_roi | geometry | 8 | Unavailable | Unavailable | Unavailable |
| hoth3_roi | hidden | 4 | Unavailable | Unavailable | Unavailable |
| hoth3_roi | raw | 8 | Unavailable | Unavailable | Unavailable |
| point_room | censored | 8 | 1 / 7 / 0 | 0.125 | 1.000 |
| point_room | geometry | 8 | 1 / 7 / 0 | 0.125 | 1.000 |
| point_room | hidden | 2 | 1 / 1 / 0 | 0.500 | 1.000 |
| point_room | raw | 8 | 1 / 7 / 0 | 0.125 | 1.000 |
| reflected_patch | censored | 0 | 0 / 0 / 0 | N/A | N/A |
| reflected_patch | geometry | 0 | 0 / 0 / 0 | N/A | N/A |
| reflected_patch | hidden | 2 | 0 / 2 / 0 | 0.000 | N/A |
| reflected_patch | raw | 0 | 0 / 0 / 0 | N/A | N/A |
| sky_aperture | censored | 8 | 0 / 8 / 0 | 0.000 | N/A |
| sky_aperture | geometry | 0 | 0 / 0 / 0 | N/A | N/A |
| sky_aperture | hidden | 0 | 0 / 0 / 0 | N/A | N/A |
| sky_aperture | raw | 0 | 0 / 0 / 0 | N/A | N/A |
| skylight | censored | 0 | 0 / 0 / 0 | N/A | N/A |
| skylight | geometry | 8 | 0 / 8 / 0 | 0.000 | N/A |
| skylight | hidden | 4 | 0 / 4 / 0 | 0.000 | N/A |
| skylight | raw | 0 | 0 / 0 / 0 | N/A | N/A |
| spot | censored | 0 | 0 / 0 / 1 | N/A | 0.000 |
| spot | geometry | 8 | 0 / 8 / 1 | 0.000 | 0.000 |
| spot | hidden | 1 | 0 / 1 / 1 | 0.000 | 0.000 |
| spot | raw | 0 | 0 / 0 / 1 | N/A | 0.000 |
| two_colour | censored | 8 | 0 / 8 / 2 | 0.000 | 0.000 |
| two_colour | geometry | 8 | 1 / 7 / 1 | 0.125 | 0.500 |
| two_colour | hidden | 6 | 2 / 4 / 0 | 0.333 | 1.000 |
| two_colour | raw | 8 | 1 / 7 / 1 | 0.125 | 0.500 |

## Cost

| Case | Method | Spatial samples / native reported nodes | Rays | Work |
|---|---|---:|---:|---:|
| door_occlusion | censored | 357 | 20608 | 776005 |
| door_occlusion | geometry | 357 | 20608 | 773127 |
| door_occlusion | hidden | 128 | 24576 | 930104 |
| door_occlusion | raw | 357 | 20608 | 777081 |
| hoth3_roi | censored | 357 | 8384 | 1949276 |
| hoth3_roi | geometry | 357 | 8384 | 1949276 |
| hoth3_roi | hidden | 128 | 8091 | 1749611 |
| hoth3_roi | raw | 357 | 12096 | 3077890 |
| point_room | censored | 357 | 22848 | 440895 |
| point_room | geometry | 357 | 22848 | 440895 |
| point_room | hidden | 128 | 24576 | 469376 |
| point_room | raw | 357 | 22848 | 440895 |
| reflected_patch | censored | 357 | 22848 | 440895 |
| reflected_patch | geometry | 357 | 22848 | 440895 |
| reflected_patch | hidden | 120 | 23040 | 440040 |
| reflected_patch | raw | 357 | 22848 | 440895 |
| sky_aperture | censored | 357 | 22848 | 704878 |
| sky_aperture | geometry | 357 | 22848 | 763852 |
| sky_aperture | hidden | 0 | 0 | 0 |
| sky_aperture | raw | 357 | 22848 | 763852 |
| skylight | censored | 357 | 22848 | 684885 |
| skylight | geometry | 357 | 22848 | 715355 |
| skylight | hidden | 128 | 22752 | 668970 |
| skylight | raw | 357 | 22848 | 715967 |
| spot | censored | 357 | 17136 | 332367 |
| spot | geometry | 357 | 17136 | 332367 |
| spot | hidden | 128 | 18432 | 352640 |
| spot | raw | 357 | 17136 | 332367 |
| two_colour | censored | 357 | 22848 | 440895 |
| two_colour | geometry | 357 | 22848 | 440895 |
| two_colour | hidden | 216 | 41472 | 792072 |
| two_colour | raw | 357 | 22848 | 440895 |

Same declared caps, different actual work; IDF stencil positions are charged.
Hidden reports cell nodes and separately probes types/amplitudes; these counters are not identical work units.
RGB8 results have no calibrated intensity-bound or historical-source interpretation.
