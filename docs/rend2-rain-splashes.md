# rend2 rain impact splashes (`r_rainSplashes`)

Real rain impact events: the GPU notices the frame in which a rain drop crosses the first rain-exposed surface
under it, and a short-lived splash is drawn there. There are no CPU traces, no compute shaders, no append buffers
and no readbacks. The rain itself is not changed: the drops fall on as before, and `weather.glsl` is untouched.
The impact is a side effect written next to each drop's state.

Off by default. With `r_rainSplashes 0` the rain runs the exact legacy update program and 24-byte records.

## Why the rain needed a collision test

`weatherUpdate.glsl` never collides. A drop falls until it is below the map's bottom and then respawns at the top.
`weather.glsl` only hides a drop that is below the static rain occlusion map (`tr.weatherDepthImage`, a top-down
orthographic D16 1024² depth of the world plus the weather brushes, `GenerateDepthMap`). So the respawn is not an
impact. The impact is the frame in which the drop goes from above that map to below it.

## Collision math (`weatherUpdate.glsl`, `USE_RAIN_SPLASHES`)

The weather projection `u_WeatherMvp` is orthographic, so `w = 1`, and the depth is affine in world z. It grows
downward, with the map top at 0: `depth(p) = (M p).z * 0.5 + 0.5`, and a drop is under the occluder when
`depth(p) > texture(weatherDepth, uv(p))`, the same convention as `weather.glsl`.

Per drop and frame:

```
slot   = gl_VertexID / particlesPerChunk          VBO chunk slot (9 per weather type)
p0     = position + zoneOffset[slot]              world space; before the XY wrap and the respawn
p1     = p0 + velocity * dt
s      = weatherDepth(uv(p1))                     the occluder of the column the drop lands in
d0     = depth(p0) - s,  d1 = depth(p1) - s
impact if d0 <= 0 and d1 > 0
t      = d0 / (d0 - d1)
hit    = mix(p0, p1, t)                           sub-frame crossing
hit.z  = height of weatherDepth(uv(hit))          snapped onto the surface (row 2 of M solved for z)
```

Both ends are compared with the occluder of the **landing column**:

- a drop that already went under a roof (it is on its way to the floor below) is below that column's occluder
  at both ends, so the floor under a roof never gets hit;
- a drop blown sideways in under an overhang is also below the overhang at both ends, so it never hits;
- wind drift inside the step moves the sample with the drop, so no iterative solve is needed.

Rejections:

| test | why |
|---|---|
| `\|surface.z - hit.z\| > \|Δz\| + tolerance` | the snapped point must be on the step's vertical span (tolerance = 4 + one texel) |
| a ±1 texel neighbour differs by more than `max(3 texels, 32)` units | roof edges and texel quantisation; slopes and stairs stay below it |
| `weatherDepth` ≠ world-only depth (maps with weather brushes) | weather brushes are invisible; rain stops on them, splashes must not |
| outside the map's XY | clamp-to-edge texels are not surfaces |

### World-only depth for weather-brush maps

With inside or outside weather brushes, `GenerateDepthMap` bakes the brush faces into the occlusion map. That
includes the top of an inside brush above the real roof, and in outside mode the map ceiling everywhere outside
the brushes. For those maps a second D16 1024² map, `tr.weatherSurfaceImage`, gets the same world draw without
the brushes (`RenderWeatherWorldDepth`, at load only, +2 MB). An impact counts only where the two maps agree.
Maps without brushes skip it (`weatherSystem_t::surfaceMapValid`).

## Impact state

The rain slot's records switch to `rainSplashVertex_t` (`tr_weather.cpp`):

```
struct rainSplashVertex_t      // 40 bytes, interleaved XFB order
{
    vec3_t position;           // var_Position   byte 0   chunk local
    vec3_t velocity;           // var_Velocity   byte 12
    vec4_t impact;             // var_Impact     byte 24  world xyz, state w
};
```

`impact.w` allows **one impact per fall**:

| w | meaning |
|---|---|
| (0, 1] | life of this fall's splash, 1 → 0 over `r_rainSplashLifetime` |
| [-1, 0) | −life of a splash from an earlier fall that is still running (the drop respawned) |
| −2 | this fall's splash is over; no more impacts until the respawn |
| 0 | no splash, may hit |

Without that state, a drop that went under a slope or a stair and drifted with the wind into a lower column came
out above it and hit again (1.8% of all impacts in the harness). On respawn, (0, 1] becomes [-1, 0) and −2
becomes 0. Renderers read `life = w > -1.5 ? |w| : 0`.

There is at most one live splash per drop. For lifetimes shorter than a fall through the map, this never drops
an event.

## VBO, transform feedback, programs

