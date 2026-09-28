# rend2 auto grass (`r_grassCardMode`)

Stock Jedi Academy surface sprite grass is drawn as world-stable cross or tri-card tufts.
BSP, shader scripts, textures and PK3s are unchanged. `r_grassCardMode 0` (the default) is the legacy path.

## Current pipeline (legacy, unchanged)

* **Map load** (`tr_bsp.cpp`, `R_CreateSurfaceSpritesVertexData`): anchors are placed at random barycentric
  points on every triangle of a surface that has a `surfaceSprites` stage. Positions are generated once and
  never rebuilt. Each sprite is stored as **4 identical `sprite_t` records**:
  * `attr_Position.xyz`: the world anchor.
  * `attr_Position.w`: a stable random value in [0, 1].
  * `attr_Normal`: a random horizontal unit direction.
  * `attr_Position2`: width, height and skew (from `ssVariance` and `ssVertSkew`; height is negated for `ssFaceDown`).
  * `attr_Color`: the vertex colour of the emitting surface.

  The records are packed into static VBOs of up to 65535 vertices. All surfaces share one "Quads" IBO
  (`0,1,2, 0,2,3` + 4k). There is one `srfSprites_t` per (BSP surface, sprite stage), and it is culled with its surface.
* **Draw** (`tr_surface.cpp`, `RB_SurfaceSprites`): chooses an `SSDEF_*` program:
  * `ORIENTED` → `FACE_CAMERA`
  * `FLATTENED`
  * `EFFECT` → `FX | FACE_CAMERA`
  * `WEATHERFX` → `FX`
  * `ssFaceUp` → `FACE_UP`
  * plus the additive, fog and velocity flags

  It adds one `DrawItem` per surface with `numInstances = 1`. The backend already issues
  `glDrawElementsInstancedBaseVertex`. The same function serves the depth/velocity prepass, the sun cascades
  (which use the refdef view vectors so the sprites match the camera) and the colour pass.
* **Shader** (`glsl/surface_sprites.glsl`): the corner comes from `gl_VertexID % 4`.
  * `FACE_CAMERA` uses the view left/up vectors.
  * `VERTICAL` uses `normalize(attr_Normal.xy + 2 * viewLeft.xy)`, which is strongly camera biased. This is why
    the grass turns with the view.
  * Skew and idle wind move the top vertices.
  * The distance fade widens the card and erodes `var_Alpha` before the (always on) alpha test.
  * The velocity pass recomputes the offset with the previous frame time.

## New geometry

The classifier runs per draw on the CPU. A stage qualifies when all of these hold:
* its type is `VERTICAL` or `ORIENTED`;
* it is not `ssFaceUp`;
* it is not additive;
* it is not blended without an alpha test.

Effect, weather, flattened and face-up sprites are never touched.

A qualifying draw uses the `SSDEF_AUTO_GRASS` program (`#define AUTO_GRASS`, 4 extra programs: ± fog, ± velocity)
with `numInstances = n`. No CPU data is duplicated: all instances read the same four vertices.

```
old:  d = normalize(attr_Normal.xy + 2 * viewLeft.xy)          // turns with the camera
      (ORIENTED: d = viewLeft, up = viewUp)                    // faces the camera
new:  d_k = rotate(normalize(attr_Normal.xy), k * 180deg / n)  // k = gl_InstanceID
      p_k = perp(d_k)
      offset.xy = corner.x * r_grassCardWidth * d_k + corner.y * width * p_k   // corner.y = legacy 0.2 lift of the top vertex
      offset.z = corner.z                                      // then skew + wind, identical for all cards
```

* Cross (`n = 2`): cards at θ and θ + 90°.
* Tri-card (`n = 3`): cards at θ, θ + 60° and θ + 120°. A quad looks the same after a 180° turn, so 60° spacing
  is the even three-way split.
* θ comes from the random direction stored at map load. It does not depend on the camera or on time, so the
  previous-frame pose in the velocity pass matches automatically.
* All cards share the anchor, skew and wind, so the tuft leans and sways as one piece.
* Texture, vertex colour, alpha test, distance fade, fog and froxel/volumetric fog run through unchanged code.

## Modes and cvars

