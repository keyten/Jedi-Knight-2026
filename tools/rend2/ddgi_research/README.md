# DDGI field / geometric-visibility research

[Technical comparison and decision](../../../docs/rend2-ddgi-field-research.md).
This is independent CPU mathematical research, not an RTXGI port, probe-light
reconstructor or renderer integration. Python standard library only; no GPU,
OpenGL, DXR/Vulkan, game or RTXGI installation is needed.

Run from the repository root:

```powershell
python -X utf8 tools/rend2/ddgi_research/probe_weights.py
python -X utf8 -m unittest discover -s tools/rend2/ddgi_research -p 'test_*.py' -v
```

`results.json` is a deterministic small numerical witness including the experiment
source hash. Twelve tests cover moment bounds, nonprobabilistic display remaps,
opaque-wall leaks, all-blocked normalization, missing optical geometry, sky,
door state, relocation/occupancy counterexamples, units and storage arithmetic.
Repeated generation must produce identical results. It is not a GPU output or
physical-light reconstruction accuracy report.

The fixture uses the existing X04 independent triangle query, known synthetic
geometry in metres, and **declared** field samples labelled lux. It does not fit
lights or evaluate a full GI scene. A nine-ray angular bin and two-support scalar
reduction isolate visibility/interpolation effects; they do not simulate the full
SDK's eight-neighbour grid, octahedral hardware filtering, temporal accumulation,
texture quantization or relocation/classification sequence. The all-blocked example
represents missing/clamped support, not a claim about a point inside the convex
hull of two probes separated from it by a single plane.

## Reference source lock

The inspected SDK version is RTXGI-DDGI 1.3 at commit
`f33e496ca31b3f0eec1c4e2cbaa8bb620e337fa6`. `sources.json` pins URLs, hashes and sizes
for 24 selected public documentation/source files (285,535 bytes). Third-party
source stays under ignored `build/` and is not linked into the engine or copied
into this tool. Source line references and algorithm/implementation differences
are documented in the technical comparison.

To verify an existing local download without network access:

```powershell
python -X utf8 tools/rend2/ddgi_research/fetch_reference.py
```

For a fresh source-reading setup, explicitly download the pinned files:

```powershell
python -X utf8 tools/rend2/ddgi_research/fetch_reference.py --download
```

The fetcher verifies hashes before writing; limits file count, response size and
request time; rejects paths outside its output root. It installs nothing and does
not change CMake. Algorithm tests never initiate network work.

## Regression commands actually run

```powershell
python -X utf8 -m unittest discover -s tools/rend2/inverse_gi -p 'test_*.py' -v
python -X utf8 -m unittest discover -s tools/rend2/shadow_footprint -p 'test_*.py' -v
ctest --test-dir build/lightrecon-core-renderers -C Release --output-on-failure
```

Results: 12 new tests, 15 existing inverse-GI tests, 27 X04 tests and 17 CPU CTest
targets passed. CTest uses the existing configured build. No shared production
sources changed; no new SP/MP rebuild or visual/GPU runtime test is claimed here.

Local Python used:
`C:/Users/Keyten/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`
(3.12.14). `cmake/ctest` were available after prepending
`C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin` to `PATH`.

The fixture results justify keeping exact source-fitting rays separate from
approximate field weights. They do not certify any stock JA sample as free,
recover original emitters, supply a sun direction or establish physical scale
for historical LDR. Newly authored code/fixtures follow the repository's
GPL-2.0-or-later license; SDK code and documentation are referenced, not vendored.
