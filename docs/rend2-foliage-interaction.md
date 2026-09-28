# rend2 foliage character interaction (`r_foliageInteraction`)

Characters push grass and ferns aside as they walk through them. The player and the nearest NPCs are sent from cgame to
the renderer as a few vertical capsules. The vertex shaders bend the plants away from them.
- There is no per plant physics, no Verlet and no stored state.
- When a character leaves, the plant goes back to its wind pose on the same frame.
- A persistent spring back is left for a separate task.

`r_foliageInteraction 0` (the default) runs none of this code. cgame sends nothing, and the shaders take a uniform
branch.

This task also adds the root bend of `FOLIAGE_PLANT` MD3 models, such as ferns (`r_plantWind`). The interaction on
ferns is added on top of that bend. Before this task, ferns had no wind motion at all.

**Status: built and compiled offline only, never run in game.**

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

## Collider representation

The capsule is the vertical axis through the centre of the bounding box. The data is `(axis x, y, feet z, head z)` +
`(radius, velocity x, velocity y)`, where radius = the half width of the box (15 for a humanoid) ×
`r_foliageInteractionRadius`.

Distance is measured to the segment `feet + r .. head − r`, so both ends are round.
- At the feet, grass (tested at 40 % of its height) is reached by the lower legs.
- In a jump, the contact lifts off the grass (CPU mirror: a 40 unit jump gives contact 0 on grass).
- A tall fern frond is still reached higher up the body.
- Crouching shortens the capsule. A frond at z 50 drops from contact 1.00 to 0.01.

A sphere around the origin would have bent grass near the head and missed the feet.

## Max interactors and overflow

| | |
|---|---|
| `MAX_FOLIAGE_INTERACTORS` | 16 (UBO array size, shader loop bound) |
| `r_foliageInteractionMaxInteractors` | 8 by default, 1–16. The first N of the list are uploaded: the player, then the nearest NPCs |
| `r_foliageInteractionNPCs 0` | player only |

The vertex loop runs `count` times, where count is a uniform. Each collider first does an early out on horizontal
distance (`reach = 1.75 r`). There is no spatial acceleration, since the count is small and fixed.

## Current / previous state (motion vectors)

The `FoliageInteraction` UBO (slot 12) is appended once per scene in `RB_UpdateConstants`:
- `params`: current count, previous count, strength, debug no-wind
- `current[16×2]`
- `previous[16×2]`

"Previous" is **exactly the list uploaded last frame**. It is not matched by id, so a character that just joined or
left the set gives the true geometry change.

The history is reset to "previous = current" (no motion) when:
- frames are not consecutive,
- the time gap is over 250 ms or negative (cut, pause, map load),
- `tr.temporalHistoryValid` is false,
- or the colliders are frozen (debug).

The velocity pass evaluates the bend twice:
- grass: `surface_sprites` with `VELOCITY_PASS`
- MD3: `velocity.glsl`

The previous evaluation uses the previous colliders, the previous time and the previous model matrix. So TAA and
motion blur get the plant's real motion, including the snap when a character steps in.

## Views

The block is per scene and in world space. The main view, mirrors / portals, sun cascades and point shadow cubes all
draw with the same colliders.
- **Shadows** show the same deformation as the lit surface, because the same programs draw them with the same values.
- **Mirrors and portals** show the same world, so the world space colliders are correct there too.
- **The sky portal view** lives at other coordinates. Its draws get interaction 0 through the per-draw uniforms
  (`VPT_SKYPORTAL`).
- **Cubemap capture frames** never have a stamped list.

## Shader response (`glsl/foliage_interact.glsl`)

For each collider, with `q` the reference point:

```
contact = 1 − smoothstep(0.6 r, 1.75 r, distance(q, capsule segment))
n       = normalize(q.xy − axis.xy)        on the axis: move direction, else a hash direction
s       = clamp(|v.xy| / 320, 0, 1)
dir     = normalize(n + 0.6 s v̂)           walking: biased along the walk
mag     = contact · (1 + 0.35 s max(n·v̂, 0))
bend   += dir · mag                        × r_foliageInteractionStrength
```

- Contact is the primary term. A player standing still keeps the plant parted: a tuft 10 units from the axis gets
  contact 0.98 and leans 44°. Velocity only biases the direction and adds up to 35 % in front of the body.
- Contact by distance for r = 15, from the CPU mirror:

  | distance | 0 | 6 | 9 | 12 | 15 | 18 | 21 | 24 | 26 |
  |---|---|---|---|---|---|---|---|---|---|
  | contact | 1.00 | 1.00 | 1.00 | 0.90 | 0.70 | 0.45 | 0.21 | 0.04 | 0 |

