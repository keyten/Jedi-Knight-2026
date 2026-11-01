# Independent optional-provider orchestration

This change starts from `4955d7cef` on `rend2-unified-wip`. It changes CPU
producer orchestration, diagnostic metadata and canonical serialization. It does
not enable ordinary map-load reconstruction, renderer consumers, or measured
stock photometry.

Validation date: 2026-11-01. The result is resilient orchestration, not stock
physical-light recovery.

## Outcome contract

`ProviderAssessment` separates execution, evidence, coverage, admission and
requirement. `ran` means called; `operationSucceeded` means the operation returned
valid proposals or established a legitimate lack of supported evidence. Neither
means physical-source recovery. `evidenceAvailable` and `records` count provider
proposals, not verified lights. `outputsAdmitted` identifies proposals actually
converted into this scoped joint request. `searchComplete` describes bounded
proposal search, never exhaustive physical reconstruction. No provider can set
`exactLightingAdmissible` true in the current contract.

| Outcome | Meaning | Optional provider | Required provider |
|---|---|---|---|
| `NotRequested` | No call; component remains unknown | Continue | Missing required request fails |
| `Produced` | Valid evidence hypotheses | Consolidate/admit | Consolidate/admit |
| `NoSupportedEvidence` | Valid lack of supported positive/comparable evidence | Continue; UnknownSun/unresolved component | Fail |
| `Partial` | Valid proposals with limited search coverage | Admit conditionally; retain limitations | Admit conditionally; does not promise exhaustive search |
| `BudgetExceeded`, Local | Provider's explicit local search quota | Continue by default, no failed-call proposals | Fail |
| `BudgetExceeded`, Global/unknown scope | Read/global/nested/mandatory budget or allocation failure | Fail | Fail |
| `UnsupportedCapability` | Missing formula/unsupported observation/validation capability | Continue, no component emitted | Fail |
| `InvalidInput` | Invalid geometry/options/units/identity/numerics | Fail | Fail |
| `Cancelled` | Cooperative cancellation | Fail; no publication | Fail |
| `Corrupt` | Bad provider records, schema, state or immutable generation | Fail | Fail |
| `NumericalFailure` | Algorithm/invalid numerical operation | Fail | Fail |

The low-level `Core::Code` is retained. `Unavailable` is not indiscriminately
swallowed: providers declare `NoSupportedEvidence` at their legitimate terminal
evidence checks; otherwise `Unavailable` is `UnsupportedCapability`. A stale
generation, malformed lease, nonfinite geometry/profile value, `ProviderError`,
unsupported schema, cancellation or algorithm error remains fatal. Diagnostic
strings are never parsed to decide whether an error is safe.

`Hidden::Report` and `Solar::FitResult`/`Solar::Result` now expose typed outcomes.
The Hidden report and solar work identify their explicit local limits. Unknown
budget provenance is conservatively global. A real `bad_alloc` is global. On a
failed call, candidates are discarded according to each provider's existing
ownership contract; work, area exclusions and diagnostics survive. A successful
partial Hidden result retains its proposals and search-limit flags.

`ProviderPolicy` declares `hiddenRequired`, `solarRequired`, whether optional
local-budget exhaustion is tolerated, and the maximum number of independently
scoped requests. Required means the requested provider must supply admissible
evidence; it does not mean a bounded search can prove all emitters have been
found. `solarRequired` applies to the combined sun/sky provider, not proof of
existence of a direct sun. There is no separate environment generator: diffuse
sky arrives through Solar or supplied environment providers. Neither absence of
a SunSource nor an unidentifiable environment is asserted to be zero energy.

## Orchestration and scoped alternatives

Provider gathering and candidate consolidation precede fitting. Missing optional
evidence no longer calls `Check()` unconditionally. Fatal errors and mandatory
limits still propagate. Candidate source/provenance records and immutable leases
remain retained in an opaque `PreparedProviders` object. Supplied candidates can
continue when Hidden or Solar contributes nothing. A positive sky-only model
does not require a local point lamp. A successful Hidden provider does not
require a successful sun inference.

`PrepareModelRequests(request, read, result)` runs providers once and creates
`result.scopedRequests`. Each request contains the common supplied candidates
plus only its matching profile/scenario-specific proposals. Incompatible
sun/sky configurations are never concatenated. One profile's hidden proposals
are not evaluated under another attenuation recipe. Unrequested profile IDs are
an explicit input-policy error rather than an assumed compiler identity.

For a single scope with the same profile-index layout `Run()` proceeds
automatically. If narrowing profiles changes the layout, even a single scope
requires explicitly rebound trusted callbacks. For multiple scopes it
returns a `DiagnosticOnly` asset and explicit immutable requests, with no combined
joint solve or final held-out read. A trusted controller should bind its
validation/final callbacks to each request's `fitting` context:

