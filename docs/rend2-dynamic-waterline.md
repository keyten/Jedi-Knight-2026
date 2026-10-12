# Dynamic visual waterline

## Ownership

```text
collision BSP contents
  +-> gameplay movement / swimming / damage / EV_WATER_*       (unchanged)
  +-> static convex liquid brushes -> per-ray froxel coverage   (unchanged)
  +-> legacy tint/FOV fallback for renderer-unsupported liquid  (unchanged)

modern water body + render time + macro waves + interaction height
  +-> displaced surface vertices and fragment optics            (existing)
  +-> camera visual-waterline query                              (new)
       +-> primary-view SUBMERGE/EMERGE hysteresis
       +-> lens submerged state
       +-> underwater-particle camera visibility
```

The renderer query never changes collision contents. `waterlineQuery_t` reports
both states so `r_waterlineDebug` can expose disagreements rather than hiding
them.

## Audited paths

| Consumer | Boundary/state | Result |
| --- | --- | --- |
| Movement, swimming, damage, gameplay water events | BSP contents | Unchanged and authoritative |
| `cg.refdef.viewContents` | BSP contents | Still used for gameplay-compatible fallback classification |
| Legacy liquid tint | BSP contents plus `r_volumetricWaterActive` | Suppressed only when rend2 owns that class; no new full-screen tint |
| Legacy FOV wobble | BSP contents plus `r_volumetricWaterActive` | Suppressed for rend2-owned classes because its flat-plane switch conflicts with displaced geometry |
| `r_volumetricWaterActive` | Map liquid-brush availability/class mask | Unchanged |
| Froxel membership | Static convex BSP liquid brush planes, evaluated per ray/froxel | Unchanged |
| Modern surface | Displaced render geometry and depth | Unchanged; naturally supports a waterline crossing the screen |
| Surface air/water side | Fragment-to-camera vector against the displaced geometric face normal | Unchanged; micro normals only affect optics |
| Snell/TIR | Per-fragment displaced geometric side, macro + interaction + micro optical normal | Unchanged; no global camera boolean selects the optical interface |
| Underwater particles | BSP brush/body plus visual surface-height gate | Updated; a crest may contain the camera and a trough may expose it |
| Lens water | Primary rendered camera, visual query and hysteresis | Updated; sole owner of local `SUBMERGE`/`EMERGE` |
| Portal/mirror/sky/depth views | No lens transition input | Unchanged; cannot overwrite primary-camera state |
| Viewmodel, saber, hands, world entities | Per-draw froxel lookup | Unchanged; no separate camera-underwater branch |
| SSR/SSGI/SMAA/motion history | Camera cut, projection, viewport and frame continuity | No water reset; water crossing alone does not invalidate their reprojection |
| Froxel history | BSP camera liquid class and medium key | Existing targeted cut remains at the static medium boundary |
| Auto exposure/tone map | HDR luminance adaptation | No water-state branch and no forced reset |

## Surface-height query

`R_WaterlineQuery` runs only for horizontal, static-world, modern water bodies.
It first tests the actual interface triangles in XY, not just body bounds, and
then chooses the closest projected plane. This avoids selecting a disconnected
body or a hole in a concave projection. It returns the body ID, BSP gameplay
class, base plane, displaced height, relative camera height, and query cost.

The displacement uses the same render time (`r_waterWaveTime` included), body
profile, quality component selection, wind-transition wave terms, shallow
attenuation, flow advection, and body-local interaction-height field as the
vertex shader. Shore attenuation follows the same rigid/natural boundary
curves. Normal-map/micro-slope detail is deliberately excluded.

If render-only geometric displacement is disabled or a body fell back to its
original BSP mesh, the visual boundary remains the base plane even though
wave normals continue to shade it.

## Hysteresis and lens events

The primary camera uses a Schmitt trigger around the queried surface:

- above -> below requires `relativeHeight < -band`;
- below -> above requires `relativeHeight > +band`;
- default band is 1.5 world units (`r_waterlineHysteresis`);
- vertical speed progressively reduces the band to 25% between 24 and 200
  units/second, with a 0.25-unit minimum, so a real dive responds quickly;
