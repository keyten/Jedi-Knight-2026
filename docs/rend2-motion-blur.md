# Rend2: velocity based motion blur

Short, cinematographic camera and object motion blur on the HDR scene, built on the temporal data rend2 already
had for SMAA T2x (velocity buffer, previous view projection, previous entity matrices and bones). There is one
velocity representation for camera and object motion and no separate radial camera blur.

Code: `shared/rd-rend2/tr_motionblur.cpp`, shader `glsl/motionblur.glsl`; hooks in `tr_backend.cpp`
(`RB_PostProcess`, temporal/entity constants), `tr_shade.cpp` and `tr_ghoul2.cpp` (previous frame fallbacks).

Off by default (`r_motionBlur 0`): no velocity buffer or extra UBOs are created (unless `r_smaa 2` needs them),
the post process order is unchanged, the image is unchanged. Needs `r_hdr 1`; with `r_hdr 0` the scene buffer is
already the display referred LDR image and motion blur is disabled with a warning.

## Existing velocity infrastructure (audit)

- `velocity.glsl` is the shader of the depth prepass (`RB_RenderDepthOnly`) when `tr.depthVelocityFbo` exists. It
  writes `current - previous` position in texture coordinates, for one frame, into `tr.velocityImage` (RG16F).
  With MSAA the prepass renders into a multisampled RG16F renderbuffer sharing the main depth buffer and is
  blit-resolved into `tr.velocityImage` after the prepass (samples averaged on edges).
- `TemporalInfo` UBO: previous view projection (`gpuFrame_t::viewProjectionMatrix` of the main view, scene 0),
  current/previous jitter, previous time. `PreviousEntity` / `PreviousBones` UBOs are bound from the previous
  frame's UBO; the frame UBO ring has `MAX_FRAMES + 1` sets so the previous frame is not overwritten.
- It was created only for `r_smaa 2` (`tr_image.cpp`, `R_InitBackEndFrameData`); `r_motionBlur` now enables it too.
  Velocity is only written with `r_depthPrepass 1` (default).

| Geometry | Motion vectors |
|---|---|
| World (BSP) | camera motion (previous view projection) |
| Brush movers: doors, platforms, lifts | object + camera; `RT_MODEL`/`MOD_BRUSH`, matched by the (unique) inline model handle |
| Rigid entities (MD3 and others) | previous model matrix; matched by model handle + nearest predicted origin (identical moving models can be swapped) |
| Ghoul2 / skinned | previous bones (matched by ghoul2 pointer) + previous model matrix |
| MD3 vertex animation | rigid motion only: frames are interpolated on the CPU (`RB_SurfaceMesh`, `REND2_SP_MD3` is never defined) |
| GPU `deformVertexes` | rigid motion only (previous position uses the same time) |
| Alpha-tested (`SS_OPAQUE`, `DEPTHPREPASS_ALPHATESTED`) | yes (`USE_ALPHA_TEST` is global, `u_AlphaTestType`) |
| Surface sprites | yes (`SSDEF_VELOCITY`, wind history through `u_previousFrameTime`) |
| Sky, sky portal | nothing written (velocity 0, depth 1): the blur pass reconstructs the camera rotation |
| First person view model (`RF_DEPTHHACK`, depth <= 0.3) | yes (model matrix history), ~0 on screen plus bob / animation |
| Saber blade and glow, particles, additive and blended FX (not `SS_OPAQUE`) | none: not in the depth prepass, the pixel keeps the velocity of the surface behind |
| Mirrors / portals | velocity of the mirror surface; the mirrored image is not blurred |

### Fixed

- `PreviousEntity` with no previous data (entity appeared this frame, no match, not an `RT_MODEL`) bound offset 0
  of the previous frame UBO, which holds an unrelated block (a camera block): garbage previous model matrix and a
  huge false velocity on the first frame of every new entity. It now binds the entity's current block (no object
  motion, camera motion stays).
- `PreviousBones` with no cached bones of the previous frame (new Ghoul2 model) did the same (`uboPreviousOffset
  = 0`). It is now -1 and the current bones are bound.
- No history invalidation existed. With motion blur on, the history is reset (previous view projection = current,
  no object matching, no blur, SMAA T2x resolves without its history) on: map load / `vid_restart`, frames
  without a world view (menus, loading), camera movement above `r_motionBlurCutDistance` per frame (teleport),
  rotation above `r_motionBlurCutAngle` per frame (cut), FOV change above 15 % (zoom toggles), frame interval
  above 250 ms, and `r_motionBlurReset 1` from game code. The first frame after any of these is never blurred.

These fallbacks also change SMAA T2x output, only on the frames where it used to read garbage. The history
checks only run with motion blur on.

### Found, not changed

The SMAA T2x jitter is written into `projectionMatrix[2]` / `[6]` (`tr_main.cpp`). In the column major matrix these
are the z row: they tilt the depth instead of shifting x/y by a subpixel. `velocity.glsl` subtracts
`currentJitter + previousJitter`, which is always 0. So the velocity is jitter free, which is what motion blur
needs. If the jitter is fixed (`[8]` / `[9]`), the velocity shaders must subtract `(current - previous)` instead.