| | before | `r_rainSplashes 0` | `r_rainSplashes 1` (rain slot) |
|---|---|---|---|
| record | 24 B | 24 B | 40 B |
| XFB varyings | position, velocity | same | + `var_Impact` (`XFB_VAR_IMPACT`) |
| update program | `weatherUpdate` | `weatherUpdate` (same code path) | `weatherUpdate` + `USE_RAIN_SPLASHES` (`tr.weatherUpdateSplashShader`) |
| attributes | POSITION, COLOR | same | + TEXCOORD0 (vec4 impact) |

- The rain slot allocates both ping-pong buffers at the 40-byte capacity (`GenerateRainModel(…, splashCapable)`).
  Toggling the cvar re-uploads fresh particles in the other layout into the same buffers (`SwitchRainLayout`,
  `glBufferSubData`). The pool doesn't leak, and the switch happens only at a frame's simulation, never between
  the views of a frame. Other weather types keep 24-byte buffers.
- `weather.glsl` always gets the first two attributes (the stride follows the layout).
- The update samples `weatherDepth` (`TB_SHADOWMAP`) and the world-only map (`TB_NORMALMAP`) in the vertex shader.
- The update needs each slot's **world** offset. The CPU inverts the camera-dependent `zoneMapping` into
  `slotZones[9]` (`u_ZoneOffset[9]`). The simulation runs at the frame's first view (as before), so impacts are
  where that view put the slots.
- All 9 chunks are still simulated every frame. Chunk culling never touches the update, so it can't lose impacts.

## Splash draw (`weatherSplash.glsl`, `tr.weatherSplashShader`)

There is one `GL_POINTS` draw per visible chunk slot, `firstVertex = particlesPerChunk * slot`, sort 15 /
`SS_SEE_THROUGH`, right after the rain. It is premultiplied over (`ONE, ONE_MINUS_SRC_ALPHA`), depth tested,
with no depth write. Glow alpha is 0, so bloom behind it is untouched.

- **VS**: passes the record through; it samples nothing.
- **GS** (points → triangle strip, max 8 vertices):
  - Emits nothing for a drop without a live splash. That test is the first thing it does, before any texture
    fetch.
  - Beyond the fade distance (1500, or the rain's own if shorter) it also emits nothing.
  - For a live splash it takes the normal from 4 weather-depth neighbours around the impact, tilt-limited to 45°
    because the D16 map is coarse. The light is the rain's (`r_rainStreakLighting`: the merged light grid, else sun
    ambient + half the sun, else 1) × the weather tint (acid rain stays green) × 0.8. It then emits:
    - **crown**: a quad in the surface plane, lifted 0.5 units, randomly rotated, radius
      `size · (0.35 + 0.65 √age)`;
    - **spray**: a camera-facing sheet along the normal, height `1.4 size · sin(π √age)`. Only within half the
      fade distance; beyond that it would be a few pixels.
- **FS**: procedural, with no texture.
  - The crown is a ring band at 0.8 radius that widens and thins out, over a faint wet disc.
  - The spray is a thin streak plus three seed-placed droplets.
  - Alpha × `r_rainSplashOpacity` × distance fade; colour = light × alpha.

**Culling.** Stored impacts are world space. After a camera chunk remap, a slot's live splashes can still be in
the zone the slot had before. The CPU keeps each slot's zone at simulation time, its previous zone and the remap
time (`RB_TrackSplashSlots`). For `lifetime + 100 ms` after a remap, the slot's box is the union of both zones.

## Ripple hook

`rainSplashVertex_t::impact` is the hook. A later event-driven ripple pass can draw the same VBO through a
splash-like geometry shader into a camera-centred ripple height map that lightall samples, without touching the
simulation. The procedural rings of `r_weatherPuddleRipples` are unchanged. The splash crown already reads as a ring on
wet ground and puddles.

## Cvars

| cvar | default | |
|---|---|---|
| `r_rainSplashes` | 0 | 1 = impact detection + splashes (rain slot only) |
| `r_rainSplashSize` | 6 | crown radius, world units (1–64) |
| `r_rainSplashLifetime` | 350 | ms (50–2000) |
| `r_rainSplashOpacity` | 0.5 | 0–4 |
| `r_rainSplashDebug` | 0 | cheat: 1 impact points, 2 crossing test, 3 trajectories |

Debug views:

| mode | shows |
|---|---|
| 1 | every live impact as a screen-sized marker **through walls**, yellow when new → red when dying |
| 2 | every drop within the fade distance: green above its column's occluder, red under it, **cyan** = crossing this frame and accepted, **magenta** = crossing rejected (edge / brush / tolerance / already hit this fall) |
| 3 | the last 50 ms of each drop's trajectory, green above, red under the occluder |

- The weather depth itself is shown by `r_debugWeather 1` (the existing blit).
- `r_weatherDebugChunks 1` also draws the splash slot boxes: blue drawn, purple culled. It prints
  `splash slot N zone (x y) [+ previous zone] drawn|culled`.
