# S13: canonical offline reconstruction producer

This is an operational conditional CPU producer, not demonstrated general physical recovery of original JA lighting. The predecessor S12 is committed as `0aa1757cf`. The producer uses the existing source capture, profile-specific forward evaluator, candidate consolidation, Joint solver, uncertainty and passive transport. No inverse algorithm is recreated inside orchestration. No runtime lighting consumer is added.

The controlled tests can produce useful fitted hypotheses and immutable canonical assets. All six bounded stock-map attempts currently produce **zero resolved emitters**. A structurally valid diagnostic asset is not a recovered map, a successful black predictor, or evidence that the original map had no lights.

## Execution and ownership

`shared/rd-rend2/lightrecon/producer.{h,cpp}` implements:

1. Immutable map/metadata/generation, BSP dependency and bounded-request checks.
2. Optional existing Hidden/Solar providers, evidence consolidation and local/class quotas. Supplied candidate priors remain explicitly identified.
3. Existing `Joint::Solve` for amplitudes, permitted geometry/type/count/profile proposals, reference visibility/shadows and passive one-bounce transport.
4. Existing S12 complete configuration witnesses and uncertainty assessment. An incomplete uncertainty schedule publishes nothing.
5. A final independent held-out aggregate receipt after all parameter changes stop.
6. Canonical source/configuration/provenance construction, strict frozen reevaluation and source-linked decomposition.
7. Structural validation plus every referenced payload, optional explicit offline serialization/reload and generation-checked immutable Manager publication.

The producer has no target/answer reader for hidden RGB/XYZ. Its synchronous validation callbacks receive frozen models and return aggregate diagnostics. The final evaluator runs once after fitting/uncertainty. This is the existing physically separated API/process protocol, not an OS security sandbox for hostile native code. The controlling harness must mount only legitimate inputs for an untrusted solver.

`Producer::Backend` implements existing Disabled/CacheOnly/CacheOrBuild/ForceRebuild policy. Failure/cancellation/work exhaustion never publishes a partially constructed result. A disk-write failure can preserve an independently valid in-memory asset. A cached result retains its validation receipts without pretending that evaluation was freshly rerun. Normal renderer map loading still does not invoke this backend.

A real integration defect was fixed: S11 salted a model's training identity with the validation partition while S12 reassessment recomputed the original training identity. `Joint` version 2 separates the training seal from the selection/checkpoint key. Changing validation still invalidates resume, while strict model reassessment remains possible without weakening its seal.

The publication suite also exposed a frozen-transport export bug: its dummy representative was always patch zero, even if that patch had no supported illumination. Selection now uses supported root coverage, and unavailable surface fields are omitted with an explicit limitation rather than invalidating valid direct diagnostics. Material bounds from a frozen assumed reflectance are labelled conditional; unknown/bounded original reflectance clears total-upper-bound availability.

## Canonical semantics and formats

Canonical semantic schema is **4**. Schemas 1/2/3 are explicitly rejected. The independent `.jkobs` observation encoding is unchanged. The codec adds assessment section 17 and new contribution-domain/configuration/membership fields.

| Record | Meaning |
|---|---|
| `PhysicalEmitter` / `SunSource` | Conditional finite/directional source geometry, provenance, proper calibration reference and separate uncertainty; never automatically Resolved |
| `EnvironmentLighting` | Distinct sky/environment hypothesis; direct-sun inclusion is explicit |
| `LightingConfiguration` | Complete mutually exclusive source/profile/material configuration, including legacy Q amplitudes; alternatives are never averaged |
| `TransportField` | Conditional passive source-linked first bounce or identified legacy ambient; no synthetic GI point lights |
| `ContributionReference` | Source/root/reflection order, selected configuration, domain, calibration and inclusion metadata |
| `BuildAssessment` | Seven independent readiness dimensions, source/use admissions, coverage denominators, profile hypotheses, input/partition hashes and prediction receipts |

Canonical source IDs are map-scoped deterministic allocations from uncertainty tracks. They cannot collide with a provider's small integer IDs or input metadata. Every originating provider/evidence reference is retained in provenance; unsupported multi-part fixture unions are deferred instead of replacing the union with its first rectangle.

