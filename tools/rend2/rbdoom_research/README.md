# RBDOOM lighting-field research controls

[Technical investigation and JA requirements](../../../docs/rend2-rbdoom-field-research.md).
Independent mathematical experiments, not a RBDOOM port, game bake, inverse light
estimator or renderer integration. Python 3.12 standard library only; the numerical
quadrature comes from this repository's existing `shadow_footprint/integrate.py`.
No game, GPU, assets, new packages or third-party implementation are required.

Run from the repository root:

```powershell
python -X utf8 tools/rend2/rbdoom_research/field_basis.py
python -X utf8 -m unittest discover -s tools/rend2/rbdoom_research -p 'test_*.py' -v
```

`results.json` records the experiment source hash, conventions and deterministic
numerical witness. Generation uses fixed quadrature nodes and held-out angular
directions, with no random state. Repeated generation in the recorded environment
is identical; floating-point/library changes can affect last-bit results.

The 13 tests cover SH counts and signs, orthonormality, diffuse convolution,
constant-radiance E/pi normalization, directional error, ringing, independent
reference convergence, SG normalization, FP16 coefficient error, valid black
samples, invalid inputs and repeated results. The experiment compares:

- SH maximum degrees 1, 2, 3, 4 and 8, separately for incoming radiance and diffuse
  irradiance of three coloured SG lobes plus a constant.
- An angular delta's diffuse response, whose pointwise radiance RMS is undefined.
- A known-SG oracle capacity control, **not** a fitted unknown-source benchmark.
- Explicit sample validity versus rejecting dark RGB during interpolation.

The independent SG irradiance reference integrates azimuth analytically and the
remaining hemisphere coordinate numerically. It is not a RBDOOM shader replay.
RGB values are synthetic relative-linear quantities, without JA physical-scale
calibration. FP16 tests round coefficients, not the engine's octahedral texture or
BC6H compression. L8 is a mathematical comparison beyond the pinned engine's
maximum supported SH degree 4.

## Pinned source reading

RBDOOM commit: `ea29c006e84fedcc0e7c173c383a087ce0a8c0d5` (2026-08-07).
`sources.json` lists 43 public source/documentation files, 1,398,197 bytes,
with URLs, SHA-256 hashes and Git blob identifiers checked against the pinned tree.
The original license and exceptions are included in that source-reading manifest;
no third-party code/assets are redistributed or linked into JA.

Verify an existing local download without network access:

```powershell
python -X utf8 tools/rend2/rbdoom_research/fetch_reference.py
```

For a fresh setup, explicitly download the pinned reference files:

```powershell
python -X utf8 tools/rend2/rbdoom_research/fetch_reference.py --download
```

The fetcher writes under ignored `build/rbdoom-research/reference/` by default. It
checks SHA-256 and Git blob hashes before writing, limits count/response size/time,
and rejects paths outside its output root. It installs nothing. Numerical tests
do not access the network or downloaded source.

## Regression commands actually run

```powershell
python -X utf8 -m unittest discover -s tools/rend2/ddgi_research -p 'test_*.py' -v
ctest --test-dir build/lightrecon-core-renderers -C Release --output-on-failure
```

Results: 13 new tests, 12 existing DDGI tests and all 17 existing CPU CTest targets
passed. CTest uses the existing configured build. No production sources changed;
no new SP/MP compilation, RBDOOM build, game bake or GPU/runtime validation is
claimed. The report separately identifies unverified capture orientation, atlas
seams, codec errors and actual bake memory consumption.

Local Python used:
`C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`
(3.12.14). `ctest` was available after prepending
`C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin` to `PATH`.

Newly authored code/fixtures follow the repository's GPL-2.0-or-later license.
The mathematical controls justify field/quantity requirements; they do not
recover a physical emitter, verify a stock bake profile or implement a consumer.
