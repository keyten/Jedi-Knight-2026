# X09 disjoint passive support

The production CPU implementation is `lightrecon/transport_mesh.{h,cpp}`. It takes
source triangles/material-side geometry, never RGB or partition labels. `Map`
intersects each original receiver footprint with the resulting support. Joint
fitting evaluates direct illumination on the original receiver; only indirect
illumination uses area-normalized transport weights.

No game, GL, q3map2 rebuild, licensed asset distribution, or external numerical
library is required for the controlled tests.

```powershell
$env:PATH='C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin;'+$env:PATH
cmake -S tools/lightrecon_test -B build/s06-gcc -DCMAKE_BUILD_TYPE=Release
cmake --build build/s06-gcc -j 2
New-Item -ItemType Directory -Force build/x09 | Out-Null
build/s06-gcc/lightrecon_transport_mesh_test.exe build/x09/controlled.json
build/s06-gcc/lightrecon_transport_mesh_test.exe build/x09/controlled-repeat.json
ctest --test-dir build/s06-gcc --output-on-failure -j 2
python -X utf8 -m unittest discover -s tools/rend2/transport_topology -p 'test_*.py' -v
python -X utf8 tools/rend2/transport_topology/verify_results.py
```

The full CTest command includes the separately expensive point-position suite.
The X09 recorded run excludes that unchanged suite; the report gives exact checks
actually executed. Public verification checks frozen results and hashes; it does
not rerun licensed stock tests or certify arbitrary later source changes.

For the private all-pages archives from the original stock audit:

```powershell
$private="$env:TEMP/jk-lightrecon-all-stock-20261031"
# Reproduce the exact original failure with the preserved original adapter.
python -X utf8 tools/rend2/all_stock/run.py --private $private --tool "$private/binaries/map-solver-v2.exe" --revision x09-before --maps mp/siege_desert t2_dpred t3_bounty
# Use new revision names if any of these already exist; attempts are retained.
python -X utf8 tools/rend2/transport_topology/run.py --private $private --revision x09-final-c
python -X utf8 tools/rend2/transport_topology/run.py --private $private --revision x09-final-d
# Optional full producer continuation, explicitly a conditional DIRECT-ONLY model.
python -X utf8 tools/rend2/transport_topology/run.py --private $private --revision x09-producer --producer
python -X utf8 tools/rend2/transport_topology/collect.py --private $private --revision x09-final-c --repeat-revision x09-final-d --producer-revision x09-producer --controlled build/x09/controlled.json --repeat build/x09/controlled-repeat.json
```

Native `lightrecon_map_solver archive.jkobs output-directory BSP-SHA transport-only`
also runs directly. The destination must exist. Without `transport-only`, the
existing producers and Joint solver run after geometry preparation. The wrapper
limits the exact owned child to 600 seconds / 1 GiB working set and records actual
Windows peak RSS. It never kills another agent's processes. The geometry-only
mesh uses the previously defined most-normal-diverse 512-WU source-geometry cell,
128-WU maximum edges, and additional 256-WU boxes around each old coincident-node
contact (up to 16). These scopes are fixed by geometry, not target brightness.
Outside support is unavailable GI. A source search can independently run under a
whole-model direct-only conditional hypothesis; it is not evidence of zero bounce.

Private outputs include:

- `transport-overlap-witnesses.tsv`: original sample, page/texel/surface IDs,
  orientation/contact classification, overlap area and full triangle coordinates.
- `transport-singular-geometry.tsv`: all mapped and original source triangles
  for the old coincident-node contacts, including numerically noncoplanar cases.
- `transport-contact-regressions.json`: reconstruction at the actual original
  singular contacts, even if outside the main geometry-selected ROI.
- `transport.obj`, `transport-patches.tsv`: disjoint retained geometry, area,
  side normals and provenance counts.
- `observation-transport.tsv`: many-to-many footprint intersections, unavailable
  and partial mappings; contains no RGB targets.
- `transport.json`: raw and supported area, exclusions, passivity, local work
  exhaustion and stage durations.
- `native-resources.json`: actual process RSS/time, command and exit code.

Only aggregate results, hashes and source/luxel witness identities are committed.
The stock historical profile remains `UnknownProfile`; no physical source truth
or stock GI completeness is asserted.
