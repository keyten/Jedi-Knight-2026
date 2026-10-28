# S03: directional sun / diffuse sky implementation audit

S03 repeats the sun/sky objective previously implemented as S02. There is one
production estimator, `Core::Solar` in `lightrecon/sun_fit.h/.cpp`, shared by SP,
MP and standalone tools. This checkpoint reuses that implementation, expands
captured-input regressions and reruns the compiled-fixture workflow. It does
not introduce another solver, rename the producer or install runtime sunlight.

The [S02 method and API specification](rend2-sun-sky-reconstruction-s02.md)
contains the equations, default limits, provenance and ownership contracts.
The [S03 machine-readable audit](../tools/rend2/sun_fit/results/s03-audit.json)
records this checkpoint's source identities, tests and measurements. The original
S02 witnesses remain historical results, rather than being rewritten as new runs.

## Requirement mapping

| S03 requirement | Implementation and evidence | Limits |
|---|---|---|
| 1. Sun geometry, colour, intensity, provenance and uncertainty | `Proposal`, `Hypothesis`, canonical directional `PhysicalEmitter` + `SunSource`; conditional intensity slices, sampled angular alternatives, diagnostic evidence | Relative compiler amplitudes, not measured lux; angular extent unknown |
| 2. Authored declarations as priors | `AuthoredHints(EmitterEvidenceSet)`; real `q3map_sun`, `q3map_sunExt`, `q3gl2_sun`, `sun` extraction is tested | Seed/tie-break only; non-static/style-nonzero hints are not activated; shader presence does not prove bake applicability |
| 3. Direction conventions | Unit `toSource` is receiver-to-sun; canonical `Frame.forward = -toSource` is propagation | Same original right-handed, +Z-up world space as observations |
| 4. Directional evidence | Surface normals/RGB, actual sky rays, shadow changes, authored/grid/deluxe/shadow hint seeds | Grid/deluxe mixtures do not have a verified solar likelihood; they initialize search only. Shadow evidence is evaluated through rays, not an inferred exact edge correspondence |
| 5. Contamination | Separate sun/sky amplitudes; fixed pre-encoding local-light term; ambient/fill controls; independent model rejection | Local lamps and GI are not jointly discovered. Unexplained reflection or multiple suns may reject the one-sun model |
| 6. Search/refinement | Bounded Fibonacci sphere + axes/hints, followed by tangent-ring refinement; Huber IRLS bounded amplitude solve | Finite search, not proof of a unique global optimum |
| 7. Intensity/chromaticity | Independent nonnegative RGB amplitudes, canonical Y=1 chromaticity + relative Y intensity | No absolute calibration from raw BSP RGB |
| 8. Sky geometry | `SkyGuard` requires `SkyReach::Reached` and geometric reliability from the CPU visibility API | Empty-space miss, alpha uncertainty, unsupported blockers remain unknown |
| 9. Angular extent | Canonical angular-radius estimate remains unknown | Soft-sun/penumbra extent estimation is explicitly unsupported |
| 10. Multiple hypotheses | Separate joint sun/sky/profile alternative assets, sampled directional spread | Alternatives must not be summed; simultaneous multi-sun fitting is unsupported |
| 11. Insufficient evidence | `Unavailable`, `UnknownSun`, `UnresolvedEnvironment`; positive signal, normal diversity and independent validation gates | Small residual alone cannot promote a sun; empty assets are not a successful reconstruction |
| 12. Separate sky | Independent diffuse `sky17` component, `EnvironmentLighting`, no point emitter | A profile-specific angular quadrature, not arbitrary environment recovery |
| 13. Indoor local evidence | Independently supplied local contribution precedes encoding; indoor/occluded/reflection controls | No global sun override and no free GI residual |
| 14. Runtime separation | Only CPU core/tools call the estimator | No assignment to `tr.sunDirection`, `r_forceSun`, CSM, atmosphere or consumer enablement |
| 15. Diagnostics | Existing `lightrecon_bake_forward_tool --solar`; direction, alternatives, receiver support/exclusions, errors, work counters, canonical payload summary | Explicitly partial coverage is retained |

## Observation model and acceptance semantics

For the implemented profile branch, before output packing:

```
L_i = fixed_local_i + recipe_ambient_i
      + sun_rgb * max(0, normal_i . to_sun) * sky_visibility_i(to_sun)
      + sky_rgb * sky17_response_i
prediction_i = production_profile_encode(L_i)
```

