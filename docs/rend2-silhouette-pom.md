# Rend2: silhouette parallax occlusion mapping

Ordinary POM (`r_parallaxMapping 1`) shifts texture coordinates inside the rasterized base polygon. A fragment
shader only runs where the geometry is drawn, so ordinary POM can never move the outline of a surface, and the depth
buffer keeps the flat base. **Silhouette POM** (SPOM) is a separate opt-in mode for materials whose displacement
should show at their edges (broken stone edges, chunky panels, rocks, rubble): the displaced height field changes
the visible contour and writes its own depth, which every depth-based effect then uses.

Code: `shared/rd-rend2/tr_pom_silhouette.cpp` (shell builder, front end decisions, uniforms, info command),
`glsl/pom_silhouette.glsl` (shared fragment library: trace, footprint exit, depth), `glsl/pom_silhouette_depth.glsl`
(depth prepass with velocity / sun cascades), silhouette variants of `lightall.glsl` and `fogpass.glsl`
(`USE_SILHOUETTE_POM`), surface types `SF_POM_SHELL` / `SF_POM_FADEBASE` (`tr_surface.cpp`), hooks in `tr_bsp.cpp`,
`tr_world.cpp`, `tr_shade.cpp`, `tr_backend.cpp`, `tr_glsl.cpp`. Test materials: `tools/rend2/make_spom_test.py`.

Off by default. With `r_pomSilhouette 0` no shell is built, no program is compiled, nothing is drawn differently:
the preprocessed lightall / fogpass of every existing permutation are token-identical to the previous ones (checked for
34 permutations), ordinary POM is exact.

## Background: established techniques

| technique | idea | silhouettes | why not (as is) here |
|---|---|---|---|
| Parallax / steep parallax / POM (Kaneko 2001, McGuire & McGuire 2005, Tatarchuk 2006) | offset texture coordinates by a height field ray march inside the polygon | no | this is the ordinary Rend2 POM |
| Relief mapping (Policarpo, Oliveira, Comba 2005) | linear + binary search of the height field | no; with per-vertex curvature (Oliveira & Policarpo 2005) approximate silhouettes of curved meshes by discarding rays leaving [0,1] | curvature fitting is per-mesh work and approximate at edges |
| Shell / prism methods: GDM (Wang et al. 2004), Hirche et al. 2004, prism POM (Dachsbacher & Tatarchuk 2007) | extrude every triangle into a prism covering the displaced volume, ray cast inside it | yes | per-triangle prisms need a geometry shader or tetrahedral splits; internal prism faces cost overdraw everywhere |
| Shell maps (Porumbescu et al. 2005), curved shell mapping (Jeschke et al. 2007) | a curved offset shell with a texture space mapping | yes, also on curved meshes | complex, mainly for curved surfaces |
| Engine "silhouette POM" (e.g. CryEngine 3 SPOM) | displacement volume raster for silhouettes on selected materials | yes | only the capability is taken as reference, not an implementation |

The Rend2 solution is a CPU built displacement shell per **planar group** (not per triangle), with the ray bounded
analytically by the group footprint. It keeps GL 3.2 / GLSL 150: no tessellation, no geometry shader (the program
builder has no geometry stage), no conservative rasterization extension.

## Architecture

### Planar groups and the shell (map load)

For every world / brush model surface whose shader has `pomSilhouette`, at the end of `R_CreateWorldVBOs` (the packed
vertices already have their MikkTSpace tangents):

1. positions are welded on a 1/8 unit grid and edges are matched;
2. **groups** are grown from connected triangles that are coplanar (normal dot > 0.9999, plane distance < 0.05), have
   the same winding orientation, the same affine texture (and lightmap) mapping and the same coordinates at both ends
   of the shared edge: inside a group texture space is one affine map of the plane, a ray can move through it freely.
   A group is capped at 48 boundary edges (a fragment tests them all), a bigger region is split;
3. the **top cap** is the group moved to the top of the displaced volume, `bias * D` above the base plane;
4. **side walls** go down to `(bias - 1) * D` along the **boundary edges only**: the surface border, UV / lightmap
   seams, creases between groups and splits. No wall on internal edges, no bottom cap (front sided materials are not
   seen from below).

