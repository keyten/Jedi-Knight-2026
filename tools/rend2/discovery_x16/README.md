# X16 blind discovery bridge

See [the technical report](../../../docs/rend2-blind-discovery-x16.md) and
[measured results](results/v1/tables.md). These tools extend the existing V02
controllers and native S05/Joint components.

* `run.py`: public-only proposal replay, frozen baseline, exact-recipe legacy,
  eight-ticket control and geometry-axis policy.
* `fit.py`: training-only admission and native canonical consolidation/Joint.
  Validation values stay in the trusted controller. Final target files open
  after parameter freeze.
* `oracle_scope.py`: supplied-answer observability control, never discovery input.
* `report.py`: post-fit private-answer measurements; it cannot change selection.
* `collect.py`: adds supplied-XYZ controls, resources, build validation and hashes.
* `verify.py`: compares deterministic proposal identities, parameters and work.

Use fresh outputs. `fit.py --cases new --variants direct --profile 0` repeats one
case under the same numerical policy. `--cases refine` includes geometry and
original planar-face alternatives. Profiles 0/1 are independent inverse-square
and linear hypotheses. Policies v1-v6 retain unsuccessful experiments. v6 adds
bounded final diagnostics for three alternatives in existing validation order;
held-out scores never reselect a model.

Successful report export is not physical acceptance: inspect `accepted_nonempty`,
required blocks, unknown values and budget states. The frozen V02 fold has no
positive direct spotlight validation observations. No result certifies stock
lights, absolute photometry, the complete prism or renderer readiness.

No script here launches a game or enables a lighting consumer.