- In any debug mode, an async `GL_PRIMITIVES_GENERATED` query on the first splash batch of a frame is read back
  once the GPU has it, with no stall. It prints the live splash count next to the upper bound "every column
  exposed": `drops × lifetime × fall speed / map height`. Live counts far above that bound would mean repeated
  impacts.
- `r_speeds 100` has a GPU timer "Weather splashes" next to "Weather simulation" and "Weather draw". The
  `Weather:` line counts splash draws and culled splash slots.

## Measured offline (no game launch)

Harness: the real `weatherUpdate.glsl` and `weatherSplash.glsl`, linked with the engine's XFB setup, run through
transform feedback on the installed drivers. The scene is a synthetic 2048×2048×1024 map, with a D16 256² weather
map at 8 units/texel, nearest filtering like `R_CreateImage` depth images. It contains:

- flat ground at 100;
- a roof at 400 over that ground;
- a bridge deck at 300;
- stairs of 16 every 32;
- 45° and 70° slopes;
- an invisible inside-brush top at 700 that exists only in the weather map.

The run is 40 000 drops in 2 chunk slots (one offset 300/-200), wind 0.25/-0.15, 16 ms steps, 900 frames. Intel
UHD and RTX 2060 give identical results.

| check | result |
|---|---|
| XFB link, varyings | `var_Position` @0, `var_Velocity` @12, `var_Impact` @24, 40 B record |
| impacts | 640 541 |
| repeated within one fall | **0** (1.8% before the one-impact-per-fall state) |
| state machine errors (life decay, respawn) | 0 |
| floor under the roof | **0** hits (roof 15 282) |
| invisible brush top | **0** (6 353 with the brush check off) |
| impacts outside their slot's zone | 0 |
| snap error on flat ground, roof, bridge, slopes | ≤ 11 units (70° slope: 2.75 × 4-unit tolerance band) |
| off-surface impacts | only on **stair step edges** (1 883 of 20 155 stair hits): the texel straddling a step reports the upper step, so the splash sits up to half a texel over the lower step, where `weather.glsl` also stops the rain |

GPU time, update of 45 000 drops (heavy rain, 5000 × 9 chunks), min of 6 interleaved medians:

| | Intel UHD | RTX 2060 |
|---|---|---|
| HEAD `weatherUpdate` (24 B) | 0.209 ms | 0.045–0.060 ms |
| `r_rainSplashes 0` | 0.209 ms | same as HEAD |
| `r_rainSplashes 1` (40 B + detection) | 0.317 ms | 0.109 ms |

Most of the extra update cost is the 16 bytes per drop that are read and written each frame (1.08 → 1.8 MB each
way). The detection branch alone measured +0.006 ms on Intel.

Splash draw, 1920×1080 with two RGBA16F targets, no depth test (every fragment shaded). This is a worst case: all
45 000 drops sit inside a 2048² map in view, 20 420 of them with a live splash. In a game map the same density
covers 6000², and most of it is outside the fade distance or in culled chunks.

| | Intel UHD | RTX 2060 |
|---|---|---|
| splashes, fade 1500 (spray LOD) | 1.39 ms | 0.32 ms |
| splashes without the spray LOD | 1.70 ms | 0.36 ms |
| no live impact (GS early out, 45 000 points) | 0.12 ms | 0.016 ms |

Not measured: in-game GPU timings, screenshots, real maps.

## Known limits

- Stair and step edges: up to half a weather texel of overhang at the upper step's height (see above). The real
  maps' texels are larger than the harness's (map size / 1024).
- Only the rain slot splashes. Snow, sand and the rest keep the 24-byte path.
- Moving geometry (movers, doors, entities) is not in the static occlusion map, the same as for the rain.
- With portals or mirrors, the frame's first view sets the chunk slots, as for the simulation itself.
- Toggling `r_rainSplashes` restarts the rain particles once (both layouts share the buffers).

## Validation checklist (in game)

Use `docs/samples/rain-splashes.cfg`, on a rain map and with `rain` / `heavyrain` + `constantwind`:

1. Flat ground, roof, bridge, stairs, steep slope: splashes on the surface (`r_rainSplashDebug 1` markers sit
   on the surfaces).
2. Under-roof interior: no splash on the floor. Courtyard floor next to it: splashes.
3. `r_rainSplashDebug 2`: white never appears; crossings are cyan, and magenta only along roof edges and brush
   tops.
4. Wind (`constantwind ( 200 0 0 )`): splashes stay on surfaces. `r_rainSplashDebug 3` shows the tilt.
5. Heavy rain: the printed live count stays below the "every column exposed" bound.
6. Walk across a chunk boundary (every 2000 units, `r_weatherDebugChunks 1`): no splashes vanish; boxes show
   "+ previous zone" for a moment.
7. Look at distant impacts: they fade out by 1500 units.
8. `r_speeds 100`: "Weather simulation", "Weather draw" and "Weather splashes" GPU times, with
   `r_rainSplashes 0` / `1`.
