# ASR20 — fit fixture color, power and direction offline

`tools/lightrecon_test/aesthetic_fit.{h,cpp}` implements a separate experimental
`Rend2LightRecon::Aesthetic` objective. It uses the existing modern
`Shape::Prepare` forward evaluator and source-support admission without changing
`Shape::Fit`, `Joint`, the historical solver, canonical storage, or runtime
illumination. Its output is a conditional aesthetic **Candidate**, never a
resolved source or evidence of an exact historical lamp.

The supplied fixture identity, position, support, dimensions and emitting
triangle leases stay fixed. Point, smooth spot, supported rectangle and cylinder
models use their existing modern laws. ASR19's `ShapeSelection::Rank` can supply
mounted shapes. The command-line tool accepts explicit point anchors or original
rectangle surface ordinals; the C++ API accepts all four supported shapes.
Unknown placement or optical coverage makes the entire affected configuration
unavailable. An unavailable configuration is not a black prediction or evidence
against a fixture. Independent geometry, BSP occupancy, source-side and endpoint
checks remain in force.

## Target and power contract

The caller must select one domain explicitly:

* **Attested relative linear:** incident targets and comparable receiver response
  are supplied explicitly. Encoded RGB8 cannot be declared calibrated irradiance
  by this tool.
* **Forward encoded RGB8:** predict modern relative incident response, add the
  bounded relative background, apply a fixed declared `Bake::Encode` transfer,
  and compare against RGB8 quantization intervals. Saturation is a one-sided
  censored interval. Unknown transfer is unavailable. A same-material response
  assumption is allowed only here and appears in diagnostics. It is a conditional
  appearance comparison, not a recovered albedo or an irradiance measurement.

For receiver `i`, channel `c`, and fixed geometry alternative `v`, the prediction
before optional encoding is

```
gain * (background[c] + sum(modernCoefficient[i, f, v] * power[f, c]))
```

All RGB powers and background components have explicit finite nonnegative upper
bounds. `gain` is a positive fixed gauge and is never optimized with power. The
same predictions result from multiplying the gauge and dividing powers,
background and their bounds by the same amount. Neither fitted compiler units
nor these relative parameters are called watts, candela, nits or lux.

The constant background is a nuisance term shared by all receivers and fitted
only on training data. The no-local-light baseline fits the **same** nuisance
with the same bounds, loss and receiver weights. There is no free per-receiver
background, fitted albedo, or conversion of residual bounce into direct power.
A background bound hit, zero direct response, or highly correlated fixture
columns is reported explicitly. A bound hit can indicate model mismatch.

## Objective and optimization

Each of the six dominant signed receiver-normal bins receives equal total mass.
Within a normal bin, each represented spatial block receives equal mass; inside
that block, samples are weighted by represented area. All three channels must be
comparable. The objective is the weighted Huber loss of the prediction's distance
from each target interval, divided by its declared sigma. Raw interval MSE and
maximum residual are separate diagnostics. Splitting a sample's represented area
does not increase its vote.

Deterministic bounded coordinate minimization fits per-channel RGB power and
background. Each coordinate uses a bounded coarse scan followed by a local
golden-section bracket. Forward encoding may couple RGB channels; no linear
RGB8 least-squares certificate is claimed. The stopping flag records coordinate
stagnation, not a global-optimum or statistical confidence certificate. Finite
scratch, row, fixture, alternative, ray, arithmetic and sweep limits, and
cancellation apply. The implementation is intended for bounded fixture regions,
not an unrestricted whole-map search.

Spot direction/cone optimization is deliberately finite: the caller supplies a
seed and independently evidenced, bounded static alternatives. XYZ, kind,
dimensions and mounting contract cannot change. At least three training blocks
and a declared minimum training-loss improvement are needed to change the seed.
Ties retain it. Every alternative is independently admitted as a static source;
this does not certify sweeping a physical fixture between poses. There is no
automatic reflector, tube body, emitting mask, stand-off or dimension discovery.
`directionConstrained` means a conditional preference among these supplied
alternatives, not uniquely identified photometry.

## Frozen evaluation

