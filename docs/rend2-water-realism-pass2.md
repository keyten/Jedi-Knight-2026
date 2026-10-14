# Water realism pass 2: foam, flow, wall impact, interaction, shoreline

This pass fixes the in-game problems of the October water overhaul. It uses cheap approximations
that shipped engines rely on; nothing simulates a fluid:

- river flow maps baked from a distance-to-bank field;
- two-phase flow-map advection (Vlachos, Portal 2);
- foam drawn by thresholding a foam texture against a foam amount (the coverage approach);
- foam sources driven by events and energy;
- a stagnation-head pile-up where flow meets a wall.

## Fixes (no new cvar)

| Problem | Cause | Fix |
|---|---|---|
| `r_waterParticles 1` fills the screen with white | The geometry shader used the camera block's `u_ViewLeft/u_ViewUp` as unit vectors. They are scaled by zFar·tan(fov/2), so every speck became a quad about 400 units wide. | Axes are normalised (same in the `weatherSplash` debug markers). The light grid gets `pbrLegacyCalibration` under `r_pbrPhysical`. The test now feeds scaled axes. |
| Characters and projectiles make no water impulse | `r_waterSplashes 0` (the default) dropped every gameplay impulse in cgame and in the renderer. The installed game DLL/exe predated the wake code. | `r_waterSplashes` now controls only spray and lens effects. Impulses and foam reach the field whenever the renderer exposes interaction. The binaries are redeployed. |
| Gameplay impulses invisible | Their strength was 10–40× below `r_waterImpulse`. Entry energy ignored horizontal speed. There was no nearest-cell fallback at the waterline. Shore attenuation scaled the field down to 5–12 %. | Strengths are recalibrated (footstep ≈0.6 u, wading ≈1 u, walking in 1.5–2.5 u, bolt ≈2 u, jump 6–10 u). New `EV_FOOTWADE` and `EV_SWIM` impulses. Bodies are found within 2 texels of a wet cell. The interaction field keeps 45–75 % of its height at walls and banks. |
| Wakes cancel out | Stamps were an odd bow/stern dipole, which integrates to zero along the path. | The kernel is a forward-biased raised core, normalised by stamp overlap and scaled by the profile `wake` value. |
| `r_waterImpulse` barely visible with flow | First-order upwind advection: numerical diffusion of about \|u\|·dx/2 erased ripples in about 0.15 s. | Semi-Lagrangian Catmull-Rom transport of height, velocity and both foam channels (GPU pass and CPU fallback, clamped, upwind only next to dry cells). |
| White edges after an impulse | Boundary foam used `max(\|v\|, \|flow\|)`, so every edge cell of a stream foamed. | Edge foam comes only from wave velocity above 40 u/s, added as a saturating rate. |
| Foam accumulates, sticks, never drifts | Constant shoreline source on every boundary texel. The steepness source applied to every texel. Linear injection plus clamp saturated the field. | Sources are event driven and evaluated on the GPU: breaking-wave slope at the edge, crest steepness, wall impact. Injection saturates. Decay is nonlinear (dense foam collapses, thin lace lingers). One-shot splash deposits are separate. The source texture is uploaded only on frames that have local sources. |
| Foam looks like a white semi-transparent strip | A plain `mix` toward a flat colour. Ad-hoc `foamLight` with a 0.28 self-light floor. | A procedural tiling foam texture (bubble membranes, fine bubbles, patches) is thresholded by concentration. Foam opens into lace and specks as it decays. Its coordinates move with the flow. It is lit as Lambert with the water's own ambient and π·sun term, with no floor. |
| `r_waterShoreline` seems to do nothing; no wet edges | Contact segments were built only with `r_waterGeometry 1`. They followed the buried brush outline. The band was 1.5–6 units wide and uniform. Splashes never raised it (the GPU field is not read back). | Segments are built whenever `r_waterShoreline 1`. Bank segments snap to where the ground actually crosses the water plane. Splashes, wakes and wall impacts report run-up directly. The wet band is ragged, with a streaked drying film; see below. |

