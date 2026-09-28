# rend2 leaf flutter (`r_leafFlutter`)

A very small, spatially coherent motion of stock MD3 tree leaf and vine cards. The leaves rustle, the tree does not
bend: trunks, branches, the model origin and every non leaf surface stay exactly where they are. `r_leafFlutter 0`
(the default) runs no flutter code: the shaders take a uniform early out and the batches are unchanged.

Depends on the automatic foliage classification (`tr_foliage.cpp`, `r_autoFoliage`): only MD3 surfaces classified
`FOLIAGE_LEAF` move, so `r_autoFoliage` must be 1 (conservative) or 2 (broad). Alpha test alone never qualifies a
surface.

## Which stock surfaces move

Every tree in the stock Yavin maps is a `misc_model_static` (MD3 entity), e.g. yavin1b: 95× tree09_b, 29× tree10_b,
24× tree02_b, 10× tree08_b, 4× tree06_b, 3× tree_sidehill_b. They reach `R_AddMD3Surfaces`.

| model | moving (FOLIAGE_LEAF) | static |
|---|---|---|
| tree02_b | leaves01, leaves02, mesh04 (tree2_b) | tree-truck2 (tree2b) |
| tree05_b | leaves top, leaves03 (tree2_b), vines thin (tree05_vines_b) | trynk (tree05) |
| tree06_b | canopee (tree06b_b) | tree-truck (tree06) |
| tree08_b | canopee, mesh01–04 (tree08b_b) | tree-truck (tree08) |
| tree09_b / tree_sidehill_b | canopee, canopee01 (tree09a_b), mesh01–04 (tree09b_b), mesh05–10 (tree09d_b), vines* (tree09_vines_b, vines_b) | cylinder02, trunk (tree09), trunkbase (tree09c) |
| tree10_b | same as tree09_b | cylinder02 (tree09), stump (tree09c) |

The leaf materials are all `alphaFunc GE128`, `cull twosided`, `rgbGen lightingDiffuse`, so they go through lightall
with `USE_LIGHT_VECTOR` and per-pixel lighting. The trunk materials are opaque and are never classified. Use
`r_autoFoliageList yavin` in game for the authoritative list.

Not covered: trees placed as plain `misc_model` are baked into BSP triangles by q3map2 and never pass through the MD3
path.

## Movement function (`glsl/leaf_flutter.glsl`)

It is a pure function of the rest world position `p` of the vertex, a per-tree seed, time and the cvars. It takes
no camera input.

```
dir, side = r_foliageWindDirection basis (shared with the r_foliageWind grass)
seed   = integer hash of floor(entity origin / 8) -> [0, 1)          per tree phase and rate
weight = smoothstep(0.08, 0.45, |objectPos.xy| / model xy radius)    0 at the trunk axis, 1 at the crown edge
tw     = t * r_leafFlutterSpeed * (0.9 + 0.2 seed)
slow   = 0.6 sin(1.7 tw + 0.010 p·dir + 6.28 seed) + 0.4 sin(1.1 tw + 0.013 p·side + 3.1 seed)    ~600 u band
fast   = sin(5.3 tw + p·(0.041, 0.037, 0.053) + 11 seed) * (0.6 + 0.4 sin(2.3 tw + 0.02 p.z))   ~130 u band
offset = amplitude * weight * (dir 0.6 slow + side 0.45 fast + up 0.3 fast)      |offset| <= 0.81 amplitude
normal = normalize(n + (side fast + dir 0.5 slow) * r_leafFlutterNormal * weight)   (lightall only)
```

* The displacement is zero mean, so the crown never leans.
* The slow band moves a whole card. The fast band puts the corners of one card slightly out of step: the card
  breathes, it does not boil.
* The function is continuous in `p`, so a vertex shared by two triangles gets one offset and cards cannot tear.
* The offset is added to the world position after `u_ModelMatrix`. The object-space position stays at rest for
  tcGen and disintegration. Entity scale does not change the amplitude.

