# Frozen aesthetic viability decisions

[AR-34 decision](../../../docs/rend2-relighting/viability-decision.md) and
[protocol v1](policies/viability-v1.json) freeze one original duel10 two-housing,
three-receiver-chart diagnostic. **Blocked-SourceSupport**: native geometry-only
responses are available, but the stronger native named-mount check refuses both
oblique housings. No original-target fit was performed. Start with the standalone
AR-41 prompt in the [plan](../../../docs/rend2-aesthetic-relighting-plan.md#ar-41).

The [evidence receipt](results/ar34-v1/evidence.json) pins original inputs,
existing successes, native outputs, retained failures and fresh integrity checks.
The [manifest](results/ar34-v1/manifest.json) hashes the public deliverables.
Licensed geometry, images, positions and native inputs remain in ignored
`build/ar34-v1/`; missing private inputs are Blocked, never synthetic replacements.
This directory supplies policies/receipts, not a new evaluator or runtime asset.

Recheck the decisive probes with the existing frozen inputs, from repository root:

```powershell
$taskCapture = (Get-Content -Raw build/asr25-v1/maps/mp_duel10/inputs.json | ConvertFrom-Json).mp_duel10.capture
build/s06-gcc/lightrecon_aesthetic_tool.exe $taskCapture build/ar34-v1/joint-coverage.txt --coverage
build/s06-gcc/lightrecon_aesthetic_tool.exe $taskCapture build/ar34-v1/dummy-admission-v2.txt build/ar34-v1/dummy-held-v2.txt
```

The first command reports 141 available geometry responses. The second executes
the existing fitting/admission path with **literal zero dummy targets**, not
original RGB8, and reports all three local configurations unavailable with
`Named mounting surface absent at declared contact plane`. Its zero baseline
scores are not measurements of original lighting quality. The earlier
`dummy-admission.txt` probe preserves a scratch block-ID collision refusal.
Do not substitute the unit-response result for source admission.

Before any authorized future fit, check every policy/input/native hash and use
the exact frozen source poses and receiver IDs. Historical ASR24/25 results and
thresholds remain unchanged. V1 permits at most one real-target fit/repeat and no
post-final repair fit; the reserve lacks one housing's shadow-control pairs.
Keep every refusal, unavailable metric, assistance distinction and D fallback.
