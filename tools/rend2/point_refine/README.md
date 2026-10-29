# S07 bounded point XYZ refinement

The numerical implementation is production C++ `PointRefine::Refine`, calling
`Amplitude::Fit` and the strict `Bake::CpuForwardEvaluator`. Python only repeats
the owned synthetic CPU fixtures and measures resources. It does not fit lights.

```powershell
cmake --build build/s06-gcc --target lightrecon_point_refine_test --parallel 2
python -X utf8 tools/rend2/point_refine/benchmark.py --tool build/s06-gcc/lightrecon_point_refine_test.exe --out build/s07-results
python -X utf8 -m unittest discover -s tools/rend2/point_refine -p 'test_*.py' -v
```

No packages beyond Python's standard library are needed. `numerical.json` and
`open-room-trace.csv` must match across repeated runs; time and OS peak RSS are in
`resources.json`. The RSS helper reuses S06's Windows process-counter reader;
unsupported operating systems report unavailable RSS rather than zero.

See [method, constraints, measurements and limitations](../../../docs/rend2-point-position-refinement-s07.md).
Truth XYZ/Q exist in the test controller only. Neither the production request nor
the returned diagnostic is a recovered canonical/runtime lighting asset.
