Jedi Academy 2026 live rendering overlay (SP)

Install the supplied openjk_sp.x86_64.exe next to the existing game executable.
Put zzzz_render2026_menu.pk3 in GameData/base (or your active mod). Restart.
Open with Esc -> RENDER 2026, or console: uimenu render2026_0

The game remains visible and running; mouse/keyboard control the overlay.
CLOSE or Escape returns to gameplay. Live controls update immediately.
* means a renderer restart is required. APPLY / RESTART is enabled only
while a restart setting differs from its active value; it runs vid_restart.
Latched controls show the requested value before restart.

Drag numeric sliders; left/right arrows make fine adjustments. Integer
controls stay integer. Every feature has an estimated GPU cost on hover;
each page shows the highest cost among its features. Costs apply when the
effect is active, not to moving a slider. Map/material requirements still apply.

LUTs: put .cube files in base/luts, open Choose color palette, click a row.
RESCAN LUT FOLDER refreshes the list without restarting the game. Automatic
uses the map LUT; Neutral selects identity. Files inside loaded PK3s also work.
Vector settings have named presets instead of free-form text.

This version needs the supplied updated SP engine for LUT browsing, pending
value display, accurate restart tracking and integer slider rounding.
Use matching 2026 Rend2 renderer and game modules for the rendering features.
This overrides ui/ingame.menu; other mods overriding it may conflict.
Remove the PK3 to remove the overlay. Existing rendering choices are retained.
