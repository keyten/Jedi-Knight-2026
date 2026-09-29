# EFX asset audit

`audit_stock_fx.py` reads commercial PK3s in place and writes derived research
reports. It does not change assets, install overrides, or implement runtime
physicalization. Candidate material labels are not validated classifiers.

Python 3.8+; Pillow is optional and required only for `--textures`.

Archives must be supplied from lowest to highest priority. For the stock JA
corpus use assets0, assets1, assets2, assets3; do not silently mix test/mod PK3s
into that baseline. For a single installed base directory use case-insensitive
filename order, and write a separate report.

```powershell
$fxBase = 'C:\Users\Keyten\Desktop\projects\OpenJK\build-rend2\base'
python tools/fx/audit_stock_fx.py "$fxBase\assets0.pk3" "$fxBase\assets1.pk3" "$fxBase\assets2.pk3" "$fxBase\assets3.pk3" --out build/fx-audit/stock --source-root . --textures

$fxArchives = Get-ChildItem -LiteralPath $fxBase -Filter '*.pk3' | Sort-Object { $_.Name.ToLowerInvariant() } | ForEach-Object { $_.FullName }
python tools/fx/audit_stock_fx.py @fxArchives --out build/fx-audit/installed --source-root . --textures

python -m unittest discover -s tools/fx -p 'test_*.py' -v
```

Outputs:

- `inventory.json`: full primitive properties/ranges/curve flags, alternatives,
  child references, shader stages, texture statistics, BSP entities, source
  literal refs, detected graph reachability, archive provenance and overrides.
- `inventory.md`: summary, map placements and per-effect primitive tables.

The syntax reader preserves duplicate groups, reads aliases and list-valued
media, and treats SHADER empty directives differently from EFX nested groups.
Shader references strip image extensions, and texture resolution tries alternate
extensions, because scripts may request `.tga` while PK3s contain `.jpg`.
Unsupported primitive types remain in the inventory with a separate flag rather
than disappearing from the denominator. No commercial assets are extracted.

Limitations:

- This models a supplied PK3 overlay, not the complete engine filesystem:
  loose files, homepath, fs_game and server pure ordering are not resolved.
- Multiple shader definitions in different script files are reported as
  alternatives, not assigned a presumed runtime winner.
- Source string hits are potential references (including comments/preloads),
  not observed calls; dynamic strings, configs and scripts are not fully scanned.
- Map counts are placement counts, not active emitters or play frequency.
- Texture metrics use thumbnails up to 96×96 and the alpha or luminance mask;
  they do not recover physical extinction/albedo/phase from a texture.
- Vocabulary rules are deliberately incomplete. Mixed and unknown cases are
  left for review; `none` does not claim that an asset cannot become a medium.

See [the design review](../../docs/legacy-fx-physicalization-review.md).

## Stage-1 runtime profiles

The [runtime guide](../../docs/legacy-fx-physicalization.md) describes the four
policy modes, effect lists, conservative classifier and diagnostics.

`generate_physicalization_stock.py` emits reviewed effect/shader identifiers and
raw-file checksums, never asset contents. Generate only from the stock baseline:

```powershell
python tools/fx/generate_physicalization_stock.py "$fxBase\assets0.pk3" "$fxBase\assets1.pk3" "$fxBase\assets2.pk3" "$fxBase\assets3.pk3" --out build/fx-stock-check.h
```

Compare this output with `shared/fx/FxPhysicalizationStock.h`; do not regenerate
the reviewed stock table from a mod overlay. Whitespace/line-ending edits also
invalidate exact matches, after which only the conservative generic policy applies.

Compile the engine-independent C++11 harness and replay all stock alternatives:

```powershell
python tools/fx/test_physicalization.py --compiler 'C:\path\to\g++.exe' --stock-base $fxBase
```

`--stock-base` is optional; without it, only the synthetic policy/optics harness
runs. The runtime integration is also built through the normal SP game and MP
engine targets. Generated headers and reports retain only derived metadata.

## Composition and adaptive metadata

`fx_physicalizationComposite` and `fx_physicalizationAdaptive` are independent,
default-off experiments. See the runtime guide for confidence rules, hash-guard
limitations, reload requirements and cost. `generate_physicalization_shapes.py`
derives six reviewed shapes from the same private stock baseline; it emits only
metrics, names and checksums, never images or shader contents:

```powershell
python tools/fx/generate_physicalization_shapes.py "$fxBase\assets0.pk3" "$fxBase\assets1.pk3" "$fxBase\assets2.pk3" "$fxBase\assets3.pk3" --out build/fx-shape-check.h
```

Compare with `shared/fx/FxPhysicalizationShapes.h`. Python shape tests cover mask
support and empty masks; the C++ harness covers composition vetoes, bounded
calibration, alias conflicts and unchanged random state.

Optional Windows SP integration test (requires compiled SP game DLL in
`build/msvc-all/RelWithDebInfo` and an installation with stock private PK3s):

```powershell
python tools/fx/test_physicalization_runtime.py --installation 'C:\path\to\OpenJK\build-rend2'
```

It launches three owned SP processes sequentially with isolated homepaths under
ignored `build/`: cold flags-off, live composite/adaptive switching, and a
semantically unchanged private shader file with a deliberately changed checksum.
It checks renderer submission counts and classification logs. It does not
change the installation or constitute visual approval of every stock effect.
The private shader copy remains confined to ignored test output.