```cpp
Producer::Result prepared;
Check(Producer::PrepareModelRequests(input, read, prepared));
for (const auto& scope : prepared.scopedRequests) {
    Validation validation(scope->fitting, privateValidationRows);
    Final final(scope->fitting, privateHeldoutRows);
    Producer::Result conditional;
    Check(Producer::Run(*scope, read, conditional, &validation, &final));
    // Keep complete alternative configurations. Do not sum their emitters.
}
```

Use validation, never final held-out targets, for selecting between alternative
configurations. Final evaluation is value-only after freezing; it must not drive
parameter updates or another round of model selection.

No implicit ranking using hidden ground truth is introduced. Scope enumeration
is deterministic, explicitly bounded and independent of provider ordering. Its
budget is mandatory: a caller cannot silently truncate incompatible models and
claim complete search. Frozen requests pin geometry/settings/training/partition
identities; moving geometry or changing policy invalidates the binding. No
provider target RGB is newly exposed to numerical fitting.

## Canonical assessment and cache semantics

Canonical semantic schema is **5**, `BuildAssessment.version` is **2**, and the
producer algorithm is **2**. The assessment serializes provider receipts,
profile/scenario identities, execution/coverage/admission flags, work counters
and sun/environment evidence status. Sun/environment status is either Unknown
or Conditional, never verified absence. A diagnostic asset with no sun record
therefore remains `UnknownSun`; it does not introduce a black environment root.

Fatal provider receipts cannot appear in a published asset. A required provider
without evidence cannot be published. Provider completion cannot certify exact
light suitability. The codec checks outcome enums, consistency and single
selected scenario. Schema-4 assets/caches are explicitly rejected; independently
versioned `.jkobs` observations do not change.

Policy flags, producer/schema versions and prepared outcome/generation identities
participate in settings fingerprints. Cache keys cannot be reused across this
semantic change. Optional provider/search policies still lack a complete
canonical settings serializer: such requests deliberately remain `NotCacheable`.
This task does not claim complete production cache preflight. Explicit supplied
candidate builds keep their existing offline cache support; cached receipts are
restored for reporting without running a provider. Scene-wide regional scheduling
is a separate outstanding problem.

## Validation and stock evidence

Results and exact commands are recorded under
`tools/rend2/provider_orchestration/results/`. Controlled tests distinguish supplied
geometry priors from generated proposals and real provider calls from trusted
test validation responses. No test claims autonomous stock light recovery.

The audit's `t1_danger` original snapshot is reused privately with the same
78 training observations, 32 directional seeds, geometry, assumptions and two
explicit profiles. The former failure was `Unavailable` solar training after a
useful Hidden search. The new execution preserves `NoSupportedEvidence` solar,
UnknownSun and partial hidden-search records, and proceeds to `Joint::Solve`.
Numerical results, work and the next blocking condition are reported separately;
passing an optional provider is not a recovered source.

Remaining limitations include the global/regional job scheduler, sparse sampling
and source-admission quality, uncertain stock optics/materials, unknown historical
profile and absolute scale, and lack of authoritative original stock emitter
answers. A failed no-source shadow baseline is not silently removed from the
stock report. No renderer setting, shadow map, fog texture or gameplay light is
changed by this orchestration.

## Executed regression results

The final native producer witness has **549 assertions, 23 diagnostic records,
zero failures**, and is byte-identical in two executions. It retains the original
full sun/sky publication, payload/cache/lifecycle and consumer-disabled assertions
as well as the new optional-provider cases. Typed canonical receipts survive
serialization/cache reload; tampered exact-suitability and component-certification
claims are rejected. Changing provider policy invalidates cache identity; changing
the frozen scope's geometry invalidates reuse.

| Controlled case | Outcome | Held-out interval MSE | Evidence scope |
|---|---|---:|---|
| Positive local lamp, no solar observations | Completed; UnknownSun | 4.3633×10⁻¹³ | Supplied XYZ, fitted relative RGB |
| Local lamp, optional Hidden local budget exhausted | Completed; partial provider coverage | 4.3633×10⁻¹³ | Failed provider emits no proposals/energy |
| Positive hidden proposals, unavailable solar training | Completed | 1.6197×10⁻⁶ | Four generated proposals retained; supplied local prior remains |
| Positive hidden proposals and Solar fit, unavailable solar validation | Completed; no promoted sun | 1.6197×10⁻⁶ | Real provider calls; trusted intentionally unavailable validation callback |
| Positive diffuse sky, no local lamp or hidden proposals | Completed, one environment | 9.9778×10⁻⁸ | Supplied sky hypothesis, fitted relative amplitude |
| Local lamp plus supported directional prior, optional providers have no evidence | Completed, two conditional sources | 4.1870×10⁻⁴ | Supplied geometry/direction; not autonomous sun discovery |
| Two incompatible sun/sky scenarios | Separate requests, never summed | Separately evaluated | Extra hidden-only baseline tested independently |

