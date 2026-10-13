# Body-local interactive water disturbances

`r_waterInteraction 1` adds transient, world-caused disturbances to the existing modern water surface. It is off by default and requires `r_waterSurface 1`; enabling it is latched and therefore requires `vid_restart`. Ambient waves, authored deformation, optics, flow, and legacy water are not replaced.

When `r_waterCausticsMode 2` is selected, the same field also drives the
underwater direct-sun caustic; see
[`rend2-surface-caustics.md`](rend2-surface-caustics.md).

The final modern-water evaluator is:

```
surface = ambient analytic displacement + interactive field displacement
normal  = geometric normal - ambient slope - interactive resolved slope - micro slope
```

The combined position and normal remain in `watersurface.glsl`, so the displaced render mesh, waterline/silhouette, depth, refraction, SSR/cubemap reflection, Fresnel/Snell/TIR tests, lighting, and debug views consume one surface definition. If `r_waterGeometry 0`, the interaction still contributes its real height-field slope to shading but cannot change the flat BSP geometry. No normal-map-only ripple path exists.

## Quick visual check

Run these commands before judging the feature. Both allocation controls are latched:

```
r_waterSurface 1
r_waterInteraction 1
r_waterGeometry 1
vid_restart
```

Load a map, stand above a modern water surface, aim the crosshair at it, and run `r_waterInteractionInfo`. It must say `enabled`, list at least one body field, and report either `geometry displacement+optics` or `geometry optics-only`. Then run `r_waterImpulse`; the no-argument test impulse is deliberately strong. The console must say `queued for body N`. If it instead reports that the crosshair missed, aim directly at an allocated surface or use `r_waterInteractionDebug 1` to display its domain.

Use `r_waterInteractionDebug 3` for an unmistakable height-field view, `5` for the actual slope, and `8` for active/sleeping state. Return to the normal result with `r_waterInteractionDebug 0`. For an exaggerated beauty-view check, use `r_waterImpulse 6 48`; this is a diagnostic value, not the recommended gameplay calibration.

## Domain, mapping, and mask

Each eligible resolved `waterBody_t` owns a separate rectangular grid. World XY maps stably to body UV:

```
uv = (world.xy - body.bounds.min.xy) /
     (body.bounds.max.xy - body.bounds.min.xy)
```

The mapping parameters travel with the body draw in `u_Water[30]`; future sources submit world positions and never see texture/FBO coordinates. Mostly-horizontal interfaces are the current eligibility rule, matching the existing modern-water renderer.

At map load the renderer rasterises every classified interface triangle of that body into a byte mask. Five sub-cell coverage samples retain thin edge triangles. Solver cells outside the triangle union are inert. A neighbour outside the mask is replaced with the centre height (zero normal derivative), producing a stable reflecting solid boundary. Disconnected triangle islands inside one bounding rectangle therefore cannot exchange waves. The same mask is encoded in the field's alpha channel for rendering and diagnostics.

## Solver

The field stores height `h` and vertical velocity `v`. For a fixed step `dt = 1/60 s`, interior cells use a five-point nonuniform stencil:

```
laplacian(h) = (hL - 2h + hR) / dx^2 + (hD - 2h + hU) / dy^2
v[n+1] = clamp(v[n] + dt * (c^2 laplacian(h[n]) - 2 damping v[n]))
h[n+1] = clamp(h[n] + dt * (v[n+1] - flow dot upwindGradient(h[n])))
```

`c = (32 + 32 * profile.speed) * r_waterInteractionSpeed`, clamped to the explicit solver's two-dimensional CFL limit. Damping is `0.32 * profile.damping * r_waterInteractionDamping`. Existing resolved world-space flow is applied only when `r_waterFlow 1`; still pools remain isotropic. Flow is independently scaled when necessary so `dt * (abs(vx)/dx + abs(vy)/dy) <= 0.95`, protecting the first-order upwind advection at high flow scales and fine grid resolutions.

The primary GL 3.2 path stores each body in two ping-pong `RGBA16F` state images. A fragment pass applies the same five-point stencil; dry neighbours are replaced by the centre sample, preserving the CPU boundary condition. A second fragment pass resolves height, world-space slope and foam directly into the body's shared-atlas rectangle. There is no simulation readback and no per-frame full-body CPU repack/upload. The compact wet-cell topology remains available to the CPU fallback.