### Position vs normal

The stock cards are 130–500 units across but have only 4–12 vertices (tree09_b mesh01..10 has 4 vertices each,
canopee has 12). Vertex motion can therefore only move card corners: it gives a gentle breathing of each card, never
individual leaves.

The normal wobble tilts the interpolated card normal. The leaves are per-pixel lit (`lightingDiffuse` → lightall
`LIGHT_VECTOR`), so the tilt changes direct light, specular and IBL across the card with no extra silhouette motion.
On vertex lit (`LIGHT_VERTEX`) or fast light paths the effect is smaller.

This has **not been checked in game**. `r_leafFlutterNormal 0` vs `0.25` is the A/B; if it turns out invisible,
the default should become 0 and the normal code can be removed.

Leaf level shimmer (a UV micro-offset inside the card) is left for a follow-up. It would need the same UV offset
in the alpha test of the depth, shadow and velocity passes.

## Cvars

| cvar | default | |
|---|---|---|
| `r_leafFlutter` | 0 | 1 = on for FOLIAGE_LEAF surfaces (needs `r_autoFoliage ≥ 1`) |
| `r_leafFlutterStrength` | 1 | 0–4; amplitude = 1.5 units × strength (≈1 % of a card) |
| `r_leafFlutterSpeed` | 1 | 0–4, time scale |
| `r_leafFlutterNormal` | 0.25 | 0–1 normal wobble, scaled by strength (capped at 2); 0 = position only |
| `r_leafFlutterDebug` (cheat) | 0 | bits: 1 = ×8 amplitude (×3 normal), 2 = freeze time, 4 = highlight fluttering surfaces green, 8 = color by displacement (blue 0 → yellow max) |

The debug bits combine: 9 = exaggerated + magnitude, 3 = exaggerated frozen. `r_autoFoliageDebug` still colors the
classes; the leaf flutter debug colors take precedence on fluttering surfaces.

CPU mirror numbers at the defaults, on the real stock cards: max offset 1.2 u, mean 0.3–0.5 u, max vertex speed
4.3 u/s, worst edge length change 0.74 u on a 13 u vine segment (5.6 %). Copies of tree09_b 8, 300 and 1200 units
apart correlate at −0.03, 0.00 and +0.07, so a forest does not oscillate in sync.

## Implementation

* **No legacy `deformVertexes`.** Deform shaders never reach lightall (`CollapseStagesToGLSL` skips them), and
  deform data is per shader, not per surface.
* **Shared GLSL:** `glsl/leaf_flutter.glsl` is a vertex-only library. `GLSL_LoadGPUShader` now has a vertex-library
  slot next to the fragment one, and `LoadLeafFlutterLibrary` pastes the library into lightall (incl. SPOM variants),
  generic, fogpass (incl. SPOM fog) and velocity.
* **No new permutations.** A uniform branch on `u_LeafFlutter.z > 0` gates the code.
* **Uniforms:** `u_LeafFlutter` (dir xy, amplitude, speed), `u_LeafFlutterParams` (time, previous time, 1 / model xy
  radius, normal amount), `u_LeafFlutterDebug`.
* **`RB_SetLeafFlutterUniforms` (`tr_leafflutter.cpp`)** writes them on every draw that uses these programs:
  `RB_IterateStagesGeneric` (main, depth prepass, velocity, sun cascades, point shadows), `RB_FogPass`, `DrawTris`,
  and "off" in the POM debug and sky draws. Uniforms stick per program, so inactive draws write amplitude 0.
* **Batching:** `tess.leafFlutter` is a batch key in `RB_SubmitDrawSurfs` and `RB_SubmitDrawSurfsForDepthFill`.
  It is kept across the tess restarts in `tr_surface.cpp`. A leaf and a static surface of the same shader / entity
  never share a draw.
* **Time:** `RB_LeafFlutterBeginFrame` latches the scene time and the previous frame time once per scene. With no
  usable history (a gap over 0.25 s or a time jump back) it uses previous = current.