`FitAndValidate` snapshots bounded training and validation rows and rejects
overlapping sample IDs or spatial blocks. All geometry, power and nuisance fits
freeze before validation forward evaluation. Validation uses the same balanced
objective and an optional explicit per-fixture penalty. A predeclared robust-loss
slack (default `1e-4`) retains validation-equivalent improvements; the fewest
fixtures among them is selected. The no-local-light baseline must lie outside
that equivalence slack. The slack is a practical policy, not a probability.

`Experiment` keeps its frozen state private and exposes const diagnostics.
`AuditHeldout` accepts a third disjoint partition and evaluates the frozen
geometry, powers, nuisance and transfer. It cannot select, refit, change the
equivalent set, or turn sources into resolved assets. The CLI opens the held-out
target file only after `FitAndValidate` returns.

The native suite covers nonnegativity including a zero green channel, relative
gauge invariance, robust fitting with a training outlier, a mounted original
panel, bounded spot axis/cone choice, encoded transfer, area/block/normal
balance, fixed geometry, unknown geometry, material/transfer rejection,
partition leakage, memory/work/cancellation limits, power bound hits,
correlated fixtures and frozen held-out behavior.

## Reproduction and evidence

The checked-in receipts are under
[`tools/rend2/aesthetic_fit/results/asr20-v1`](../tools/rend2/aesthetic_fit/results/asr20-v1).
Source and executable hashes freeze before evaluation. The runner verifies every
historical core and canonical type file against the ASR19 protocol, then checks
that inputs and sources remain unchanged. Owned, compiled and stock reports
repeat byte for byte.

The final owned run passes **365 assertions**, and the selected CPU regression
suite passes **55/55 tests**. C++11 syntax checks for all three new translation
units pass with `-Wall -Wextra -Werror -pedantic`.

| Frozen compiled scene | Selected local configuration | Held-out baseline interval MSE | Held-out fitted interval MSE |
|---|---|---:|---:|
| `point_room` | One supplied point | 1764.33 | 9.61e-9 |
| `two_colour` | Two supplied points | 773.66 | 0.11253 |
| `door_occlusion` | One supplied point, original blocker retained | 2001.47 | 0 |

The two-color dual and triple configurations remain validation-equivalent. The
third fixture is not needed for the selected configuration. These are conditional
encoded-output interval errors, not linear irradiance errors. The owned mounted
area witness improves relative held-out MSE from approximately **0.16784** to
**2.19e-6**; the bounded spot witness chooses the supplied axis/cone alternative
and improves approximately **0.18888** to **1.44e-13**.

```powershell
$env:PATH = 'C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;' + $env:PATH
cmake -S tools/lightrecon_test -B build/s06-gcc
cmake --build build/s06-gcc -j 2
ctest --test-dir build/s06-gcc --output-on-failure -j 2 -E '^lightrecon_point_refine$'
python -X utf8 tools/rend2/aesthetic_fit/run.py --out build/asr20-reproduce
```

Use a fresh output directory. The unchanged long-running historical coordinate
refinement test is excluded from this broad check; ASR20 never moves positions.
The existing Shape, Joint, producer, support and storage regressions are included.

For the optional stock audit, add `--stock-archive <original mp/duel4 .jkobs>`.
The runner freezes original surface **1027**, a **512 WU** radius, and a stable
geometry-only same-material receiver selection. Exact UV-clipped footprints plus
a filter gap separate 256 WU blocks at a fixed offset and seed. Original geometry,
RGB samples and full fitted stock positions stay in an ignored private build
directory; public receipts contain summaries and hashes.

The stock region is deliberately retained even if it fails to improve. Its
whole-face luminous mask, original bake transfer, material response and bake-time
model states remain conditional or unknown. The observed audit admits the panel
bound but finds zero direct training response, fits zero local RGB, and retains
the nuisance-only baseline. It supplies no positive stock lamp claim.

The compiled tests use the existing pinned q3map2 BSPs and independently frozen
training/validation/held-out views for `point_room`, `two_colour` and
`door_occlusion`. Only fixture positions enter from the authored benchmark
prior; source RGB and intensity do not enter fitting. A third supplied midpoint
rival in `two_colour` tests redundant-source competition. Single/dual/multiple
errors are retained, including validation-equivalent alternatives. These are
modern appearance fits to compiled output, not newly identified historical
authoring recipes. No GPU consumer, world-shading change or double-lighting
behavior is implemented.
