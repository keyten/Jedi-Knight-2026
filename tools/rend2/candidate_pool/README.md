# S05 CPU candidate consolidation

See [design and exact commands](../../../docs/rend2-candidate-consolidation-s05.md).

```powershell
cmake --build build/lightrecon-disk-standalone --target lightrecon_candidates_test --parallel 2
python -X utf8 tools/rend2/candidate_pool/run.py --tool build/lightrecon-disk-standalone/lightrecon_candidates_test.exe --out build/s05-validation.json
```

The executable tests the production C++ consolidation/scheduling stage on owned
synthetic fixtures. The Python standard-library controller verifies two identical
semantic witnesses and per-home CSVs, then reports timing/RSS separately. Its
timeout is 120 seconds per full test process. No game starts and no source fitting
occurs. Results measure input-candidate retention/work admission, not correctness of
recovered physical emitters. Existing renderer light budgets are untouched.
