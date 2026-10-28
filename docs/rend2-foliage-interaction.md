# rend2 vegetation deformation and character interaction

Grass and plants bend with the wind and when characters walk through them. Two models, one for each kind of
vegetation, both inextensible:

| | MD3 plants (ferns, bushes, grass cards) | surface sprite grass |
|---|---|---|
| state | per frond, on the CPU (`tr_plantsim.cpp`) | per patch of ground, on the GPU (`tr_foliagefield.cpp`) |
| model | chain of rigid segments along the frond centerline (XPBD) | analytic beam (moment / stiffness, integrated tilt) |
| drawn by | 2-bone skinning in every vertex program (`glsl/plant_skin.glsl`) | `FoliageBladeBend` in `glsl/surface_sprites.glsl` |
| character | hard one-sided contact of the capsules with the segments | hard one-sided contact of the capsules in the field |
| wind | drag towards the coherent breeze (`r_plantWind`) | tip displacement of the breeze (`r_foliageWind`) |

`r_foliageInteraction 0`, `r_plantWind 0`, `r_foliageWind 0` (the defaults) leave all of it off.

**Status: built, GLSL compiled and GPU-tested offline (`tools/rend2/test_foliage_gl.py`, Intel + NVIDIA), the frond
analysis run on the stock models; never run in game.**

## Why it was rewritten (2026-10-13)

The previous version turned every vertex of an MD3 plant around one root of the whole model, by an amount that grew
with its distance to that root (`t^1.5`), pushed away from the capsule axis *at that vertex*. Three defects followed
directly:
- only `|vertex - root|` was kept, not the distance between neighbouring vertices: fronds stretched and shrank;
- every vertex fled the leg on its own side: a frond around a leg wrapped around it;
- stiffness depended on the distance to the model center, not on the position along a frond.

Grass sprites were quads whose top edge moved: a tilted straight card, never a curved blade. The contact was a
force field starting at 1.75 × the body radius.

## Data flow: cgame → renderer

The renderer never reads game globals, and it never uses the camera position.

1. `CG_AddFoliageInteractors` builds the capsule list every frame, right after `CG_AddPacketEntities(qfalse)`, when
   `lerpOrigin` is current. It lives in SP `code/cgame/cg_view.cpp` and MP `codemp/cgame/cg_view.c`.
   - **Player (slot 0, always first).** The predicted body, never `cg.refdef.vieworg`: that is the camera, which sits
     behind the player in third person and anywhere during cinematics.
     - SP: `cg_entities[0].lerpOrigin` (the predicted origin), `gent->mins/maxs` (crouch lowers `maxs`), and
       `predicted_player_state.velocity`.
     - MP: `predictedPlayerState.origin`, the box from `standheight`/`crouchheight` and `PMF_DUCKED`, and its velocity.
     - The player is skipped during intermission, as a spectator, when dead (MP), when riding a vehicle, and when
       `EF_NODRAW` (SP).
   - **NPCs and other players.** They come from `cg.snap->entities`.
     - SP: `ET_PLAYER` with a client, alive, not `EF_NODRAW`, not a vehicle.
     - MP: `ET_NPC` / `ET_PLAYER`, not `EF_DEAD|EF_NODRAW`, not a vehicle, box decoded from `solid`.
     - Only those within 1024 units (horizontal) of the player count. They are kept sorted by insertion into 15 slots.
     - Overflow drops the farthest.
2. The list goes out through the **optional renderer extension** `GetRefFoliageAPI` (`tr_public.h`), built the same
   way as the LTC area light extension. `refdef_t`, `refexport_t` and `REF_API_VERSION` stay unchanged, so mod cgames
   and other renderers keep working.
   - SP: syscall `CG_R_SETFOLIAGEINTERACTORS`, added after `CG_R_ADDLINELIGHTTOSCENE`.
   - MP: `trap->ext.R_SetFoliageInteractors`. The legacy VM stub does nothing.
   - cgame calls it only with `r_foliageInteraction` set, which it mirrors as a vmCvar. Older engines don't have the
     syscall.
3. `RE_SetFoliageInteractors` (`tr_foliageinteract.cpp`) copies the list and stamps it with `tr.frameCount`.
4. `RE_BeginScene` → `R_FoliageInteractionBeginScene`: the first world scene of the frame latches the list.
   - A list stamped in another frame counts as 0.
   - A stale list is never drawn.
   - HUD and menu scenes (`RDF_NOWORLDMODEL`) and the sky portal scene get no colliders.


