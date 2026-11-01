# Optional-provider orchestration regressions

See [the outcome/API contract](../../../docs/rend2-provider-orchestration.md).
No game, GL or stock answers are needed for the controlled tests. The producer
test directly calls the real Hidden/Solar providers, production forward operator,
candidate consolidation, Joint solver, S12 uncertainty and canonical codec.
Trusted deliberately unavailable/corrupt solar-validation callbacks exercise
orchestration responses; they are not reported as successful sky estimation.
Supplied directional/environment-prior tests are distinguished from inference.

```powershell
cmake --build build/s06-gcc -j 2
ctest --test-dir build/s06-gcc --output-on-failure -j 2
$cache = Join-Path (Get-Location) 'build/provider-orchestration/cache-accepted'
build/s06-gcc/lightrecon_producer_test.exe $cache build/provider-orchestration/controlled-accepted.json
build/s06-gcc/lightrecon_producer_test.exe $cache build/provider-orchestration/controlled-accepted-repeat.json
```

The native disk cache requires an absolute root. Time/RSS are not numerical
witness fields. `controlled-accepted.json` and `controlled-accepted-repeat.json` must match
exactly, including provider outcomes, scenario IDs, cache keys and objective values.

For a licensed private `t1_danger` snapshot from the previous 57-map audit:

```powershell
$private = "$env:TEMP/jk-lightrecon-all-stock-20261031"
python -X utf8 tools/rend2/all_stock/run.py --private $private --revision optional-providers-sealed --maps t1_danger
python -X utf8 tools/rend2/provider_orchestration/collect.py --controlled build/provider-orchestration/controlled-accepted.json --repeat build/provider-orchestration/controlled-accepted-repeat.json --stock "$private/runs-optional-providers-sealed/t1_danger"
```

Use a new revision if rebuilding the binary. This does not re-bake q3map2 or
recapture stock data. Outcomes remain conditional on the same explicit stock
profile/material/transfer hypotheses and the audit's bounded spatial subset.
Do not interpret reaching Joint fitting, a provider proposal count or a valid
empty diagnostic asset as original-light recovery. Aggregate reports can be
versioned; JKOBS/BSP and coordinate-level debug files remain private.

Verify the checked-in witness without stock payloads:

```powershell
python -X utf8 tools/rend2/provider_orchestration/verify_results.py
```