`D` (world units per unit of depth) matches ordinary POM: the ray direction in texture space is the ordinary POM
offset direction, `D = parallaxDepth * sqrt(cx |dP/du| * cy |dP/dv|)` with the ordinary aspect correction `cx, cy` of
non square height maps (exact for texel-isotropic mappings, as ordinary POM assumes).

Each shell vertex is a `packedVertex_t` (same layout as the world VBO, so the lightall programs read it unchanged)
plus a `vec3` in the `attr_Position2` slot (unused by world surfaces): `s0` (depth of the vertex in the volume,
0 top, 1 bottom), `D`, and `group header texel * 2 + 1 for wall bottom vertices`. Texture / lightmap coordinates,
colour and light direction are those of the **foot point** on the base plane; they are affine on a plane, so the
perspective correct interpolation along a wall is exact. Normal and tangent are the flat frame of the group. The wall
flag is read `flat` from the provoking (last) vertex, which the builder makes a bottom vertex.

The **group buffer texture** (RGBA32F, one per world, unit 16) holds per group a header (first edge, edge count),
the affine map texture coordinates → lightmap coordinates, then the boundary edges in texture space oriented with
the group on their left.

Leaf / node bounds, brush model bounds and the surface cull bounds are widened to the shell so frustum culling keeps
a surface whose displaced edge reaches into view. Shell surfaces are never merged (`r_mergeLeafSurfaces`).

### The fragment (all passes)

A shell fragment knows where its camera ray enters the volume (foot point coordinates, `s0`). `PomSilhouetteTrace`
(`pom_silhouette.glsl`):

1. ray direction in `(u, v, s)` per world unit from the group frame (same formula as ordinary POM);
2. interval: until the ray leaves the slab `0 ≤ s ≤ 1` or crosses a boundary edge of the group from inside to
   outside (the wall it entered through is crossed inwards and does not count);
3. adaptive linear search, `r_pomSilhouetteSteps` at the normal up to `r_pomSilhouetteMaxSteps` at grazing angles,
   then `r_pomSilhouetteBinarySteps` of binary refinement and a secant step;
4. the hit is the first point where the ray goes from above the height field to below it. A top cap touching the
   height field is a hit at the entry. A wall entry below the height field is inside the solid (its side is not a
   displaced surface, the neighbouring geometry owns it): the search starts where the ray leaves the solid again;
5. no hit: `discard`. A hit gives the displaced texture coordinates, the lightmap coordinates of the hit (group
   affine map), the **virtual world position** and `gl_FragDepth`.

The height convention is the ordinary one: the red channel of the normalHeightMap image is the flipped height
(`tr_image.cpp`), i.e. the depth below the top of the volume; the base plane lies at `s = parallaxBias`.

Internal walls (a split group, two SPOM surfaces touching) are harmless: each fragment traces only its own group,
the depth test keeps the nearest hit.

### Passes and depth

| pass | program | depth |
|---|---|---|
| depth prepass (velocity FBO) | `pom_silhouette_velocity`: trace, exact hit depth, motion vector of the virtual point (previous camera and entity transform) | writes the hit depth |
| depth prepass (no velocity FBO) / sun cascades | `pom_silhouette_depth` (orthographic: ray = light direction) | writes the hit depth; cascades push the caster away from the sun by `max(0.25, 2 r_shadowDepthBias)` |
| colour | `lightall_…_SPOM` (lightmap / vertex × spec-gloss × cloth, the same defines as the lightall permutation) | LEQUAL against the prepass, no depth write, hit pulled 0.02 units (1e-4 × distance) towards the camera so separately compiled programs cannot fail the test; without a prepass it writes the exact hit depth |
| fog (legacy fog volumes) | `fogpass_SPOM` (+ fallback global fog variant) | as the colour pass instead of `GLS_DEPTHFUNC_EQUAL`, fog of the virtual point |

The depth prepass and the colour pass share the trace function and the uniforms, so they find the same hit.