Each ray terminates on actual declared sky or is blocked/unknown. The production
C++ forward evaluator supplies both directional and sky responses. Search
reevaluates directional visibility; it does not differentiate frozen binary rays.
The selected compiler recipe samples a luxel centre; this is not substituted for
a physical receiver-footprint integral. Receiver footprints also enforce partition
separation.

Raw RGB8, pre-encoding compiler-relative RGB, quantization intervals and packed RGB8
remain distinct. Saturated channels are censored; samples containing saturation
cannot determine the linear amplitude solve. Shared-peak encoding is applied after
the source sum. No renderer gamma/overbright, current PBR albedo or lux conversion
is applied to manufacture a match.

`Fit` sees training samples only and freezes parameters. `ValidateAndPublish`
uses spatially disjoint validation to accept/reject complete frozen explanations,
without refitting amplitudes. `Assess` scores untouched held-out samples after
selection. An external malicious native solver still requires restricted input
mounts; the diagnostic CLI is a trusted staged controller, not an OS sandbox.

Canonical results remain `Candidate`, with unknown emitter-existence probability
and `exactLight = Unsuitable`. Environment payloads explicitly exclude direct sun.
Unknown sky-shaped nuisance is unresolved transport with unavailable field data,
not verified bounce. Physical source recovery, unknown stock bake profiles and
general map reconstruction are not claimed by these conditional fits.

## Added S03 regression coverage

The existing dependency-free `lightrecon_sun_test.cpp` now also tests:

- Real captured shader evidence through `AuthoredHints`, including original text,
  stable IDs and receiver-to-source direction. Evidence is identified by provenance,
  not declaration order: the capture publishes a stable-ID ordering.
- Sky/skylight declarations without any authored sun; no fallback is created.
- Actual lightgrid direction decoding, quantization bounds and rejection of
  animated styles, invalid record indices and unavailable directions.
- Actual deluxe RGB8 decoding and correspondence failures; these are world-space
  incoming-direction hints, not normal-map samples.
- Two deliberately contaminated grid/deluxe hints: independent surfaces still
  determine the sun; no average hint direction is promoted to a physical source.
- A nonzero authored lightstyle produces the same searched directions/parameters
  as no active prior. Its provenance is retained, without activating that style.
- Strong authored priors with no positive training RGB remain Unavailable.
- Positive training with black validation produces no canonical sun asset.

Existing tests retain the requested sun/shadow, local-lamp, diffuse-only, interior
skylight, reflected-light, conflicting multi-directional, indoor, sparse,
saturated and matching/conflicting authored-prior scenes. Cancellation, invalid
input, unknown geometry/profile and train/validation leakage also remain covered.

## Revalidated numerical results

Two sweeps of the same eight frozen compiled BSPs have identical semantic output,
including identities, parameters, exclusions and work counters. They also match
the S02 compiler witness, SHA256
`80af43b11cb59ec0378ae609fe5d01ce1a4eec862e65fa606388b7abe8915f36`.
These are newly executed inference runs on previously compiled/hash-checked inputs,
not eight new compiler invocations.

| Compiled fixture | Outcome | Sun angular error | Held-out interval RGB RMSE | Supported / requested held-out |
|---|---|---:|---:|---|
| sun_oriented | SunHypotheses | 0.40453° | 0.10610 | 72/72 |
| sun_shadow | SunHypotheses, explicitly partial | 0.40453° | 0.09346 | 69/72 |
| sun_with_lamp | SunHypotheses with known fixed lamp | 0.40453° | 0.15966 | 69/69 of supported subset; 3 nominal samples excluded |
| sky_aperture_positive_v2 | SkyOnly; no sun | Not applicable | 0.01890 | 72/72 |
| sky_aperture v1 | UnknownSun | Unavailable | Not admitted | positive validation confined to one block |
| ambient_only | Unavailable; known ambient explains RGB | Unavailable | Not admitted | no additional sun evidence |
| no_sun | Unavailable | Unavailable | Not admitted | no positive evidence |
| multi_directional | UnknownSun | Unavailable | Not admitted | one-sun model cannot explain two coloured suns |

