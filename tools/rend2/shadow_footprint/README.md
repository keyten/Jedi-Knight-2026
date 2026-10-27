# X04 shadow footprints (offline only)

See [the technical report](../../../docs/rend2-shadow-footprints-x04.md) for
equations, compiler source lines, exact reproduction commands, limits and results.
Python3.8+ standard library only; no GL, game assets or package installs required.

```powershell
python -X utf8 -m unittest discover -s tools/rend2/shadow_footprint -p 'test_*.py' -v
python -X utf8 tools/rend2/shadow_footprint/benchmark.py --output build/x04-new
```

The optional compiled test needs the **original hash-checked**
`build/bake-model-comparison-final/authoring/base/maps/multi_sun.bsp`. It explicitly
skips if absent; do not report the boundary reproduction as passed in that case.
The documented compiler installation/recipes remain in
[the existing bake-model tools](../bake_model/README.md).

* `geometry.py`: original-UV clipping/affine footprint pieces, duplicate/overlap
  checks, curved approximation envelope and tiny independent triangle reference.
* `integrate.py`: disjoint projected-shadow polygons plus bounded smooth cubature,
  finite-source quadrature, optical intervals; point/grid/stratified/Halton methods.
* `adaptive.py`: point-query adaptive approximation with explicit aliasing warning.
* `analytic.py`: independent exact solid-angle values, moving-boundary dual-number
  derivatives, finite rectangle visibility and separate smooth-response AD.
* `api.py`: coarse/reference requests, explicit unavailable/error results and
  retraced finite differences labelled as numerical estimates.
* `compiler_trace.py`, `compiler_boundary.py`: restricted locked opaque/sky
  trace-tree replay and the unchanged35-texel discrepancy export.
* `compile_variants.py`: seven newly baked sampling/bias/subdivision recipes;
  original reference tolerances and fixtures are unchanged.
* `localize.py`: one-height/one-intensity multistart experiment, including failures.
* `benchmark.py`, `report.py`: deterministic numerical tables, separate timings/RSS,
  source hashes, public self-authored observations and an SVG seam diagram.

`results/` contains own-content scalar/witness/observation artifacts, not licensed
stock BSPs or images. Full compiler outputs/logs are under ignored `build/x04/`.
No runtime renderer or production visibility/shadow API is installed or modified.
SI analytical fixtures do **not** calibrate Quake/JA bytes to lux/candela. Their
area integral must not substitute for an unknown compiler sampling profile.

Binary samples can be deterministic and have the **wrong source-motion gradient**.
Nested-rule differences are error estimates, not certified bounds. Unknown optics,
missing sky and approximate curved geometry remain uncertain. No universal
two-world-unit boundary fudge, recovered physical source or Raven compiler claim
is introduced.