In the colour pass the silhouette code only replaces the inputs of the **common** lighting: texture coordinates,
lightmap coordinates, the view vector / world position (virtual point), the vertex normal (base frame; walls face
other directions). Everything after it is the ordinary lightall path: legacy and Forward+ dynamic lights (cluster of
the virtual point), sun shadow of the virtual point, cubemap IBL, diffuse IBL, SSR / SSGI outputs, emission, AO. The
`DynamicLightReceiverVisibility` hook receives the virtual position. Material maps are sampled with the gradients
of the foot point (the hit coordinate jumps at the displaced silhouette).

### Screen-space features

Nothing special per effect: they read the depth buffer and the material outputs of lightall.

| feature | what it sees |
|---|---|
| GTAO / contact shadows (`tr_ao.cpp`) | the prepass depth = displaced surface; GTAO reconstructs normals from it; contact shadows are not applied to shell pixels (see Shadows) |
| SSR (`tr_ssr.cpp`) | displaced depth; normal / roughness of the virtual material point (`out_SSRNormal`) |
| SSGI (`tr_ssgi.cpp`) | albedo / radiance of the virtual point, view depth of the virtual point |
| froxel fog (`r_volumetricFog 2`) | the opaque composite uses the displaced depth; legacy fog volumes use `fogpass_SPOM` |
| motion blur / SMAA T2x | velocity of the virtual point (world geometry and brush movers) |
| particles, soft sprites, other geometry | test against the displaced depth |

### Crossfade and distance

Shells are used up to `r_pomSilhouetteDistance` (or `pomSilhouetteDistance` of the shader if smaller). Surfaces farther
away keep ordinary POM. In the band `[distance - r_pomSilhouetteFade, distance]` both the shell and the base surface
are drawn with complementary 4x4 ordered dither masks (the same threshold in every pass): every pixel is drawn by
exactly one of them, no pop. With SMAA T2x / motion blur the dither is mostly hidden; without it the band shows a
stable pattern. The decision uses the distance to the shell bounds, the dither the distance of each pixel.

There is no normal-mapping-only distance level: the existing POM has no distance policy either.

### Shadows

- **Sun cascades (stage 2, done)**: with `r_pomSilhouetteShadows 1` (default) the shell is drawn into the cascades
  with the orthographic trace (sun direction, not the camera). Rend2 renders depth-shadow views with flipped culling
  (back faces); the shell is drawn with **unflipped** culling (its sun facing side is the displaced surface), the
  base surface stays an ordinary caster for its parts facing away from the sun.
  The caster is pushed `max(0.25, 2 * r_shadowDepthBias)` units away from the sun against acne. Shells are drawn in
  the cascades up to the same distance from the *camera*.
- **Receivers**: a shell pixel does not look the shadow map up at its virtual hit but where the ray from the hit
  towards the sun leaves the top of the volume (`PomShadowLookupPosition`: `hit + L * s * D / max(N.L, 0.2)`).
  Looking up at the hit made the displaced surface its own caster: at cascade resolution (a texel spans several
  units, the height field varies much faster) that is acne, and PCSS found the surface itself as the blocker, so the
  penumbra collapsed into hard black shadows. Seen from the sun every caster of the own shell lies at or behind the
  top plane, so the lookup only finds real occluders and keeps the same soft penumbra as ordinary POM, which looks up
  its flat base. The shell still casts onto other surfaces.
- **Relief shadowing its own surface**: POM self shadow (`r_pomSelfShadow`, `GetPomSelfShadow`) with the shell hit
  (`g_pom` is filled from it), independent of the shadow map resolution.
- **Contact shadows** (`r_contactShadows`) march the displaced depth buffer and turn the grooves hard black; they
  are not applied to shell pixels unless `r_pomSilhouetteContactShadows 1`. Crossfade base pixels keep them.
- **Point light cube shadows and pshadows**: base geometry (documented limitation).

## Surfaces

Supported: opaque world BSP `SF_FACE` and `SF_TRIANGLES` (triangle soups made of planar pieces with hard edges),
brush models (movers: the shell is in model space, velocity from the entity transforms).

Fallback to ordinary POM (the reason is printed once per shader with `developer 1`, counted in `r_pomSilhouetteInfo`):

- `SF_GRID` patches and triangle soups with smooth normals across creases ("curved"): the planar shell would open
  cracks along the creases, the planar assumption is not applied silently;
