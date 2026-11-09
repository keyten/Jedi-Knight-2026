# ASR07: dominant sun and sky before local source discovery

ASR07 reuses the existing offline `Core::Solar` estimator, adds a conservative
discovery controller and a native original-archive audit, and validates the
distinction between a directional field and identification of a physical sun.
The intended output is conditional aesthetic source hypotheses for later local
search. No renderer, world shading, runtime consumer, canonical asset schema,
cache/storage contract or historical solver is changed.

Implementation: [controller and reproduction](../tools/rend2/solar_discovery/README.md),
[decision policy](../tools/rend2/solar_discovery/policy.py),
[native archive audit](../tools/lightrecon_test/lightrecon_solar_audit.cpp),
[machine-readable witness](../tools/rend2/solar_discovery/results.json).

## Audit and reused model

The [S02 method](rend2-sun-sky-reconstruction-s02.md) and
[S03 audit](rend2-sun-sky-reconstruction-s03.md) already provide bounded directional
search, independent RGB amplitudes, a separate diffuse `sky17` response, robust
fitting, positive-signal and normal-diversity gates, actual sky tracing, frozen
proposals, independent validation and final-test scoring. Replacing that solver
would add another incompatible interpretation of the same evidence.

The model sums, before the selected profile's encoding:

```
fixed local contribution + declared recipe ambient
+ sun_rgb * max(0, normal . to_sun) * actual_sky_visibility(to_sun)
+ sky_rgb * profile_specific_diffuse_sky_response
```

Search compares sun+sky, sky-only and uniform-fill controls. It uses original
lightmap bytes and normals, searches visibility again at each candidate direction,
and never reads validation RGB before freezing fitted parameters. Final-test RGB
is used only for reporting. Receiver-to-source vectors are reported directly;
canonical propagation frames use their negative. Canonical native candidates keep
unknown existence probability, relative photometry and unsuitable exact-source
status. ASR07 does not publish those candidates to storage or the renderer.

Authored sun declarations are provenance/seed evidence, not a renderer fallback.
The existing `AuthoredHints` understands captured `q3map_sun`, `q3map_sunExt`,
`q3gl2_sun` and bare `sun`. Existing native tests cover contradictory declarations,
styles, real lightgrid decoding and contaminated grid/deluxe mixtures. The new
controlled runs withhold all authored directions and intensities from search.
Their discovery decision checks only whether the **used sky material** contains a
sun declaration. Unused helper shaders have a regression test: their declarations
must not support the scene's sun.

Grid and deluxe directions are aggregate incoming mixtures. Grid coherence is
measured without treating encoded directed RGB as linear weights. The stock audit
also tests actual sky reach along original grid directions. None of these
statistics is a calibrated solar likelihood. Large local lamps, diffuse sky and
reflections can produce directional agreement.

Shadow evidence enters the reused solver through actual visibility and the
sun-shadow compiled control. There is no new automatic caster/edge association,
penumbra inversion or inferred soft-sun angular radius. Unknown intersections,
optics, patch approximation, invalid placement and empty-space misses do not count
as clear sky. The stock audit uses the native conservative visibility API and
source domain; it never feeds stock geometry through the controlled opaque/sky
fixture parser, which does not implement general patches or optical materials.

## Historical encodings that prevent unconditional inference

The [bake-model specification](rend2-bake-model.md) remains authoritative:

- Stock Raven compiler/recipe identification is unknown. A successful modern
  NetRadiant control does not identify a stock compiler or establish its formula.
- Gamma, shared-peak normalization, compensation, exposure and optional sRGB act
  after source summation. RGB8 is encoded evidence with quantization/censoring;
  it is neither calibrated irradiance nor a domain in which lamp subtraction is
  valid. Fixed local terms in this study precede encoding.
- Grid ambient/directed splitting, dominant-mixture reduction, angular packing
  and lossy record merging differ from surface irradiance. The canonical decoder's
  `/256` direction convention and quantization bound are preserved; a historical
  writer's `/255` formula is not silently substituted for it.
- Modern `sky17` is an explicit compiler-relative angular quadrature, not a
  universal historical sky model or recovered environment map. Earlier skylight
  implementations need separate assumptions.
- Compiler receiver nudges, sky-required traces, fast cutoffs, patch/material
  handling and bake options affect visibility. A geometric or photometric model
  failure is insufficient evidence for zero sunlight.

## Conditional alternatives and local-light ambiguity

The controller exposes `SunPresent`, `SunAbsent` and `SunUncertain` without changing
the native/canonical enum contracts. `SunPresent` requires an independently
validated native directional improvement, actual sun-visible validation receivers
in at least three 128-WU blocks, a baseline of at least 256 WU, and independent
used-sky sun provenance. The measured positive baselines are 768 WU. Strength is
relative compiler-domain luminance; chromaticity is normalized to Y=1. Neither is
SI photometry.

`SunAbsent` means a supported positive diffuse-sky explanation needs no direct sun.
It does not bound actual sunlight to zero: a weak, occluded or unresolved sun can
remain. Dark interiors, failed models, unknown profiles, missing sky evidence and
unidentified directional fields become `SunUncertain`. Their sunlight strength
and physical upper bound remain unavailable.

Even `SunPresent` records `finite_local_alternative=NotExcluded`. Authored shader
evidence supports a plausible sun hypothesis under the declared model; it does not
prove that a historical compiler used it or that a finite alternative is impossible.
Without that independent evidence a distant lamp-compatible field stays uncertain,
regardless of excellent RGB error or number of texels. The six policy regressions
include a finite-distance, multi-normal, 600-WU-baseline counterexample producing
identical toy-quantizer bytes. That analytical counterexample is separate from the
historical JA observation operator and the actual compiled distant-lamp test.