Contributions are pre-encoding incident values under an explicit contract. Original compiler-relative Q is **not candela**. Unknown primaries keep canonical chromaticity/intensity unknown while Q survives in configurations and legacy-channel datasets. Even an attested linear relative colour has unknown absolute photometric scale. Physically authored SI output requires an explicit physical calibration record; renderer `physicalToScene`, exposure or PBR base colour is not such evidence.

`LegacyRelativeChannels` is deliberately rejected by the general physical `LightingSample` adapter. It cannot silently become Rec709 radiance. Source-linear relative, compiler-relative, scene-equivalent and physically authored quantities remain distinct.

Producer payloads use little-endian bounded chunks with version-1 encodings:

- `org.openjk.producer-sample-domain`: stable patch/sample identity, original surface/material/block, position, normal, area and style.
- `org.openjk.producer-source-contributions`: source/root/order, availability, optical issues, conditional value/bounds.
- `org.openjk.producer-observation-diagnostics`: strict predicted total, pre-encoding components, availability and signed observation-domain interval discrepancy.

Existing passive-transport payloads retain their own decoder and source/root/order metadata. Validation checks all checksums, ordering, spans, identities, reference geometry, units, membership and source/component sums. Encoded RGB8 components are never asserted to add after peak normalization, clipping or quantization. Signed discrepancy is not positive unresolved irradiance and is never converted to a physical field. Grid/deluxe diagnostic operators remain distinct from surface root contributions.

## Support and limitations

| Path | Supported scope | Physical claim |
|---|---|---|
| Point/spot historical direct | Feature-scoped documented NetRadiant subsets, strict production C++ predictor | Conditional compiler-equivalent relative Q; no generic Raven identity |
| Rectangle/tube | Existing finite diffuse integral/shape fitter; historical facet models only where supported | Specified relative physical reference; no automatic equivalence to runtime LTC or historical tubes |
| Sun | Explicit directional source + actual sky intersections | Direction/relative amplitude conditioned on supported recipe; no renderer fallback evidence |
| Sky | Explicit supported sky17 environment operator | Distinct from direct sun and bounce |
| GI | Bounded passive source-linked one-bounce `E1 = K_rho E0` | Conditional on finite geometry, visibility and bake-time reflectance |
| Unresolved | Explicit unknown/unavailable or identified boundary root | No arbitrary free patch emission or positive field invented from residual |

UnknownProfile is nonnumeric. Explicit alternative hypotheses can be evaluated, but their ranking never identifies the historical production compiler. Multiple exclusive provider scenarios require separate joint requests; the producer explicitly refuses to sum incompatible Solar configurations. Full alternative Joint configurations within a scoped request are preserved. This scenario-expansion scheduling remains an integration limitation.

Current admission is conservative: exact emitter/shadow/specular suitability is unavailable. Approximate-field suitability requires its own usefulness calibration. No source is promoted just because it lowers RGB error or appears in an initial candidate list. No posterior existence probabilities or arbitrary heuristic-confidence products are introduced.

## Coverage and stock attempts

The existing complete all-page X02/X06 captures were reused; they were not replaced by screenshots/GPU textures. The bounded native attempt uses at most two training-local legitimate glow hypotheses, 24 samples per spatial role, explicit inverse-square/linear conditional profiles, and a separate UnknownProfile diagnostic. Local selection uses TRAIN plus geometry; if independent geometry partitions are absent, the declared fallback is full-map geometry strata. This is not exhaustive stock candidate search.

| Map | Original colour pages | Raw colour texels | Mapped world samples | Logical grid cells | Attempted train/validation/held-out | Resolved |
|---|---:|---:|---:|---:|---|---:|
| hoth3 | 38 | 622,592 | 403,084 | 229,068 | 24 / 24 / 24 | 0 |
| hoth2 | 12 | 196,608 | 79,565 | 1,044,126 | 24 / 24 / 24 | 0 |
| vjun2 | 42 | 688,128 | 448,811 | 152,500 | 24 / 24 / 24 | 0 |
| kor1 | 24 | 393,216 | 235,843 | 562,326 | 24 / 24 / 24 | 0 |
| yavin1b | 10 | 163,840 | 92,167 | 798,537 | 24 / 24 / 24 | 0 |
| mp/ffa1 | 22 | 360,448 | 226,573 | 118,320 | 24 / 24 / 24 | 0 |