### Combining with wind: one bend, applied once

`bend` is a horizontal **bend vector**: tip displacement per unit of stem length.
`FoliageApplyBend(v, bend, weight)` turns the stem vector `v` (vertex − root):

```
bent = normalize(v/|v| + (bend·weight, 0)) · |v|        length kept: bends, never stretches
angle(v, bent) ≤ 65°                                    clamped by a slerp limit: never folds over
```

The limit holds for every stem direction and every strength. The CPU mirror checked 20000 random stems and pushes:
the maximum turn is 65.00° and the length is kept.

- **Ferns**: `finalBend = windBend + interactionBend`, summed and applied once.
- **Grass**: the existing wind code (legacy sway or `r_foliageWind` breeze) is unchanged. The interaction then bends
  the stem, which already includes skew and wind, further. That is the same composition applied in sequence.
- There is no world space translation after the wind in either case.

## Grass vs fern

| | grass (SurfaceSprites) | fern (`FOLIAGE_PLANT` MD3) |
|---|---|---|
| shaders | `surface_sprites` (all non FX / FACE_UP permutations) | lightall (+SPOM), generic, fogpass (+SPOM), velocity |
| root | the sprite anchor | the bottom centre of the model bounds (`foliageMins/Maxs`) |
| weight | bottom vertices 0, top vertices 1 | `t^1.5`, t = distance from root / plant size (object space) |
| reference point q | anchor + 40 % of the height: the feet part it | the vertex rest position: tall fronds are pushed higher up |
| bend per | **tuft**: everything depends on the anchor only, so all cross / tri cards move their top edge by the same vector (mirror: identical deltas) | vertex: fronds near the body bend more, the whole plant parts |
| normals | unchanged (billboards) | normal and tangent rotated with the stem (Rodrigues), lightall |
| wind | existing `r_foliageWind` | new `r_plantWind`: a per plant breeze lean + sway in the `r_foliageWindDirection` basis |

- **Culling.** Grass: the sprite lod bounds are already padded by the largest card (height + skew), which covers any
  turn of the stem. MD3 plants: `R_PlantBendCullMargin` grows the cull bounds of map object models by half the plant
  size while plant bend is active.
- **Batching.** `tess.leafFlutter` became `tess.foliageMotion` (`FOLIAGE_LEAF` / `FOLIAGE_PLANT` / none). It is a batch
  key in both submit loops, and the per-draw uniforms are written at every draw site, since the values stick per
  program.

## Cvars

| cvar | default | |
|---|---|---|
| `r_foliageInteraction` | 0 | 1 = characters bend grass and `FOLIAGE_PLANT` models (plants need `r_autoFoliage ≥ 1`) |
| `r_foliageInteractionStrength` | 1 | 0–4 (1 = a stem in full contact leans ~45°, never more than 65°) |
| `r_foliageInteractionRadius` | 1 | capsule radius scale, 0.5–3 |
| `r_foliageInteractionMaxInteractors` | 8 | 1–16 colliders, player first |
| `r_foliageInteractionNPCs` | 1 | 0 = player only |
| `r_plantWind` | 0 | Enable fern root-bend breeze (0/1); direction / speed from `r_foliageWindDirection` / `r_foliageWindSpeed` |
| `r_plantWindStrength` | 1 | Root-bend strength, 0–4 (1 ≈ 14° at the strongest gust) |
| `r_foliageInteractionDebug` (cheat) | 0 | bits: 1 draw colliders (yellow player, cyan NPC, + velocity line), 2 ×2 radius and strength, 4 interaction only (no wind), 8 contact heat (blue 0 → yellow 1), 16 freeze colliders, 32 player only |
| `r_foliageInteractionList` (command) | | prints the submitted and uploaded colliders |

`tools/foliageinteract_ab.cfg` binds the A/B screenshots, the debug toggles and a timing run.

## Cost

These are static NVIDIA vertex program sizes (`!!NVvp5.0` instructions, RTX 2060 driver).

| program | HEAD | now |
|---|---|---|
| sprite AUTO_GRASS | 337 | 527 |
| sprite AUTO_GRASS + velocity | 505 | 868 |
| sprite legacy vertical | 252 | 442 |
| lightall LIGHT_VECTOR | 213 | 505 |
| velocity (alpha test) | 143 | 570 |

- The growth is the collider loop plus the plant and interaction code.
- When the interaction is off, and for every non plant MD3 draw, all of it is skipped by uniform branches.
- The per collider body runs only for colliders inside the horizontal reach. Its cost is about one capsule distance,
  one smoothstep and two normalizes.