## Pass order

```
depth prepass (velocity)  ->  main pass  ->  RB_PostProcess:
  MSAA resolve (color + depth)
  [r_smaa 2]   SMAA edges -> weights -> neighborhood -> temporal resolve -> history copy
  MOTION BLUR  renderImage (HDR) -> motionBlurImage -> subsequent color passes
  dynamic glow / bloom source (downscale + highpass of the blurred HDR scene)
  [r_smaa 1/3] SMAA edges -> weights (neighborhood blending happens in the tone map pass)
  tone map + color grading -> sun rays -> glow composite -> debug overlays -> refraction
UI / HUD / menus (2D, after the post process: sharp)
```

Without motion blur the order is the old one (glow, then SMAA); glow and SMAA do not depend on each other.

- After the SMAA T2x temporal resolve and its history copy: the history stays unblurred, so blur is not
  re-blended frame after frame (no double blur), reprojection of the history stays valid and blurred history
  cannot ghost. Blurring first would also hide the edges from SMAA.
- Before the SMAA 1 edges: no history there, so the anti-aliasing works on the final (blurred) image and its
  blending weights match the image that is tone mapped.
- Before bloom: blur is the integration on the sensor during the exposure, bloom the scattering in the lens after
  it. A fast bright highlight first becomes an HDR streak with its energy conserved, then blooms along the
  streak. Blurring after bloom would smear the halo apart from its source, or (after tone mapping) give an LDR
  smear with clipped highlights.
- Before tone mapping and the UI: the linear HDR scene is blurred, the UI is drawn later and stays sharp.

## Shutter model

The velocity buffer holds the motion over one real frame interval. The exposure time is

    exposure = (r_motionBlurShutterAngle / 360) / r_motionBlurReferenceFps

(180 degrees at 60 = 8.3 ms), and the pass scales the velocity by `exposure / frame interval`
(`r_motionBlurShutterScale` multiplies it, the scale is clamped to 4). The streak length is screen speed x
exposure, independent of the frame rate:

| fps | motion per frame of a 1200 px/s pan | scale (180 deg @ 60) | streak |
|---|---|---|---|
| 30 | 40 px | 0.25 | 10 px |
| 60 | 20 px | 0.5 | 10 px |
| 120 | 10 px | 1.0 | 10 px |
| 144 | 8.3 px | 1.2 | 10 px |

`r_motionBlurReferenceFps 0` uses the real frame interval instead: a true shutter angle, the blur then gets
shorter at higher frame rates like a real camera. The frame interval is measured with a high resolution clock
and smoothed (ms timers would make the length flicker at 144 fps). Real time is used, not game time, so slow
motion (`timescale`) gives less blur, like high speed footage, and a paused game gives none.

The exposure is centered on the rendered frame (samples at -0.5..+0.5 of the exposure motion), so objects stay
where SMAA, the UI and gameplay see them.

## Sampling and edges

Per pixel (`motionblur.glsl`):

1. Velocity: the velocity buffer where the prepass wrote it (depth < 1); the camera motion reconstructed from
   depth and the inverse / previous view projections for the sky (direction only: rotation) and everywhere when
   there is no velocity buffer (`r_depthPrepass 0`). View model pixels get `r_motionBlurViewModelScale`.
2. Exposure scale, clamp to `r_motionBlurMaxPixels` (at 1080p, scaled with the height).
3. Below 0.5 px nothing is done (full blur from 1.5 px): a still scene costs one texel copy per pixel.
4. Samples: about one per 2 px of streak, up to 6 / 10 / 16 (`r_motionBlurQuality` 0 / 1 / 2) or
   `r_motionBlurSamples`; stratified with a static interleaved gradient noise offset, no fixed sample count.
