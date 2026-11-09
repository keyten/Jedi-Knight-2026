# ASR08: conditional local-light evidence after sun and sky

ASR08 adds a tool-only, bounded C++ diagnostic API:
[`LocalLightEvidence::Evaluate`](../tools/lightrecon_test/local_light_evidence.h).
It exposes the remaining evidence for **possible local direct light** as an
encoded-space conditional cost function. It does not produce a subtraction image
or claim to recover local irradiance from RGB8.

The implementation reuses `Bake::CpuForwardEvaluator`, `Bake::DescribeRecipe` and
`Bake::Encode`. It accepts frozen `Solar::Proposal` values from the existing solar
path or explicit controlled configurations. No historical solver, canonical asset,
storage format, original baked input, renderer or runtime consumer changes.

## Inspected evidence and comparison domain

The [bake-model specification](rend2-bake-model.md),
[`bake_forward.cpp`](../shared/rd-rend2/lightrecon/bake_forward.cpp),
[`decode.h`](../shared/rd-rend2/lightrecon/decode.h),
[`lightgrid.cpp`](../shared/rd-rend2/lightrecon/lightgrid.cpp) and
[`sun_fit.cpp`](../shared/rd-rend2/lightrecon/sun_fit.cpp) establish the constraints:

- Original surface RGB8 has compiler/recipe uncertainty. A decoded RGB8 value or
  a renderer-linear value does not establish calibrated irradiance or preserve
  the magnitude lost in historical normalization/clipping.
- The implemented NetRadiant branch applies gamma, shared-peak normalization or
  exposure, compensation, optional sRGB and byte conversion **after** source
  summation. A change in one channel can change the encoding of every channel.
  There is no channelwise inverse suitable for unconditional source subtraction.
- The raw top code is censored. Shared-peak normalization also leaves a family of
  possible brighter inputs. Quantization/saturation masks must survive comparison.
- Lightgrid ambient/directed RGB results from mixture reduction, encoding and
  potentially lossy record merging. Packed lat/long describes an aggregate incoming
  direction, not a vector-valued irradiance term. The canonical `/256` decoder
  differs from the inspected writer's `/255` packing rule. ASR08 refuses grid,
  deluxe and direction-only records; it never subtracts packed angles or claims
  to residualize the grid using the surface operator.
- The [ASR07 solar path](rend2-dominant-sun-sky-asr07.md) already retains
  conditional sun/sky alternatives. Unknown stock compiler/profile applicability
  remains unknown; modern controlled recipes do not identify historical bakes.

The chosen supported domain is **profile-specific prediction in encoded space**.
For receiver `i`, global alternative `h`, and nonnegative finite-source trial `t`:

```
G_h(i) = declared recipe ambient + forward sun_h(i) + forward sky_h(i)
L_ht(i) = forward local sources_t(i), using profile_h
P_ht(i) = Encode_profile_h(G_h(i) + L_ht(i))
C_ht(i) = mean_RGB distance(P_ht(i), observed_code_intervals_h(i))^2
```

Only compatible forward predictions are added before encoding. The local
evaluator's `direct` term is used, so recipe ambient enters exactly once. Source
cutoffs, receiver pushes, falloff, normals and visibility use the existing forward
operator. Targets are never passed to that operator. No RGB8 target minus RGB8
sun calculation occurs. A `zero-local` trial is always reported.

This function is the residual evidence: a downstream offline search may propose
spatially coherent local sources and query their compatibility with the remaining
observations. It is deliberately not a recovered RGB energy field. It performs
no local parameter search and selects no global alternative.

## API, units and states

The library is built only in `tools/lightrecon_test`; it is not added to the shared
core/renderer target. See [reproduction](../tools/rend2/local_light_evidence/README.md)
and the [controlled test adapter](../tools/lightrecon_test/lightrecon_local_evidence_test.cpp).

`Request` supplies immutable leased original observations, global alternatives,
finite local source trials, native static visibility, budgets and a read context.
Each global alternative requires an ID, provenance, an explicit profile and a
separate `encodingAttested` claim for that profile/dataset. Setting
`allowAssumedProfiles` is explicit and sets a mask; it does not attest the target
encoding. Uniform-fill solar controls are refused as sun/sky hypotheses. An
explicit zero-sun/zero-sky configuration is supported; it is not inferred absence.

Typical integration uses `validatedHypothesis.fitted` as `candidate`, preserving
each retained hypothesis separately. A controlled configuration can populate the
same `Solar::Proposal` directly. Only its profile, model, direction and amplitudes
are used. Training scores and acceptance labels are not reinterpreted as proof.
Fixed local contributions previously supplied through `Solar::Sample` are not
included in the global term: local trials must supply their own source hypothesis.

| Output | Units and meaning |
| --- | --- |
| `observed[c]` | Original profile-specific encoded code interval, with censoring and declared symmetric `uncertaintyCodes`; this allowance is not sigma. |
| `globalPreencoding`, `trialPreencoding` | Forward-predicted `CompilerLinearRelative255`, never inverted measurements, lux, cd or nits. Source parameters remain compiler-relative. |
| `predictedCodes` | The selected profile's encoded prediction after all source summation. |
| `underCodes`, `overCodes` | Separate nonnegative distances below/above the observed code interval. Neither is energy or a local RGB estimate. |
| `rmseCodes` | Square root of mean per-receiver RGB interval squared error. Equal receiver weights; no statistical independence claim. |
| `baselineRmseCodes` | Zero-local score on exactly the local trial's supported receiver subset. |
| `improvementCodes` | Baseline RMSE minus trial RMSE, in code values; a negative score improvement is not negative lighting. |

