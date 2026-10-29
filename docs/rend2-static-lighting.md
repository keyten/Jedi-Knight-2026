# Rend2 static lighting

Original BSP lightmaps and the legacy entity lightgrid remain the default lighting
inputs. The failed lightgrid-gradient source fitter, promotion, artificial light
portals, directional fog moments and grid-driven LTC calibration have been removed.
See the [dependency inventory and removal validation](rend2-grid-reconstruction-removal.md).

Independent rendering remains: PBR map calibration (`tr_pbr_contract.cpp`),
entity sun visibility/calibration (`tr_light.cpp`), original entity grid textures
and optional BSP-direction L1 probes (`tr_bsp.cpp`), baked fog baseline/sun split
(`tr_bakedlighting.cpp`), authored/automatic LTC and dynamic lights.
None requires reconstruction controls.

The replacement is a separate [canonical asset](rend2-reconstructed-lighting.md),
[read-only API](rend2-reconstructed-lighting-api.md),
[CPU core](rend2-reconstruction-core.md),
[map lifecycle](rend2-reconstruction-lifecycle.md),
[independent opt-in controls](rend2-reconstruction-controls.md) and
[offline observation datasets](rend2-offline-observation-datasets.md).
CPU diagnostics now include [fixed-geometry RGB fitting](rend2-amplitude-fitting-s06.md)
and [geometry-constrained point XYZ refinement](rend2-point-position-refinement-s07.md).
There is no complete map-reconstruction solver or production consumer. Missing
capabilities remain explicitly unavailable; enabling use never invents a light.

The [old design](rend2-static-lighting-obsolete.md) and
[18 SP / 2 MP map investigation](rend2-static-lighting-validation.md) are retained
as historical technical evidence. They do not document supported cvars or APIs.
