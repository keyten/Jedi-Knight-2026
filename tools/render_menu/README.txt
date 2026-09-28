Jedi Academy 2026 rendering menu (single player)

Copy zzzz_render2026_menu.pk3 into GameData/base (or your active mod).
Restart the game. During gameplay: Esc -> RENDER 2026.
Settings update cvars immediately. APPLY / RESTART runs vid_restart,
which creates buffers and applies latched settings. BACK does not undo edits.
Numeric values: click the value, edit, then press Enter. Right-click multi
choices to cycle backwards. Hover a row for its description.

First page contains existing pipeline prerequisites. Enable HDR and tone
mapping for linear lighting / HDR bloom / motion blur / rain lens. Enable
depth prepass for GTAO and contact shadows; sunlight for sun shadows.
POM needs normal mapping and material height data. LTC saber lights need
Forward+ and LTC. Wetness needs active map rain; puddles/runoff need wetness.
Foliage flutter/plant wind need auto foliage; persistent field needs interaction.
Some effects need updated engine/game modules, authored materials or map data.
Automatic LTC changes may need map reload (r_ltcReloadLights).

Use the matching Jedi Academy 2026 Rend2 binaries. This PK3 adds menus only.
Labels use English to work with stock fonts. Debug views and authoring commands
are intentionally omitted. Existing save/load/setup/datapad screens are retained.
This overrides ui/ingame.menu; mods replacing that menu may conflict.
Remove this PK3 to uninstall. Rendering cvars remain in your configuration.