- **Frame timings for 0 / 1 / 4 / 8 / 16 colliders have not been measured.** Run F3 in the cfg in game.

## Validation done

- MSVC Release x64 builds, no new warnings: `rd-rend2`, `rdsp-rend2`, `openjk.x86_64`, `openjk_sp.x86_64`,
  `jagamex86_64`, `cgamex86_64`.
- Offline GLSL compile and link on Intel UHD and NVIDIA RTX 2060, with the libraries spliced as at run time. There are
  22 cases, all OK:
  - lightall × 5
  - generic × 3
  - fogpass × 2
  - velocity × 3
  - surface_sprites × 9 (legacy, AUTO_GRASS, ±VELOCITY_PASS, USE_FOG, FACE_CAMERA, FACE_UP, FX, FLATTENED)
- CPU mirror (python) of the falloff and bend. It checks:
  - the length is kept and the turn stays ≤ 65°
  - the root stays fixed
  - a standing player keeps the push
  - a jump lifts it off the grass
  - crouching lowers the reach
  - contact falls monotonically with distance
  - the velocity bias works
  - all cards of a tuft move the same

## Not validated (in game, by the user)

- **Third person**: stand in a fern, walk slowly, run, strafe, jump, crouch.
- **Camera independence**: orbit the camera around a still player (the plants must not follow), move the camera while
  the player stands, and a cinematic camera.
- **NPCs**: one NPC walking through grass, then several.
- **Plants**: cross card grass, ferns.
- **Temporal and shadows**: TAA (`r_smaa 2`) / motion blur ghosting, sun shadows.
- **Debug overlay (bit 1)**: compare the capsule with the body.
- **Timings and debug video.**
- **Which stock models are `FOLIAGE_PLANT`** (`r_autoFoliageList`): the fern side needs map models whose names contain
  "fern" / "plant".

## Persistent bend field (`r_foliageBendField`)

Without it, a plant goes back to its wind pose the frame the character leaves it. The field adds a short spring-back.
There is no per plant state and no CPU physics. Code: `tr_foliagefield.cpp`, `glsl/foliage_field.glsl`, and
`FoliageFieldBend` / `FoliageCharacterBend` in `glsl/foliage_interact.glsl`.

### Representation and world mapping

- Two `RGBA16F` square textures, ping-pong. Size is `r_foliageBendFieldSize` (64 / 128 / 256, latched, default 128).
- Per texel: `rg` = bend vector d (same units as the direct push, tip offset per stem length), `ba` = its velocity v.
- The square is `r_foliageBendFieldExtent` world units wide (default 1024, so 8 units per texel at 128), centered on the
  **player collider**, never the view origin. Z is ignored: it is a 2D ground map.
- `uv = (worldXY − center) / extent + 0.5`. The border fades out over the outer 3 %; outside the square the term is 0.
- Memory: 2 × 128² × 8 B = 256 KB (64: 64 KB, 256: 1 MB). The textures always exist, so the cvar toggles without
  `vid_restart`.

### Update (once per frame, fragment pass; rend2's GL 3.2 baseline has no compute)

The first world scene of the frame queues `RC_FOLIAGE_FIELD` right after its constants. It runs before that scene's
draws and reads the colliders from the scene's `FoliageInteraction` block. Per texel:

```
prev    = texelFetch(previous, pix + shift)            // 0 outside: newly exposed strip
target  = Σ colliders on the ground: push(texel center, feet + 10) × strength
v      += Σ contact × speed factor × walk dir × 12 × r_foliageBendFieldImpulse × dt
repeat ceil(dt·60) (≤ 6) times, h = dt / n:            // semi-implicit Euler
    v += (k (target − d) − c v) h ;  d += v h
clamp |d| ≤ 1.5, |v| ≤ 24; snap tiny values to exactly 0
```

- `k = ω²`, `c = 2ζω`, with `ω = 3 / (ζ T)`. T is `r_foliageBendFieldRecoveryTime` (default 0.8 s) and ζ is
  `r_foliageBendFieldDamping` (default 0.6).
- There is no blur. The state of a patch never spreads to other patches.
- **Airborne colliders** write nothing. cgame sets the new `FOLIAGE_INTERACTOR_AIRBORNE` flag from `groundEntityNum`.
  The direct push still uses the capsule height, so jumping over grass leaves no trail.
- CPU mirror (`ff_sim.py`, 0.3 s of contact at 200 u/s):

  | Settings | Back to 5 % after leaving | Overshoot | Frame-rate independent |
  |---|---|---|---|
  | defaults | 0.83–0.88 s | 8 % | yes: 30 / 60 / 144 fps |
  | ζ = 1 | 1.4 s | none | — |
  | T = 0.5, ζ = 0.4 | 0.3 s | 22 % | — |

