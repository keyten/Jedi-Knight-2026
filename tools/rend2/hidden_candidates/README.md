# Hidden source candidate diagnostics (S04)

See the [API, mathematics, limits and validation report](../../../docs/rend2-hidden-light-candidates-s04.md).
The C++ provider uses baked surface response and geometry; its output is only
point/spot geometry hypotheses. No final intensity, reconstructed asset promotion
or runtime renderer consumer is introduced.

`run.py` reads the existing immutable `TrainingView`, provides geometry/training RGB
to `lightrecon_hidden_tool`, and performs trusted candidate-distance evaluation only
after proposals are frozen. Geometry/material/fixture priors are permitted; original
source XYZ, amplitudes, validation and held-out colours are not solver inputs.

Python dependencies: standard library only. Tools/own synthetic fixtures are
GPL-2.0-or-later. Stock data remain local under ignored `build/`. `candidates.obj`
and per-observation diagnostic JSON are debugging aids, not recovered lighting.
