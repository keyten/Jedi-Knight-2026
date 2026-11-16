# Phong tessellation for cinematic character close-ups

The feature is disabled by default. With `r_phongTessellation 1`, the renderer
selects the nearest player/NPC Ghoul2 bodies inside the current camera frustum
and distance limit, up to `r_phongTessMaxCharacters`. Selection is made once per
scene from the camera, rather than independently for each model surface/pass.
Duplicate powerup submissions of one Ghoul2 instance consume one character slot.

SP cgame explicitly marks scripted cameras with `RDF_CINEMATIC` and character
bodies with `RF_CHARACTER`. Third person, binoculars and sniper zoom do not mark
a cinematic. Ordinary gameplay stays unchanged unless the separate, latched
`r_phongTessGameplay` override is enabled. UI previews and skybox scenes are excluded.

## Configuration

| Cvar | Default | Meaning |
| --- | ---: | --- |
| `r_phongTessellation` | `0` | Enable cinematic close-up tessellation. |
| `r_phongTessMaxCharacters` | `1` | Maximum nearest characters; `0` disables selection. |
| `r_phongTessDistance` | `160` | Maximum distance from camera to model origin, in game units. |
| `r_phongTessGameplay` | `0` | Allow gameplay as well; latched, requires `vid_restart`. |
| `r_phongTessLevel` | `4` | Subdivisions per triangle edge, clamped to 2–16 and the hardware limit. |
| `r_phongTessStrength` | `0.5` | Tangent-plane smoothing strength, 0–1. |
| `r_phongTessMaxDisplacement` | `1` | Maximum vertex displacement, in world units, 0–8. |
| `r_phongTessDebug` | `0` | `1`: selection statistics; `2`: selected bodies green; `3`: displacement heatmap. |
| `r_phongTessellationCompare` | `0` | `1`: original left / smoothed right; `2`: original everywhere. |
| `r_compareSplit` | `0.5` | Shared comparison divider, 0–1. |

Debug modes 2 and 3 also print selection statistics, including the selected
entities' positions, camera distances and smoothing strengths. The heatmap runs from blue
(no displacement) to red (one quarter of the displacement cap or greater).
Configuration changes are live except for `r_phongTessGameplay` and the existing
OpenGL tier switch, `r_gl43`. GPU tessellation requires `r_gl43 1` and the
`MODERN_TESSELLATION` capability; otherwise the ordinary model path remains active.

```cfg
seta r_gl43 1
seta r_phongTessellation 1
seta r_phongTessMaxCharacters 1
seta r_phongTessDistance 160
seta r_phongTessGameplay 0
```

Run `vid_restart` if changing `r_gl43` or `r_phongTessGameplay`.
For visual inspection, use `r_phongTessellationCompare 1` and optionally
`r_phongTessDebug 2` or `3`. Return both to `0` for performance measurements.

## Geometry and rendering

Skinning runs first. The tessellation evaluation shader projects each barycentric
point onto the three skinned vertex tangent planes, blends the projections and
limits the displacement. One tessellation level is shared by the whole character,
including material boundaries. Smoothing fades with smoothstep over the last
quarter of `r_phongTessDistance` to avoid a sudden shape change at the range limit.

The same displacement is used by lightall, generic, depth prepass, velocity and
fog programs. Velocity evaluates both current and previous skeletal poses.
Comparison mode submits both original triangles and tessellated patches with
separate scissors, so the original half also has its original silhouette/depth.
This extra submission is only used while the split comparison is active.

Shadow maps retain the original character geometry, avoiding tessellation in
every shadow view. Material vertex deformations, alpha-fade effects, disintegration, distortion,
first-person weapons and silhouette-POM surfaces keep their existing paths.
Collision meshes and animations are unchanged. Phong smoothing follows authored
vertex normals; it does not add sculpted detail. A load-time geometry crease mask
preserves shared vertices with incompatible normals or skinning, including across
material/UV seams. Their tangent-plane contribution is disabled on every copy,
keeping those edges continuous without changing the authored lighting normals.
The mask is a separate four-byte stream; ordinary G2 vertex strides and fetches
remain unchanged. Fully preserved patches keep their boundary tessellation but
use fewer interior triangles because their displacement is identically zero.