## Colliders

A character is the vertical capsule through the centre of its box: `(axis x, y, feet z, head z)` + `(radius, velocity
x, velocity y, airborne)`, radius = half the box width (15 for a humanoid) × `r_foliageInteractionRadius`. Both ends
are round (segment `feet + r .. head − r`), so a crouch shortens it and a jump lifts it.

`MAX_FOLIAGE_INTERACTORS` = 16; `r_foliageInteractionMaxInteractors` (8) uploads the player and the nearest NPCs;
`r_foliageInteractionNPCs 0` keeps the player only.

The `FoliageInteraction` UBO (slot 12) carries this frame's colliders, the previous frame's (exactly the previous
upload) and the grass field parameters. The history is reset (previous = current) on a time gap, a pause, a cut or a
map load.

## MD3 plants: fronds as inextensible chains

### Frond skeleton (`tr_plantskel.cpp`, at model load)

For map objects whose surfaces classify as `FOLIAGE_PLANT` / `FOLIAGE_GRASS` (the broad `r_autoFoliage` mode, so
either mode finds a skeleton), single frame MD3 only:
1. vertices welded by position (0.05 u grid);
2. fronds = triangle groups connected through vertices that are not near the model root (8 % of its size); a near
   vertex that joins two groups is a **hub** (yavin/fern3_b: all fronds meet in one vertex); separate surfaces
   (nar_shaddar/plant) and separate pieces (yavin/plant_b) are separate fronds;
3. attachment: the hub, else the base band of the frond (bottom edge of a standing card, inner end of a radial leaf);
4. centerline: kernel regression of the vertex positions over their distance to the attachment, extended to the
   tip, resampled into equal segments of ~16 u (1–7, at most vertices − 1);
5. each vertex is projected on it: two neighbouring segments and a weight, blended from the middle of one segment to
   the middle of the next; the base half of the first segment follows it fully;
6. lamina direction: the principal direction of the vertex offsets across the centerline (power iteration over the
   whole frond), made perpendicular to every segment; lamina half widths per node.

Bone `firstBone + j` = segment j of a frond, bone 0 = rest (hubs, other surfaces). At most 32 fronds (the longest
kept) and 7 segments: ≤ 225 bones. The skin (bone A, bone B, weight of B, 255) goes into the MD3 VBO in the
`attr_BoneWeights` slot, which MD3 draws never used (G2 programs never draw plants).

Stock results (C++ harness): fern3_b 16 fronds / 69 bones, plant_b 8 / 55, nar_shaddar/plant 21 / 100,
grass_b and grass_tall_b 6 cards / 19.

`r_autoFoliageList <filter>` prints the skeleton of each model.

### Simulation (`tr_plantsim.cpp`, once per frame)

Instances are keyed by model and origin; their state persists across frames (a frond swings back after a
character left). XPBD (Macklin et al. 2016), fixed 120 Hz sub steps (at most 8 per frame), 4 iterations:
- node 0 pinned; **segment lengths hard** (no stretching);
- **bending**: the next node towards its rest direction in the parallel transported frame of the previous segment,
  per axis of that frame: compliance `1 / (m_carried (2πf)²)` from a frequency profile (fern: 4 Hz at the base to
  1.6 Hz at the tip, grass card 2.6 → 1.8 Hz, × √`r_plantStiffness`); **across the lamina 25 × stiffer** than out of
  its plane, from 30 % of the length on (a leaf bends out of its plane, its width forbids bending within it; the
  round petiole turns either way). Without this a sideways push bends the wide lamina in its plane and stretches its
  outer edge;
- joint limits: 75° at the base, 55° along the frond;
- **contact**: each segment against each capsule near the plant, reach = radius + ¼ of the lamina width (≤ 12 u),
  pushed out exactly to that distance. The whole frond uses one side of the body (the side its centerline is on
  when the contact starts, the walk direction when it is right across), kept until 0.3 s after the last contact:
  the frond swings aside around its base instead of wrapping around the leg;
- **standing in the plant** (the pinned base inside the body: no pose clears the leg, so "aside" had no solution and
  the frond spun around the character as long as it stood there): the frond is splayed instead, every node pushed
  along its own horizontal line from the base out to the edge of the body, at most 0.7 × its arc from the base
  (a 45° droop), which always has a pose. The mode keeps until the base is half a radius outside the body (a swaying
  body would flip it); the legs are a vertical cylinder down to the feet (the capsule bottom no longer presses fronds
  into the ground);
