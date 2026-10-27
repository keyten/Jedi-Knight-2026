# Passive inverse-GI research controls

Decision, equations, limitations, measured tables and future interfaces:
[research document](../../../docs/rend2-inverse-gi-research.md).
Python standard library only, no GL, game, neural network or production solver.
Analytical scenes use metres/cd/lux; q3map2 experiments use a **different,
compiler-relative observation operator**, never measured SI intensities.

From the repository root (PowerShell, `python` 3.10+):

```powershell
python -X utf8 tools/rend2/inverse_gi/experiment.py
python -X utf8 -m unittest discover -s tools/rend2/inverse_gi -p 'test_*.py' -v
python -X utf8 tools/rend2/inverse_gi/measure.py
```

`experiment.py` regenerates `results/analytic.json`; it excludes machine timings
and includes its code hashes. Repeated generation is checked by a unit test.
`measure.py` writes ignored `build/inverse-gi-research/performance.json`, with
median wall times, traced Python allocation peak and separate process RSS peak.
Timing values are intentionally not deterministic test assertions.

## Real compiler controls

Reuse the pinned executable from the [existing compiler workflow](../../../docs/rend2-q3map2-validation.md).
SHA-256 must be `894049bd136e7828b0f068d12c932833153b3ada87b99655dac90e4a60c85ad4`.

```powershell
$q3 = 'build/toolchain/netradiant-20220628/portable/netradiant_1.5.0-20220628-windows-amd64/q3map2.exe'
python -X utf8 tools/rend2/inverse_gi/compiler_controls.py --q3map2 $q3 `
  --forward-root build/bake-model-comparison-final `
  --dataset-root build/q3map2-bake-verified `
  --work build/inverse-gi-controls
```

This command was executed. It verifies the frozen 22-case BSP hashes, recomputes
their existing restricted kernel comparisons, verifies the original D09 colour
pair, and executes **14 real bakes / 42 stages**: zero/one bounce on point room,
red wall, doorway, sealed wall, skylight and changed-white-texture controls;
one bouncegrid variant; a same-settings doorway repeat. Sources come from the
existing generator. `-super 1 -nocollapse` is explicitly added to both sides of
new pairs to keep dark charts comparable. Original frozen inputs are unchanged.

`results/compiler.json` is the numerical witness. Exact executed argv, stage
logs, map/material/BSP hashes and local timings are also under
`build/inverse-gi-controls/`. An error/missing compiler is an error, not a silently
substituted analytic test. One subprocess has a 120-second bound. No stock content
or generated BSP belongs in Git.

On a fresh checkout, first reproduce the **existing** inputs:

```powershell
python -X utf8 tools/rend2/bake_model/compare_bakes.py --q3map2 $q3 --output build/bake-model-comparison-final
cmake -S tools/lightrecon_test -B build/lightrecon-offline
cmake --build build/lightrecon-offline --config Release --parallel 2
python -X utf8 tools/rend2/datasets/run_pipeline.py `
  --tool build/lightrecon-offline/Release/lightrecon_dataset_tool.exe `
  --output build/q3map2-bake-verified --q3map2 $q3
```

Use a fresh `run_pipeline` output folder. Single-configuration generators omit
`Release/`; Unix omits `.exe` and needs the matching supported compiler workflow.
Frozen raw hashes may differ after re-baking ancillary BSP fields: see the
existing D09 reproducibility report. Do **not** edit a BSP/hash to bypass a
rejection. To explicitly use the independently regenerated forward witness,
add `--forward-witness build/bake-model-comparison-final/result.json` to the
compiler-controls command. It must report the same pinned compiler/source and
passing 22-case checks; its content hash and the new raw input hashes are retained.
This is a declared new experiment, not replacement of the checked old witness.
A whole-BSP reproducibility
claim is stronger than stable lightmap/geometry statistics; near-solid grid
records can vary even in same-settings repeats.

The following numerical regressions also passed:

```powershell
python -X utf8 -m unittest discover -s tools/rend2/bake_model -p 'test_*.py' -v
python -X utf8 -m unittest discover -s tools/rend2/light_localization_research -p 'test_*.py' -v
python -X utf8 -m unittest discover -s tools/rend2/shadow_footprint -p 'test_*.py' -v
ctest --test-dir build/lightrecon-core-renderers -C Release --output-on-failure
cmake --build build/lightrecon-core-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 2
```

The existing configured MSVC build is used above. For first-time configuration
follow the repository's SP/MP build instructions. On this machine `cmake/ctest`
were available after prepending the adjacent
`C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin` to `PATH`.
Python executable used:
`C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`
(3.12.14).

## Files and interpretation

| File | Responsibility |
|---|---|
| `transport.py` | Tiny bounded passive power-transfer algebra, nonnegative amplitude and deliberately unsafe residual competitors (active-set convex QP, KKT checked). |
| `experiment.py` | Profile/material ambiguity, source collapse, optical paths, finite-area reference, sparse censored data and uncertainty controls. Reuses X04 independent triangle/area routines. |
| `compiler_controls.py` | Existing source-locked BSP tests and new paired compiler measurements; reuses X03 encoding/interval loss. |
| `test_inverse_gi.py` | 15 deterministic guards, including train-only fitting and compiler witness semantics. A witness test is not itself a new compiler invocation. |
| `measure.py` | Optional machine-dependent local performance measurements. |
| `sources.json` | Paper versions, PDF hashes/read sections and compiler source-lock references. |

The analytical `linear_alternative` is an explicitly simplified confounding
profile. The compiler script uses actual NR inverse-square/cosine and
linear/no-angle kernels. `UnknownProfile` retains a declared conditional family,
not a stock-map default or posterior probability.

The toy passive network has black receivers, so only its reflector produces the
first bounce. A separate two-patch network tests later bounce contraction. Its
midpoint form factors are reciprocal and passive, but not an exact room solution.
Finite-area tests independently demonstrate when this approximation fails.
`physical_gi` in the diagnostic model table means a **conditional physical
transport model in this controlled scene**, not verified JA GI.

Source recall at 1.5 cd for a known 3-cd light is an explicit toy metric. Known
source geometry/ground truth belongs to the evaluator in these controls. This
tool does not build a blind production solver or publish sources, a map
reconstruction or a runtime field.

Material/source/profile uncertainty, optical unknowns, absolute-scale ambiguity
and signed model discrepancy remain separate. A positive paired byte difference
is measured compiler behaviour, not an exact physical GI quantity. Constant fill
is unresolved even when held-out prediction is accurate. No q3map transport
replay, historical Raven identity, stock GI recovery or renderer integration is
claimed.

Newly authored analytical fixtures/code are GPL-2.0-or-later under the repository
license; original D09 fixture licensing is unchanged. Third-party papers are
referenced, not redistributed.