For manual comparison, install `physicalization_explosion.efx` in `effects/test/`
and `fx-stage2-demo.cfg` in the active game directory. Install
`test-fx-stage2-sp.cmd` next to the SP executable to enable the experiments before
map registration. Run `exec fx-stage2-demo.cfg` after loading a map. This synthetic
fixture is ours; it references installed shaders without copying stock assets.

## Emission, exact coalescing and measured coverage

New default-off flags: `fx_physicalizationEmission` (exact reviewed flames/fireballs)
and `fx_physicalizationAggregate` (only coincident automatic non-emissive shapes).
The runtime guide describes budgets, warm-up/reload, preservation of sprites and
lights, and the scope of the first coalescing step. Normalizing total density of
an arbitrary plume or recovering physical radiance from an image is not implemented.

Generate the emission table only from stock PK3s:

```powershell
python tools/fx/generate_physicalization_emission.py "$fxBase\assets0.pk3" "$fxBase\assets1.pk3" "$fxBase\assets2.pk3" "$fxBase\assets3.pk3" --out build/fx-emission-check.h
```

Compare with `shared/fx/FxPhysicalizationEmissionStock.h`. It records six reviewed
primitives/18 alternatives with EFX identifiers and hashes of scripts and every
source image. It contains no stock image/EFX contents. The C++ harness checks
exact-only emission, bounds/time envelope, authoring and physics vetoes, alias
resolution, pointwise density conservation, incompatible shapes and unchanged RNG.

Additional optional SP runtime checks (fresh SP DLL/exe in `build/msvc-all`,
fresh SP rend2 in `build/msvc`, all RelWithDebInfo):

```powershell
python tools/fx/test_physicalization_stage3_runtime.py --installation 'C:\path\to\OpenJK\build-rend2'
python tools/fx/measure_physicalization_coverage.py --installation 'C:\path\to\OpenJK\build-rend2'
```

The runtime check uses an unchanged private stock rocket EFX in isolated ignored
homepaths to bypass the installation's authored override, plus our coincident-puff
fixture. It tests switches, glow cap, shader hash fallback, master off, exact
coalescing, and authored emission receiving slots before automatic glows.
`--case enabled`, `hash-mismatch` or `authored-budget` runs only one case.
The enabled case also captures emission-isolation PNGs in its isolated home.

Coverage uses the developer SP command `fxaudit <effect>`, which registers through
the actual engine parser without spawning effects, reports resolved alternatives,
and releases only templates temporarily created by the audit (including child
preloads). It warms renderer asset caches; existing FX handles/schedules remain.
Enable profile flags before loading the map when auditing advanced candidates.
The tool stages current binaries in ignored output and measures stock and installed
overlays separately (`--scope stock`, `installed` or `both`). Windows hosts denying
private stock hardlinks use copies instead; no baseline asset is modified.

Derived `build/fx-coverage/*-coverage.json` includes exact/family/composite/adaptive/
emission/authored counts per primitive, separate file/primitive/alternative totals,
registration failures and scope limitations. Owned demonstration EFX are excluded.
This measures eligibility, not play frequency, call reachability, lists/master
policy, display time or GPU acceptance. Pure-server and normal user-home overrides
are not modeled by the isolated runtime.

For manual tests, install `test-fx-stage3-sp.cmd` next to the executable and both
`fx-stage3-*-demo.cfg` files in the active game directory; install
`physicalization_cluster.efx` in `effects/test/`. Emission demo plays existing
`env/small_fire`; aggregate demo compares GPU uploads with unchanged shape/density.


Source-context groundwork (separate default-off `fx_physicalizationSources`)
tracks independent bursts across scheduling and child effects in SP/MP, without
changing classifier eligibility or optical/render behavior. Bolted scheduler
loops retain a sidecar owner; independent world event calls remain separate
bursts until the callers provide reliable entity generations. No guessed owners.
`fxsources` (developer 1, loaded map) prints bounded last-scene frontend source
snapshots; counts precede renderer culling/budgets. Traces require the existing
`fx_physicalizationDebug 1`. Old particles/schedules remain unknown after cold
enable, reset, or disable/re-enable; spawn fresh effects to see valid IDs.

```powershell
python tools/fx/test_physicalization_sources_runtime.py --installation 'C:\path\to\OpenJK\build-rend2'
```

The isolated SP test uses fresh built core binaries and current rend2. It checks
same-position independent owners, immediate/delayed primitives/runners, emitter
and death children, physics restrictions, cold enable, and generation resets.
The synthetic EFX stays owned; no commercial contents are copied into the repo.
Shared tests also cover scope restoration, unknown/stale parents, portal vetoes,
RNG independence and the bounded scene ledger. MP runtime, real saved-game loop
restoration and GPU/performance profiling are not covered by this test.

Install `test-fx-stage4-sp.cmd` next to the executable,
`fx-stage4-sources-demo.cfg` in the active game directory, and
`physicalization_sources_smoke.efx` plus `physicalization_cluster.efx` in
`effects/test/`. After loading a map run `exec fx-stage4-sources-demo.cfg`.
This substage provides diagnostics and ownership groundwork; visual shape is
unchanged. For the next spatial aggregation substage, reliable environmental
caller ownership, birth/expiry records and a renderer bridge are still required.