Mapped counts are not independent localization information or certified area. Whole-map area, source transfer, primaries, bake profile and absolute scale remain unknown where the input does not establish them. Region IDs are spatially incomplete. Grid index count is not direct/indirect physical observation count. Derived full-map readiness and the tiny actually evaluated subset are reported separately.

The current offsets from glow geometry are only point hypotheses, not measured lamp positions. Most failed geometric placement/support checks; `kor1` had no admitted legitimate glow candidates. Unknown rays remain Unknown. The result is `DiagnosticOnly` with null prediction metrics, no accepted physical sources and no asserted absence of sun/GI. This exposes a real admission/search limitation; it does not prove that all JA direct light is unidentifiable. Stock source XYZ/photometric accuracy is unavailable, never zero.

Licensed archives, observations, geometry, fitted diagnostic archives and screenshots remain local outside versioned results. Only aggregate counts, hashes and diagnostics are recorded in Git.

## Controlled numerical results

The independent native adapter fits through production C++, then builds/validates/cache-round-trips the canonical artifact. Fixed XYZ is an allowed supplied prior in these three compiled cases, not a discovery result. Hidden true amplitudes remain evaluator-only. Quantized/censored interval loss, correct profile encoding, visibility and final spatial hold-out are preserved.

| Compiled case | Train interval MSE | Validation interval MSE | Held-out interval MSE | Black held-out MSE | Relative Q allocation error |
|---|---:|---:|---:|---:|---|
| point_room | 0 | 0 | 0 | 2495.889 | 0.0484% |
| two_colour | 0.21817 | 2.17680 | 0.22287 | 1170.602 | 5.16%, 5.86% |
| door_occlusion | 0 | 0 | 0 | 2495.889 | 0.0484% |

Wrong linear profiles fail the frozen independent block gate and yield DiagnosticOnly, not a successful empty reconstruction. The two-colour allocation error remains visible despite low RGB error; limited planar/quantized evidence does not justify exact amplitudes. The input views explicitly cover 345 selected mapped floor samples, not the full two-page original fixture (32,768 raw texels); only 18 training and 15+15 independent samples are fitted/scored.

A controlled position-refinement producer case reduces 0.3 to approximately zero world-unit error under its declared bounds. Point/spot/rectangle/tube choices, explicit sun/sky, coloured passive bounce and ambient-without-fake-emitter are exercised. The shared full Joint scene suite is also available as `lightrecon_producer_suite`, using identical scenes and source-level metrics through publication. It separates true geometry search from supplied fixture priors and preserves count/type/profile ambiguity.

## Cache and lifecycle

The explicit offline key includes BSP identity, actual sanitized archive hash, observation/operator/generation identities, structured solver/uncertainty/sampling/source settings, profile rules, calibration policy, seed and independent partition identities. Moving a source or changing a partition invalidates reuse. Unserialized optional provider policies or special geometry contexts mark settings incomplete and disable cacheability.

Every payload is verified on cache reload. Corrupt metadata/payload, missing chunk, stale generation, wrong map, cancellation or exceeded work budget cannot produce a partially accepted artifact. Tests include retained old immutable leases, warm CacheOnly without validation/fitting, write failure with valid memory result and repeated native atomic replacement.

Forty immediate native replacements passing does **not** establish the cause of the historical intermittent atomic-replacement failure. Production renderer dependency collection and early cache preflight still return `NotCacheable`; offline cache reuse is not proof of a fast real game warm load.

## Visual debugging and renderer isolation

`r_lightReconPreview` explicitly imports a bounded text marker file keyed to the original captured BSP SHA. It validates finite frames/extents, IDs and limits; it clears on map lifecycle/reset. Empty state performs no shader registration or poly submission. Markers use the existing depth-tested debug path, never scene lights, fog, CSM or `tr.sunDirection`.

Actual hoth3/hoth2 runs used 640x360, picmip 3, 30 FPS, no sound, and disabled PBR/Forward+/LTC/fog/shadows/SSR/SSGI/IBL/AA. Only this task's sequential child processes were started/stopped. The imported markers were **unfitted glow priors**, explicitly magenta/Unknown, because no stock source passed fitting admission.

