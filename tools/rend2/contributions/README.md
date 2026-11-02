# P06 prepared contribution queries

This extends S13 attribution codecs and canonical data. It performs no baked
subtraction and enables no runtime lighting consumer. Read the
[technical report](../../../docs/rend2-contributions-p06.md) before using values.

CPU entry points:

- `Core::Attribution::Prepare(Producer::Artifact, ReadContext, ...)` validates
  and indexes immutable contribution chunks before publication.
- `Snapshot::SampleContribution(query)` selects stable source, sample-domain,
  sample and optionally dataset/configuration IDs. Compiler-relative requests
  require `allowLegacyRelativeChannels=true`; these never fill `LightingSample`.
- `Prepared::Audit(domain, sample, ledger)` separates pre-encoding components,
  encoded/linear total prediction and signed nearest-target-interval error.
- `Attribution::CanAdd(asset, componentA, componentB)` checks root/path/domain
  compatibility, exclusive alternatives and double counting.
- `Attribution::MayReplace(asset, prepared, request)` checks scoped independent
  proofs, physical calibration, receiver support and bounded error/leakage. It
  grants no renderer operation. All conditional producer fits forbid replacement.

The native tests use owned geometry and independent authored physical receipts,
plus actual production two-colour fitting. A separate existing trusted controller
uses hash-pinned compiled q3map2 BSPs, supplied fixed XYZ and blind amplitudes.
It now reads every stored contribution through the published Snapshot after
cache reload. It does not discover source XYZ or demonstrate stock recovery.

## Reproduction

Use the existing GCC/MinGW toolchain on PATH, or an existing MSVC build tree.
Python scripts use the standard library and existing repository modules only.
Use fresh result paths to preserve earlier attempts.

```powershell
cmake -S tools/lightrecon_test -B build/s06-gcc -DCMAKE_BUILD_TYPE=Release
cmake --build build/s06-gcc --parallel 2
ctest --test-dir build/s06-gcc --output-on-failure -j2 --output-log "${PWD}/build/p06-ctest-final.log"
ctest --test-dir build/s06-gcc --output-on-failure -R 'lightrecon_(contributions|disk)$' -j2 --output-log "${PWD}/build/p06-ctest-sealed.log"
python -X utf8 tools/rend2/contributions/benchmark.py --tool build/s06-gcc/lightrecon_contributions_test.exe --output build/p06-native-sealed
python -X utf8 tools/rend2/producer/benchmark.py --tool build/s06-gcc/lightrecon_joint_tool.exe --views build/x07-verified --bakes build/q3map2-bake-verified --out build/p06-compiled-sealed
python -X utf8 tools/rend2/bake_model/validate_cpp.py --exe build/s06-gcc/lightrecon_bake_forward_tool.exe --bsp-dir build/bake-model-comparison-final/authoring/base/maps --output build/p06-forward-final
$env:PYTHONPATH='tools/rend2/evaluation;tools/rend2/datasets'
python -X utf8 -m unittest discover -s tools/rend2/evaluation -p 'test_*.py' -v *> build/p06-evaluation-tests.log
python -X utf8 -m unittest discover -s tools/rend2/datasets -p test_blind_views.py -v *> build/p06-blind-tests.log
python -X utf8 -m unittest discover -s tools/rend2/evaluation/x05 -p 'test_*.py' -v *> build/p06-positive-tests.log
python -X utf8 -m unittest discover -s tools/rend2/producer -p 'test_*.py' -v *> build/p06-controller-tests.log
python -X utf8 tools/rend2/all_stock/architecture.py > build/p06-architecture.json
$env:CL='/MP2'
cmake --build build/s13-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 2 *> build/p06-renderers-sealed.log
python -X utf8 tools/rend2/test_lightrecon_legacy_gl.py --output build/p06-disabled
python -X utf8 tools/rend2/producer/compare_revision.py --revision e3c333650 --gpu-report build/p06-disabled/report.json --out build/p06-disabled-vs-input
```

The disabled shader test retains its historical baseline failure. All 27 current
programs compile and their sources/bindings match the input commit exactly; the
old baseline is not rewritten. No game is needed or launched.

Versioned public reports contain hashes, numerical results and measured resource
use, never proprietary map pixels/geometry. Collection after successful checks:

```powershell
python -X utf8 tools/rend2/contributions/collect.py --native build/p06-native-sealed --compiled build/p06-compiled-sealed --forward build/p06-forward-final --out tools/rend2/contributions/results/v1
python -X utf8 tools/rend2/contributions/collect.py --verify tools/rend2/contributions/results/v1
```

## Support and interpretation

| Stored/query quantity | Prepared query | Addition | Exact replacement |
|---|---|---|---|
| Linear Rec.709 incident irradiance / radiance with explicit units | Typed value and LightingSample | Only compatible complete roots/orders and available values | Only scoped physical Lux direct point/parallel-sun contracts with every proof |
| Compiler-relative pre-encoding channels | Explicit opt-in typed channels | Conditional pre-encoding algebra only | Forbidden |
| Encoded RGB8 total / censored target intervals | Diagnostic ledger | Components are not independently additive | Forbidden |
| Source-linked first bounce | Existing root/order records | Must exclude duplicated roots/path orders | No direct replacement |
| Grid ambient/directed/direction, deluxe direction | Stable geometry/total diagnostics | Source-specific packed reduction unavailable | Unsupported |
| Signed model discrepancy | Signed observation-domain diagnostic | Never physical positive GI | Forbidden |
| Missing sparse value / unknown visibility | Unavailable | Unknown, not zero | Forbidden |
| Mutually exclusive source/profile/configuration hypotheses | Explicit selection or Ambiguous | Cannot combine branches | Forbidden |

The isolated authored positive replacement test does not establish renderer
GGX/LTC compatibility. Point/spot/area/tube historical fitting remains governed by
the existing feature-scoped forward evaluator; shape support alone never proves
safe subtraction. Stock UnknownProfile, unknown absolute scale, censored channels
and unresolved allocations continue to block physical replacement.

See [frozen results](results/v1/manifest.json) for numerical/resource evidence.
