# Body-local interactive water disturbances

`r_waterInteraction 1` adds transient, world-caused disturbances to the existing modern water surface. It is off by default and requires `r_waterSurface 1`; enabling it is latched and therefore requires `vid_restart` or a map restart. Ambient waves, authored deformation, optics, flow, and legacy water are not replaced.

The final modern-water evaluator is:

```
surface = ambient analytic displacement + interactive field displacement
normal  = geometric normal - ambient slope - interactive central-difference slope - micro slope
```

The combined position and normal remain in `watersurface.glsl`, so the displaced render mesh, waterline/silhouette, depth, refraction, SSR/cubemap reflection, Fresnel/Snell/TIR tests, lighting, and debug views consume one surface definition. If `r_waterGeometry 0`, the interaction still contributes its real height-field slope to shading but cannot change the flat BSP geometry. No normal-map-only ripple path exists.

## Domain, mapping, and mask

Each eligible resolved `waterBody_t` owns a separate rectangular grid. World XY maps stably to body UV:

```
uv = (world.xy - body.bounds.min.xy) /
     (body.bounds.max.xy - body.bounds.min.xy)
```

The mapping parameters travel with the body draw in `u_Water[30]`; future sources submit world positions and never see texture/FBO coordinates. Mostly-horizontal interfaces are the current eligibility rule, matching the existing modern-water renderer.

At map load the renderer rasterises every classified interface triangle of that body into a byte mask. Five sub-cell coverage samples retain thin edge triangles. Solver cells outside the triangle union are inert. A neighbour outside the mask is replaced with the centre height (zero normal derivative), producing a stable reflecting solid boundary. Disconnected triangle islands inside one bounding rectangle therefore cannot exchange waves. The same mask is uploaded in the field's alpha channel for rendering and diagnostics.

## Solver

The field stores height `h` and vertical velocity `v`. For a fixed step `dt = 1/60 s`, interior cells use a five-point nonuniform stencil:

```
laplacian(h) = (hL - 2h + hR) / dx^2 + (hD - 2h + hU) / dy^2
v[n+1] = clamp(v[n] + dt * (c^2 laplacian(h[n]) - 2 damping v[n]))
h[n+1] = clamp(h[n] + dt * (v[n+1] - flow dot upwindGradient(h[n])))
```

`c = (32 + 32 * profile.speed) * r_waterInteractionSpeed`, clamped to the explicit solver's two-dimensional CFL limit. Damping is `0.32 * profile.damping * r_waterInteractionDamping`. Existing resolved world-space flow is applied only when `r_waterFlow 1`; still pools remain isotropic.

Frame time enters an accumulator, is clamped to 250 ms after a hitch, and at most four steps are consumed per real frame. `backEndData->realFrameNumber` guards the update. Portal, mirror, sky-portal, and reflection views only sample the state produced by the main view.

An impulse adds a compact, smooth Mexican-hat velocity kernel. Its positive core and compensating trough are mean-corrected after mask clipping, conserving volume in a closed body and preventing a permanent DC water-height offset. Radius is clamped to at least 1.5 texels and at most 1024 world units; strength is clamped to `[-8, 8]`. An optional tangent direction biases the kernel without introducing a single-texel spike.

Continuous sources use the same stamp, scaled by the fixed `dt`, so injection is deterministic rather than render-frame dependent. Multiple sources and impulses superpose in the shared field and interfere naturally.

Energy is the masked mean of `h^2 + v^2/c^2`. A body with energy below `1e-5` for one second is zeroed and put to sleep. A new impulse/source wakes it. Sleeping bodies execute no stencil and make no texture upload. Ambient analytic waves continue normally during sleep.

## Resolution and memory

The requested density is physical:

| quality | target world units per texel |
|---|---:|
| 0 | 32 |
| 1 (default) | 16 |
| 2 | 8 |

Each dimension is rounded up to eight texels and clamped to 16–512. Tiny pools can therefore be finer than the target; huge lakes are capped rather than allocated at one texel per world unit. Under total-budget pressure the longer dimension is halved until the body fits. Bodies beyond the body/texel/memory limits receive no field and render exactly as before.

The uploaded resource is one clamped `RGBA16F` texture per allocated body:

- R: disturbance height;
- G: vertical velocity;
- B: transient splash foam;
- A: physical body mask.

GPU memory is exactly 8 bytes per allocated texel. The reference implementation integrates on the CPU and uploads only dirty active fields; there is no compute-shader requirement and no simulation draw pass. CPU working memory is approximately 41 bytes per texel (mask, current/next height/velocity/foam float state, RGBA upload staging), plus vector overhead. `r_waterInteractionInfo` reports actual resolution and GPU bytes per body.

## Event/source API

The optional renderer export `GetRefWaterInteractionAPI` contains:

```c
void AddWaterImpulse(const refWaterImpulse_t *impulse);
void SetWaterSources(const refWaterSource_t *sources, int count);
```

Both structures take world position, radius, strength, optional directional flag/vector, and a caller-defined diagnostic type. Continuous sources also have a stable caller ID. SP exposes matching cgame syscalls guarded by `cl_rendererWaterInteraction`; MP appends matching optional import callbacks. Existing `EV_WATER_TOUCH` and `EV_WATER_LEAVE` events submit nominal positive/negative impulses. Older engines/renderers remain safe because the capability/export is optional.