- shader: not front sided, not opaque, blended / alpha tested, more than one stage after collapsing (glow stages,
  lightmap styles), not a lightall stage, grid lit, tcGen / tcMod, deformVertexes, polygonOffset, refractive,
  sky / portal, no normalHeightMap, `parallaxDepth 0`, animated normal map;
- degenerate texture mapping, displacement out of range, group buffer too large, renderer started without
  `r_pomSilhouette` (latched: needs `vid_restart`), fewer than 17 fragment texture units.

Not supported: MD3 / Ghoul2 models (never shells), `r_forceParallaxBias` (the shell uses the material bias), the
camera inside the shell volume (thin volumes, e.g. pressed against a displaced wall), pshadows on shells.

## Material syntax

```
textures/test/rock_edge
{
    pomSilhouette                // opt-in, nothing is automatic
    pomSilhouetteDistance 384       // optional, shell range (min with r_pomSilhouetteDistance)
    pomSilhouetteSteps 32           // optional, linear steps at grazing angles (replaces r_pomSilhouetteMaxSteps)
    {
        map textures/test/rock_edge
        normalHeightMap textures/test/rock_edge_nh
        parallaxDepth 0.06       // the ordinary POM displacement, reused
        parallaxBias 0.5         // 0: recessed only, 1: protruding only, 0.5: both
        rgbGen identity
    }
    {
        map $lightmap
        blendFunc GL_DST_COLOR GL_ZERO
    }
}
```

### Automatic mode: materials that already have ordinary POM

Silhouette POM needs a height field. In the installed packs it exists as **`_nh` images** (normal in RGB, height in
alpha; 467 of them in `assets8_pbr1/2`), which rend2 already finds next to the diffuse image and uses for ordinary POM
(`CollapseStagesToLightall`, `parallaxDepth = r_baseParallax`). The 792 `_n` images are normal maps without height:
no silhouette can come from them. Two `_h` files exist, rend2 does not read that suffix.

```
r_autoPOMSilhouette 1|0                    every material with ordinary POM (default 0)
r_autoPOMSilhouette <shader> 1|0|default   one shader; a trailing * is a prefix: textures/bespin/*
r_autoPOMSilhouette <shader>               state of the matching shaders of the current map
r_autoPOMSilhouette list                   shaders with a shell on this map + POM materials that kept ordinary POM (reason)
r_autoPOMSilhouette clear                  remove all per-shader switches
```

- Precedence per shader: switch `0` → off (also for `pomSilhouette` shaders), switch `1` → on, keyword → on, else
  `r_autoPOMSilhouette`. Among switches an exact name beats a prefix, a longer prefix a shorter one.
- The global state is the archived cvar `r_autoPOMSilhouetteMode` (set by the command). The per-shader switches are
  written to `pomsilhouette.cfg` in the mod folder by the command and read on first use.
- Everything switches live: with `r_pomSilhouette 1` the shells are built at map load for **every** eligible POM
  surface (keyword or `_nh`), and the front end decides per frame. The price: shell memory for all of them (printed at
  load, `r_pomSilhouetteInfo`), and these surfaces are not leaf-merged even while unused (their ordinary draws still
  merge into multi-draws of the same VBO).
- Needs `r_pomSilhouette 1` (latched): otherwise `r_autoPOMSilhouette requires r_pomSilhouette 1` is printed once,
  nothing is enabled on the user's behalf. `r_parallaxMapping 1` as for every POM.
- Automatic materials use the existing POM data: `parallaxDepth` of the stage (`r_baseParallax` for discovered `_nh`)
  and its `parallaxBias`, which is 0 unless the shader sets it: the volume lies below the base plane, so the contour
  is carved (eroded edges, notches), it does not grow outwards. A shader with an explicit `parallaxBias` (or the
  keyword path) is needed for protruding displacement.
- Fallback reasons of automatic candidates (more than one stage after collapsing, curved, tcMod, …) are only counted
  (`list`, `r_pomSilhouetteInfo`); they are printed for keyword shaders and for shaders switched on by the command.

There is no separate depth scale: `parallaxDepth` / `parallaxBias` define the displaced volume for both modes. The
height range is the known `[0, 1]` of the map (no offline analysis): the volume is `D` thick, `bias * D` above the base
plane.

