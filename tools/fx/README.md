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
