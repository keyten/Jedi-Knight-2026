# Recorded optimization evidence

See the [technical decision](../../../../docs/rend2-optimization-benchmark.md) and
[commands/dependencies](../README.md). This is offline numerical research, not a
published reconstruction or runtime solver.

- [result.json](result.json): 620 independent method/start records, all failures,
  six compiler-backed dataset identities and numerical controls.
- [metrics.csv](metrics.csv): per-run quality and counted work.
- [tables.md](tables.md): per-problem/method summaries. Read parameter-error units
  in the full record; do not compare that column across different parameter blocks.
- [timings.json](timings.json): actual local solve wall times, separate from semantic
  JSON/CSV determinism.
- [distance.json](distance.json): 96 additional matched receiver-baseline fits.
- [resources.json](resources.json): ten methods and one import-only process with
  actual independent-process peak working sets.
- [validation.json](validation.json): full/repeated commands, OS memory/time,
  source/currentness checks, semantic byte hashes, tests and discarded-run reasons.

Full/repeat runs took **153.771 / 135.766 s**, with **81,612,800 / 81,797,120 bytes**
peak working set. `result.json` and `metrics.csv` were byte-identical. Those are
benchmark CPU-process costs, not a full-map reconstruction estimate.

Thirty new tests, 39 evaluation tests, 27 shadow-footprint tests, 15 GI tests and
all 17 existing CPU CTest targets passed. Original compiled scalar checks were
rerun against four pinned BSPs. Real multi-source/shadow archives retained
58/42/48 training/validation/held-out observations each after explicit unknown-ray
exclusion. No new q3map bake, gameplay run, GPU test or SP/MP rebuild was performed.

The near-correlated and spot-core cases retain ambiguity even when RGB fits.
The 96-record baseline ablation shows why repeated pixels at one position do not
identify source height/intensity. The direct/indirect control shows a free residual
can erase a true lamp. Numerical stationarity, low residual, optimizer restart
frequency and candidate count are never labelled calibrated source confidence.

Compiler-relative scalar/RGB units, synthetic SI shadow controls and algebraic
matrices remain separate. All data are small self-authored fixtures or derived
scalar metrics; no licensed stock BSP, image, geometry or texture is included.