**Where to use it**: broken stone edges, chunky panels, rocks, rubble, strongly displaced props, i.e. where the
outline matters. **Not** on ordinary floors, subtle relief, scratches, smooth panels or big flat walls: the shell
costs a trace per shell pixel (walls included) and there is no automatic enabling.

## Cvars and commands

| cvar | default | |
|---|---|---|
| `r_pomSilhouette` | 0 | archive, latched. Needs `r_parallaxMapping 1` (warning `Silhouette POM requires r_parallaxMapping 1`, never auto-enabled) and `r_normalMapping 1` |
| `r_pomSilhouetteDistance` | 512 | shell range; 0 = ordinary POM everywhere (runtime A/B) |
| `r_pomSilhouetteFade` | 96 | crossfade band width (0 = hard switch) |
| `r_pomSilhouetteSteps` | 12 | linear steps along the normal |
| `r_pomSilhouetteMaxSteps` | 48 | linear steps at grazing angles |
| `r_pomSilhouetteBinarySteps` | 6 | binary refinement |
| `r_pomSilhouetteViewDependence` | 1 | how fast the steps grow towards grazing angles (0 = constant) |
| `r_pomSilhouetteShadows` | 1 | shells in the sun cascades |
| `r_pomSilhouetteContactShadows` | 0 | screen-space sun contact shadows on shell pixels |
| `r_pomSilhouetteDebug` | 0 | cheat, see below |
| `r_pomSilhouetteInfo` | command | shells, groups, walls, memory, fallbacks, last frame counters |
| `r_autoPOMSilhouette` | command | automatic mode and per-shader switches, see "Automatic mode" (`r_autoPOMSilhouetteMode`, archive, 0) |

`r_speeds 7` adds a line: shells drawn (all passes), shell triangles, crossfade base surfaces.

### Debug views (`r_pomSilhouetteDebug`)

| | view |
|---|---|
| 1 | translucent shell over the scene: the displaced surface shows through where the ray hit, the background where it missed |
| 2 | shell wireframe (no depth test) |
| 3 | original mesh wireframe of the shelled surfaces |
| 4 | top cap green, walls orange, base surfaces in the crossfade band blue |
| 5 | pixels drawn through a boundary wall (red) |
| 6 | shell pixels the ray missed, magenta (discarded otherwise) |
| 7 | virtual hit distance, 32 unit bands |
| 8 | height samples per ray (heat) |
| 9 | split screen: ordinary POM left, silhouette POM right |
| 10 | linear view depth of the virtual point (2048 units) |
| 11 | material normal |

Views 4–8, 10, 11 are written unlit (tone mapping bypassed). 1–3 are world surfaces only.

## Memory and performance

Per shelled surface: top vertices (one per vertex and group) + one bottom vertex per boundary vertex, 140 bytes each
(`packedVertex_t` 128 + 12); triangles: the surface + 2 per boundary edge; group buffer: `(3 + edges) * 16` bytes per
group. A 64 × 64 unit face with 2 triangles: 8 vertices (1.1 KB), 10 triangles, 112 bytes of group data. Printed at map
load (`...silhouette POM: N surfaces, groups, walls, verts / tris, KB`) and by `r_pomSilhouetteInfo`. Shell data only
exists for opt-in materials; the world VBOs are unchanged.

Cost is per shell **pixel**: one trace of `steps + binary` height samples (debug 8), plus the footprint test (up to 48
edges, usually 3–8). `gl_FragDepth` disables early depth rejection for shell and crossfade draws, which is why the
colour pass relies on the prepass (LEQUAL, no depth write). Walls add overdraw at grazing views. GPU timings were not
measured (the game was not run): use the checklist below, e.g. `r_speeds 7` / `r_speeds 100` at 1080p / 1440p, with
the material on a small and on a large surface, `r_pomSilhouetteDistance 0` as the reference.

## Validation done