Sun RGB relative vector errors remain 0.705%, 0.662% and 0.555%; sky v2 is
0.472%. These are errors against hidden **compiler-relative** answers, not lux.
The shadow full-coverage gate remains failed; partial RGB accuracy is not complete
shadow parity. The lamp fixture also excludes two nominal training samples. V1
skylight is retained as a failed positive test rather than silently replaced by V2.

The expanded CPU executable performs 17,631 checks with zero failures on GCC and
MSVC, including 13 named numerical witnesses. Individual checks include ray/sample
assertions; they are not 17,631 independent scenes. Its contaminated grid/deluxe
case retains the data-supported direction (0.22238° error), while black validation
prevents publication. The initial new adapter assertions incorrectly assumed
shader-declaration ordering; they were corrected to locate the original declaration
by provenance, preserving the existing stable-ID ordering and production code.

Under shared workstation load, eight-map sweeps took 19.84 s and 11.21 s. The first
overlapped the renderer rebuild. Sun-oriented fit/validation took 1.465 s and
0.258 s respectively; positive skylight took 0.443 s and 0.386 s. Actual peak C++
process working sets and controller peaks are recorded separately in the audit.
Timing/RSS are excluded from deterministic semantic hashes. No full-stock-map
performance or renderer image equivalence is inferred from these small controls.

Final validation: 19/19 standalone CPU targets passed (73.33 s total), 9/9 Python
acceptance tests passed, and both `rdsp-rend2_x86_64` and `rd-rend2_x86_64` Release
builds passed. The production-source hashes in the previous witness are unchanged;
only the expanded CPU test differs within that witness's source set. S03 changes
the test, adds this audit/report JSON, and links it from the existing tool README.
The shared module/build wiring from S02 remains the single implementation. Existing
Git-tag/CPack and MSVC `/arch:SSE2` notices are unrelated build warnings.

The maximum measured C++ process working set was 10,616,832 bytes (10.125 MiB).
Controller memory is recorded separately; neither is a full-map solver RAM claim.

## Reproduction

Use CMake/compiler on PATH and Python 3 (standard library only). From repository root:

```powershell
cmake --build build/lightrecon-core-renderers --config Release --target lightrecon_sun_test lightrecon_bake_forward_tool rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 6
cmake --build build/lightrecon-disk-standalone --parallel 6
ctest --test-dir build/lightrecon-disk-standalone --output-on-failure
build/lightrecon-core-renderers/Release/lightrecon_sun_test.exe build/s03-cpu-msvc.json
build/lightrecon-disk-standalone/lightrecon_sun_test.exe build/s03-cpu-gcc.json
python -X utf8 -m unittest discover -s tools/rend2/sun_fit -p 'test_*.py' -v
python -X utf8 tools/rend2/sun_fit/run.py --tool build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe --reuse build/s02-compiled-verified --output build/s03-solar-a
python -X utf8 tools/rend2/sun_fit/run.py --tool build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe --reuse build/s02-compiled-verified --output build/s03-solar-b
python -X utf8 tools/rend2/sun_fit/report.py --run build/s03-solar-a --repeat build/s03-solar-b --cpu build/s03-cpu-msvc.json --output build/s03-report
```

For a clean machine, first follow the [tool README](../tools/rend2/sun_fit/README.md)
to configure the build and generate the eight self-authored fixtures with the
pinned q3map2. `--reuse` validates existing BSP/recipe/compiler hashes; it does
not invoke the compiler again or claim a fresh bake. Use fresh output directory
names for subsequent sweeps. No stock assets are distributed.

The compiler is NetRadiant `2.5.17n-git-d9338309e`, executable SHA256
`894049bd136e7828b0f068d12c932833153b3ada87b99655dac90e4a60c85ad4`.
Gamma 1, compensate 1, exposure 0, bounce 0, super 1, no collapse, JA, one thread
are part of the recorded recipes. The source compiler remains unidentified for
stock JA. A successful controlled conditional prediction cannot identify Raven.

## Scope of completion

S03's sun/sky API, search, explicit absence/alternatives, canonical output and
requested controlled-scene tests use the existing implementation. This checkpoint
adds input-adapter regressions and traceable revalidation rather than a second
sun estimator. General HDR/transfer branches, dynamic styles, soft-sun extent,
joint multiple-sun fitting and GI recovery remain explicit unsupported numerical
combinations. Those limitations must be carried into any future runtime adapter.

No game rendering or map-load capture code is changed for S03. No gameplay visual
validation is claimed; the task produces CPU hypotheses only.
