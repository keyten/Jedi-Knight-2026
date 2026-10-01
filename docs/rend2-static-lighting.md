# Rend2: Static Lighting Reconstruction

Status: first vertical slice (physical point / spot lights, energy promotion, unshadowed recovered lights in the
froxel fog, debug). It builds, and the synthetic tests pass (`tools/volrecon_test`, 866 checks). **It has not been
run in game yet.** There is no disk cache yet, so the cost is paid at every map load.

## Why

The light grid only keeps per cell ambient, directed and one direction. The directional baked light
reconstruction (`r_volumetricFogStaticDirectional 1`, see [rend2-volumetric-fog.md](rend2-volumetric-fog.md)) fits
*source proxies* to it. A proxy only means "light seems to arrive from here", which is enough for the L1 fog
moments. A doorway, a corridor bend or two converging lamps produce the same field as a real lamp. A lamp drawn
where there is none is acceptable for fog, but not for exact lights.

The reconstruction therefore keeps two confidences apart:

- **transport confidence**: the proxy fit (`vrProxy::confidence`), meaning the incoming direction is known
- **physical confidence**: the origin really is an emitter that the runtime light model can reproduce

## Ownership

```
BSP light grid + BSP (traces) + sun + static area emitters
                │
     tr_staticlighting.cpp   R_BuildStaticLighting (map load, after R_LoadAreaLights)
     └─ tr_volrecon.cpp      VR_Reconstruct (pure; visibility through the vrInput::trace callback)
                │
   B / S / M (residual) ───── tr_volumetric_reconstruct.cpp   froxel textures (adapter)
   structured lights  ─────── R_StaticLights()  → R_VolumetricBuildLightLists, r_staticLightDebug
```

v1 builds only when its one consumer, the froxel fog, is active (`r_volumetricFog 2`). Structured lights need
`r_volumetricFogStaticDirectional 1` and `r_staticLightReconstruction 1`.

## Structured lights (`vrStaticLight`)

There is one structured light per proxy. Its kind is `TRANSPORT`, `POINT`, `SPOT` or `RECT` (area anchors, not
promoted in v1). Its flags are `PHYSICAL`, `PROMOTED`, `RELOCATED` and `ANIMATED`. It also stores the origin, a
calibrated color, the radius R, the cone, the confidences, leak, excess, and the BSP cluster and area.

Per point proxy (`ClassifyPoint`, tr_volrecon.cpp):

1. **Contributions**: the attributed part `L_ij = Q * maxM * m_ij / sumM` of this source is recomputed from the
   finished accumulators (`MatchSource`, shared with `Attribute`). It is never stored per source.
2. **Probes**: 6 distance bins × 8 octants of the source domain. Each bin keeps its brightest probe and evenly
   spaced others, **dark ones included**, because the dark probes carry the evidence for the radius and the cone.
3. **Visibility** V: the light-weighted share of the lit probes that the origin sees, using BSP traces.
4. **Relocation**: if V < 0.8, the origin moves to the best of 52 positions within `min(sigmaP, 0.75 cell
   diagonal)`, scored by V × Gaussian(sigmaP). It never moves further than that, so the fit cannot invent a lamp.
5. **Runtime model fit**: the fit uses exactly the shader attenuation `clamp(0.5 R²/r² − 0.5, 0, 1)` [×
   `smoothstep(cosOuter, cosInner, cos)`], on visible probes only. A wall shadow is not a falloff.
   - The fit is **one-sided**. The brightness is the 25th weighted percentile of `L / A` over the probes that the
     model reaches and does not saturate (a lower envelope). The quality is (explained − 2 × overshoot) / total.
     Too little light is safe, because the remainder stays baked. Too much light is excess in the fog.
   - Why: q3map lights fall off as 1/r², while the runtime attenuation saturates at 1 inside R/√3. Energy-weighted
     least squares fits only the unmatchable near field. Relative least squares lets the colour collapse to 0
     wherever many dark probes are in reach. Both were tried, and both failed the omni and spot cases.
   - R uses 28 log steps in [0.5, 1.25] × the attribution range, then a golden-section refinement.
6. **Spot vs point**: this is decided *before* the physical test, because a point model of a spot explains
   almost nothing.
   - The outgoing directions are weighted by the angular intensity `lum · r²`. Weighting by lum alone lets the
     nearest probes decide, and they surround the light.
   - Tests: ρ ≥ 0.35, and the near and far axes agree within about 20°.
   - Search: the origin slides along the axis (9 steps within 1.5 sigmaP; the seed fit of a cone lands inside it)
     × outer angle (P92 of θ ±5°/±10°) × inner fraction {0.4, 0.6, 0.75, 0.9}.
   - The spot is accepted if E_spot ≤ 0.75 E_point and the **dark sector is open**: at least 25–75% of the probes
     outside the cone must be visible from the light. Behind a doorway the dark sector is blocked, so the light
     stays a point (design §16).
7. **Confidences**:
   - `physical = V · sqrt(quality · R²/(R²+4σ²)) · confidence^¼`. The visibility decides; the transport
     confidence only tempers it.
   - `spot = physical_spot · ρ-term · improvement-term · open-term`
   - Thresholds: physical 0.6 (`r_recoveredPhysicalConfidence`); spot 0.6 × physical (`r_recoveredSpotConfidence`);
     transport below 0.35.
