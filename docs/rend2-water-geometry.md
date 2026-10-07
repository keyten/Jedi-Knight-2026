# Render-only water geometry

`r_waterGeometry 1` (latched, default 0) builds a second static mesh at BSP load. It leaves BSP faces, collision and gameplay unchanged. It requires `r_waterSurface 1`; `r_waterWaves 1` enables its displacement. OpenGL 3.2 uses CPU subdivision at map load and a vertex shader at draw time. The CPU does no per-frame mesh work.

## Stock geometry and draw path

The installed stock audit (`rend2-water-body-stock-audit.json`) records modern-water `MST_PLANAR` faces (BSP type 1) and `MST_PATCH` grids (type 2). `t2_rancor` has six separate 4096-unit-area planar pools. `t3_hevil` has nine modern planar surfaces, including a 7-surface lake body; its eight vertical patch waterfalls remain legacy. `yavin1` has four modern planar surfaces and eleven modern patches; its larger bodies span 9 and 17 surfaces. `vjun1` has one large planar slime surface. This path consumes each surface's already-triangulated indices, so neither convexity nor quad topology is assumed. `SF_TRIANGLES` is supported for overrides and future maps.

Classification and body grouping happen after BSP surfaces load. `R_CreateWorldVBOs` packs positions, normals and primary UVs for ordinary world drawing. The geometry builder runs after environment profile overrides and world VBO creation, before leaf merging. It creates one extra VBO/IBO per eligible body and per-surface index ranges. Only the modern-water draw selects these buffers. The first textured stage's `tcMod` continues to act on the original interpolated primary UVs. Portal and sky surfaces never receive a water mesh. Eligible water surfaces are excluded from leaf merging while this option is on, preserving their ranges and body IDs.

The existing SP cached BSP handoff and optional `.tspace` sidecar supply source map data and tangent space. Neither caches GPU water geometry. This mesh is derived from the current classified surfaces and body overrides on every map load, so it cannot become stale when water CVARs or `env.json` profiles change.

## Tessellation, seams, and boundaries

The longest original triangle edge in a body sets a common dyadic subdivision level. An edge midpoint cache shares vertices between triangles of the same BSP surface. Matching edge segments on touching surfaces therefore get identical positions and wave phases. Where different original BSP surfaces have unmatched edge partitioning, downward 64-unit render skirts cover a possible vertical gap. They also cover gaps exposed against pool walls when the crest rises. Skirt feet stay at the base position; tops move with the wave. This overlap may be visible from underneath an open shore. It is a temporary border treatment until a shoreline-specific boundary model exists.

The vertex shader evaluates the same deterministic analytic waves as the fragment shader. The original world position is retained as `basePosition`; the displaced world position drives projection, depth writing, refraction, view vectors, SSR, and Snell tests. The fragment re-evaluates the wave at `basePosition` for its analytic slope. The body plane remains available for grouping and planar reflection capture. Bounds expand conservatively for vertical motion, horizontal drift and skirts; the flat face plane is no longer used to cull these meshes.

## Budgets and diagnostics

- `r_waterGeometryEdge 64`: target maximum triangle edge length, clamped to 16–512 world units.
- `r_waterGeometryBodyVerts 250000`: per-body vertex cap.
- `r_waterGeometryMapVerts 1000000`: whole-map vertex cap.
- `r_waterGeometryDebug`: 1 wireframe, 2 displacement magnitude, 3 original triangulation with the same wave, 4 body colors with skirts in red, 5 expanded surface culling bounds.
- `r_waterGeometryInfo`: per-body vertices, triangles (including skirts), subdivision level, build time and estimated VBO/IBO bytes.

Budget pressure lowers the body-wide level; if even the source mesh exceeds the remaining budget, the body uses its original BSP draw and logs the fallback. Level is fixed for a map load, so there are no distance-dependent LOD seams. With `r_waterGeometry 0`, no extra mesh is uploaded and the regular modern-water/legacy paths continue.

## Verification

Use a grazing camera angle with `r_waterSurface 1; r_waterWaves 1; r_waterGeometry 1`, compare debug mode 3 to 0 and inspect mode 1. Keep the camera above the highest crest when comparing silhouettes; placing it inside a crest exercises underwater compositing instead. The principal stock cases are the huge `t3_hevil` lake, six `t2_rancor` pools, shallow `yavin1` patches, `vjun1` slime, and a joined multi-surface body such as `t3_hevil` body 9. `r_waterGeometryInfo` exposes fallbacks on each map.

The OpenGL 3.2 shader test verifies that displacement changes rasterized water depth. The SP runtime test loaded all four maps with the mesh and captured grazing, wireframe, magnitude, original-mesh and seam views. On `t3_hevil`, body 9 built 73,668 vertices and 144,640 triangles at level 6 (328 ms and 17.6 MB for the entire map); its wireframe and original-mesh comparison show the added geometry and moving surface. `t2_rancor` also passed with `r_waterGeometry 0`. SP and MP rend2 DLLs compiled with MSVC and MinGW. The stock audit establishes topology and body membership; unusual custom-map seams still need a visual check because unmatched source edge partitions use skirts rather than body-wide remeshing.
