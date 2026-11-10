# ASR25 — original 57-map source-discovery audit

This is an offline research controller around the existing ASR09 catalogue,
ASR24 original-grid discovery, native independent ROI diagnostics and ASR20/24
fitting/validation/removal audit. It does not create canonical lighting assets or
change the historical solver, renderer, world shading or runtime consumers.

The corpus is exactly 34 SP and 23 MP original BSPs from an explicit, licensed
**assets0–3-only** view. No downloaded/replacement assets or synthetic maps are
substituted. Raw geometry, textures, RGB8, XYZ and hypotheses stay in ignored
`build/`. Public results are counts, losses, limitations and hashes.

## Reproduce

Python 3.8+, Pillow and the existing native CPU tools are required. The default
native executable paths and original stock archives are the same as ASR24.
Supply an existing capture manifest whose `dataset` paths name local original
observation files. A manifest entry is not proof that the file still exists.

```powershell
python -X utf8 -m tools.rend2.stock_audit.run --work build/asr25-reproduce

$taskCaptures = "$env:TEMP/jk-lightrecon-all-stock-20261031/captures.json"
python -X utf8 -m tools.rend2.stock_audit.verify --work build/asr25-reproduce --captures $taskCaptures
python -X utf8 -m tools.rend2.stock_audit.packet --work build/asr25-reproduce --captures $taskCaptures
python -X utf8 -m tools.rend2.stock_audit.response --work build/asr25-reproduce
python -X utf8 -m tools.rend2.stock_audit.collect --work build/asr25-reproduce --out build/asr25-public-reproduce
python -X utf8 -m unittest tools.rend2.stock_audit.test_audit tools.rend2.stock_aesthetic.test_protocol -v
```

Use fresh output directories; all prior successes and failures are retained.
`--base`, `--stock-private` and `--bin` override the original input/tool roots.
Process failures are recorded per map and never stop the rest of the inventory.
Windows process timeouts terminate the entire worker process tree.

## Frozen operations and limits

[policy-v1.json](policy-v1.json) and the input/implementation/executable hashes are
written into the private `protocol.json` before any discovery. The implementation
and native binaries are copied into the research work folder. All maps use the
same policy and two concurrent map workers:

| Operation | Limit |
|---|---:|
| Original luminous catalogue | mask axis 32; 1,000,000 work; 250,000 pieces; 180 s |
| Original-grid fixture ranking | unchanged ASR24; 256 eligible components; 180 s |
| Independent original-observation ROI probe | six 256-WU-half-extent cubes; 180 s |
| Full original receiver export, only when numerical follow-up is possible | 240 s |
| Prepare source/receiver geometry, folds and input freeze | 300 s |
| Fit, repeat, solar/association diagnostics and source-removal audit | 300 s |

The catalogue and independent ROI statistics are attempted even when subsequent
discovery cannot run. Missing captures, native section/format refusals, dynamic
geometry, budgets, unknown support and unmeasurable folds remain explicit.
Native tools retain their own work/memory/geometry gates; process limits do not
replace them. The legacy Python row reader also retains its two-million-row cap.

Six observation ROIs per map are designed from original world planar surfaces:
vertex centroid plus 4 WU signed normal, first anchor by stable map/surface hash,
then farthest-point anchors with stable hash tie breaking. No fixture selection,
brightness, lightmap pixels, grid color, fitted source or fit score chooses them.
The existing native `lightrecon_dataset_roi` scans **captured mapped rows** and
reports fit eligibility, material uncertainty, visibility and upward sky reach.
These are observation statistics. They are neither illumination fit errors nor
accepted-source coverage; overlaps must not be added as unique coverage. An
anchor is not certified free merely because it was designed from a face.

ASR24 discovery retains its static-map guard and all eligibility filters. Its
first three ranked components with at least three distinct original-grid records
receive follow-up. The sole cohort adapter permits **one to three** supported
fixtures instead of requiring exactly three, and accepts explicit original
capture/source/rows paths uniformly for SP and MP. It changes no numerical or
geometric gate. Static BSP fixture counts do not count external MD3 emitters.

The ASR24 local128, common-normal, native receiver-hit policy freezes source
positions, bounds, material/normal selection and disjoint geometric folds before
fitting. All acceptance gates remain unchanged: admitted positive source,
convergence without an upper-bound hit, validation selection, at least 10% held-out
MSE and 5% robust-loss improvement, original-grid support and a substantive frozen
source-removal response. Unknown forward coverage is excluded and counted;
known blocked zeros remain known. No RGB8-to-linear-irradiance assumption is made.

## Read results correctly

`maps.json`, `maps.csv` and `maps.md` cover every stock map. `accepted_lights` counts
accepted bounded one-source configurations, **not historical lamps**. Overlapping
or alternative configurations cannot establish source-count truth.

* `Blocked`/incomplete evaluation: accepted count and source coverage are null.
* Completed discovery with no supported component: count 0 under this bounded
  policy; no empty-light solution or successfully relit map is published.
* Completed cohort: accepted support counts unique original sample IDs across
  accepted regions and separately counts held-out IDs. This is not whole-map
  source recall or a spatial area fraction.
* `archived_observation_census` is explicitly labelled preserved v4 evidence. It
  does not recertify a missing capture or become a fresh ASR25 measurement.

Work counters keep native fitter work, ROI ray primitive tests and logical
discovery queries in separate units. No total combines unlike counters. Actual
elapsed and summed map wall times include OS scheduling/I/O and are not expected
to reproduce exactly. Individual native results are repeated byte for byte.

The private `inspection/index.html` links seven scene plates and every generated
fitting-region report, including failures. They are original geometric
projections, not remastered screenshots. External model origins are marked as
presence hints; their unavailable triangles/emission are not treated as empty.
Diagnostic plate selection never changes discovery or scoring. `verify` rehashes
all BSPs, surviving captures, catalogues and source images, checks unchanged
implementation/tools and compares each selected fitting row with original BSP
RGB8 bytes, sample IDs and separated geometric blocks.

`response` adds private original/on/off held-out appearance plates to accepted
regions, using the same frozen native encoding input. Grey cells are unmeasured
support. It never refits a source or changes an acceptance result.

Historical source count, exact historical XYZ, absolute SI, stock transfer and
not-yet-supported shapes remain Unknown/Unavailable. MP keeps the qualified SP
material interpretation. Conditional aesthetic candidates never grant historical
`Resolved` certification.
