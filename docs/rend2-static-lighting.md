# Rend2: Static Lighting Reconstruction

Status: physical point / spot lights with energy promotion and unshadowed recovered lights in the froxel fog;
phase 2 adds entity L1 probes, light portals with volumetric shafts, and grid-calibrated area lights. Everything
builds, the synthetic tests pass (`tools/volrecon_test`, 1087 checks) and the changed shaders compile on Intel and
NVIDIA. **None of it has been run in game yet.** There is no disk cache yet, so the cost is paid at every map load.

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
   light portals      ─────── R_StaticLightPortals() → froxel shafts, r_lightPortalDebug
   entity directions  ─────── R_BuildEntityLightProbes (tr_bsp.cpp) → lightall USE_ENTITY_GRID_L1
   calibrated RECT    ─────── R_CalibrateAreaLight (tr_arealights.cpp)
```

The reconstruction is built when any consumer is on: the froxel fog (`r_volumetricFog 2`), the entity probes
(`r_entityLightGrid 2` + `r_entityLightProbes`), the portals (fog + `r_lightPortals`), or the area calibration
(`r_ltcAreaLights` + `r_ltcAreaCalibration`). It runs in mode 1 (structured) whenever one of them needs it, even with
`r_volumetricFogStaticDirectional 0`; the fog then still gets only its isotropic split. Promotion (lights and
portals) happens only when the fog consumes it, because only the fog injects the exact light.

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

## Entity L1 probes (`r_entityLightProbes`, with `r_entityLightGrid 2`)

The legacy grid gives a character one ambient colour, one directed colour and **one** direction. With a red lamp
on the left and a blue one on the right, that is a purple compromise from the middle. The probes store first-order
irradiance per colour channel instead: `I_c(N) = C0_c + C1_c · N` (design §70–81).

- **Library**: `vrOutput::entityDir[c]`, the per-channel direction mix `((D − E)·bspDir + M) / D`, taken from the
  attribution **before** any promotion, since characters do not receive the recovered lights. The attributed part
  E is spread over its sources (the moment M); the unattributed rest and the baked sun keep the BSP direction.
  `|mix| ≤ 1`. Without a reconstruction (modes 0 and 2, or `r_staticLightReconstruction 0`) it is the BSP direction.
- **Renderer** (`R_BuildEntityLightProbes`, tr_bsp.cpp):
  - Uses the same A/D as `R_PackEntityLightGrid`: all styles active at load, linear, with `r_ambientScale` and
    `r_directedScale` applied at load.
  - `C0 = A + D/4`, `C1 = D/2 · mix`, de-ringed so that `|C1| ≤ C0`. This is the L1 projection of the legacy model.
  - Three `RGBA16F` volumes (R, G, B: `C1.xyz`, `C0`; `w = −1` marks wall cells), bound to the same three entity
    grid pointers and texture units. No new sampler.
- **Shader** (`USE_ENTITY_GRID_L1`):
  - Same manual 8-cell fetch as the legacy grid: wall cells are skipped and the rest renormalized.
  - `L = normalize(luma C1)`, `directed = 2·max(C1_c·L, 0)`, `ambient = C0 + C1·N − directed·max(N·L, 0)`. The
    diffuse is then exactly the L1 irradiance, and the specular keeps a directional lobe.
  - The LDR minimum light (`RF_MINLIGHT`) still applies.
- **Limitations**:
  - Animated light styles are frozen at their load state (no per-frame refresh in L1).
  - `r_ambientScale` and `r_directedScale` need a `vid_restart` to take effect.
  - Debug views 1–9 of `r_entityLightGridDebug` compare the legacy volumes (the probes are then off); 10 shows C0,
    11 shows the L1 direction.

## Light portals and shafts (`r_lightPortals`, with `r_volumetricFog 2`)

A light portal is an aperture (a door, window or gap) that the light of one source comes through (design §32–50).
It is not a new source: its shaft replaces the baked light that came through it.

**Detection** (`BuildPortals`). It runs over the 64 sources with the most explained light that are not promoted
lights.

- The design's ray-bundle bottleneck cannot tell a door from a lamp (the rays of a lamp converge on the lamp, §40),
  so the aperture is found **geometrically**.
- **Receivers**: stratified over the source's domain (distance bins × octants), so the few cells behind a door are
  among them. Only receivers that can see the source are used.
- **Constriction**: march along the segment from the receiver to the source in ¼-cell steps. The first point where
  lateral probes (2 cells) hit solid on **both** sides of one axis is a constriction.
- **Clustering**: constrictions on the same plane across the seed direction, laterally within 4 cell diagonals,
  form one cluster. A cluster needs at least 6 receivers, at least 3% of the visible sampled receivers, and an
  angular spread below about 34°. Receivers are counted, not weighted by light: behind a door they are dim next to
  the lamp's own room.
- **Aperture**: a 16×16 open/solid mask on the plane, from short traces through it.
  - Take the open component around the cluster. If it reaches the mask border, retry with a mask of 2.5, 5, then
    10 cells; if it still reaches the border, it is open space and rejected.
  - At least 50% of the ring around the component must be solid.
  - The rectangle comes from a PCA of the component. Upstream sky gives a `SKY` portal, which is metadata only
    (the sun shafts come from the cascades, §113).
- **Shaft model**: the design's finite linked source (§47). For a point x behind the plane:
  - Take the point q where the line from the source proxy to x crosses the plane.
  - The soft rectangle gate is 0 at the rim, so the shaft never passes through the wall. The soft edge is ¼ cell
    plus the source's σ seen through the aperture.
  - Multiply by the runtime point attenuation with `R = |source − center| + range`, and fade out over the range.
  - A first version back-projected along one mean direction. It failed because rays from a lamp diverge after the
    door.
- **Energy**:
  - Color: the one-sided envelope fit, as for lights, over the attributed cells the shaft reaches.
  - Confidence: quality × solid ring × receiver share; a portal needs at least 0.25.
  - Promotion: leak ≤ 15% (the cell must see its plane point q) and excess ≤ 20%. Then `Q`, `B` and `P` are
    updated as for lights.
  - The portal's source stays in the attribution of the remaining light, so its own room keeps its moments.

**Runtime**:

- The light texel stride goes from 4 to 5. A portal is marked by texel 1 `w = −3 − soft edge`. Texels: center and
  range, color, source and penumbra, `right·halfW`, `up·halfH`.
- Portals are appended after the dynamic and recovered lights, culled by the PVS of either side, then by the beam's
  bounding sphere.
- The `DynamicLights` portal branch evaluates the same shape with the HG phase towards the source. There is no
  shadow lookup (the aperture is the occluder) and no media march. Particle light receives it through the same
  lists.
- `r_lightPortalDebug`: 1 draws the rectangles, the normal and the line to the source; 2 adds the shaft range;
  3 leaves only the shafts in the fog; 4 removes them from the fog. `r_vfogStaticStats portals` lists the portals
  with the BSP clusters and areas on both sides (front = receivers, back = source side).

## Area light calibration (`r_ltcAreaCalibration`, with `r_ltcAreaLights`)

The area anchors were already calibrated against the grid: the grid light ≈ `calibration · E_area(x)`, where
`E_area` is the cosine-weighted solid angle of the emitter, so the emitter radiance is `calibration · chroma`
(design §90–97). The quadrature is now 5×5 within about one emitter size of a probe.

- `vrAreaSource::sourceIndex` / `vrStaticLight::areaSourceIndex` link an anchor back to its `s_al.lights` entry
  (explicit lights by index, automatic lights by matching the candidate).
- `R_CalibrateAreaLight`:
  - **Automatic** lights take `color = radiance · r_ltcAreaCalibrationScale` and `intensity 1`; their range is
    recomputed by `R_AreaLightDefaultRange` and the metrics are recached.
  - **Authored** lights keep their values; `developer 1` reports the grid value next to theirs.
  - Anchors below 0.3 confidence are left untouched.
- The grid→LTC constant has not been validated against q3map output (§94); `r_ltcAreaCalibrationScale` holds it.
  On the synthetic panel the recovered radiance is 3.003 for a true 3.0.

## Cvars and commands

| cvar | default | |
|---|---|---|
| `r_staticLightReconstruction` | 1 | structured lights (latched) |
| `r_recoveredVolumetricLights` | 1 | promote and inject (latched) |
| `r_recoveredVolumetricMaxLights` | 32 | promoted per map, and per view in the fog (latched) |
| `r_recoveredPhysicalConfidence` | 0.6 | developer, map load |
| `r_recoveredSpotConfidence` | 0.6 | developer, × physical threshold, map load |
| `r_staticLightDebug` | 0 | 1 lights, 2 + range / sigma, 3 promoted only, 4 fog: recovered lights only, 5 fog: without recovered lights (residual) |
| `r_entityLightProbes` | 1 | L1 probes instead of the legacy grid volumes, with `r_entityLightGrid 2` (latched) |
| `r_lightPortals` | 1 | portals and froxel shafts, with `r_volumetricFog 2` (latched) |
| `r_lightPortalDebug` | 0 | 1 portals, 2 + shaft range, 3 fog: shafts only, 4 fog: without shafts |
| `r_ltcAreaCalibration` | 0 | automatic LTC area lights take the grid radiance (latched) |
| `r_ltcAreaCalibrationScale` | 1 | developer, grid→LTC constant (latched) |
| `r_entityLightGridDebug` | 0 | adds 10 L1 C0 and 11 L1 direction |

`r_staticLightDebug` 1–3 colors: green physical (bright = promoted), yellow uncertain point, magenta transport,
cyan area. Spots show their outer cone at R/4.

`r_vfogStaticStats [proxies | lights | portals]` prints the active consumers, the portal statistics, and the reconstruction stats, promoted energy, excess and partition
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
| 64×64×32 grid, 40 lights | reconstruction 0.8 s total including portals (box traces; real BSP traces cost more) |
| doorway with floor and frame | one portal at the gap, 200 u wide (gap 192), 520–640 u tall (gap about 616), shaft promoted, leak 0.03 |
| omni light / true spot in an empty room | no portal |
| red + blue | entity L1: red from the left, blue from the right |
| ceiling panel of radiance 3.0 | calibrated radiance 3.003 |

## Not done yet (next steps of the design)

- Disk cache (sections MOM1 / SRCS / …); currently rebuilt at every load.
- Static shadows for recovered lights (cached cube atlas). Until then the leak gate keeps lamps near walls baked,
  which is most indoor lamps on real maps. Expect few promotions on stock maps.
- Forward+ specular-only lights, residual probes with exact recovered light on characters (§76), and rain on the
  L1 probes.
- Portal graph and regions (§99), merging of coplanar portals of different sources (each source keeps its own
  shaft for now), and portals for sources that cannot see their receivers (around corners).
- Validating the grid→LTC calibration constant against q3map-compiled test maps.
- Tuning thresholds on stock maps (only synthetic grids so far).
