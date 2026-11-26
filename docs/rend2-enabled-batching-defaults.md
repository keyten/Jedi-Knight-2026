# Enabled batching defaults (2026-11-26)

The user-selected factory defaults for both SP and MP are now:

| Control | Default | Meaning |
|---|---:|---|
| `r_worldSurfaceList` | 1 | Compact ordered BSP surface membership |
| `r_surfaceSpritesCompute` | 2 | GPU per-plant culling and per-plant LOD |
| `r_modelOcclusion` | 1 | Current-frame conservative Ghoul2 query guards |
| `r_md3Instancing` | 1 | Compatible rigid repeated MD3 instance draws |

Every existing 0 reference/off path remains available. Hardware, failed-program,
capacity, unsafe-material and view fallbacks are unchanged. These are defaults for
fresh/unset variables; explicit archived values retain priority. No installed game
configuration is rewritten. This request changes the four controls above; it does
not enable experimental point-shadow caches, Forward+ capacity or water tessellation.

The earlier timing results did not establish a net frame-time benefit for every
scene. Enabling these defaults is the user's explicit preference, not a new FPS
claim. The prior implementation receipts remain historical evidence for their
recorded settings.

## Registered-default smoke

Both SP and MP candidate renderer builds include these defaults. The source contract
`tools/rend2/test_enabled_batching_defaults.py` checks registrations and archive/off
controls in both. The bounded Yavin smoke resets the four controls to their actual
registered defaults and checks `r_help` output, rather than merely setting equivalent
values manually. Shader optimization 7 is fixed off to isolate the defaults change.

The frozen scene contains native grass, 40 repeated MD3s, six Ghoul2 characters,
initialized sun cascades and fused velocity, with character culling on. Default/repeat
and off/restore captures are **byte exact** (zero differing channels). Samples showed
40 MD3 draws grouped into 2 (38 saved), and 5,852 candidate plants reduced to 774,
with mode-2 geometry at 1,858 cards / 13,312 vertices and seven cached pass reuses.
The model query path was exercised; one nonblocking lower-bound sample confirmed
one skipped draw / 1,320 indices. No timing measurement is attributed to this smoke.

The helper has an explicit correctness-only foreign-process allowance and records
its inventory; it never terminates foreign processes. The successful replay started
with no foreign OpenJK process. An earlier private attempt exceeded SP's 32 startup
console-line limit; the helper now forwards only essential context/cache controls,
keeps filesystem arguments first and asserts the command limit before launching.

[Smoke receipt](rend2-enabled-batching-defaults-validation.json) contains the private
launch settings and binary/capture hashes. The final integration report records
merged-main builds and preservation separately.
