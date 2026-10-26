# Historical grid-reconstruction validation tools

The grid-gradient source fitter and its launch/export/fitting harnesses have
been removed. This directory retains parsers/inspection utilities for the
[historical stock-map investigation](../../../docs/rend2-static-lighting-validation.md).
They do not implement or launch the current reconstruction architecture.

Use [offline datasets](../datasets/README.md) and `tools/lightrecon_test` for current
CPU development. Existing `bspinfo.py`, `budget.py`, `parselog.py` and archived
results are diagnostic evidence, not calibrated emitter ground truth. `mkent.py`
is only an original-entity-lighting comparison generator; it does not fit sources.
