# Lens water harness

Headless checks for the lens water simulation core, `shared/rd-rend2/tr_lenswater.cpp` (`r_rainLens`). It runs
without a renderer, GL, or the game.

```
tools\lenswater_harness\build.bat
```

The script compiles `harness.cpp` together with `tr_lenswater.cpp` using MSVC `/O2`, runs it, and returns non-zero
on failure.

It covers these scenarios:
- A single bead stays pinned and a large drop slides.
- Two drops merge: r ≈ 1.26 r, mass is conserved, and the merged drop depins.
- Trails leave film and wetness, and residual beads appear.
- The camera roll turns the flow.
- A near-vertical view keeps drops pinned.
- The roof test: under cover no rain spawns, drops keep moving and the film decays.
- The heavy profile produces flows and sheets, but no more large static drops.
- Caps hold under emerge and splash spam.
- The lens dries completely.
- The spray emitter wets its own side and stops once out of range.
- World event falloff, facing and lens side.
- Camera inertia applies only when enabled.
- The acid tint and refraction.
- It also prints the update cost at the caps.

Game visuals and GPU cost are checked in game (`docs/rend2-rain-lens.md`, Validation).