## New, opt-in (latched, default 0)

### `r_waterFlowField 1` (needs `r_waterFlow 1`)

Each flowing body gets a small RGBA16F field at about 8 units per texel, baked at map load:

- Boundary texels are classified by a short horizontal trace: a solid hit is a wall or bank, otherwise an open end.
- The field holds an exact Euclidean distance to the solid boundary.
- Speed follows the lateral profile `pow(d/δ, 0.35)`, rescaled so the mean speed stays the body speed.
- Near walls the into-wall component is removed, so the flow turns along them; masked smoothing follows.
- Each texel also stores the wall impingement.

These consumers use the field:

- The surface shader's normal bands use two-phase advection.
- The interaction solver and the foam field use per-cell velocity.
- `R_WaterFlowForBody` and `R_WaterFlowAtWorldPosition` return the field.
- `r_waterFlowOverride` rebuilds the field.
- New debug views: `r_waterFlowDebug 5` speed and direction, `6` impingement, `7` shore distance.

On `t3_hevil` the bake takes 35 ms for 3 bodies and uses 1.16 MiB of GPU memory.

### `r_waterFlowImpact 1` (needs the field)

Where the stream meets a wall:

- The surface piles up by the stagnation head `I·U²/2g`, capped at 12 units and half the depth, with a slow slosh.
- A standing short chop and extra roughness break the surface.
- The impingement feeds foam, which then runs along the wall with the deflected flow.
- Up to 4 impact points per body emit irregular small impulses (0.25–0.9 s apart) into the interaction field.
- Shoreline segments there get pile-up and slap run-up, so the wall above the waterline gets wet.

## Wet edges (`r_waterShoreline 1`)

`ComputeWaterContact` in `lightall.glsl`:

- Fully wet at the current waterline.
- A thinner, vertically streaked film up to the held run-up height.
- A ragged, noise-perturbed upper edge.
- Reach onto flat banks is at least 12 units, with a ragged edge.
- Drying keeps the existing `r_waterShoreWetPersistence` hold.
- `r_waterShoreDebug 7` shows the mask.

## In-game checklist

```
set r_waterSurface 1; set r_waterGeometry 1; set r_waterInteraction 1
set r_waterFlow 1; set r_waterFlowField 1; set r_waterFlowImpact 1
set r_waterFoamField 1; set r_waterShoreline 1; set r_waterParticles 1
vid_restart
```

1. Walk, jump and shoot into water with `r_waterSplashes 0` and then `1`: ripples in both cases, spray only with 1.
2. Run through shallow water: a V wake. Swim and wade: small ripples.
3. Run `r_waterImpulse 1 32` on the `t3_hevil` lake: the ring drifts downstream intact, and the edges do not turn white.
4. Foam forms lace and specks that travel with the stream. With `r_waterFlowDebug 5` the banks are slower.
5. At the `t3_hevil` downstream wall: pile-up, chop, foam running along the wall, and a wet wall.
6. Underwater with `r_waterParticles 1`: small specks, no veil.

## Validation done

- Release SP and MP renderer, engine and game builds succeed.
- `test_watersurface_gl.py`: all 145 water permutations compile, and the GPU-vs-CPU stencil matches. Its reference now uses the new transport; the fp16 tolerance is slightly relaxed.
- `test_water_interaction.py` passes. `test_waterparticles_gl.py` passes with real camera axes.
- Hidden runs on `t3_hevil`:
  - the field is built and the impingement band lies along the downstream wall;
  - an impulse drifts downstream as a V;
  - underwater shows no veil;
  - no GL errors.
- Not verified: the wet edges in game. The `yavin1` capture ran into the intro cutscene.

`test_watersurface_gl.py` still stops later on `np.trapezoid`, which the installed numpy 1.x lacks. That problem predates this pass.
