# Shadow localization and identifiability demonstrations

The [research document](../../../docs/rend2-light-localization-research.md)
derives geometry/shadow constraints, failure cases, uncertainty and proposed
JA tests. This directory contains **analytical research fixtures**, not a
production light reconstruction solver or a bake emulator.

Python 3.8+ standard library only, from repository root:

```powershell
python -m unittest discover -s tools/rend2/light_localization_research -p 'test_*.py' -v
python tools/rend2/light_localization_research/shadow_localization.py --output build/light-localization-research/results.json
python tools/rend2/light_localization_research/shadow_localization.py --output build/light-localization-research/results-repeat.json
Get-FileHash build/light-localization-research/results*.json -Algorithm SHA256
```

The report contains no timestamps, host paths or randomness. Outputs were
byte-identical on the recorded Python interpreter. Numerical last bits may
differ across platforms; geometric assertions use tolerances rather than a
universal report hash.

## Explicit assumptions

- Metres, known horizontal diffuse receiver, inverse-square positive point
  emission and a specified linear fixture scale. Candela/lux apply **only to
  these independently authored analytical examples**.
- RGB8 uses nearest/half-up rounding, not q3map's coupled `ColorToBytes`.
- Known, exact opaque geometry; one source is isolated in the height example.
- Caster–shadow and mesh-edge associations are **provided**, not discovered.
- The height example knows the source's vertical axis; it demonstrates reduced
  depth ambiguity, not unconstrained 3D source recovery.
- The tiny linear solvers return unavailable (`None`) for numerical singularity.
  An algebraic intersection still needs ray ordering/silhouette/solid checks
  before it could be treated as a physical source in a future implementation.
- The wall intersection helper is an independent fixture reference; its
  coplanar convention is not an API policy for real scene geometry.

No map I/O, shader integration, general candidate search, automatic edge matching,
GI, uncertainty calibration, canonical asset publication or production optimizer
is included. The answer parameters are used to generate synthetic measurements
and evaluate the demonstration, not presented as observations from a JA map.

## Results recorded on 2026-10-26

**33/33 new tests passed.**

| Demonstration | Measured result / required interpretation |
|---|---|
| Three close floor observations, unknown height/intensity | All **301** heights from 2 to 8 m at .02 m spacing give grey code 128 at each sample, with `I=(128/255)*H^2`. This is practical quantized ambiguity. |
| Same scene plus known shadow boundary ±.01 m | **9** sampled candidates remain; continuous admissible height `[3.9126213592, 4.0927835052]` m. Finite-width interval remains, rather than an exact recovered height. |
| Distant-limit boundary | `[67.6666667, unbounded)` m; no fabricated finite upper error bar. |
| Independent wall traces at held-out floor x=1.2,1.4,1.6 | Height 2: all blocked; height 4: blocked/clear/clear; height 8: all clear. |
| Two exact known caster lines | Position error `2.6950077933603723e-15` m. One line / parallel lines rejected as underdetermined. |
| Three exact independent edge planes | Position error `1.9891280697202825e-15` m. Repeated identical edge rejected as underdetermined. Supplied correspondence arithmetic, not real-map accuracy. |
| Two symmetric fixed source positions | Identical intensity columns without wall. Finite wall changes one visibility pattern and recovers supplied 2 and 5 cd amplitudes. No position discovery claimed. |
| Penumbra | One width admits multiple height/extent pairs; widths .2/.6 m from caster heights 1/2 m recover ideal H=4,D=.6 m. No bake filter or radiance curve inverted. |
| Bias / float32 | .03 m receiver-plane shift moves the test edge by .01 m. A .001 world-unit displacement at coordinate 100000 is lost in float32. |
| Visibility-boundary derivative | Exact footprint integral has derivative `1.1111111111 / m`; central differences agree, while five fixed point samples give zero at the same step. This isolates visibility, not full irradiance. |

Recorded report SHA256:
`e40b2f889734654d65916b2fcc8b72a726feead4da8696092f554282f2a51dfc`.
No source is certified as reconstructed by this report.

The following existing numerical suites were also freshly executed:

```powershell
python -m unittest discover -s tools/rend2/inverse_lighting_research -p 'test_*.py' -v
python tools/rend2/bake_model/test_kernels.py -v
```

**25/25** previous research tests and **7/7** bake-model kernel tests passed,
for **65 numerical tests total**. The compiler harness's existing 22-BSP witness
was inspected, not regenerated. No new compiled-map, stock-map localization,
statistical uncertainty coverage, SP/MP build, GPU or gameplay validation was
performed. Renderer and core implementation files remain unchanged.

Local Python executable:
`C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`.

## References and input provenance

`sources.json` records author PDFs, SHA256s, read sections, versioned framework
references and LF-normalized hashes of the repository inputs. Downloaded papers,
extracted text, selected page renders and reports stay under ignored
`build/light-localization-research/`; copyrighted papers and JA assets are not
redistributed.

The supplied bake-model documents/tools were present as workspace changes at
baseline `ff02fa5d4`; their hashes pin the inspected content rather than claiming
they were already in that commit. This task does not edit or stage that work.
