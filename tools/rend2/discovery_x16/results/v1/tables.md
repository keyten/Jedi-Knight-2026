# X16 measured discovery and selection

| Variant / policy | Candidates | Point 1 / point 2 / spot distance WU | Proposal recall | Rays |
|---|---:|---|---:|---:|
| direct / baseline | 208 | 6.270 / 11.803 / 74.343 | 0.667 | 146640 |
| direct / legacy_adapter | 208 | 6.270 / 11.803 / 74.343 | 0.667 | 146640 |
| direct / budgetmatched | 215 | 6.270 / 11.803 / 74.343 | 0.667 | 203136 |
| direct / new | 194 | 6.270 / 11.803 / 11.800 | 1.000 | 215376 |
| passive_bounce1 / baseline | 218 | 22.788 / 21.432 / 75.676 | 0.667 | 162456 |
| passive_bounce1 / legacy_adapter | 218 | 22.788 / 21.432 / 75.676 | 0.667 | 162456 |
| passive_bounce1 / budgetmatched | 217 | 22.788 / 21.432 / 71.318 | 0.667 | 221448 |
| passive_bounce1 / new | 217 | 22.788 / 21.432 / 11.800 | 1.000 | 240000 |

| Run | Native state | Accepted nonempty | Models | Rays | Held-out MSE |
|---|---|---:|---:|---:|---:|
| jk-lightrecon-x16-fit-isq-v1 / direct-baseline-geometry0-finite0-profile0 | Budget | False | 81 | 12000000 | Unavailable |
| jk-lightrecon-x16-fit-isq-v1 / direct-new-geometry0-finite0-profile0 | Budget | False | 77 | 12000000 | Unavailable |
| jk-lightrecon-x16-fit-isq-v1 / direct-new-geometry1-finite1-profile0 | Budget | False | 24 | 12000000 | Unavailable |
| jk-lightrecon-x16-fit-isq-v3 / direct-baseline-geometry0-finite0-profile0 | Unidentifiable | False | 90 | 7373830 | 31.78438626910547 |
| jk-lightrecon-x16-fit-isq-v3 / direct-new-geometry0-finite0-profile0 | Unidentifiable | False | 91 | 8853232 | 31.78438626910547 |
| jk-lightrecon-x16-fit-isq-v3 / direct-new-geometry1-finite1-profile0 | Budget | False | None | None | Unavailable |
| jk-lightrecon-x16-fit-isq-v3 / passive_bounce1-baseline-geometry0-finite0-profile0 | Budget | False | None | None | Unavailable |
| jk-lightrecon-x16-fit-isq-v3 / passive_bounce1-new-geometry0-finite0-profile0 | Partial | False | 91 | 7396444 | 32.21558879035137 |
| jk-lightrecon-x16-fit-isq-v3 / passive_bounce1-new-geometry1-finite1-profile0 | Budget | False | 27 | 12000000 | Unavailable |
| jk-lightrecon-x16-fit-linear-v3 / direct-baseline-geometry0-finite0-profile1 | Unavailable | False | 88 | 6585860 | Unavailable |
| jk-lightrecon-x16-fit-linear-v3 / direct-new-geometry0-finite0-profile1 | Unavailable | False | 102 | 9800814 | Unavailable |
| jk-lightrecon-x16-fit-linear-v3 / direct-new-geometry1-finite1-profile1 | Budget | False | None | None | Unavailable |
| jk-lightrecon-x16-fit-linear-v3 / passive_bounce1-baseline-geometry0-finite0-profile1 | Unavailable | False | 82 | 2808240 | Unavailable |
| jk-lightrecon-x16-fit-linear-v3 / passive_bounce1-new-geometry0-finite0-profile1 | Unavailable | False | 85 | 3335234 | Unavailable |
| jk-lightrecon-x16-fit-linear-v3 / passive_bounce1-new-geometry1-finite1-profile1 | Budget | False | 28 | 12000000 | Unavailable |
| jk-lightrecon-x16-fit-final-v5 / direct-new-geometry0-finite0-profile0 | Budget | False | None | None | Unavailable |
| jk-lightrecon-x16-fit-final-v5 / direct-new-geometry1-finite1-profile0 | Budget | False | 27 | 12000000 | Unavailable |
| jk-lightrecon-x16-fit-final-repeat-v5 / direct-new-geometry0-finite0-profile0 | Unidentifiable | False | 91 | 8853232 | 31.78438626910547 |
| jk-lightrecon-x16-frozen-diagnostics-v6 / direct-new-geometry0-finite0-profile0 | Unidentifiable | False | 91 | 8853232 | 31.78438626910547 |
