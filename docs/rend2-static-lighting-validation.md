> **Historical validation of a removed subsystem.** The grid-source fitter and
> its controls/tools are obsolete. Preserved results explain its removal; these
> commands are not supported by current builds. See [removal report](rend2-grid-reconstruction-removal.md).

# Rend2: Static Lighting Reconstruction — validation on stock maps

Date: 2026-10-15. Branch `rend2-unified-wip`, HEAD `266fc26c9` plus the fixes listed in
[Fixes made during the validation](#fixes-made-during-the-validation). Design doc:
[obsolete design](rend2-static-lighting-obsolete.md). Captures: [rend2-static-lighting-validation/](rend2-static-lighting-validation/).
Tools: [tools/rend2/static_lighting/](../tools/rend2/static_lighting/).

**Short answer: on 18 stock SP maps and 2 MP maps the reconstruction promotes nothing.** It finds 0 physical
lights, 0 promoted points, 0 promoted spots and 0 shaft portals, and it attributes 0.0–3.3 % of the baked
light. The synthetic tests pass because their grids differ from real q3map grids in four ways that matter (walls
marked invalid, HDR-like unclamped values, clean 1/r² falloff, few emitters). The problems are mostly
algorithmic, not thresholds: lowering the physical threshold to 0.4 makes 6 lamps physical on hoth3 and kor1, and
the leak gate then rejects every one of them (leak 0.76–0.87).

## How it was run

- Build: MSBuild Release (`build/msvc` renderers, `build/msvc-all` engines + game DLLs) of the current HEAD.
- Run: an isolated test install (fresh exe, renderers and `jagamex86_64.dll`, its own `fs_homepath`, assets from
  `OpenJK/build-rend2` as `fs_basepath`), driven by `+exec` scripts (`devmap`, `exitview`, `setviewpos`,
  `screenshot`, `quit`), `logfile 2` for the full console. The game window is hidden by a watcher
  (`hide.ps1`); rendering and screenshots are unaffected.
- Every run used: `developer 1, r_forwardPlus 1, r_volumetricFog 2, r_staticLightReconstruction 1,
  r_volumetricFogStaticDirectional 1, r_recoveredVolumetricLights 1, r_lightPortals 1, r_entityLightGrid 2,
  r_entityLightProbes 1`, `r_loadProfile 1`. These are set on the command line. SP execs `autoexec_sp.cfg`,
  not the install's `autoexec.cfg`, and the command line overrides the config files, so every latched value
  was in effect from the first renderer start (checked: `consumers fog on, entity probes on, portals on`).
  Every latched A/B used a separate launch or `vid_restart`.
- Ground truth: the stock BSPs contain **no `light` entities** (q3map strips them). Instead, `bspinfo.py` lists the
  emissive surfaces of every map (shaders with `q3map_surfacelight` or a `glow` stage), clustered within 48 u
  into "fixtures", plus the grid statistics. Most JKA lamps are entity lights with a glow texture next to them,
  so this is a proxy, not exact positions.
- Offline harness: `vrdiag.cpp` `#include`s `tr_volrecon.cpp` and runs it on grids exported from the BSP
  exactly as `tr_staticlighting.cpp` feeds them (`export_grid.py`: LDR bytes / 255, `r_mapOverBrightBits 0`). It
  reproduces the in-game seed and fit counts exactly (hoth3 319 fits / 221 merged, t1_sour 208/166, kor1 234/189)
  and prints the rejection reasons per stage. It has no BSP traces, so it stops before the structured lights.

### Feature flags at HEAD (`code/rd-rend2/tr_init.cpp`, the MP file matches)

| cvar | default | flags |
|---|---|---|
| `r_staticLightReconstruction` | 1 | archive, latch |
| `r_volumetricFog` | **0** | archive, latch |
| `r_volumetricFogStaticDirectional` | **0** | archive, latch |
| `r_recoveredVolumetricLights` | 1 | archive, latch |
| `r_recoveredVolumetricMaxLights` | 32 | archive, latch |
| `r_recoveredPhysicalConfidence` | 0.6 | latch |
| `r_recoveredSpotConfidence` | 0.6 | latch |
| `r_staticLightDebug` | 0 | cheat |
| `r_lightPortals` | 1 | archive, latch |
| `r_lightPortalDebug` | 0 | cheat |
| `r_entityLightGrid` | **0** | archive, latch |
| `r_entityLightProbes` | 1 | archive, latch (needs `r_entityLightGrid 2`) |
| `r_entityLightGridDebug` | 0 | cheat, **latch** (10/11 need `vid_restart`) |
| `r_forwardPlus` | 0 | archive |
| `r_ltcAreaLights` | 0 | archive, latch |
| `r_ltcAreaCalibration` | 0 | archive, latch |

With the defaults, the structured reconstruction runs only if a consumer is on. The default install
(`autoexec.cfg`: `r_volumetricFog 1`) builds none of it. The help text of `r_staticLightReconstruction` says
"with r_volumetricFogStaticDirectional 1", but the code also runs it for the entity probes, the portals and the
area calibration. `r_ltcAreaLights 0` does **not** stop the static area sources: they are collected from the
emissive candidates regardless (see below).

### Map set

| purpose (task) | map used | note |
|---|---|---|
| big grid, outdoor/indoor, sun | `hoth2` | sun parsed, 321 902 sun rays |
| very big grid, interiors, perf | `vjun1` | no parsed sun |
| indoor/artificial lights | `t2_trip` → **also `hoth3`, `t1_sour`, `kor1`, `academy1`** | `t2_trip` is the outdoor swoop chase, and its grid has only **63 unique values** in 1 M cells: nothing to reconstruct |
| indoor, weather | `t3_hevil` | 256-unit grid cells (18 879 cells) |
| foliage / sun | `yavin1b`, `yavin2` | **neither has a parsed sun** (0 sun rays) |
| more interiors | `vjun2`, `vjun3`, `t1_fatal`, `t2_rogue`, `t2_dpred`, `t3_bounty`, `t3_stamp`, `taspir1`, `hoth3` | |
| MP control | `mp/ffa2`, `mp/duel9` | MP engine and `rd-rend2`; stats only (the screenshot caught the team menu) |

## Results per map

`proxies` = point proxies + area anchors (in brackets: point sources dropped over the 256 budget).
`build ms` = `R_BuildStaticLighting` total. "Fixtures" = emissive clusters / those with any proxy within 128 u.

| map | grid cells | build ms | proxies | physical | points | spots | promoted | portals | promoted portals | promoted fraction | major false positives | major false negatives | visual issues |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| hoth2 | 1 044 126 | 3041 | 13 + 105 | 0 | 0 | 0 | 0 | 0 | 0 | 0 % | 13 transport proxies, none near a lamp (nearest fixture 174–18 000 u) | 92 glow fixtures, no point light near any | — |
| vjun1 | 1 001 520 | 3689 | 0 + 256 (1257 dropped) | 0 | 0 | 0 | 0 | 0 | 0 | 0 % | — | **every point source dropped** (anchors fill the budget) | — |
| t2_trip | 1 047 540 | 1532 | 3 + 0 | 0 | 0 | 0 | 0 | 2 sky | 0 | 0 % | 2 sky "portals" in open desert (metadata only) | n/a (flat grid) | — |
| t3_hevil | 18 879 | 1052 | 0 + 256 (2384 dropped) | 0 | 0 | 0 | 0 | 0 | 0 | 0 % | — | all point sources dropped | — |
| yavin1b | 798 537 | 1381 | 1 + 5 | 0 | 0 | 0 | 0 | 0 | 0 | 0 % | — | sun not parsed: the baked sun stays in B | no shafts visible |
| yavin2 | 866 304 | 1387 | 2 + 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 % | — | 2 fits from 512 seeds | — |
| hoth3 | 229 068 | 2938 | 79 + 71 | 0 | 6 | 0 | 0 | 0 (5 candidates) | 0 | 0 % | — | the tube lamps are found as points 105–125 u below the fixture, phys 0.40–0.47 | markers clipped by the poly budget (fixed) |
| vjun2 | 152 500 | 1426 | 0 + 256 (1847 dropped) | 0 | 0 | 0 | 0 | 0 | 0 | 0 % | — | all point sources dropped | — |
| vjun3 | 203 232 | 1606 | 0 + 256 (1336 dropped) | 0 | 0 | 0 | 0 | 0 | 0 | 0 % | — | all point sources dropped | — |
| t1_sour | 298 320 | 2612 | 38 + 218 (4 dropped) | 0 | 0 | 0 | 0 | 0 | 0 | 0 % | 38 transport, none fitted (fit 0) | 149 fixtures | — |
| t1_fatal | 375 375 | 1467 | 1 + 211 | 0 | 0 | 0 | 0 | 0 | 0 | 0 % | — | 2 fits from 512 seeds | — |
| t2_rogue | 980 352 | 4160 | 0 + 256 (1235 dropped) | 0 | 0 | 0 | 0 | 0 | 0 | 0 % | — | all point sources dropped | — |
| t2_dpred | 46 464 | 1132 | 0 + 256 (235 dropped) | 0 | 0 | 0 | 0 | 0 | 0 | 0 % | — | all point sources dropped | — |
| t3_bounty | 295 200 | 1291 | 0 + 256 (171 dropped) | 0 | 0 | 0 | 0 | 0 | 0 | 0 % | — | all point sources dropped | — |
| t3_stamp | 553 610 | 1993 | 0 + 256 (472 dropped) | 0 | 0 | 0 | 0 | 0 | 0 | 0 % | — | all point sources dropped | — |
| taspir1 | 52 155 | 1332 | 0 + 256 (772 dropped) | 0 | 0 | 0 | 0 | 0 | 0 | 0 % | — | all point sources dropped | — |
| kor1 | 562 326 | 1936 | 45 + 0 | 0 | 1 | 0 | 0 | 0 | 0 | 0 % | point 27 is 3.8 k u from any fixture | the braziers are FX, not glow surfaces | markers clipped (fixed) |
| academy1 | 29 952 | 317 | 9 + 0 | 0 | 1 | 0 | 0 | 0 | 0 | 0 % | — | — | — |
| mp/ffa2 | 122 766 | 1308 | 32 + 96 | 0 | 4 | 0 | 0 | 1 | 0 | 0 % | portal 291×200 u with the same BSP cluster on both sides (953\|953), leak 0.33 | 145 surfacelight fixtures | — |
| mp/duel9 | 496 800 | 1567 | 23 + 0 | 0 | 2 | 1 | 0 | 0 | 0 | 0 % | the only SPOT: axis straight down, cone 76°/85° (a hemisphere, not a spot) | — | — |

More detail (from `r_vfogStaticStats`):

| map | seeds / fitted | ray RMS mean / P95 (diag) | sigma mean (diag) | attributed | directional |
|---|---|---|---|---|---|
| hoth2 | 512 / 23 | 0.73 / 0.85 | 0.98 | 0.0 % | 0.0 % |
| hoth3 | 512 / 319 | 0.67 / 0.79 | 0.92 | 0.6 % | 0.5 % |
| kor1 | 512 / 234 | 0.70 / 0.81 | 0.95 | 1.5 % | 1.3 % |
| t1_sour | 512 / 208 | 0.60 / 0.75 | 0.85 | 0.3 % | 0.2 % |
| academy1 | 512 / 90 | 0.63 / 0.80 | 0.88 | 2.0 % | 1.7 % |
| mp/ffa2 | 512 / 158 | 0.72 / 0.82 | 0.97 | 3.3 % | 2.9 % |
| mp/duel9 | 512 / 142 | 0.71 / 0.85 | 0.97 | 2.1 % | 2.0 % |
| anchor-only maps | 512 / 4–314 | — | — | 0.0–1.8 % | 0.0–0.7 % |

Leak / excess distribution: on default settings, no light reached the promotion gates. At
`r_recoveredPhysicalConfidence 0.4` (hoth3, kor1) the 6 physical lights had **leak 0.76, 0.78, 0.79, 0.84, 0.85,
0.87** and excess 0.00–0.16: all rejected by leak, none by excess.

Totals over all 20 maps: **3010 rect anchors, 231 transport, 14 point, 1 spot; 0 physical, 0 promoted**.
Reasons for not promoting: 3010 rect (never promoted in v1), 224 transport (physical confidence below 0.35),
7 transport with no fit at all, 15 points/spots with low physical confidence. The weakest factor of
`phys = V · sqrt(quality · cPos) · conf^¼` is the fit quality for 207, the visibility for 27, and the transport
confidence for 5. Leak, excess, animated style and the source cap: 0 (none reached those gates).

## Answers

1. **Source proxies on stock maps:** 0–79 point proxies (median 2) + 0–256 area anchors. On 10 of 20 maps there
   are 0 point proxies, because 256 area anchors fill `PROXY_MAX` first and 171–2384 point sources are dropped.
2. **Classification:** transport 231, physical point 0, physical spot 0, rect 3010. Non-physical points: 14, spots: 1.
3. **Positions vs lamps:** the 15 point/spot lights lie at a median 365 u from the nearest emissive fixture (4 within
   128 u, 5 within 256 u). The good cases are the hoth3 tube lamps: the point sits 105–125 u straight below the
   tube, about one 128 u grid layer (`hoth3_debug2_tube_lamp_point.jpg`). sigmaP is 0.85–1.06 cell diagonals
   (130–170 u), which matches that error. Transport proxies are mostly far from any lamp.
4. **Radius / colour:** R is 830–1925 u for all point lights. R is searched in [0.5, 1.25] × the attribution range,
   and that range sits at `RANGE_MAX` (10 diagonals = 1568 u) for every proxy (the LDR profile never falls to 1 %
   of the mean luma). The colour is 0.000–0.02 in grid units, while the grid has median A ≈ 0.25–0.5 and
   D ≈ 0.45–1.0: about 1 % of the baked light, because the attributed light L it is fitted to is ~1 % of Q.
   Neither matches the baked illumination.
5. **PROMOTED:** 0 on every map.
6. **What blocks promotion:** first the physical confidence, through the fit quality. 57 of 79 hoth3 proxies get
   fit = 0, because fewer than 6 probes carry attributed light. When the threshold is lowered, **leak** rejects
   every lamp, because R ≈ 6–12 cells reaches through walls. Bad fit as such, excess, animated styles and the
   source cap: none on stock maps.
7. **Portals at real openings:** across 20 maps there are 3 portals: 2 sky on t2_trip and 1 on mp/ffa2. hoth3 has 5
   constriction candidates and none passes the aperture or confidence gates. No real door, window, arch or ceiling
   aperture was found.
8. **False-positive portals in open space:** the 2 t2_trip sky portals are in the open desert (793×528 u and
   1050×560 u, sky metadata only, no shaft). The mp/ffa2 portal has the same BSP cluster on both sides.
9. **SPOT lights:** 1 in 20 maps (mp/duel9), and it is not a spot (cone 85°, straight down), not physical (0.57).
10. **Recovered volumetric point/spot lights visible:** no, there are none to show. `r_staticLightDebug 4`
    (now really "recovered only") is black fog (checked on hoth3; with nothing promoted it must be the same everywhere).
11. **Portal shafts visible:** no, there are no shaft portals.
12. **Entity L1 probes:** they do not improve NPC/player lighting on stock maps. With ~0 % attributed, `entityDir`
    falls back to the BSP direction, so L1 = the legacy grid projected to L1. In the A/B shots (vjun2 red/blue,
    t3_stamp red/cyan, t3_bounty orange/blue, hoth3) the two sides of the character do **not** get different
    colours. The L1 result is slightly flatter than legacy (the clamped cosine lobe becomes a linear term:
    `vjun2_entity_legacy.jpg` vs `vjun2_entity_l1.jpg`). Debug 11 shows one uniform direction over the whole model.
    No seams or popping were seen at the tested spots (single frames only, no motion test).
13. **Regressions vs the legacy grid:** none found that the reconstruction causes. Two dark or bright cases turned
    out to be the same with `r_entityLightGrid 0`:
    - a black lamp hood and player near the hoth3 ceiling: stock grids have no invalid wall cells, so black wall
      cells are interpolated (`hoth3_ceiling_legacy_grid0.jpg` = `hoth3_ceiling_grid2_l1.jpg`);
    - an overexposed player at the yavin1b spawn.

    The L1 path is slightly flatter than legacy. With nothing promoted, the fog's baked field equals the legacy
    split, so the fog has no regression either (debug 5 ≡ normal).
14. **Map-load cost:** 0.3–4.8 s per load (3 repeated loads within ±8 %). See [Performance](#performance-bottlenecks).
15. **Code fixes vs tuning:** see the sections below. Almost everything needs code. Threshold tuning alone cannot
    produce a single promotion.

## Confirmed working

- The pipeline runs on every stock map without crashes, NaNs or partition errors (`partition error 0`,
  split error ≤ 0.042 % relative).
- Sun visibility traces on hoth2 (277 454 cells, 11 112 refined at edges).
- Seed fitting recovers real lamp positions where the grid has a clean local peak: hoth3 tubes, 105–125 u below
  the fixture, within sigmaP.
- Area anchors coincide with emissive surfaces (by construction) and survive the grid agreement test (e.g. vjun1:
  256 accepted of 4798).
- The leak gate does its job: a 1–2 k u unshadowed light in a hoth3 corridor would leak, and it is refused.
- Debug drawing (after the poly fix), `r_vfogStaticStats proxies|lights|portals`, `r_staticLightDebug 1/2/4/5` and
  `r_entityLightGridDebug 10/11` behave as documented. `r_staticLightDebug 3` and the `r_lightPortalDebug` views
  could not be checked: nothing is promoted, and the only portals were out of view.
- The energy bookkeeping cannot be disproved, but it is also not exercised: with 0 promotions,
  `B' + P + S == legacy` is trivially true.

## Confirmed broken

1. **Area anchors starve the point proxies.** `PROXY_MAX = 256` is one budget with anchors first. On 10 of 20 maps
   (all of Vjun, t2_rogue, t3_*, taspir1, t2_dpred) every point source is dropped. The anchors come from every
   glow/surfacelight candidate (up to 9310 per map), even with `r_ltcAreaLights 0`, and are never promoted (v1).
2. **Attribution is ~0 % on real grids** (0.0–3.3 %; hoth3 0.6 %). Measured factors over all point sources'
   domains (vrdiag): falloff `predicted / observed` Q-weighted mean 0.04–0.15 (below 0.2 in 58–94 % of the cells),
   BSP agreement 0.02–0.25 (below 0.2 in 60–95 %), proxy confidence ≈ 0.31–0.36. `m` multiplies all of these,
   so E = Q · max m stays near zero. Every structured stage downstream (fit, physical, spot, promotion, portals,
   entity L1) works on this starved L.
3. **Range always saturates at `RANGE_MAX`** (10 diagonals, 1568 u at 64×64×128). The 1 % of the mean luma target
   is never reached by LDR profiles, so the domains flood about 20 000 cells **through walls** and R ends up
   830–1925 u. That large R is what makes the leak gate reject everything.
4. **Stock grids have no wall cells.** `styles[0]` is never `LS_LSNONE` (0 of 4001 unique entries on hoth2, and 100 %
   "valid" on every map). Walls are black valid cells (18–93 % of the cells). The synthetic tests mark walls
   invalid, so on real maps:
   - floods cross walls;
   - 66–79 % of the seeds touch a black (wall) cell (5 of 6 maps; academy1 12 %);
   - gradients are saturated at wall boundaries (8–41 % of the lit cells have strength 1);
   - the entity L1 wall flag (`w = −1`) never triggers (legacy also interpolates the black cells).
5. **LDR saturation flattens the seeds.** D is clamped at 255; the median lit D is 1.000 on t3_hevil and 0.98 on
   yavin1b. The global seed score range is 1.00 → 0.86, so the 512 seeds concentrate on saturated plateaus, and
   ray RMS ≈ 1 diagonal fails `SIGMA_RAY` 0.75 (`cRay` is the weakest factor in 160–347 of the rejected fits per
   map).
6. **Debug drawing was clipped by the scene poly budget** (fixed, see below). `max_polys = Q_min(r_maxpolys, 600)`:
   raising `r_maxpolys` does not help, and the markers were drawn in list order, so only the first ~8 lights showed.
7. **`r_staticLightDebug 4` / `r_lightPortalDebug 3` did not show "only" the exact lights** (fixed): only the
   dynamic or recovered lights were dropped, while the baked field and the sun stayed in the fog.
8. **The MP cgame did not link** at HEAD (`MAX` undefined in `CG_InsertWaterWake`, codemp/cgame/cg_view.c): fixed
   (`Q_max`), unrelated to the reconstruction.

## Needs threshold tuning

Only after the algorithmic fixes; on their own they change nothing:

- `r_recoveredPhysicalConfidence`: the hoth3 lamps peak at 0.40–0.47 (limited by `conf^¼` ≈ 0.73 and
  `sqrt(quality)` ≈ 0.6–0.86). 0.6 is above anything real grids produce.
- `SIGMA_RAY` (0.75 diag), relative to the latLong quantization and the 128 u z cells: the stock ray residual is
  0.60–0.81 mean.
- `RANGE_FALLOFF` (1 % of the mean luma): needs an LDR-aware rule (or a cap at the first wall), see below.
- `PORTAL_MIN_RECEIVERS` / share: these only matter once sources have attributed light.

## Needs algorithmic changes

1. **A wall model for stock grids:** treat the black cells (all channels 0, A = D = 0) next to lit cells as
   invalid, or use BSP point contents at the cell centre, before gradients, seeds and floods. This is the biggest
   difference between the synthetic and the real case.
2. **Separate budgets:** point proxies get their own budget, anchors are ranked and capped (or only built when a
   consumer uses them).
3. **Attribution:**
   - the falloff term must accept LDR clamped / q3map linear profiles; the profile is fitted on 4 cells and
     extrapolated as 1/r², and it underpredicts beyond the support;
   - the domain must be bounded by visibility (walls), not only by range;
   - the confidence should not cap m at ~0.35.
4. **Radius:** fit R against the visible, wall-bounded domain. A lamp of 6–12 cells' radius will always leak until
   static shadows exist.
5. **Seeds:** per-region or per-cluster seed budgets (not one global top-512), with saturated plateaus excluded.
6. **Spots:** the 76°/85° hemisphere on mp/duel9 shows the spot test accepts wide downward lobes: cap the outer angle.
7. **Portals:** require different BSP clusters or areas on both sides (the mp/ffa2 case), and reject sky portals
   in open terrain (t2_trip).

## Blocked by missing static shadows

- Promotion of indoor lamps: even with the right R, a q3map lamp near a wall leaks (the synthetic "lamp 6 u off a
  wall" case: leak 0.30). On hoth3, all 6 candidates are rejected by leak (0.76–0.87) with today's R.
- Visual validation of the promotion invariant (combined ≈ original, dark holes, overbright, popping, cone
  aliasing): impossible on stock maps today, because nothing is promoted. Debug 4 is black, and debug 5 ≡ normal.
- Portal shafts: none exist to validate.

## Performance bottlenecks

Release build, 3 repeated loads each (spread ≤ 8 %), ms:

| map | cells | total | sun trace | area sources | gradients + seeds | anchors | attribution | lights (traces) | portals | upload |
|---|---|---|---|---|---|---|---|---|---|---|
| vjun1 | 1.0 M | 3361–3754 | 0 | 614–708 | 540–600 | 168–213 | 1052–1264 | 13 | ~40 | 515–579 |
| hoth2 | 1.0 M | 2598–2876 | 577–659 | 159–173 | 410–440 | 7 | 245–275 | 215–243 | ~120 | 492–584 |
| t2_rogue | 0.98 M | 4714–4800 | 0 | 1365–1404 | 550–580 | 499–514 | 1300–1389 | 17–20 | ~30 | 495–511 |
| hoth3 | 0.23 M | 3317–3453 | 0 | 189–196 | 90 | 10 | 215–236 | 1903–1964 | ~600 | 114–135 |

- **Attribution** dominates on the emissive-rich maps (vjun*, t2_rogue, t3_*: 0.5–1.4 s). Its cost is the
  256 anchors, each with a 5×5 quadrature over a domain of up to 32 768 cells.
- **Area-source collection** (`R_CollectStaticAreaSources` → the area light candidates): 0.6–1.4 s on the same maps.
- **BSP traces** in the structured lights: 1.9 s on hoth3 (~280 K traces for 79 proxies), and the portals add
  ~0.6 s. Wherever proxies exist, this is the cost that grows.
- **Sun traces:** 0.6 s on hoth2 (322 K rays).
- **Upload** (3D textures + entity probes): ~0.5 s on 1 M-cell grids.
- Gradients + seeds: ~0.5 s per 1 M cells. Allocations were not a measurable cost.
- Share of the world load: 15–45 % (e.g. hoth3 3.3 s of 6.0 s; vjun1 3.4 s of 8.2–9.3 s).

## Recommended next fixes

In order; each one can be checked with `tools/rend2/static_lighting/vrdiag.cpp` offline before any game run:

1. Mark black cells next to lit cells (or cells in solid) invalid for the reconstruction, and re-run vrdiag on
   hoth3, t1_sour, kor1, academy1.
2. Give point proxies their own budget, and build area anchors only for active consumers (or cap them by energy).
3. Bound the domains and the range by visibility, and fit R against the visible domain (this is what
   unblocks leak).
4. Replace the multiplicative attribution score with one whose expected value on a clean lamp is ~1, and measure
   the attributed fraction on stock grids (target: tens of %, not 1 %).
5. Then re-tune `r_recoveredPhysicalConfidence`, `SIGMA_RAY` and the spot cone cap on the stock set, and add a stock
   grid regression case (an exported `.vrg` of hoth3) to `tools/volrecon_test`.
6. Static shadows for the recovered lights (design next step): needed for most indoor lamps.

## Fixes made during the validation

- `shared/rd-rend2/tr_staticlighting.cpp`: `r_staticLightDebug` draws the nearest markers in front of the view first
  and stops before the scene poly budget runs out (64 polys left for the game). `r_lightPortalDebug` does the same.
  Before: `hoth3_debug2_before_poly_fix.jpg` and `kor1_debug2_markers_missing.jpg`; after:
  `hoth3_debug1_after_poly_fix.jpg`.
- `shared/rd-rend2/tr_volumetric.cpp`: `r_staticLightDebug 4` and `r_lightPortalDebug 3` zero the baked-field and
  sun scales of the froxel inject (the recovered and portal colours are scaled on the CPU), so they show only the
  exact lights as documented (`hoth3_debug4_recovered_only.jpg`).
- `codemp/cgame/cg_view.c`: `MAX` → `Q_max` (the MP cgame did not link at HEAD).

Notes for running the game from scripts: the `screenshot` readback is asynchronous (a PBO read a few frames later),
so a `vid_restart`, `devmap` or `quit` right after it drops the file. Put `wait 10` after every `screenshot`.
`setviewpos` cannot move a player on a vehicle (t2_trip swoop).

## Captures

`rend2-static-lighting-validation/`:
- `hoth3_debug2_before_poly_fix.jpg`, `kor1_debug2_markers_missing.jpg`: markers clipped by the poly budget (before
  the fix).
- `hoth3_debug1_after_poly_fix.jpg`: after the fix (yellow uncertain point, magenta transport proxies).
- `hoth3_debug2_tube_lamp_point.jpg`: a reconstructed point below a Hoth tube lamp, with its range and sigma circles.
- `hoth3_debug4_recovered_only.jpg` / `hoth3_debug5_residual.jpg`: nothing promoted, so 4 is black fog and 5 ≡ normal.
- `vjun2_debug1_area_anchors.jpg`: the anchors on the vjun2 light strips.
- `t2_trip_portal_debug2.jpg`: the swoop start (the vehicle blocks `setviewpos`, the sky portals are out of view).
- `vjun2_entity_{legacy,grid2,l1,l1dbg10,l1dbg11}.jpg`, `hoth3_entity_*.jpg`, `t3_stamp_entity_{legacy,l1}.jpg`:
  the entity lighting A/B.
- `hoth3_ceiling_legacy_grid0.jpg` vs `hoth3_ceiling_grid2_l1.jpg`: black wall cells interpolated, the same in legacy.