SP needs the updated `jagamex86_64.dll` as well as `rdsp-rend2_x86_64.dll`: the
camera and body markers are emitted by cgame inside the game DLL. MP needs the
updated cgame DLL for body markers; its ordinary gameplay requires the explicit
gameplay override because it has no SP scripted-camera signal.
`BuildRend2.bat` builds these cgame modules together with the renderers.

## Performance choices and verification

The disabled path exits before candidate collection or shader creation. Enabled
selection reuses its candidate buffers, rejects distance using squared lengths,
and computes square roots only for selected bodies. Tessellation reuses the
ordinary vertex shader's skinned position and normal instead of skinning twice.
Programs are created on demand, use the existing program binary cache, and are
cached directly on their base program for subsequent draws. Tessellation cost
grows roughly with the square of `r_phongTessLevel`.

`tools/rend2/test_phong_tessellation_gl.py` compiles the material/depth/velocity/
fog/refraction variants on a real GL 4.3 driver and measures generated geometry
with transform feedback: subdivision, curvature, displacement cap, shared edges,
and skinning. It requires NumPy and uses the repository's SDL2 test helpers.
It also reproduces the hard-normal seam defect as a negative control and checks
that the crease mask closes it. `test_phong_crease.cpp` checks the actual load-time
classification of UV duplicates, hard normals, skinning seams and export rounding.

`tools/rend2/test_phong_tessellation_runtime.py` launches an isolated SP instance
under `build/phong-runtime`, preserving the installed game. It checks the gameplay
gate, character budget, distance rejection, debug views, split halves and depth
prepass consistency. `--velocity` checks the velocity-buffer path; `--cinematic`
checks an actual scripted camera. `--bench` collects warmed A/B GPU timer samples
and wall-clock frame intervals in alternating order. Captures, logs and JSON
results remain under the corresponding runtime directory.

## Measured cost and validation

Measured on Intel UHD Graphics, GL 4.3, 1280×720, `mp/ffa3`, a frozen Kyle
close-up, one selected character, level 4, strength 0.5 and displacement cap 1.
The same Release binary renders both cases (`r_phongTessellation 0` / `1`),
with debug/comparison disabled and programs warmed before timing. Values below
are medians of three round medians, in alternating A/B order.

| Measurement | Off | On | Difference |
| --- | ---: | ---: | ---: |
| GPU main view, including depth prepass | 4.033 ms | 4.966 ms | +0.933 ms / +23.1% |
| GPU depth prepass (part of the main view) | 0.296 ms | 0.430 ms | +0.134 ms |
| Complete wall-clock frame interval | 5.989 ms | 6.904 ms | +0.915 ms / +15.3% |
| Reciprocal of median frame interval | 167 FPS | 145 FPS | −22 FPS |

GPU main-view round medians varied from 4.01–7.99 ms off and 4.59–4.99 ms on;
wall-clock medians were 5.94–6.08 ms off and 6.83–7.13 ms on. These are measurements
of this model/view on this GPU, with system/driver variation, rather than a fixed
cost for all characters. Raw samples and settings are in
`build/phong-runtime/gameplay1/results.json` and its `home/OpenJK/qconsole.log`.
Wall-clock sampling runs separately from GPU timer logging to avoid including
the synchronous query/logging overhead in the frame-interval comparison.

Validation completed for:

- SP/MP Release renderers, SP game/cgame and MP cgame builds;
- 30 real-driver shader combinations and transform-feedback geometry checks;
- the native crease-classification test, preserving original vertex data;
- gameplay with the override off: no selected/tessellated characters;
- positive selection, zero budget, distance rejection and comparison-off mode;
- split halves matching their full-frame references within one display rounding step;
- depth prepass verification: both orders, zero nearer samples across 228 surfaces;
- the velocity-buffer path, including the `velocity_SK_PHONG` program;
- actual `academy1` scripted cameras with gameplay override off, selecting at most one character;
- the hard-normal seam regression and in-game selected-body captures: no new high-contrast cracks.

The before/after seam close-up is saved at `build/phong-runtime/seam-fix-comparison.png`;
the corrected split view is at `build/phong-runtime/gameplay1/home/OpenJK/screenshots/split.png`.