### Velocity

`velocity.glsl` evaluates the offset twice:
* at the current time, on `u_ModelMatrix`;
* at the previous frame time, on `u_PreviousModelMatrix`.

Both use one seed, so motion vectors are exact. With freeze both times are equal and only camera motion remains.
With flutter off the old expression is used unchanged.

### Shadows

Sun cascades and point shadows draw alpha-tested leaves through the same lightall / generic programs, so shadows
flutter with the leaves at no extra cost. A leaf shader has no `DEPTHPREPASS_SIMPLE`, so it is never replaced by the
default shader in depth passes.

### Fog

The fog pass uses depth func EQUAL. fogpass.glsl adds the same offset with the same expression as lightall. No
`invariant` qualifier was added, so as not to change the rounding of existing programs relative to each other. If
fog on fluttering leaves ever shows z-fighting speckles, `invariant gl_Position` in the four vertex shaders is the fix
to try.

### Culling

The model bounds are static. `R_CullModel` grows the sphere radius and the box by `R_LeafFlutterCullMargin` =
0.81 × amplitude + 0.5 for map object models while flutter is on. For the box the margin is divided by the smallest
entity scale. This is a cvar constant, not a per-frame recomputation. Tree radii are 300–850 units, so the extra draws
are negligible, and shadow cascades cull with the same function.

### Not covered

Animated MD3s go through the CPU `RB_SurfaceMesh` path. The shader still applies the flutter, since the positions are
object space either way. Refraction, pshadow and shadow-volume passes have no flutter, because leaf shaders never use
them.

## Cost

Static NVIDIA vertex program instruction counts (RTX 2060, `glGetProgramBinary`), HEAD → now. Both paths are
counted; a non leaf draw executes only the branch.

| program | HEAD | now |
|---|---|---|
| lightall LIGHT_VECTOR per pixel (stock leaves) | 124 | 213 |
| lightall depth / shadow | 110 | 191 |
| lightall LIGHT_VERTEX | 129 | 221 |
| generic | 37 | 106 |
| fogpass | 18 | 80 |
| velocity (2 evaluations) | 36 | 143 |

It is pure vertex work on 4–100 vertex surfaces, with no CPU simulation and no extra entities. The fragment shaders
only gain the debug color branch.

**GPU timing: not measured** (no game run). Use `tools/leafflutter_ab.cfg` F3.

## Validation status

Done:
* MSVC SP and MP Release builds.
* Offline compile and link on Intel UHD and RTX 2060 for 17 permutations of lightall (LIGHT_VECTOR per pixel /
  fast / LIGHT_VERTEX / LIGHTMAP / entity grid + skeletal + tcGen / depth-only / SPOM), generic, fogpass (+ SPOM)
  and velocity.
* CPU mirror (`leafflutter_sim.py`, session scratchpad): offsets, strain, desync, freeze, cull margin.

**Not run in game.** In-game checklist (yavin1b, `r_autoFoliage 1; r_leafFlutter 1`, `exec leafflutter_ab.cfg`):

1. `r_leafFlutterDebug 4`: only leaf / vine cards are green, trunks and bark are not.
2. `r_leafFlutterDebug 1`, close-up video of trunk + crown: the trunk is pixel-still, the cards breathe, nothing
   tears, and different trees are out of step.
3. Orbit the tree with `r_leafFlutterDebug 3`: nothing moves, so the motion is not camera dependent.
4. `r_leafFlutterNormal 0` vs `0.25` at production strength: is the lighting shimmer visible?
5. Motion blur / SMAA T2x velocity: no smear on still trees, and freeze leaves only camera motion.
6. Sun shadows follow the leaves. Fog on leaves: no speckles (see Fog).
7. Turn so a tree sits at the screen edge: no pop-in or culling flicker.
8. `r_leafFlutter 0` matches HEAD. GPU frame time off / on at a fixed view in a dense area (F3).