Frame time enters an accumulator, is clamped to 250 ms after a hitch, and at most four steps are consumed per real frame. `backEndData->realFrameNumber` guards the update. Portal, mirror, sky-portal, and reflection views only sample the state produced by the main view.

An impulse adds a compact, smooth Mexican-hat velocity kernel. Its positive core and compensating trough are mean-corrected after mask clipping, conserving volume in a closed body and preventing a permanent DC water-height offset. Radius is clamped to at least 1.5 texels and at most 1024 world units; strength is clamped to `[-8, 8]`. An optional tangent direction biases the kernel without introducing a single-texel spike.

Moving sources are tracked by stable entity ID. Their horizontal path is sampled at a physical spacing derived from body width; the leftover distance carries into the next frame. Injection density therefore depends on distance travelled rather than render frames or solver steps. Wake samples use an elongated bow/stern height-velocity dipole, and multiple samples, sources, and impulses superpose in the shared field and interfere naturally.

The GPU path avoids a synchronising energy readback. It keeps an active body alive for a conservative damping-scaled interval after its last source, then performs one rare clear and puts it to sleep. The CPU fallback retains the exact masked-energy sleep test. A new impulse/source wakes either path. Sleeping bodies execute no stencil or resolve. Ambient analytic waves continue normally during sleep.

## Resolution and memory

The requested density is physical:

| quality | target world units per texel |
|---|---:|
| 0 | 32 |
| 1 (default) | 16 |
| 2 | 8 |

Each dimension is rounded up to eight texels and clamped to 16–512. Tiny pools can therefore be finer than the target; huge lakes are capped rather than allocated at one texel per world unit. Under total-budget pressure the longer dimension is halved until the body fits. Bodies beyond the body/texel/memory limits receive no field and render exactly as before.

The uploaded resource is one region of a clamped `RGBA16F` atlas per allocated body:

- R: disturbance height;
- G/B: resolved world-space height slope X/Y;
- A: zero for a dry cell, `1 + foam` for a wet cell (foam is the transient splash plus persistent object-intersection tracer).

The visible-water and surface-caustics shaders recover height, both slope components, occupancy and foam from one bilinear fetch. Vertical velocity stays in the body-local GPU state and is not copied into the render atlas merely for a diagnostic view.

GPU storage is the RGBA16F render atlas plus two RGBA16F state images and one RGBA16F source image per body: 32 bytes per allocated cell before atlas shelf padding. A power-of-two shelf search chooses the smallest practical atlas; `r_waterInteractionInfo` reports the actual total. The implementation requires only GL 3.2 fragment shaders/FBOs, not compute shaders. CPU source staging is uploaded only when impulses or contacts occur and only for their accumulated dirty rectangle; a local splash never uploads the full body. The CPU arrays and compact wet-cell topology are retained as a fallback, not stepped in the normal GPU path.

## Event/source API

The optional renderer export `GetRefWaterInteractionAPI` contains:

```c
void AddWaterImpulse(const refWaterImpulse_t *impulse);
void SetWaterSources(const refWaterSource_t *sources, int count);
```

Both structures take world position, radius, strength, optional directional flag/vector, and a caller-defined diagnostic type. Moving sources also carry a stable caller ID, velocity, waterline width, vertical bounds, and foam amount. The renderer tests those bounds against the body-local displaced surface, rejecting fully submerged and airborne objects while retaining swimmers, floating vehicles, and shallow-water walkers. SP exposes matching cgame syscalls guarded by `cl_rendererWaterInteraction`; MP appends matching optional import callbacks. Existing `EV_WATER_TOUCH` and `EV_WATER_LEAVE` events submit nominal positive/negative impulses.

Extension points require no solver changes:

- rain: batch visible impact points as small one-shot impulses;
- waterfall impact: keep a stable continuous source ID at the impact basin;
- scripted effects/projectiles: call `AddWaterImpulse` with their world hit position, radius, energy, and direction;
- spatial stream flow: replace the existing uniform `R_WaterFlowForBody` lookup with its planned body-local flow texture; the solver already consumes body-space flow advection.

