# Surviving authored lights in stock Jedi Academy maps

Read-only extraction from licensed local `assets0.pk3` through `assets3.pk3`.
No downloaded game assets, mod overrides, inverse light fitting or renderer
changes. Python 3.8+ standard library only.

```powershell
python -u -m tools.rend2.stock_lights.extract --base 'C:\Program Files (x86)\Steam\steamapps\common\Jedi Academy\GameData\base' --output build/stock-light-extraction
python -m unittest tools.rend2.stock_lights.test_extract -v
python -m tools.rend2.stock_lights.verify --base 'C:\Program Files (x86)\Steam\steamapps\common\Jedi Academy\GameData\base' --output build/stock-light-extraction --q3map2 build/toolchain/netradiant-20220628/portable/netradiant_1.5.0-20220628-windows-amd64/q3map2.exe
```

The stock archive view is explicit: later `assetsN.pk3` overrides an earlier
member with the same name. Loose files and other PK3s are excluded. All 61 BSP
archive members are inventoried, including the four overridden MP maps; final
exports cover 57 distinct maps (34 SP, 23 MP). Duplicate shader definitions are
retained and flagged instead of silently choosing an interpretation.

Outputs stay under ignored `build/` because they include original map data:

- `lights.json`: all surviving `light*` entities, with map/source SHA-256,
  entity ordinal, original key/value pairs and convenient numeric fields.
  Missing numeric fields stay null; no compiler defaults or physical units
  are invented. `light` and `scale` are separate authored parameters.
- `summary.json`, `report.md`: every map's counts and source hashes; summary
  also inventories each original/overridden archive member.
- `maps/<name>/entities.ent`: byte-exact original entities lump, including NUL.
- `maps/<name>/lights.ent`: verbatim light blocks only, an entity fragment,
  **not** a complete map or a runtime entity replacement file.
- `maps/<name>/lighting.json`: entity lights, complete worldspawn, shader
  sunlight declarations with `_noshadersun` suppression, original shader
  definitions, possible spot targets and explicit runtime light entities.
- `maps/<name>/surface-emitters.json`: BSP surface fragments referencing a
  shader with positive `q3map_surfacelight`, with original positions, normals,
  base UVs, local indices, model ownership and surface type. Patches retain
  their Bezier control nets; inline-model vertices retain local coordinates.
- `verification.json`: independent Q3Map2 `-exportents` comparison for all 61
  BSP members, including byte-exact direct lump and native light-fragment
  comparisons for all 57 selected maps. Native Windows text-mode CRLF and
  trailing NUL/newline differences are the only comparison normalization.

Measured on 2026-11-10: **52 entity lights** in three SP maps: `t1_fatal` (1),
`t3_bounty` (47), `t3_stamp` (4). All have `targetname` and switchable lightstyles.
There are also **3,606 BSP surface fragments** with positive surfacelight
metadata in 35 maps. These are neither individual lamps nor exact compiler
emission samples. Fourteen shader sun declarations are referenced, three are
suppressed by `_noshadersun`; two of the remaining declarations have zero
intensity. These metadata counts do not certify the historical bake.

`glow` alone is counted separately; it does not establish light emission into
the baked map. No removed point lights, reflected lights, FX lights, external
model geometry, historical compile settings or SI light calibration are
reconstructed. The exporter does not infer texture-derived emission color.

See [the investigation](../../../docs/rend2-stock-authored-lights.md) for the
engine/compiler evidence and practical uses.