8. **Animated styles**: a light whose attributed light is mostly in cells with styles[1..3] is flagged
   `ANIMATED` and never promoted (design §82).

## Promotion and energy (`BuildLights`)

Candidates are the physical, non-animated lights, ordered by confidence × explained energy × physical. At most
`r_recoveredVolumetricMaxLights` (32) are promoted. For each candidate:

- `w = physical`. The color is scaled by w, which is a partial promotion. The model is already a lower envelope,
  so it is not scaled twice.
- Every cell within R is traced from the light.
  - **leak** = modelled light at blocked cells / all modelled light. The runtime light has no shadow yet, so a
    lamp whose light would pass through walls stays baked (`LEAK_MAX` 0.15).
  - **excess** = modelled light above the remaining budget Q at visible cells / modelled light there
    (`EXCESS_MAX` 0.2).
- If both checks pass: `add = min(model, Q)`, then `Q −= add`, `B −= add`, `P += add`. The promoted lights are then
  removed from the attribution, which runs again on the residual Q, so M' comes from the others only.

Invariants (checked on every test case): `B' + P + S == legacy`, `B' + P == B`, `P ≤ Q ≤ B`, `|M'| ≤ B'`. The
runtime fog becomes `B' + L1(M') + Σ exact promoted lights`. No energy is added: the confident part of the
low-frequency field is replaced by structured lights.

Because promotion changes the textures, it is latched: `r_recoveredVolumetricLights` and the thresholds take
effect on the next map load or `vid_restart`.

## Froxel consumer (`R_VolumetricBuildLightLists`)

- Recovered lights are appended **after all dynamic lights**. The per-cluster cap (32) fills in list order, so a
  saber or an explosion is never pushed out by a lamp; the lamp is only missing where a cluster overflows.
- Culling: the PVS of the view cluster against the light origin's cluster, then the froxel range test
  (`R_VolumetricLightRange`), then sorting by importance, capped at `r_recoveredVolumetricMaxLights`.
- Texels are the normal point and spot format, unshadowed. The color is pre-scaled by
  `r_volumetricFogStaticScale / r_volumetricFogDlightScale`, because the dynamic sum is multiplied by the dlight
  scale in the shader and the baked light by the static scale.
  **Limitation**: `r_volumetricFogDlightScale 0` also removes the recovered lights, whose energy is no longer baked.
- Cone softness: `inner ≤ outer − froxel angular footprint` at half the radius, so narrow cones do not alias
  (design §31).
- Main view only, like the rest of the froxel fog. Particle light (`r_particleLighting`) receives the recovered
  lights automatically through the same lists.

## Cvars and commands

| cvar | default | |
|---|---|---|
| `r_staticLightReconstruction` | 1 | structured lights (latched) |
| `r_recoveredVolumetricLights` | 1 | promote and inject (latched) |
| `r_recoveredVolumetricMaxLights` | 32 | promoted per map, and per view in the fog (latched) |
| `r_recoveredPhysicalConfidence` | 0.6 | developer, map load |
| `r_recoveredSpotConfidence` | 0.6 | developer, × physical threshold, map load |
| `r_staticLightDebug` | 0 | 1 lights, 2 + range / sigma, 3 promoted only, 4 fog: recovered lights only, 5 fog: without recovered lights (residual) |

`r_staticLightDebug` 1–3 colors: green physical (bright = promoted), yellow uncertain point, magenta transport,
cyan area. Spots show their outer cone at R/4.

`r_vfogStaticStats [proxies | lights]` prints the reconstruction stats, promoted energy, excess and partition
error. `lights` lists each structured light with kind, flags, confidences, leak, excess and cluster.
`r_vfogLightStats` shows the recovered lights in the volume and those culled by the PVS.

## Synthetic results (`tools/volrecon_test`)

| case | result |
|---|---|
| one omni light, sub-cell light | promoted POINT at the lamp, ~25% of B, 0% excess |
| red + blue, opposing / same-side whites | both promoted, ≤ 1% excess |
| doorway (lamp behind a wall) | physical, **not** promoted (leak 0.38), no spot |
| true 35° spot pointing down | SPOT, origin 12 u from the lamp, axis down, promoted, 0% excess |
| lamp 6 u off a wall | physical, in the room, not promoted (leak 0.30) |
| window edge | physical, not promoted (leak ~0.7) |
| 64×64×32 grid, 40 lights | reconstruction 0.55 s total (the light stage about 0.2 s, about 60k traces) |

## Not done yet (next steps of the design)

- Disk cache (sections MOM1 / SRCS / …); currently rebuilt at every load.
- Static shadows for recovered lights (cached cube atlas). Until then the leak gate keeps lamps near walls baked,
  which is most indoor lamps on real maps. Expect few promotions on stock maps.
- Light portals, portal shafts, Forward+ specular-only lights, the L1 entity probes (`r_entityLightGrid 3`), rain,
  and area-light calibration.
- Tuning thresholds on stock maps (only synthetic grids so far).