- an emerge film requires at least 100 ms of prior submersion and has a 250 ms
  duplicate guard.

The renderer primary-view transition is the only local lens event owner.
Cgame's BSP/player `LENSWATER_EMERGE | LENSWATER_F_LOCAL` notification is
consumed but not emitted into the lens simulation. Spatial splash/spray events
remain independent physical inputs. Portal, reflection, skybox and depth views
are rejected before state evaluation.

## Partial submersion, near plane, and froxels

No split-screen mask or extra full-screen pass was added. The displaced water
mesh, depth-tested surface composite, and per-draw froxel lookup continue to
make upper air and lower water portions of a view independently visible.

Near-plane handling remains standard homogeneous triangle clipping on the
subdivided water mesh. The existing refined mesh prevents a large source
triangle from disappearing at the eye plane, while downward boundary skirts
close displaced edge gaps. The global near distance is not changed. Water is
already rendered two-sided for below-surface Snell/TIR. This change does not
add a depth clamp, which would incorrectly paint water over foreground pixels.

Froxel liquid volumes deliberately remain the static BSP brushes. Rebuilding
them for waves would be expensive and would destabilize temporal media. The
remaining discrepancy is a thin layer bounded by macro/interaction
displacement; the displaced opaque/transmissive interface covers the seam in
normal views. The lens and particles use the corrected boundary, while distant
geometry retains exact BSP-clipped volumetric paths. Strong custom waves can
still expose this approximation at extremely grazing angles.

## Debug and deterministic tests

- `r_waterlineDebug 1`: once-per-second gameplay membership, class, body ID,
  base/displaced height, relative height, visual state, hysteresis band, last
  transition, pending lens event, Snell side, and CPU query microseconds.
- `r_waterlineDebug 2`: also prints a 64-sample relative-height history (`#`
  water, `|` hysteresis band, `.` air, `?` no modern body).
- `r_waterWaveTime <seconds>`: freezes analytic wave phase; `-1` follows game
  time.
- `r_waterWaveAmplitude <0..4>`: simplifies or strengthens macro waves (`0`
  removes geometric wave height).
- `r_waterImpulse [strength] [radius]`: deterministic crosshair interaction
  impulse; inspect with `r_waterInteractionDebug 3`.
- `r_waterSnellDebug 1..6`, `r_volumetricFogDebug 61..68`, and
  `r_waterParticlesDebug 3` retain their focused diagnostics.

Recommended stock coverage:

- `t2_rancor`: six small rigid indoor pools; slow/hover/near-plane tests;
- `t3_hevil`: large 368-unit-deep outdoor lake; strong waves, below-surface
  Snell/TIR, sun, and waterfall-adjacent transitions;
- `yavin1`: shallow multi-surface water and natural banks;
- `yavin1b`: shallow streams, rain/lens interaction, and third-person changes;
- `t2_trip`: small generic bodies and fast crossings;
- `vjun1`: slime profile;
- `t2_port`/`taspir2`: water that is also an authored BSP fog medium;
- `vjun2`: negative control for the known moving brush-model liquid fallback.

## Performance and validation status

The query is CPU-only, once for particles and once for the primary lens input.
It scans candidate body interface triangles and evaluates at most eight wave
components; it allocates no memory and adds no GPU pass. Live cost is printed
by `r_waterlineDebug`; no credible GPU delta exists because no draw or dispatch
was added.

The SP rend2 `RelWithDebInfo` target compiles successfully with MSVC 2019. This
workspace run did not launch the game, so screenshots, video comparisons, and
hardware runtime timings have not been fabricated. In-game slow/fast/hover,
camera-mode, near-shore, and below-surface checks remain required on the target
machine.

Known limitations: moving brush-model liquids remain BSP/legacy fallback;
water bodies without modern horizontal interface triangles cannot be queried;
the froxel boundary remains static; and triangle scanning cost depends on the
source interface topology (the runtime microsecond diagnostic is the guard for
unusual custom maps).