Visual alignment is **not verified**: initial markers were occluded/out of view; a geometry-facing follow-up camera was inside/near obstructing geometry and the hoth2 frame changed independently. Before/after hoth3 frames were identical, but that alone does not validate visible markers. These are failed/inconclusive visual checks, not passed stock reconstruction. Screenshots and console IDs are preserved locally.

No shader source/permutation or normal lighting path changed. The historical frozen shader baseline reports pre-existing drift; it was not rewritten. A separate comparison against input commit `0aa1757cf` shows zero source/binding differences for all 27 programs, with current GPU compilation. This distinction is retained in machine-readable results.

## Readiness

| Dimension | Current result |
|---|---|
| Immutable data | Full-map stock resources/readiness available; local observation/material/visibility uncertainty remains |
| Forward model | Verified controlled feature subsets; stock hypotheses conditional |
| Offline producer | Operational on supported bounded controlled scenes; rejection/diagnostic paths explicit |
| Known-source recovery | Quantified controlled priors/search; not general source discovery or universal physical proof |
| Stock physical reconstruction | Not demonstrated by these attempts; zero resolved emitters |
| Explicit offline disk cache | Round trip/determinism/payload validation supported |
| Production map-load cache | NotCacheable; early complete dependency collector still absent |
| Runtime fog/entity/shadow/LTC/PBR consumers | Unimplemented/unavailable; never automatically activated |

See [reproduction commands](../tools/rend2/producer/README.md) and the versioned result witness in `tools/rend2/producer/results/`. Remaining work includes provider-scenario scheduling, better independently supported stock candidate placement, material/profile admission, full-map optimization scale, rigorous existence/use calibration, reliable visual alignment and production cache preflight. A renderer adapter must independently validate and consume the appropriate component; it cannot infer correctness from array names or successful serialization.


## Executed validation and reproduction

The exact local toolchain was GCC 16.2.0 (MinGW Release standalone), MSVC 2019 x64 Release for both renderers, and Python 3.12.14. Python research formulas are not used for fitting. The original q3map2 executable is not rebaked by S13: hash-pinned compiled BSPs and recipes are reused. The forward regression checks the production C++ against Python and original compiler samples, retaining unsupported bounce replay as unsupported.

Actual commands (the bundled Python path can replace `python`; MinGW tools were explicitly added to PATH):

```powershell
cmake --build build/s06-gcc -j 2
ctest --test-dir build/s06-gcc --output-on-failure -j 2
python -X utf8 tools/rend2/joint/benchmark.py --tool build/s06-gcc/lightrecon_producer_suite.exe --out build/s13-publication-suite-b
build/s06-gcc/lightrecon_producer_test.exe C:/Users/Keyten/Desktop/projects/OpenJK-rend2/build/s13-cache-release build/s13-producer-release.json
python -X utf8 tools/rend2/producer/benchmark.py --tool build/s06-gcc/lightrecon_joint_tool.exe --out build/s13-compiled-release
python -X utf8 tools/rend2/bake_model/validate_cpp.py --exe build/s06-gcc/lightrecon_bake_forward_tool.exe --bsp-dir build/bake-model-comparison-final/authoring/base/maps --output build/s13-forward-verified
python -X utf8 -m unittest discover -s tools/rend2/evaluation -p 'test_*.py' -v
python -X utf8 -m unittest discover -s tools/rend2/datasets -p 'test_blind_views.py' -v
python -X utf8 -m unittest discover -s tools/rend2/evaluation/x05 -p 'test_*.py' -v
python -X utf8 -m unittest discover -s tools/rend2/producer -p 'test_*.py' -v
cmake --build build/s13-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 2
python -X utf8 tools/rend2/test_lightrecon_legacy_gl.py --output build/s13-disabled-path-b
python -X utf8 tools/rend2/producer/compare_revision.py --revision 0aa1757cf --gpu-report build/s13-disabled-path-b/report.json --out build/s13-disabled-final
```

The shader command above deliberately reports its historical baseline failure (3,870 diff lines), while driver compilation, dependency audit and the explicit input-commit comparison pass. No baseline is rewritten to obtain success. Initial MSBuild generated-directory writes were denied by the sandbox; the final isolated SP/MP builds succeeded with permitted build-directory access. No source/game directory ACLs were changed.

