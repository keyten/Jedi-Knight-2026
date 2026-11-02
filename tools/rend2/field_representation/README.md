# P03 angular field representation comparison

Independent C++11 CPU SH/SG mathematics with an owned, deterministic angular
benchmark. No game, GPU, stock observations, Python numerical packages or external
SDK is required. See [the decision report](../../../docs/rend2-directional-fields-p03.md).

The shared `Core::Angular` module exposes `Project`, `FitSG`, `Evaluate`,
`Encode` and `Decode`. It does not install a `Snapshot` field evaluator, change
the canonical schema, generate a stock field or modify source fitting. Existing
`SampleIndirect` / `SampleIncomingRadiance` requests remain representation neutral.
All units, calibration and root/path provenance belong on the existing owning
canonical field/contribution, never on a GPU texture or a guessed RGB gain.

`Field.quantity` distinguishes radiance coefficients from already convolved
irradiance. Irradiance cannot be inverted into incoming radiance. Raw signed SH
reconstruction and a negative flag are returned without a hidden clamp. SG lobes
have unit spherical integral; their nonnegative RGB parameters are integrated
radiance, not peak radiance or candela. The experimental angular block is bounded,
versioned and checked; it is not registered as a production Dataset codec.

## Reproduce

Use the existing compiler/CMake on PATH. Commands below were run with GCC 16.2,
CMake 4.4 and the existing Python 3.12 standard library. Preserve previous run
directories; benchmark and freeze refuse to overwrite them.

```powershell
$env:PATH='C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;'+$env:PATH
cmake -S tools/lightrecon_test -B build/s06-gcc -DCMAKE_BUILD_TYPE=Release
cmake --build build/s06-gcc --parallel 2
ctest --test-dir build/s06-gcc --output-on-failure -j2 --output-log "${PWD}/build/p03-ctest.log"
python -X utf8 tools/rend2/field_representation/benchmark.py --tool build/s06-gcc/lightrecon_field_basis_tool.exe --test-tool build/s06-gcc/lightrecon_field_basis_test.exe --output build/p03-comparison-final
python -X utf8 tools/rend2/field_representation/octahedral.py --output build/p03-octahedral.json
python -X utf8 -m unittest discover -s tools/rend2/field_representation -p 'test_*.py' -v *> build/p03-python-tests.log
python -X utf8 -m unittest discover -s tools/rend2/ddgi_research -p 'test_*.py' -v *> build/p03-ddgi-tests.log
python -X utf8 -m unittest discover -s tools/rend2/rbdoom_research -p 'test_*.py' -v *> build/p03-rbdoom-tests.log
python -X utf8 tools/rend2/all_stock/architecture.py > build/p03-architecture.json
$env:CL='/MP2'
cmake --build build/s13-renderers --config Release --target rdsp-rend2_x86_64 rd-rend2_x86_64 --parallel 2 *> build/p03-renderers.log
python -X utf8 tools/rend2/field_representation/freeze.py --run build/p03-comparison-final --output tools/rend2/field_representation/results/v1
python -X utf8 tools/rend2/field_representation/freeze.py --verify tools/rend2/field_representation/results/v1
```

Local Python used:
`C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`.
SP/MP use the existing MSVC 2019 Release build tree. No shader baseline is changed.

## Witnesses

- `quality.json` / `quality.csv`: 172 rows, 18 scenes, raw unclamped radiance and
  irradiance metrics, quantization, work and fitted SG parameters.
- `performance.json`: two process time/RSS measurements and per-operation CPU
  timings, separated from deterministic numerical output.
- `spatial.json`: independent segment visibility support through a wall/doorway,
  unknown optics, unavailable support. Angular order cannot repair spatial leaks.
- `sg-recovery.json`: evaluation-only matching to the old three-lobe truth;
  generating axes never enter `FitSG`.
- `octahedral.json`: FP32/FP16 E-only atlas capacity, virtual seams and interpolation;
  exact diffuse texel values are supplied, not estimated with the SH/SG training budget.
- `checks.json`, `architecture.json`, `manifest.json`: tests/builds, renderer
  independence, source/executable/receipt hashes.

The main SH/SG comparison uses the same 3,200 training directions and 8,192
independent test directions. A separate SH projection sensitivity control uses
12,800 training directions; it is not presented as a matched-budget SG comparison.
Reference irradiance uses analytic polygon projection or independent converged
source/hemisphere integration, rather than a low-order field as truth. A delta's
pointwise radiance is `null` / `NotComparable`.

Explicit-source hybrid results assume the declared geometry/function and
amplitudes are already supplied correctly. They are representation capacity
controls, not new inverse source recovery. Root/path addition safeguards reuse
P06 `Attribution::CanAdd`, tested with legal direct+bounce and illegal total+direct
or unknown membership. Reconstructed ambiguous sources remain conditional.

CPU query times use the generic validation/recurrence implementation, not SIMD or
GPU polynomials. Eight-neighbour tests measure eight evaluations and value
blending of identical in-cache supports. They do not measure DRAM throughput,
spatial cache misses or GPU execution. SG axes/widths must not be interpolated as
linear SH coefficients. No BC6H/device-compression accuracy is asserted.