Opaque-depth object contacts can also feed the foam channel when
`r_waterIntersectionFoam` is enabled; see
[`object/intersection foam`](rend2-water-intersection-foam.md). Its tracer has
independent persistence but shares this field's mask, upload and flow.

## Controls and commands

| cvar | default | purpose |
|---|---:|---|
| `r_waterInteraction` | 0 | master, archived + latched |
| `r_waterInteractionQuality` | 1 | physical density table above, latched |
| `r_waterInteractionStrength` | 1 | global source-strength multiplier |
| `r_waterInteractionDamping` | 1 | profile damping multiplier |
| `r_waterInteractionSpeed` | 1 | profile propagation-speed multiplier, CFL guarded |
| `r_waterInteractionMaxBodies` | 8 | allocated body limit, latched |
| `r_waterInteractionMaxTexels` | 524288 | total texel limit, latched |
| `r_waterInteractionMemoryMB` | 16 | GPU field memory limit, latched |
| `r_waterInteractionDebug` | 0 | debug modes below |
| `r_waterWakes` | 1 | moving player/NPC/vehicle/mover sources |
| `r_waterWakeStrength` | 1 | wake source-strength multiplier |
| `r_waterWakeMinSpeed` | 35 | minimum horizontal source speed |
| `r_waterWakeFoamSpeed` | 260 | speed at which low wake foam begins |
| `r_waterWakeMaxEntities` | 16 | nearest tracked entities, player first |
| `r_waterWakeDebug` | 0 | tracked bounds, waterline/shape, direction/strength |

Commands:

- `r_waterImpulse [strength=3] [radius=32]`: inject an intentionally obvious test impulse at the water point under the crosshair. Pass explicit lower values when tuning gameplay. A nearest masked-texel fallback covers thin discretised shores, limited to ten degrees from the ray.
- `r_waterInteractionInfo`: print path, resolutions, physical texel sizes, mask coverage, active/sleeping state, estimated energy, fixed steps, CPU submission time, average API submission cost, and GPU memory. Actual GPU pass time is exposed by the `Water interaction fields` screen timer.

Debug modes: 1 domain/UV, 2 mask, 3 height, 4 encoded slope X/Y, 5 the same resolved slope in the final surface frame, 6 latest source, 7 transient foam, 8 active/sleeping, 9 world-to-body mapping grid. The numeric body energy remains available through `r_waterInteractionInfo`. Captures from stock `t2_rancor` are in [`water-interaction-debug`](water-interaction-debug/).

## Validation and measured data

Automated checks:

- `tools/rend2/test_water_interaction.py`: stable expansion, exact 30/60/144 FPS agreement, disconnected-mask isolation, signed multi-impulse interference, and sleep;
- `tools/rend2/test_watersurface_gl.py`: all 144 OpenGL 3.2 water permutations, a 30-step GPU-vs-CPU interaction-stencil comparison (irregular mask, source, advection, foam), direct interactive-height depth displacement and interactive-slope optics checks, plus the existing geometry, ambient, flow, refraction, SSR, Snell/TIR, and underwater regressions;
- `tools/rend2/test_watersurface_runtime.py --interaction`: real map load, crosshair injection, all nine debug captures, stats, and GL error scan.

Historical measurements on Intel UHD / OpenGL 3.2 at 60 FPS (high-resolution CPU timer), taken before wet-cell topology compaction and one-fetch slope resolve:

| Case | Allocated fields | Active field | Last fixed work | Solver | Upload | API submit |
| --- | --- | --- | --- | ---: | ---: | ---: |
| `t2_rancor`, indoor pool | 6 x 16x16; 12,288 GPU bytes total | 16x16, 2,048 bytes | 4 steps | 0.054 ms | 0.125 ms | 3.0 us |
| `t3_hevil`, outdoor lake | 216x216, 200x200, 288x40; 785,408 bytes total | 216x216 | 2 steps | 11.850 ms | 1.858 ms | 5.7 us |
| `yavin1`, shallow/multi-surface | 4 fields; 914,432 bytes total | 224x136, mask 6,008/30,464 | 4 steps | 3.732 ms | 0.329 ms | 9.6 us |
| `vjun1`, slime | 328x512; 1,343,488 bytes | 328x512 | 4 steps | 13.401 ms | 0.819 ms | 1.7 us |
| `vjun1`, quality 2 maximum body | 512x512; 2,097,152 bytes | 512x512, mask 98,733/262,144 | 4 steps | 10.462 ms | 1.436 ms | 2.0 us |