The first two rows use the specified relative physical-integral observation
domain; historical rows use their explicit encoded q3map-domain intervals.
These different domains are not pooled into one accuracy score. Every published
source remains a candidate with zero resolved-source count. Positive tests improve
on their black baseline. Ambiguous or unsupported exact-light suitability stays
unavailable, not calibrated existence confidence.

Negative assertions cover required provider absence, required/local/global budget
failure, corrupt profile/schema and trusted provider response, stale geometry,
cancellation inside a provider, missing independent validation, numerical bad
amplitudes, changed immutable scope, and mandatory scenario-count/memory limits.
The absent-sun and unavailable-validation tests do not supply hidden physical
answers. Supplied geometry tests are labelled accordingly.

An initial test incorrectly expected only two scenarios while leaving a positive
hidden-only provider active. The planner correctly retained a third baseline.
The final tests explicitly check that three-scope preservation, then isolate the
separate no-hidden/two-solar fixture. No production filter, threshold or search
budget was changed to force two scenarios.

### Actual stock joint stage

`t1_danger` source BSP:
`6a24f3cd86d93766034fd064be480cd3f3e70cd512733c9826ecbc64cc444ca3`.
Captured archive:
`e92302cfceace6627aede38d5be1f04333586ffa51b03d3dfda7d83c375b550c`.
The same 78/28/13 training/validation/held-out geometry selections are retained.
The original stock compiler, transfer/primaries and absolute photometry remain
unknown. Both numerical recipes are explicit conditional hypotheses.

| Per-profile measure | Inverse-square | Linear |
|---|---:|---:|
| Original supplied evidence candidates | 1,102 | 1,102 |
| Hidden proposals | 8, partial | 5, partial |
| Solar fit | NoSupportedEvidence | NoSupportedEvidence |
| Joint structural models attempted / rejected | 96 / 96 | 96 / 96 |
| Joint evaluations / strict forward calls | 2 / 2 | 2 / 2 |
| Joint rays | 1,887 | 1,887 |
| Invalid support/path/bounds rejections | 94 | 94 |
| `Shadow source count` rejections | 2 | 2 |
| Final state | Mandatory structural-search Budget | Mandatory structural-search Budget |
| Published sources / artifact | None | None |
| Final held-out error / stock XYZ accuracy | Unavailable | Unavailable |

The final linked adapter reproduces the earlier successful transition past Solar.
Total native wall time for both profiles is **16.766 s**, actual OS peak working
set **38.84 MiB**. Whole-map input scanning/preparation is included; this is not
successful full-map optimization cost. Hidden hypotheses remain deferred when the
current fitting-wave admission is filled; this task does not claim to fix the
scene-wide/local budgeting scheduler. The two zero-source shadow-objective
rejections are preserved rather than deleting that baseline to get a fit.

### Builds and checks actually run

- Full CPU CTest executed all 31 tests in 1,659.27 s under shared-machine load.
  Thirty passed initially. The sole failure was the intentionally frozen empty
  archive hash still expecting schema 4. Updating the versioned schema-5 byte
  witness to `ceed5d33…` fixes it; disk rerun passes 1/1. No numerical tolerance
  was changed. After final strict receipt validation, disk and producer rerun
  passes 2/2 in 47.17 s.
- Final expanded producer witness passes 549/549 twice, including the preserved
  original publication assertions and the additional no-hidden/solar scope tests.
  First measured run: 75.390 s, actual peak RSS 8.46 MiB. Timing/RSS are outside
  deterministic witness JSON. Public-witness verification passes without assets.
- Both SP and MP MSVC 2019 x64 Release renderer targets build successfully with
  the final CPU code. GCC 16.2.0 builds the standalone core; Python 3.12.14 controls
  the test/witness tools.
- Disabled-path source/binding comparison against input commit `4955d7cef` reports
  zero differences over 27 programs. The earlier GL-3.2 compilation record is
  reused; no fresh GPU visual test is claimed. The pre-existing historical shader
  baseline drift remains separately reported, not rewritten.
- No game was launched, no stock data re-captured, no new q3map2 bake executed,
  and no runtime lighting/visual recovery was validated in this task.

Exact commands and the aggregate witness are in
[`tools/rend2/provider_orchestration/`](../tools/rend2/provider_orchestration/README.md).
Initial development failures remain in local build logs; only passing final
acceptance witnesses are published. The original all-stock audit remains frozen.