- MSVC builds of MP and SP (`build/ab/*-prespom.dll` = HEAD, `*-spom.dll`).
- Offline GLSL (Intel UHD, NVIDIA RTX 2060): all silhouette lightall variants (light type × spec-gloss × cloth ×
  SSR / SSGI / SSAO / sun / F+ debug / diffuse IBL), both depth programs, the fog variants (legacy, volumetric,
  froxel with the combined library), plus the legacy programs: 96 compiles + links, no error.
- Preprocessed lightall / fogpass without `USE_SILHOUETTE_POM`: token-identical to HEAD in 34 permutations.
- CPU harness (the builder extracted unchanged from `tr_pom_silhouette.cpp`, a float replica of the GLSL trace, a
  brute force ground truth of the displaced surface, the shell rasterized per ray): quad, closed box (6 groups, 24
  walls), smooth normals → "curved" fallback, UV seam → 2 groups with walls on both sides, 16 × 16 grid split into 4
  groups (≤ 48 edges, outer walls once, split edges twice), wall winding / provoking vertex. Trace vs ground truth,
  96 × 96 rays at 90°, 60°, 30°, 10° elevation over the surface edge:

  | height field | agreement hit / miss | mean distance error | hits outside the base polygon projection | base pixels carved away |
  |---|---|---|---|---|
  | smooth bumps, bias 1 (protruding) | 99.95–100 % | 0.002–0.12 u | up to 1262 rays (10°) | up to 1282 |
  | smooth bumps, bias 0 (recessed) | 99.99–100 % | 0.002–0.06 u | up to 1815 | up to 601 |
  | blocks (steps, groove floors at full depth), bias 0.5 | 99.99–100 % | 0.003–0.07 u | up to 2357 | up to 78 |

  "Hits outside the base polygon projection" is the silhouette proof: rays whose intersection with the base plane
  lies outside the surface still hit the displaced surface (outer contour grows); "carved" rays cross the base
  polygon but miss (the contour shrinks where the surface is recessed). Occasional large distance errors at 10°
  (a thin crest skipped by the linear search, a later crest hit) are the usual ray march limit, reduced by
  `pomSilhouetteSteps`.

The game was **not** run: no screenshots, no GPU timings yet.

## In-game checklist

Materials: `python tools/rend2/make_spom_test.py --out <base>/spom_test --override <a stock floor/wall shader>:blocks`
(patterns blocks, bumps, rocks, steps; `--bias 0 / 0.5 / 1`, `--depth`). Then `r_pomSilhouette 1; r_parallaxMapping 1;
vid_restart`, `developer 1` for fallback reasons, `r_pomSilhouetteInfo`.

1. Contour: look at an edge from the front, 30°, 60°, 80° (grazing); `r_pomSilhouetteDebug 9` (ordinary left, shell
   right) and 1 (translucent shell): the outline of the displaced surface differs from the polygon.
2. `--bias 1` protruding, `--bias 0` recessed, `--bias 0.5`; two shelled surfaces touching; ordinary POM next to it.
3. Depth: stand a character in a groove / behind a bump (intersections follow the displaced surface); debug 10.
4. GTAO (`r_aoMode`), contact shadows, SSR, SSGI, froxel fog and a legacy fog volume on the displaced edge.
5. Sun shadows (`r_sunlightMode 2`) from the bumps onto the surface and the floor; `r_pomSilhouetteShadows 0` for
   comparison; acne on lit bumps. Shadow edges of other geometry on a shelled surface as soft as with
   `r_autoPOMSilhouette 0` (`r_shadowDebug 4/5/6`: blocker depth, penumbra, visibility);
   `r_pomSilhouetteContactShadows 1` for comparison.
6. Crossfade: walk towards / away from the surface (`r_pomSilhouetteFade`, `r_pomSilhouetteDistance`).
7. Movers with a shelled texture (brush models), motion blur / SMAA T2x on the displaced edge.
8. `r_pomSilhouette 0` equals the `*-prespom.dll` build.
9. Automatic mode: `r_autoPOMSilhouette 1`, `r_autoPOMSilhouette list`; switch one shader off and on
   (`r_autoPOMSilhouette textures/... 0`), a prefix (`textures/bespin/* 0`), restart the game and check that
   `pomsilhouette.cfg` kept the switches.
10. Timings: `r_speeds 7` / `r_speeds 100`, with and without the material, debug 8 for the step count.