Post-compaction measurements used the actual SP renderer on the same Intel UHD,
one strong impulse, 120 active samples per case, and report time per fixed step
so a delayed frame cannot inflate the solver number merely by executing several
catch-up steps. Source processing stayed at or below 0.002 ms median in every
case. Values below are `solver median / upload median` milliseconds:

| Map/body | Quality 0 | Quality 1 | Quality 2 |
| --- | ---: | ---: | ---: |
| `t2_rancor`, 16x16 pool | 0.041 / 0.137 | 0.040 / 0.120 | 0.030 / 0.101 |
| `t3_hevil`, body 9 | 2.988 / 1.143 | 9.538 / 2.838 | 33.093 / 5.584 |
| `yavin1`, active body | 0.498 / 0.409 | 1.454 / 0.526 | 5.467 / 1.577 |
| `vjun1`, body 1 | 3.827 / 0.991 | 10.963 / 2.432 | 15.184 / 3.357 |

The corresponding solver p95 values were 0.076/0.069/0.056 ms for
`t2_rancor`, 4.026/11.954/39.090 ms for `t3_hevil`, 0.688/2.063/7.137 ms for
`yavin1`, and 5.248/13.959/20.103 ms for `vjun1`. Wet-cell compaction works
well for sparse `yavin1` and tiny pools, but does not remove the CPU bottleneck
for dense large bodies: default quality regularly costs about 9.5–11.0 ms per
active fixed step on `t3_hevil`/`vjun1`, before a 2.4–2.8 ms upload. Quality 2
is not viable for those bodies on this CPU/iGPU. The result cleared the
previously stated 5–10 ms threshold and motivated the implemented GL 3.2
fragment-pass ping-pong path. It retains the CPU path as fallback and moves
both simulation state and render-field resolve, with no full-state readback.

On the same Intel UHD / OpenGL 3.2 driver, direct `GL_TIME_ELAPSED`
measurements of the production RGBA16F fragment path were:

| Dense field | GPU update / fixed step | GPU atlas resolve |
| --- | ---: | ---: |
| 256x256 | 0.220 ms | 0.153 ms |
| 512x256 | 0.373 ms | 0.299 ms |
| 512x512 | 0.753 ms | 0.597 ms |

Thus a dense 512x512 body costs about 1.35 ms for the common one-step frame,
versus 15–33 ms CPU solver plus 3–6 ms upload in the measured worst cases. A
four-step hitch recovery is about 3.61 ms GPU for that body. These are isolated
field-pass timings, not whole-game FPS claims. Sleeping bodies perform no
simulation or resolve. The visible shader's older synthetic full-screen result
remains +0.615 ms for the one-fetch render field versus +1.280 ms for the
former five-fetch slope reconstruction.

Validated stock cases: `t2_rancor` indoor pools; `t3_hevil` outdoor multi-surface lake; `yavin1` shallow/multi-surface water with the flow path enabled; and `vjun1` slime, including the 512x512 maximum per-body resolution. Checked-in screenshots cover `t2_rancor`. `t3_bounty` still pools and `yavin2` ambiguous curved-flow bodies remain operator test candidates.

## Known limitations

- CPU waterline/contact queries intentionally use the analytic ambient surface while the GPU interaction path is active; the visual disturbance state is not synchronously read back. Wake eligibility therefore remains stable but does not follow a local interaction crest exactly. The CPU fallback retains exact interaction-height queries.
- Body coordinates are world XY and currently target the existing mostly-horizontal modern-water eligibility. Vertical waterfall faces remain legacy; their impact basin should feed a horizontal body's source.
- Allocation is per map and bounded, but body fields are image-manager resources until renderer restart, like other renderer-created images.
- Mask resolution cannot preserve a channel narrower than one texel. Quality/budget controls determine that physical limit.
- Gameplay water entry/leave, missile/thrown-saber crossings, and bounded moving-object wakes are wired. Rain and waterfall producers remain explicit extension points rather than guessed from shader names.
- Source texture upload is asynchronous on the tested driver and occurs only for dirty sources; `Water interaction fields` is the authoritative GPU timer for update/resolve work.