`State::Valid` labels a supported observation interval only. `Conditional` labels
numeric comparisons under the supplied profile, global configuration, placement,
geometry and source assumptions. `Unavailable` labels missing/unsupported
comparisons. Numeric payloads are inactive when unavailable; zero initialization
does not represent black light. An overall conditional result can contain
unavailable alternatives, trials and individual rows. Inspect each row and `used`.

For the truncating encoding branch, byte `b<255` has conservative closed interval
`[b,b+1]`, widened by the declared code allowance. `Bake::Encode` rounds the sRGB
branch internally, so its encoded prediction uses `[b,b]` before widening. Top code
255 has a lower constraint and `upperKnown=false`; the stored upper numeric field
is inactive. No finite local-energy upper bound is inferred from saturation.

Rows retain channel saturation masks and these receiver masks:

| Mask | Interpretation |
| --- | --- |
| `Missing`, `Ineligible` | Original input missing or outside the caller's eligible subset. |
| `UnknownEncoding` | No attested applicable output recipe; no numerical default. |
| `UnsupportedObservation` | Grid, deluxe/direction-only, HDR/other encoding or unsupported receiver domain. |
| `UnsupportedReceiver` | Placement, normal convention, style, mapping or profile capability unsupported. |
| `UnknownGlobal`, `UnknownLocal` | Required global/local forward prediction unavailable. |
| `Saturated`, `ObservationUncertainty` | Censoring or explicit code allowance retained. |
| `GlobalOverprediction` | The **zero-local baseline** overpredicts at least one code interval. This is a mismatch diagnostic, not proof that no positive local addition can help. |
| `AttributionUnresolved` | Always retained: direct lamp, bounce, color bleed, recipe mismatch and other explanations remain possible. |
| `AssumedProfile`, `CompetingGlobals` | Applicability is assumed or multiple requested global alternatives exist. |

The visibility guard requires actual sky reach for visible sun/sky rays, and
rejects uncertain optics, approximate/unsupported/incomplete geometry and policy
assumptions. A clear segment without a sky boundary is unknown. Missing geometry
is not treated as empty space. Unknown local rays exclude those receivers from
both trial and paired baseline scores. Different masks or profiles must not be
ranked by their RMSE alone; the API never ranks them automatically.

Limits bound receiver/global/trial/source counts, retained fixed-size result
storage, accumulated pairs/rays/traversal and encoding calls. `maxBytes` is a
conservative record-storage allowance, not a total process-RSS measurement;
native geometry also needs scratch capacity in `ReadContext.maxBytes`.
Cancellation, invalid inputs or budget failures return an error and clear the
entire output. Negative/nonfinite local or global amplitudes are rejected rather
than repaired into local lights.

## Controlled results

Targets use independent analytical point/cosine response and output encoding;
production native BVH geometry supplies actual sky and shadow visibility. There
are 35 receivers across a 768 by 512 WU floor, a warm point at `(-160,0,128)`, a
mirrored wrong-position source and a broad white point alternative. These are
owned numerical fixtures, not fresh q3map2 compilations or stock-map recovery.

The checked [witness](../tools/rend2/local_light_evidence/results.json) records all
trial scores, coverage and row masks. Representative interval RMSE values:

| Fixture / configuration | Zero local | Warm local | Other comparison |
| --- | ---: | ---: | --- |
| Mixed sun + local, supplied candidate sun | 9.1324 | 0 | Wrong location 11.4768; broad white 108.5489 |
| Same observations, explicit no-sun alternative | 123.7462 | 119.5542 | Broad white 13.6173 |
| Sun only, supplied sun | 0 | 9.0258 | No tested local trial improves on zero local |
| No sun, supplied zero global | 9.1324 | 0 | Excessive sun baseline remains incompatible |
| Saturated mixed sun + strong warm local | 44.2241 | 0 | Positive red light decreases encoded green |
| Warm local plus additional red bleeding patch | 25.2982 | 17.5029 | Grey local 19.7657; unexplained red patch remains |

The mixed fixture changes candidate preference from a broad source absorbing the
global field to the correct warm local hypothesis. It is a finite controlled
candidate-pool isolation result, not automatic map-wide lamp discovery.

Two white solar alternatives with compiler-relative RGB 300 and 600 both explain
the same fully saturated observation with zero error. Both survive separately;
no unique solar magnitude/decomposition is claimed. Other tests verify explicit
gamma, exposure and sRGB recipes; ambient counted once; original bytes unchanged;
negative/nonfinite amplitude refusal; unknown profile, placement, sky, optics and
local geometry; partial coverage with paired baseline masks; and all work/memory
limits. Grid/direction inputs are refused before numerical comparison.

The new suite passes **4,142 checks**. Two complete witness runs are byte-identical
on the tested Windows MinGW toolchain. Seven CTest suites pass: local evidence,
solar, bake forward, bake profile, visibility, decode and lightgrid.

Remaining evidence can contain colored interreflection. The bleeding fixture
retains that patch and prefers a warm trial over a grey one, while preserving
`AttributionUnresolved`. A low cost certifies compatibility with one declared
model, never a direct-light cause or an exact historical lamp. General GI,
automatic compiler identification, full grid reduction, solar uncertainty between
untested alternatives and calibrated energy recovery remain unavailable. Original
baked lighting remains the evidence; this diagnostic neither replaces nor edits it.