The full publication suite has 27 records, 351 assertions and zero failures in two semantically identical runs. Twenty-one supported positive scenes actually complete canonical publication; dark/unsupported/ambiguous diagnostics and stage-only ablations remain separate. Required mixed point/spot/rectangle, tube alternative, weak coloured lamp, hidden residual-search candidate, sealed wall and doorway controls have no false/missed emitters under the frozen compact-scene matching tolerances. Several positions/shapes are supplied priors; these matching scores are conditional, not evidence of autonomous discovery. Coincident-count and unknown-scale alternatives remain unidentifiable.

Final standalone validation passes **30/30 CTest** in 269.69 s, including producer, complete publication-suite, observations, region/visibility, forward, intensity/geometry, sun, shadow, GI, uncertainty, codec/lifecycle and legacy baked-light tests. Both SP and MP Release modules build successfully.

The dedicated producer test has 457 passing assertions including numerical fields, canonical/payload corruption, generation/lease isolation, unit semantics, conditional material bounds, cancellation, missing validation/final assessment, work limits, source movement/partition cache invalidation and write failure. Python tests pass: 39 metrics, 6 blind views, 16 positive/adversarial gates and 4 producer-controller controls. Earlier integration failures are retained in local logs and were corrected before these witnesses.

The complete metric/readiness runner also inspected 11 existing analytical answers, 12 compiled datasets and three stock censuses. Its answer-copy/zero/oracle controls are metric assertions, **not producer fitting runs**. Actual producer fits are the separate publication/compiled witnesses. Existing controlled shadow/GI/source-removal/type/profile ablations are reused; S13 does not claim a new exhaustive five-factor ablation on stock maps.

Actual OS peak working sets and wall times (not STL capacity estimates): compiled positive native runs approximately 0.4–5.4 s and 6–7 MiB; full publication suite 15.67/14.13 s and 6.49/6.60 MiB; six-map stock controller 80.62 s and 417.0 MiB, with native child geometry/fitting attempts separately measured. Metric/readiness scan took 277.95 s and 183.28 MiB. Timing/RSS are outside deterministic semantic witnesses. Ray counters in producer diagnostics cover fitting/strict/uncertainty/decomposition work; independent final-evaluator work is separately available in `FinalReceipt::score.work`, and the adapter's duplicate display evaluation is not advertised as optimizer work.

The original stock captures are reused; their capture cost is in X02/X06 rather than misreported as S13 loading cost. Default producer limits are 128 MiB accounted working storage, 64 MiB payload, 8,192 patches, 128 candidates per scoped fitting request, 128-row chunks and bounded solver/ray work. These are cooperative algorithm/storage limits, not an OS RSS reservation or a demonstrated full-map optimization budget. No mandatory global dense source-by-luxel or all-surface transport matrix is allocated.

Compiled positive archives are 19.6–26.4 KB; diagnostic-only controls are approximately 4.5 KB. The source SHA-256 witness matches the final production sources. Two-colour compiled regeneration produces byte-identical archive names/content including configuration/provenance/payloads. Warm CacheOnly is verified without fitting in the native backend test. Forty replacements passed; intermittent replacement root cause remains unproven. Visual checks remain inconclusive as described above. No absolute stock photometry, calibrated stock emitter existence, renderer-consumer eligibility or stock XYZ accuracy is claimed.


The final aggregate witness can be regenerated without proprietary payloads:

```powershell
python -X utf8 tools/rend2/producer/collect.py --out tools/rend2/producer/results/v1 --stock C:/Users/Keyten/AppData/Local/Temp/jk-lightrecon-s13-20261031-release --preview C:/Users/Keyten/AppData/Local/Temp/jk-lightrecon-s13-preview-20261031-b
```

Use a fresh output path for a repeat. The versioned witness is under [results/v1](../tools/rend2/producer/results/v1/manifest.json). Numerical assets/results and variable wall-time/RSS are separate. The frozen source compiler remains NetRadiant 2.5.17n-git-d9338309e for those controlled fixtures only. Its exact executable/BSP/recipe hashes are in `forward-oracles.json` and `compiled.json`. The 22-case regression reports 20 supported passes and two explicitly unsupported full bounce replays, with all 1,225 scoped `multi_sun` floor samples byte-exact. This does not establish Raven parity.