| cvar | values |
|---|---|
| `r_grassCardMode` (archive, default 0) | 0 legacy · 1 two-card cross · 2 three-card tuft · 3 adaptive |
| `r_grassCardLodDist` (default 600) | mode 3: the third card starts fading at this distance and is gone at ×1.33 |
| `r_grassCardWidth` (default 1) | card width scale, 0.25–2 |
| `r_grassCardDebug` (cheat) | 1 colour by card (R/G/B) · 2 colour by card direction · 3/4/5 force 1/2/3 cards · 6 colour by LOD (green = 3 cards, orange = 2) |

`r_speeds 7` prints `Surface sprites: draws N cards M (V verts)`. The counts cover all passes, and cards = sprites × instances.

### Instance count and the adaptive mode

A `DrawItem` has one instance count per surface, so the count cannot vary per sprite inside one draw.
Mode 3 combines two mechanisms:
* **Per surface (CPU, one AABB distance):** 3 instances when the view origin is within `lodDist × 1.33` of the
  surface's padded sprite bounds (`srfSprites_t::spriteMins/Maxs`, computed at load), otherwise 2.
* **Per sprite (vertex shader):** the third card multiplies `var_Alpha` by
  `1 − smoothstep(lodDist, 1.33·lodDist, dist)`. This is the same alpha erosion the stock distance fade uses. Past
  that distance the third card collapses to a zero-area primitive, so it produces no fragments.

A surface drops to 2 instances only after all of its sprites have already faded their third card, so the switch
does not pop. Both cards keep the 60° layout, so the remaining two cards never rotate.

The count depends only on the sprite view origin (`refdef.vieworg` for cascades). The prepass, velocity pass,
cascades and colour pass therefore draw identical geometry.

No one-card far tier was added. A fixed single card vanishes edge-on, and a camera-biased far card would bring
back the rotation this feature removes. Add one only if profiling shows far grass is expensive.

## Recommendation

**Use `r_grassCardMode 1` (two-card cross).** Once the grass stops turning with the camera and never goes edge-on,
most of the visual gain is already there.

The main cost is alpha-tested overdraw, not draw calls. The draw-call count is unchanged in every mode.
* **Two cards:** vertex work and rasterised card area both double. The alpha-tested fragments roughly double as
  well, because the cards overlap only near the stem.
* **Three cards:** both roughly triple.
* **Mode 3:** costs three cards only near the camera, where the tuft shape is visible.

Overlapping cutout cards do not make the grass brighter, because alpha test is not blending. Grass does look
denser, though. If a map looks too thick, lower `r_grassCardWidth` to about 0.8 rather than touching alpha.

## Validation status

* MSVC Release builds of SP and MP rend2: OK.
* Offline GLSL on the Intel UHD and NVIDIA RTX 2060 drivers: all `AUTO_GRASS` variants (± `USE_FOG`,
  ± `USE_VOLUMETRIC_FOG`, ± `VELOCITY_PASS`) and the legacy variants compile.
* Legacy exactness: the preprocessed `surface_sprites.glsl` without `AUTO_GRASS` is identical to the previous
  version for 7 define sets. With `r_grassCardMode 0` the C++ path sets the same flags and uniforms, draws one instance, and only adds counters.
* **Not yet run in game.**

### In-game checklist (Yavin grass, e.g. `yavin1` / `yavin2`)

1. Rotate a static camera through 360° in modes 0, 1 and 2. Check that tufts never vanish edge-on and do not
   visibly turn with the camera. Use `r_grassCardDebug 1` and `2` to confirm the orientation stays fixed.
2. Walk through the field: no orientation popping. In mode 3 with `r_grassCardDebug 6`, check that the
   green/orange boundary moves without pops.
3. Fog volume area, `r_volumetricFog 0/1/2`.
4. TAA and SMAA: no ghosting while moving, which checks the velocity pass.
5. `r_grassCardMode 0` looks identical to the previous build (A/B screenshots).
6. Weather sprites (rain/snow maps) and FX sprites are unchanged in all modes.
7. Sun shadows (`r_sunShadowAlphaCasters 1`): foliage shadows no longer swim with the camera.
8. Screenshots in modes 0 / 1 / 2: front, 45°, 90° to the original card, looking down, and moving.

### Timings (fill in: fixed viewpoint in dense grass, `r_speeds 7` and `r_speeds 100`)

| mode | sprite draws | cards | verts | GPU frame ms | CPU renderer ms |
|---|---|---|---|---|---|
| 0 legacy |  |  |  |  |  |
| 1 cross |  |  |  |  |  |
| 2 tri |  |  |  |  |  |
| 3 adaptive |  |  |  |  |  |
