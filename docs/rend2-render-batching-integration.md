# Render batching integration: BSP, grass, model queries and MD3

Validated together on 2026-11-25, implementation merge HEAD
`52b5e367b2116136451e1c102b9613f12972981f`. No deployment or push was performed.

| Optimization | Enable / reference | Default |
|---|---|---|
| Compact visible BSP list | `r_worldSurfaceList 1`; `2` independently compares membership; `0` original stamps/scan | 1 |
| Per-sprite GPU culling / LOD | `r_surfaceSpritesCompute 1` same LOD culling; `2` per-plant LOD; `0` direct compact records | 2 |
| Current-frame Ghoul2 occlusion | `r_modelOcclusion 1`; `2` queries with matching batching but unconditional colour; `0` original | 1 |
| Repeated rigid MD3 instance draws | `r_md3Instancing 1`; `0` individual draws | 1 |

The later user-selected defaults enable GPU sprite mode 2 and MD3 mode 1 for
fresh/unset cvars. Existing archived values are preserved. The measured reductions
are real; the earlier bounded timings did not establish an overall speedup. No FPS
improvement is inferred from this defaults choice. The ordinary shader families do not acquire MD3 instance indexing.

## Combined checks

Final SP and MP Release renderer builds passed. Four relevant CPU tests passed:
compact-world membership/order/reset and query admission, sprite capacity, MD3
eligibility/grouping/overflow, and 25 character-culling checks. The merged Intel
hidden-context checks passed actual MD3 draw/lighting parity, sprite compute/list/
indirect/deformation/velocity parity and the current-frame query protocol.
Independent branches also passed their representative Intel UHD and RTX 2060 shader
checks; this integration did not repeat the entire permutation matrix.

One private Yavin scene combined native grass, 40 rendered MD3 barrels, six Ghoul2
characters, initialized sun cascades and fused velocity, with `r_characterCull 1`.
All six strict pixel comparisons had **zero differing channels**: original versus
compact BSP, adding MD3, adding same-LOD grass culling, query-only versus conditional,
reference repeat and the moved near-camera query/conditional pair. The close-camera
query count was zero, confirming the explicit 256-unit admission bypass. The earlier
single-channel hair difference predates this gate; no tolerance hides a failed oracle.

The combined frame reduced **40 MD3 colour draws to 2**, saving 38 actual calls and
uploading 21,504 instance bytes. Shadows/velocity retained their individual draws.
Native sprite submission fell from **5,852 candidates to 774 plants** (5,078 culled),
with 28 compute dispatches reused by seven pass submissions. The original authored
sprite attributes occupy 64 instead of 224 bytes per plant on the direct path too.
The separate per-plant LOD run reduced cards 2,277 to 1,858 and vertices 22,230 to
13,312 at the explicit test distances; that LOD difference is intentional.

The combined far view exercised current-frame Ghoul2 queries: one sampled interval
resolved 756, found 126 hidden, and issued 144 guarded draws. Its nonblocking
CPU-ready lower-bound skip counter was zero; hidden counts alone do not prove a
NO_WAIT draw was skipped, so no extra saved calls are attributed here. The earlier
controlled GL check produced zero primitives for an occluded conditional draw, and
the private model runtime confirmed actual skips. See its separate receipt.

The included CPU mark+enumerate microbenchmark ran after the builds stopped:
200/200,000 visible surfaces took 150.15 -> 4.26 microseconds, 20,000 took
451.33 -> 146.02, and 180,000 took 943.76 -> 812.36. This measures this operation
including bookkeeping, not renderer frame time. Background desktop/driver activity
still limits timing interpretation.

## Preservation and evidence

Before integration, all 82 existing dirty/untracked files were copied and hashed,
the binary staged/unstaged patches and index were saved, and staged entries were
recorded. Immediately after the four sequential no-ff merges their bytes and
staged entries were unchanged. During the subsequent validation another active task
updated four unrelated relighting files. Their latest content was saved in a fresh
82-file snapshot and retained; it was never overwritten from the older snapshot.
Existing staged entries remained unchanged. No stash/reset/revert or unrelated
commit was used. The source
snapshot remains under the private MD3 worktree's `build/integration-preservation`.
Private game processes exited normally; installed game assets and DLLs were read
only. Source renderer outputs were rebuilt in the repository build directory.

[Combined machine-readable receipt](rend2-render-batching-validation.json) records
capture/binary hashes and counts. Reproduce the bounded case with
`tools/rend2/test_render_batching_integration_runtime.py`.

Implementation detail and limitations are in
[visible surfaces and queries](rend2-visible-surfaces-occlusion.md),
[grass culling](rend2-grass-culling.md), and [MD3 instancing](rend2-md3-instancing.md).