The mixed-light pair exposes another limit: the existing solar model does not
jointly discover unknown local lamps or GI. An unmodelled strong lamp causes
abstention. A supplied fixed local contribution recovers the sun. That successful
ablation is explicitly conditional on oracle local support, not evidence of
automatic nuisance removal. Subsequent local search must retain uncertainty and
must not subtract the fitted sun directly from RGB8.

## Compiled validation against frozen truth

Eight self-authored scenes were freshly compiled with the pinned NetRadiant binary,
then reused for deterministic paired inference. Compiler SHA256:
`894049bd136e7828b0f068d12c932833153b3ada87b99655dac90e4a60c85ad4`.
The recipes fix JA, one thread, gamma/compensation 1, exposure/bounce 0, super 1,
deluxe and no collapse. Map, shader, BSP, recipe, request, fold, executable and
inspected-source hashes are recorded. Truth directions and amplitudes are supplied
only to the trusted evaluator after inference inputs are constructed.

| Scene/model | Discovery decision | Angular error | Relative sun RGB error | Final-test interval RGB RMSE |
|---|---|---:|---:|---:|
| Outdoor oriented sun | SunPresent, conditional | 0.40453 degrees | 0.7050% | 0.10610 |
| Sun with wall shadow | SunPresent, conditional/partial | 0.40453 degrees | 0.6619% | 0.09346 |
| Sun + unmodelled warm lamp | SunUncertain | Unavailable | Unavailable | Not admitted |
| Same BSP + fixed local operator | SunPresent, conditional | 0.40453 degrees | 0.5546% | 0.15966 |
| Diffuse sky aperture | SunAbsent, conditional sky-only | Not applicable | Not applicable | 0.01890 |
| Dark sealed indoor | SunUncertain | Unavailable | Unavailable | Not admitted |
| Outdoor local lamp only | SunUncertain | No physical sun | Not applicable | Not admitted |
| Indoor local lamp only | SunUncertain | No physical sun | Not applicable | Not admitted |
| Distant finite lamp only | SunUncertain despite native directional fit | No physical sun | Not applicable | 0.21845 for directional alternative |

The distant source is a genuine compiled finite point at `(0,0,30000)` inside a
tall enclosing sky volume, with a frozen authored scalar of 14,400,000. It yields
a native `SunHypotheses` result across a 488-WU sun-visible validation baseline.
That field remains a lamp-compatible direction candidate; it is not labelled a
sun. This case is a measured identifiability failure of fit-only classification,
not successful localization of that lamp. The separate normal outdoor lamp is
strongly spatially varying and fails the native directional explanation.

The shadow final-test result supports 69/72 requested receivers; three unknown sky
samples remain unknown. The known-lamp operator also excludes two training and
three final-test nominal placements before fitting; final-test error then covers
69/69 of that explicit supported subset. Partial coverage is not complete shadow
parity. All other admitted final-test cases cover 72/72 receivers. Positive signal
and spatial blocks are checked, so black or missing output cannot pass by low RMSE.
The sky-only RGB amplitude error against frozen compiler-relative truth is 0.4719%.

## Real outdoor stock ROI

The licensed original Hoth2 capture matches BSP SHA256
`3fc5d0a469c7246d09cb9d0be4500fc0f9a5923ca9a23fdee51aba9eceb3f71d`.
A deterministic geometry-only scan finds a genuine zenith sky hit at grid origin
`(7104,-5856,432)`, near training geometry, after 30 eligible queries. The 512-WU
ROI includes 64 selected original training receivers. The captured scene has 32
sky surfaces and one eligible authored direction, approximately
`(0.08419,-0.02256,0.99619)` toward the source.

All 64 surface samples remain blocked or unknown on the tested sky directions;
for the authored direction the split is 13 blocked / 51 unknown. They are not
silently nudged to reproduce an unknown production recipe. The 76 static,
nonblack-directed original grid cells have unweighted coherence 0.58746, with mean
direction approximately `(-0.21346,0.03983,0.97614)`. No tested original aggregate
grid direction has certified sky reach; four remain unknown. This mixture is not
promoted to direct sun.

An explicit refusal probe calls `Solar::Fit` with the stock geometry,
`UnknownProfile`, unattested encoding and uncertified receiver placement. No target
RGB is supplied to this nonnumeric probe. It produces zero frozen proposals. The recorded
decision is `SunUncertain`; strength and sunlight upper bound are null. Authored
direction provenance remains useful to future conditional investigations, while
actual stock sun/sky strengths and true errors remain unidentifiable here. This
is a completed real outdoor evidence/abstention test, not a claim of stock lighting
recovery. No licensed map, shader text, lightmap or texture is redistributed.

## Verification and limits

The witness requires two identical full semantic inference results, including
parameters, exclusions, decisions and work counters; elapsed time is excluded.
Five targeted native suites pass: solar, visibility, shader evidence, bake profile
and bake forward. The solar executable reports 17,631 assertions with zero failures.
Six new policy tests and nine existing solar-controller tests pass. Reproduction
commands and pinned dependencies are in the tool README.

This remains a bounded single-sun/profile-specific diffuse-sky study. General sky
recovery, joint local-light discovery, general bounce, soft-source extent and
absolute calibration are not implemented. The controller's conservative provenance
gate trades recall for avoiding unsupported physical-sun identification; it is not
a statistical posterior. No game launch, renderer image comparison, GPU work or
runtime lighting change is part of this validation.
