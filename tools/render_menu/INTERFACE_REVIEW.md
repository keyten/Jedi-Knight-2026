# Rendering overlay: interface review

The menu is now a panel on the right with a translucent background. The game
remains visible and continues running. Mouse and keyboard input controls the
panel; Escape and Close return control to the player.

- 158 numeric controls use bounded sliders that display their current values.
  Arrow keys provide fine adjustments; integer parameters are rounded.
- 107 switches and modes use named options. For example, Bloom offers
  Off / Legacy / Modern; quality offers Low / Medium / High / Ultra.
- Labels describe the visual effect instead of showing cvar names. Every
  category is accessible through Contents; settings requiring a restart have `*`.
- Performance: zero / light / medium / heavy estimates the cost while an effect
  is active. Each page shows the highest cost among its effects; hovering over a
  field shows its own estimate and range. These are qualitative estimates, not
  measured FPS results.
- Dependency explanations appear at the bottom of each page. Shader vectors
  for color, wind and slope use named presets.
- Apply / Restart becomes active when a setting requiring a restart changes.
  Reverting to the original values disables the button. Controls display pending
  values, and the engine respects CVAR_LATCH.
- Choose color palette opens a scrollable list of `luts/*.cube` files from the
  game filesystem, including loaded PK3s. Automatic, Neutral and Rescan LUT Folder
  are available. Selection takes effect immediately when color grading is enabled.

Validation included an SP engine build, seven generator tests and a C++ test of
the production overlay state functions: live changes, pending values, reverting
settings, rounding, establishing a new baseline after a restart, and listing and
selecting LUTs. The game loaded the PK3 without parser errors. Automated
screenshots were black, so the visual layout has not been verified. No further
game launches occurred after the request to minimize launches.

This version requires the updated SP engine supplied in the ZIP: dynamic LUT
listing and accurate restart tracking need support in the UI code.