Extension points require no solver changes:

- wakes: submit a moving capsule as a short sequence/list of directional continuous sources;
- rain: batch visible impact points as small one-shot impulses;
- waterfall impact: keep a stable continuous source ID at the impact basin;
- scripted effects/projectiles: call `AddWaterImpulse` with their world hit position, radius, energy, and direction;
- spatial stream flow: replace the existing uniform `R_WaterFlowForBody` lookup with its planned body-local flow texture; the solver already consumes body-space flow advection.

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

Commands:

- `r_waterImpulse [strength=1] [radius=48]`: inject at the water point under the crosshair. A nearest masked-texel fallback covers thin discretised shores, limited to ten degrees from the ray.
- `r_waterInteractionInfo`: print resolutions, physical texel sizes, mask coverage, active/sleeping state, energy, fixed steps, source-processing/solver/upload time, average API submission cost, and GPU memory.

Debug modes: 1 domain/UV, 2 mask, 3 height, 4 velocity, 5 actual central-difference slope, 6 latest source, 7 transient foam, 8 active/sleeping, 9 world-to-body mapping grid. The numeric body energy remains available through `r_waterInteractionInfo`. Captures from stock `t2_rancor` are in [`water-interaction-debug`](water-interaction-debug/).

## Validation and measured data

Automated checks:

- `tools/rend2/test_water_interaction.py`: stable expansion, exact 30/60/144 FPS agreement, disconnected-mask isolation, signed multi-impulse interference, and sleep;
- `tools/rend2/test_watersurface_gl.py`: all 144 OpenGL 3.2 water permutations, direct interactive-height depth displacement and interactive-slope optics checks, plus the existing geometry, ambient, flow, refraction, SSR, Snell/TIR, and underwater regressions;
- `tools/rend2/test_watersurface_runtime.py --interaction`: real map load, crosshair injection, all nine debug captures, stats, and GL error scan.

Measured on Intel UHD / OpenGL 3.2 at 60 FPS (high-resolution CPU timer):

| Case | Allocated fields | Active field | Last fixed work | Solver | Upload | API submit |
| --- | --- | --- | --- | ---: | ---: | ---: |
| `t2_rancor`, indoor pool | 6 x 16x16; 12,288 GPU bytes total | 16x16, 2,048 bytes | 4 steps | 0.054 ms | 0.125 ms | 3.0 us |
| `t3_hevil`, outdoor lake | 216x216, 200x200, 288x40; 785,408 bytes total | 216x216 | 2 steps | 11.850 ms | 1.858 ms | 5.7 us |
| `yavin1`, shallow/multi-surface | 4 fields; 914,432 bytes total | 224x136, mask 6,008/30,464 | 4 steps | 3.732 ms | 0.329 ms | 9.6 us |
| `vjun1`, slime | 328x512; 1,343,488 bytes | 328x512 | 4 steps | 13.401 ms | 0.819 ms | 1.7 us |
| `vjun1`, quality 2 maximum body | 512x512; 2,097,152 bytes | 512x512, mask 98,733/262,144 | 4 steps | 10.462 ms | 1.436 ms | 2.0 us |

Sleeping bodies perform no solver work and no upload. The GL 3.2 baseline has no GPU simulation draw/compute pass, so GPU simulation-step time is zero; upload timing above is CPU/driver wall time. The synthetic 1920x1080 GPU benchmark measured the no-SSR water pass at 18.732 ms with interaction disabled and 20.012 ms with a live 64x64 field (+1.280 ms); scene copy was 2.671 ms, for 21.403/22.683 ms total water-path GPU time. This deliberately pessimistic full-screen Intel-UHD result is not a whole-game frame time.

Validated stock cases: `t2_rancor` indoor pools; `t3_hevil` outdoor multi-surface lake; `yavin1` shallow/multi-surface water with the flow path enabled; and `vjun1` slime, including the 512x512 maximum per-body resolution. Checked-in screenshots cover `t2_rancor`. `t3_bounty` still pools and `yavin2` ambiguous curved-flow bodies remain operator test candidates.

## Known limitations

- The GL 3.2 reference is CPU finite differences plus dirty texture upload, not a ping-pong FBO. This keeps correctness and fallback simple, but the worst 524k-texel active budget is CPU-heavy; a future GL 3.2 raster ping-pong path should retain the same equation, mask, mapping, API, and sleep policy.
- Body coordinates are world XY and currently target the existing mostly-horizontal modern-water eligibility. Vertical waterfall faces remain legacy; their impact basin should feed a horizontal body's source.
- Allocation is per map and bounded, but body fields are image-manager resources until renderer restart, like other renderer-created images.
- Mask resolution cannot preserve a channel narrower than one texel. Quality/budget controls determine that physical limit.
- Gameplay water entry/leave and missile/thrown-saber crossings are wired. Rain, wake, and waterfall producers remain explicit extension points rather than guessed from shader names.
- Texture upload is asynchronous on the tested driver; its reported number is CPU/driver wall time, not completion time on the GPU.
