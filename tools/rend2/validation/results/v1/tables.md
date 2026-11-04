# V02 generated measured tables

Unavailable metrics remain null. Stock input coverage is not fitted coverage.

| Stage | Frozen 57-map baseline | New selected maps |
|---|---:|---:|
| Original observation capture | 57 | 57 |
| Geometry/readiness | 57 | 8 |
| Local candidate generation attempted | not uniformly recorded | 8 |
| Global coordination attempted | 0 | 8 |
| Joint::Solve entered | 0 | 8 |
| Numerical direct-source forward during joint | 0 | 6 |
| Independent validation callback | 0 | 8 |
| Uncertainty assessment attempted | 0 | 0 |
| Frozen final held-out evaluation | 0 | 0 |
| Nonempty canonical publication | 0 | 0 |
| Prepared stock physical lighting field | 0 | 0 |

| Map | Source proposals (isq/linear) | Joint calls | Supported mapped area | Time s | RSS MiB |
|---|---:|---:|---:|---:|---:|
| hoth3 | 146/146 | 8 | 52.99% | 34.375 | 109.30 |
| hoth2 | 15/15 | 8 | 56.05% | 13.281 | 81.05 |
| vjun2 | 306/306 | 8 | 61.59% | 36.922 | 113.83 |
| kor1 | 38/38 | 8 | 59.39% | 19.938 | 93.83 |
| yavin1b | 17/17 | 8 | 51.17% | 7.438 | 46.42 |
| t1_danger | 20/20 | 8 | 3.76% | 41.687 | 57.44 |
| mp/duel4 | 4/4 | 8 | 62.74% | 5.531 | 34.77 |
| mp/ffa3 | 370/370 | 8 | 72.12% | 29.704 | 79.41 |

| Query | Successful calls | p50 us | p95 us | p99 us | Allocations |
|---|---:|---:|---:|---:|---:|
| snapshot_acquire | 32768/32768 | 0.125 | 0.125 | 0.125 | 0 |
| indirect_api_on_sky_field | 0/32768 | 0 | 0.125 | 0.125 | 0 |
| incoming_radiance | 32768/32768 | 2.375 | 2.625 | 3.25 | 0 |
| conditional | 32768/32768 | 2.25 | 2.375 | 3 | 0 |
| static_shadow | 32768/32768 | 0.125 | 0.125 | 0.125 | 0 |
| contribution | 32768/32768 | 0.25 | 0.375 | 0.5 | 65536 |
| probe_lookup | 32768/32768 | 0.25 | 0.375 | 0.375 | 0 |
| region_lookup | 0/32768 | 0 | 0.125 | 0.125 | 0 |