### Scrolling and reset

- The center snaps to whole texels. When the player moves, the update reads the previous state at the integer texel
  shift. Stored bends stay on their world patches, with no resampling blur, and the newly exposed strips start at rest.
- A frozen or paused frame (dt = 0) keeps the old center, and the next update applies the whole shift.
- **Clear** happens on:
  - the first use or re-enabling
  - map load (`RE_LoadWorldMap` → `R_FoliageInteractionReset`, which was never called before) and `vid_restart`
  - a non-consecutive frame (menu, pause, time jump > 250 ms)
  - a shift of more than half the field (teleport)
  - an `r_foliageBendFieldExtent` change, or `r_foliageBendFieldClear`
- **Camera cuts do not clear it**: the field is gameplay state.

### Views and temporal

- Only the first world scene of the frame writes the field. Every view of the frame samples the same textures on
  `TB_FOLIAGEFIELD` (23) and `TB_FOLIAGEFIELD_PREV` (24), bound once per frame: main view, mirrors, portals, sun and
  point shadows, depth prepass, velocity.
- The sky portal is excluded, as for the colliders.
- The velocity pass samples the previous frame's state at the previous center. The motion vectors of the swinging
  grass are therefore exact. This is the field's own history, not the screen TAA history.

### Combination

```
final bend = wind + direct + field × (1 − heat)      // heat = direct contact of this vertex / tuft
```

- While a character stands in the plant, the direct push leads, and the field has followed the same push.
- As the character leaves, the direct term fades out and the field takes over without a jump, then springs back.
- **Grass** samples the field once at the tuft anchor, so all cards of a tuft stay coherent.
- **MD3 plants (ferns)** sample it at the root. Every vertex gets root field × its `PlantBendWeight`. This keeps the
  fronds rigid; with 8-unit texels, per vertex sampling would shear them.
- **Leaf flutter (trees)** never samples the field.

### Cvars

| Cvar | Default | Meaning |
|---|---|---|
| `r_foliageBendField` | 0 | on / off (needs `r_foliageInteraction`) |
| `r_foliageBendFieldSize` | 128 | texels per side (latched) |
| `r_foliageBendFieldExtent` | 1024 | world size of the square |
| `r_foliageBendFieldStrength` | 1 | scale of the field term |
| `r_foliageBendFieldRecoveryTime` | 0.8 | seconds to rest |
| `r_foliageBendFieldDamping` | 0.6 | ζ; 1 = no swing past rest |
| `r_foliageBendFieldImpulse` | 1 | extra momentum along the walk |
| `r_foliageBendFieldDebug` | 0 | bits, see below |

`r_foliageBendFieldDebug` bits:
- 1: overlay in the lower left corner. Left: vectors (red / green = +x / +y). Right: magnitude heat. The player is the
  cross.
- 2: outline of the covered square.
- 4: freeze.
- 8: ×3 strength.
- 16: field only (no direct push).
- 32: direct push only.

Commands: `r_foliageBendFieldClear`; `r_foliageInteractionList` also prints the field state. A/B config:
`tools/foliagefield_ab.cfg`.

### Cost (estimated, not measured)

- One 128² fragment pass looping over ≤ 16 colliders, expected well under 0.05 ms. It shows as "Foliage field" under
  `r_speeds 100`.
- Per vertex: +1 bilinear fetch in every grass / plant pass; +2 in the velocity pass.

### Failure cases and limits

- 2D: grass under a bridge the player crosses gets bent too.
- The field is also active indoors.
- 8-unit texels cannot draw single-blade trails.
- The field covers ±512 units at the defaults. Beyond that, plants have only the direct push; they fade in over the
  border.
- A save / load or `map_restart` starts from rest.
- A very fast move of more than half the extent per frame clears the field.
- NPC trails are kept only while the NPC is within the player-centered square.
- The spring pulls toward the push of all colliders summed. Two characters in one patch add up, clamped at 65° by
  `FoliageApplyBend`.

### Validation of the field

- Done: both renderers, SP game and MP cgame build (MSVC). 24 GLSL cases compile and link on Intel and NVIDIA,
  including `foliage_field` (update and `DEBUG_VIEW`). CPU mirror of the spring.
- Not done (in game, by the user):
  - walk through / stop / turn / circle / sprint
  - NPC crossing
  - teleport, `map_restart`
  - cinematic camera, mirror
  - TAA ghosting
  - dense grass
  - GPU timings
  - debug captures