- contact friction: a node a body pushed moves with the body (velocity relaxes to the body's at 30 /s), and the joint
  limit skips it in the next sub step (two hard constraints against each other made a 2 Hz cycle at the tip);
- ground: no node below the bottom of the model;
- wind: acceleration across the segments towards the coherent breeze (same gust field as the grass,
  `PLANTSIM_WIND_ACCEL` 450 u/s² × `r_plantWindStrength`, ~14° at the tip in the strongest gust), air drag 2/s;
- damping: ratio ~0.3 at the tip frequency (one visible swing back).

LOD: plants within `r_plantSimDistance` (1500) or near a character get the full step; up to `r_plantWindDistance`
(3000) one coarse step per frame (1/30 s, 2 iterations); farther instances keep their last pose. At most
`r_plantSimMaxPlants` (96) plants are stepped per frame (touched plants always). A moving plant outside that budget
or beyond `r_plantWindDistance` keeps its last rendered pose instead of popping to rest. A plant with no wind, no
character near and at rest sleeps (no work, drawn without skinning).

Bones: per segment a rotation q (rest → current, transported along the chain) and t = x_j − q X_j; with the previous
frame's bones, into this frame's bone arena (≤ 64 K texels), uploaded per view in front of its draws (`RC_PLANT_BONES`,
a `GL_TEXTURE_BUFFER` RGBA32F per frame in flight). Draws bind it per draw on `TB_PLANTBONES` (24: the MD3 programs
have no bend field any more).

The detailed 120 Hz simulation keeps the state before its latest fixed step and render-interpolates by the remaining
accumulator fraction. Displays above 120 Hz therefore see a changing pose on every render frame instead of repeated
fixed-step poses; motion vectors use the previous rendered pose.

### Drawing (`glsl/plant_skin.glsl`)

`ws' = mix(qA ws + tA, qB ws + tB, w)`, normal and tangent rotated by `nlerp(qA, qB, w)`. lightall, generic,
fogpass, velocity (previous rendered bones), so the depth prepass, shadows, fog and motion vectors follow. Cull
bounds grow by ¾ of the longest frond.

GPU test (yavin/plant_b frond, a leg through its middle): GPU skinning = float64 reference to 1e-5, rest pose exact,
chain length error 0.04 %, the whole frond on one side of the leg, mesh edges elongated ≤ 4.8 % (the old root bend at
the same tip displacement: 16 %).

## Grass sprites: inextensible blades on a bend field

### Bend field (`tr_foliagefield.cpp`, `glsl/foliage_field.glsl`)

A player centred world XY square (`r_foliageBendFieldExtent` 1024, `r_foliageBendFieldSize` 256 texels, latched),
updated once per frame by a fragment pass into two ping-pong MRT pairs:
- RGBA16F: displacement δ of the grass at the probe height (16 u above the feet, the shins), world units, and its
  velocity;
- RG16F: the trampled part.

Per frame: a damped spring (`r_foliageBendFieldRecoveryTime` 0.8 s, `r_foliageBendFieldDamping` 0.6) pulls δ
towards the trampled part, then each standing character is a **hard one-sided contact**: δ is pushed out of the
capsule along the direction away from its axis (`δ += max(0, penetration − δ·n) n`), the inward velocity is
removed, the sideways part of δ is kept (no flipping, no splitting). Nothing outside the capsule moves. On the axis:
the way the grass already leans, else the walk, else a stable random way. Running adds a drag along the walk
(`r_foliageBendFieldImpulse`). While a character stands on a patch, its trampled part goes towards `r_grassTrample`
(0.25) × δ in ~1.5 s, and fades with `r_grassTrampleRecovery` (6 s) after. Airborne characters leave the field alone.

The field scrolls with the player body in whole texels, clears on map load, teleports, pauses and
`r_foliageBendFieldClear`. It is on whenever `r_foliageInteraction` is (the former `r_foliageBendField` switch is
gone: the field is the grass state).

GPU test: contact displacement error 0.008 u, displacement outside the body exactly 0, 18 % trampled after 2 s, 14 %
of the push left 1.5 s after leaving.

### Blade (`FoliageBladeBend`, `glsl/foliage_interact.glsl`)

Stiffness `EI(s) = (1 − 0.7 s)³` (thinner towards the tip). Moment of the leg at `s_c` = probe height / blade length:
`(s_c − s)` below it, 0 above (the blade above the shins stays straight); moment of the wind `(1 − s)²`. Tilt =
∫ moment / EI (closed forms), scaled so the contact point moves by δ (mean tilt below it = asin(δ / (L s_c))) and the
tip by the wind displacement; at most 80°. The position integrates the tangent over 6 straight pieces: every point
of the blade lies on one polyline whose length is exactly the blade length. Skewed rest stems keep their lean.

Both wind modes bend this way: the legacy sway (`r_foliageWind 0`) and the breeze (`r_foliageWind 1`) give a tip
displacement, the blade curves to it.

GPU test: rest exact; length kept to 0.13 %; contact point and wind tip within a few % of the target; straight
above the contact.

### Strips (`r_grassBendSegments`, `r_grassBendLodDist`)

A quad can only show a tilted card. Sprite surfaces nearer than `r_grassBendLodDist` (800) with wind or interaction
are drawn as triangle strips of `r_grassBendSegments` (4) segments: one instance per card and sprite
(`DRAW_COMMAND_ARRAYS`, `GL_TRIANGLE_STRIP`), the sprite data read per instance (stride × 4, divisor = cards per
sprite), so the sprite VBO is unchanged. The far quads lie on the same curve (their top edge is the tip). The shader
branches on `u_GrassStrip`, no permutation.

## Force push / pull (`r_foliageForceWaves`)

A Force push or pull of the player or of an NPC sends a shock wave through all of the vegetation: MD3 plants,
grass sprites and the leaves of trees. Push throws them away from the caster, pull towards it; the plants and the
grass swing back on their own springs.

**Detection (cgame, once per cast).**
- SP (`code/cgame/cg_view.cpp`, `CG_AddFoliageForceWaves`): every character whose `ps.powerups[PW_FORCE_PUSH]`
  gets a new end time (set by `ForceThrow` and by the `push_l` / `push_r` anim events; restarts within 300 ms
  are one cast). A pull is told by the newer debounce (`forcePowerDebounce[FP_PULL]` vs `[FP_PUSH]`). Radius and
  cone are the game's own tables for the power level (`forcePushPullRadius`, `forcePushCone`, `forcePullCone`).
- MP (`codemp/cgame/cg_view.c`): the rising edge of `PW_DISINT_4` (push hand effect, also on a counter push),
  `PW_PULL` for a pull, grip (which uses `PW_DISINT_4` too) excluded by its active power. The player uses its own
  power level; other clients a level 2 push (their level is not networked).
- Aim: the view angles; an aimed level 1 push (cone 1) still parts a lane of ~25°. Strength 0.75 / 1 / 1.25 for
  levels 1–3.
- Sent through the optional extension `GetRefFoliageWaveAPI` (`foliageForceWave_t`). SP: syscall
  `CG_R_ADDFOLIAGEFORCEWAVE`, only with `cl_rendererFoliageWaves` (set by the engine when the renderer has it).
  MP: `trap->ext.R_AddFoliageForceWave` (NULL in the legacy VM).

**The wave (`tr_foliageinteract.cpp`).** The front leaves the caster at 1400 u/s (512 u in 0.37 s). A point the
front crosses gets a horizontal velocity kick along the line from the caster, spread over the time the front
(half width 48 u) needs to cross it: the kick is a smoothstep pulse, so the part between any two times is exact
(`Passed(t1) − Passed(t0)`), whatever the time step. Falloffs: the cone (soft edge 0.2 in cosine, horizontal, so
a push aimed down at the grass parts it too), the distance (strongest near the caster, nothing at the reach) and,
for the MD3 plants, the height (full within 160 u of the caster, none beyond 320 u). At most 8 live waves, the 4
newest on the GPU; a wave lives until its front crossed the reach plus the 3 s the leaves need to settle.

| | kick at strength 1 | state |
|---|---|---|
| MD3 fronds | 420 u/s on every node | `tr_plantsim.cpp`: added to the node velocities in the 120 Hz steps, integrated back to back over the steps (`waveTime`); a plant the front reaches wakes, steps in detail and ignores the plant budget, even beyond `r_plantWindDistance` |
| grass sprites | 180 u/s on the field velocity | `foliage_field.glsl` `FoliageForceWaveKick`: the spring of the field brings it back (peak ~12 u at the shins ≈ 50° at strength 1, < 1 u after 1 s) |
| leaves | 6 u largest offset | `leaf_flutter.glsl` `LeafForceWaveOffset`: stateless damped swing from the arrival of the front (1.4 Hz, e^−1.8t); also with `r_leafFlutter 0` |

The grass field covers the square around the player (`r_foliageBendFieldExtent`): grass farther away does not
react. Debug: `r_foliageInteractionDebug 1` draws the front inside the cone (orange push, magenta pull) and the
reach; `r_foliageInteractionList` prints the live waves.

GPU test (`tools/rend2/test_foliage_gl.py`, Intel + NVIDIA): field kick = python mirror within 0.3 u/s of
170, direction exact for push and pull, nothing behind the cone or beyond the reach, back to rest after 4 s;
leaf offsets = mirror within 4e-4 u, zero before the front arrives, 0.7 % left after 2.9 s.

## Cvars

| cvar | default | |
|---|---|---|
| `r_foliageInteraction` | 0 | characters push grass and plants |
| `r_foliageInteractionStrength` / `Radius` | 1 / 1 | contact depth / capsule radius scale |
| `r_foliageInteractionMaxInteractors` | 8 | colliders uploaded |
| `r_foliageInteractionNPCs` | 1 | NPCs too |
| `r_foliageForceWaves` | 1 | Force push / pull throws the vegetation (with `r_foliageInteraction`) |
| `r_foliageForceWaveStrength` | 1 | its strength, 0–4 |
| `r_foliageInteractionDebug` | 0 | 1 colliders and Force waves, 2 x2 radius and strength, 4 interaction only, 8 grass contact heat, 16 freeze, 32 player only |
| `r_plantWind` / `r_plantWindStrength` | 0 / 1 | breeze on the MD3 fronds |
| `r_plantSimDistance` / `r_plantWindDistance` | 1500 / 3000 | full step / coarse step / rest |
| `r_plantSimMaxPlants` | 96 | plants stepped per frame |
| `r_plantStiffness` | 1 | frond stiffness (new instances) |
| `r_plantDebug` | 0 | 1 centerlines, 2 segment colors, 4 bone colors, 8 contact side colors, 16 print CPU cost |
| `r_foliageBendFieldExtent` / `Size` | 1024 / 256 | grass field square |
| `r_foliageBendFieldStrength` | 1 | grass displacement scale |
| `r_foliageBendFieldRecoveryTime` / `Damping` / `Impulse` | 0.8 / 0.6 / 1 | grass spring back |
| `r_foliageBendFieldDebug` | 0 | 1 overlay, 2 covered square, 4 freeze, 8 x3 strength |
| `r_grassTrample` / `r_grassTrampleRecovery` | 0.25 / 6 | trampled part and its recovery (s) |
| `r_grassBendSegments` / `r_grassBendLodDist` | 4 / 800 | grass strips |

Commands: `r_autoFoliageList`, `r_plantSimInfo`, `r_foliageInteractionList`, `r_foliageBendFieldClear`.
A/B config: `tools/vegetation_ab.cfg`.

## Cost (estimated; not measured in game)

- Plants: per frond and sub step ~K × (3 constraint axes + contact + length) × 4 iterations; a plant_b (8 fronds × 7
  segments) at 120 Hz ≈ 2 × 8 × 7 × 4 constraint groups per frame — a few µs. 96 plants: ~0.3 ms. `r_plantDebug 16`
  prints the real time. Skinning: 4 buffer fetches per vertex, cheaper than the former 8–16 capsule loop.
- Grass: one field fetch and 6 beam pieces (log + sincos) per vertex instead of the capsule loop; strips multiply the
  near vertex count by `r_grassBendSegments` + 1 / 2 (×2.5 at 4). Field: one fragment pass over 256² texels.

## Limits

- MD3 only, single frame map objects; misc_models baked into the BSP are not plants.
- Fronds are found from the topology; a frond whose base is not near the model root and not a separate piece falls
  into the group it is welded to (it then bends as one frond).
- The lamina follows the centerline rigidly per segment (no leaflet flutter, no twist DOF beyond the transport).
- The grass field covers the square around the player; NPCs farther away don't touch grass.
- Grass taller than the legs is pushed at the shins only.

## Validation

- MSVC builds of both renderers.
- `tools/rend2/test_foliage_gl.py` (Intel UHD and NVIDIA RTX 2060): 88 programs compiled and linked (lightall,
  generic, fogpass, velocity with the plant skin, every structural surface sprite permutation, the field passes),
  the plant skin, blade and field checks above.
- `R_BuildPlantSkeleton` run on the five stock plant models (bones in range, fronds as expected).
- Not run in game: frame cost, look, TAA, shadows — `tools/vegetation_ab.cfg`.
