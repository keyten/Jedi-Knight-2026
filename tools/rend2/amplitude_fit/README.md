# S06 production fixed-geometry RGB fitting

The solver is C++ `Amplitude::Fit`, not Python. See the
[design, equations and commands](../../../docs/rend2-amplitude-fitting-s06.md).

- `run.py` reuses X07's compiler/capture/hash/partition controller with the new C++
  fitter; private answers stay in the trusted evaluator.
- `worker.py` sees sanitized training inputs and sends a bounded `F` frame to the
  production executable. It performs no amplitude optimization itself.
- `benchmark.py` repeats generated C++ tests and measures process RSS separately.
- `report.py` verifies two complete runs and emits compact JSON/CSV metrics.
- `test_protocol.py` checks positive controls, the retained hidden-read denials and
  malformed requests. Missing fixtures are an explicit skip, not a numerical pass.

All scripts use Python's standard library. Existing pinned BSP fixtures are read
locally; no proprietary game data or recovered runtime lights are produced.