5. Weights: the reconstruction filter of McGuire et al. 2012 ("A Reconstruction Filter for Plausible Motion
   Blur"), with a soft depth compare in linear depth (3 %, at least 2 units):
   `w = front(Y) * cone(dist, |vY|/2) + behind(Y) * cone(dist, |vX|/2) + cyl(vX) * cyl(vY) * 2`.
   A blurry sample in front covers the pixel; a blurry pixel reveals what is behind it; a fast background does not
   smear into a foreground that is still on screen (its own velocity is small, it is not sampled), and a still
   foreground does not smear onto a moving background. The view model is always in front.
   Low quality uses the center velocity for the samples (one fetch less per sample).
6. The legacy HDR buffer holds display encoded values unless the map is lit in linear light (`tr.linearLight`):
   the samples are decoded (`pow 2.2`) and integrated in linear light, so highlights keep their energy.

No tile / neighbour max velocity yet: a moving object on a still background blurs inside its silhouette only.
`DominantVelocity()` in the shader is the place for a NeighborMax texture (tile max, e.g. 20 px, plus 3x3
neighbour max) if thin fast objects need it.

## Cvars

| cvar | default | |
|---|---|---|
| `r_motionBlur` | 0 | archive, latch. Creates the resources (velocity buffer, previous frame UBOs, output image) |
| `r_motionBlurShutterAngle` | 180 | shutter angle, 0 turns the blur off at run time |
| `r_motionBlurReferenceFps` | 60 | frame rate the angle refers to; 0 = the real frame interval |
| `r_motionBlurMaxPixels` | 32 | max streak length at 1080p |
| `r_motionBlurQuality` | 1 | 0 low (6 samples, center velocity), 1 medium (10), 2 high (16) |
| `r_motionBlurSamples` | 0 | max samples override (2..32), 0 = from the quality |
| `r_motionBlurViewModelScale` | 0.5 | blur strength on the first person weapon / hands / saber (depth hack surfaces) |
| `r_motionBlurCutDistance` | 256 | camera movement per frame treated as a teleport |
| `r_motionBlurCutAngle` | 75 | camera rotation per frame (degrees) treated as a cut |
| `r_motionBlurShutterScale` | 1 | not archived. Gameplay hook: exposure multiplier (0..4) |
| `r_motionBlurReset` | 0 | not archived. Gameplay hook: set to 1 for a cut, the renderer clears it |
| `r_motionBlurDebug` | 0 | cheat, debug views |

Recommended: `r_motionBlur 1` with the defaults. 120-150 degrees for a subtler look; `r_motionBlurQuality 0` on
weak GPUs.

### Gameplay hooks (Force Speed, cinematics)

The renderer knows nothing about Force Speed. Game code can set `r_motionBlurShutterScale` (e.g. 2 while Force
Speed is active, back to 1 after) through the normal cvar interface, and set `r_motionBlurReset 1` on camera cuts
the heuristics cannot see (e.g. cinematic cuts between nearby cameras).

## Debug views (`r_motionBlurDebug`)

Displayed directly (after tone mapping, no grading), replacing the frame:

1. velocity used for the blur: r/g = direction, brightness = length (relative to the max length)
2. camera motion only, reconstructed from depth
3. object motion only (velocity buffer minus camera motion); dark blue = no velocity data (sky)
4. samples per pixel (heat map), black = skipped
5. how much the blur changed the pixel

With any debug view, history resets are printed to the console with their reason (`motion blur: history reset
(camera teleport)`), once per reset.

GPU time: `r_speeds 100` lists "Motion blur" (pass only).

## Limitations

- Translucent / additive surfaces (saber blade and glow, blaster bolts, particles, most FX) write no velocity:
  they take the motion of the opaque surface behind them. Their own motion is not blurred (sabers have their
  gameplay trail), and they are smeared along with the background when the camera turns. A translucent velocity
  pass would not be a local change.
- MD3 vertex animation and GPU vertex deforms blur with the rigid motion only.
- No tile max: the blur of a moving object does not spill over its silhouette onto a still background.
- The dynamic glow source (`glowImage`, emissive stages) is not blurred: the glow halo of a moving opaque emitter
  stays at its current position (the scene highlight itself is blurred). A second output of the same pass could
  blur it.
- Mirror / portal contents and 3D UI scenes (`RDF_NOWORLDMODEL`) are not blurred; only the first scene of a frame.
- Several identical rigid models moving close to each other can swap their previous matrices (existing matching
  heuristic).

## Cost

Estimates, not measurements (the game was not run): full screen motion at 1080p, medium quality, about 0.3-0.6
ms on an RTX 2060 and 2-4 ms on Intel UHD; a still scene still pays for the blur
pass, but no longer for a copy back. These older estimates need remeasurement.
Memory: one full resolution RGBA16F image (16 MB at 1080p) plus, without `r_smaa 2`, the RG16F velocity buffer
(8 MB) and one extra set of frame UBOs.

## Validation checklist

Build checks done: SP and MP renderers (MSVC, and gcc for the changed files), `motionblur.glsl` (all
permutations) compiled on the Intel UHD and NVIDIA drivers. In game, with `r_motionBlur 1` (`vid_restart`):

| Case | What to look at |
|---|---|
| stationary scene | `r_motionBlurDebug 4` all black; image identical to `r_motionBlurShutterAngle 0` |
| slow pan / strafe | `r_motionBlurDebug 1` smooth field, short streaks, UI sharp |
| fast 180 degree turn | streaks clamped to `r_motionBlurMaxPixels`, sky blurs with the world |
| jump, cartwheel / flip | player model blur without background bleeding in (`r_motionBlurDebug 3`) |
| saber swing | hilt / arm blur; blade keeps its trail, not blurred by its own motion |
| NPC sprint | NPC blurred inside its silhouette, background sharp |
| moving door / platform | object velocity in `r_motionBlurDebug 3` |
| Force Speed | `r_motionBlurShutterScale 2`: twice the streak length |
| 30 / 60 / 120 / 144 fps (`com_maxfps`) | same streak length for the same turn speed |
| MSAA, SMAA 1, SMAA 2 | no ghosting, no double blur, SMAA 2 history sharp |
| camera cut / teleport / map load | first frame sharp, console message with `r_motionBlurDebug 1` |
| menu / UI / console | always sharp; leaving the menu does not smear |
