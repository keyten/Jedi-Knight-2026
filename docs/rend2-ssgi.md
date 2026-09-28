# Rend2: screen-space diffuse GI (dynamic first)

Screen-space diffuse global illumination for Jedi Academy maps, where most static light (and its bounce) is
already baked into the lightmaps. By default the GI bounces only **dynamic outgoing radiance**: dynamic lights
(sabers, blaster bolts, explosions, muzzle flashes, Force effects; with `r_forwardPlus 1`, every clustered light)
and physical emission. It does **not** bounce the baked lighting a second time. A saber lights the wall next to
it directly (dynamic light), and that wall adds a weak colored bounce to the floor and the neighbouring surfaces.

Code: `shared/rd-rend2/tr_ssgi.cpp`, shared infrastructure `tr_screenspace.cpp` (with SSR), shaders
`ssgi_source.glsl`, `ssgi_trace.glsl`, `ssgi_temporal.glsl`, `ssgi_denoise.glsl`, `ssgi_composite.glsl`,
`ssgi_debug.glsl`, library `ssr_common.glsl` (shared ray march), source output in `lightall.glsl`.

Off by default (`r_ssgi 0`, latched). Nothing is created, the preprocessed lightall is identical to the previous
one (checked for 15 permutations), and renderFbo and the main pass are unchanged. Nothing is ever enabled on
behalf of the user.

## Audit: what the final HDR color is made of

| part | where | in the default SSGI source |
|---|---|---|
| static: lightmap / vertex light / light grid (diffuse + deluxe specular) | `lightall` `lightColor`, `ambientColor` | no |
| static: realtime sun (`USE_PRIMARY_LIGHT`), already in JKA lightmaps | `lightall` | no |
| static: cubemap IBL (replaced in part by SSR) | `CalcIBLContribution` | no |
| **dynamic**: dlights, legacy 32-light mask or Forward+ clusters (one shared loop) | `CalcDynamicLightContribution` → `EvaluateDynamicLight` | **yes, diffuse lobe** |
| **emissive**: `emissiveMap / emissiveColor / emissiveScale` (linear, physical) | `u_EmissiveParams` \|w\| = 1 | **yes** (`r_ssgiEmissiveScale`) |
| legacy `glow` keyword / `r_autoEmissive` stages (the whole stage color, no physical intensity) | `u_EnableTextures.x`, \|w\| = 2 | only with `r_ssgiGlowScale` > 0 (default 0) |
| additive effects (saber blades, sprites), glass, particles | blended, drawn after the opaque pass | no (their light comes in through their dynamic lights) |

Other inputs found: depth prepass + `velocityImage` (RG16F, `r_depthPrepass`), SMAA T2x `TemporalBlock`, the
motion blur history validity (`tr.temporalHistoryValid`), the GTAO depth chain (`aoDepth`, farthest-weighted
averages built before the main pass, which is a different filter and time, so it is not reused), and the SSR material
attachments and closest-depth Hi-Z pyramid. The Hi-Z pyramid and the normal attachment now form the shared
infrastructure.

Scene space: with HDR lightmaps or `r_linearLighting` the scene buffer is linear (`tr.linearLight`). Otherwise
it is display encoded, which is the common case for JKA maps. SSGI always works in linear HDR: lightall decodes the
dynamic share of its color, and the composite adds `Enc(Dec(color) + indirect) - color`, like the emissive
materials do.

## Dependencies

- **Forward+: none.** Legacy and Forward+ lighting share one `EvaluateDynamicLight` call site. The SSGI source is
  accumulated there, so it works in both modes and contains every clustered light in Forward+ mode (not only
  32). SSGI never loops over lights itself: it bounces the radiance that lightall already computed.
- **SSR: none.** SSR and SSGI share the normal attachment, the Hi-Z pyramid, the MSAA resolve, the ray march,
  the history cut detection and the timers (`tr_screenspace.cpp`). These are created when *either* is enabled,
  and SSGI does not need `r_ssr`.
- Notes printed once, when the situation first appears:
  - `r_ssgiSource 0` with `r_dynamiclight 0`: only emissive materials are bounced.
  - `r_ssgiTemporal 1` with `r_depthPrepass 0`: no velocity buffer, moving objects reproject with the camera.
  - `r_ssgiSource 2`: experimental, the baked lighting is bounced twice.
  - trace resolution (`r_ssgiHalfRes` / quality preset) changed: applied at the next `vid_restart`.
  - `r_ssgi 1` before `vid_restart`.

## Source radiance (`dynamicRadiance`)

lightall writes it as MRT from the opaque shading pass. There is no second scene pass.

- Dynamic: `EvaluateDynamicLight` adds `lightColor * diffuseReflectance * attenuation * NL` to a global. It runs
  after the dynamic shadow cube and after `DynamicLightReceiverVisibility`, so a light already shadowed by
  POM self-shadowing (the hook) is bounced shadowed. It is the **diffuse lobe only**: the diffuse outgoing radiance is
  the same in every direction, while the specular lobe towards the camera says nothing about what the hit point sends
  to the receiver. Metals have no diffuse lobe (`diffuse *= 1 - metal`), so they bounce nothing. A light that only
  has a specular term contributes 0, which also covers a future static specular-only LTC light, while dynamic LTC
  diffuse evaluated through `EvaluateDynamicLight` will come in automatically. The vertex-lit path uses
  `diffuse * dynamic light`.
- Legacy (display encoded) scene: `Dec(C) - Dec(C - d)`, where `C` is the stage color before its own emission
  and `d` the dynamic diffuse part: the linear radiance the dynamic light adds to what is on screen.
- Emission: explicit emissive materials in linear HDR × `r_ssgiEmissiveScale`. Legacy glow / auto-emissive
  stage colors are used only if `r_ssgiGlowScale` > 0. They are not physical intensities, so the default is 0.
- The source is written only by opaque lightall stages (`RB_WritesScreenMaterial`). Additive `generic` glow
  stages are not sources.
- Debug views 7 and 8 switch the source to "dynamic only" or "emissive only" for the frame (`u_SSGIParams`).

## Attachments and buffers

| attachment of renderFbo | image | format | contents |
|---|---|---|---|
| 2 (shared with SSR) | `screenNormalImage` | RGB10_A2 | rg = octahedral world normal (after normal mapping), b = roughness, a = SSR receiver |
| 5 | `ssgiAlbedoImage` | RGBA8 | rgb = diffuse albedo after the metalness split, sRGB encoded (8 bit precision), a = GI receiver |
| 6 | `ssgiRadianceImage` | RGBA16F | rgb = linear source radiance, a = view depth (validates the data, as SSR does) |

- With SSGI, every lit opaque lightall fragment writes its normal. SSR still only treats `a = 1` pixels as its
  receivers, so its output is unchanged by SSGI.
- Attachments 3 and 4 exist only with SSR. The draw buffers then have GL_NONE gaps (`FBO_SetupDrawBuffers`),
  and the fragment outputs keep fixed locations.
- 12 extra bytes per pixel and sample without SSR (16F radiance: saber and explosion intensities, plus the
  validation depth in alpha). R11G11B10F radiance with a different depth check is the option if profiling shows
  the bandwidth matters.

| pass buffer | format | size |
|---|---|---|
| `ssgiSource` | RGBA16F, 5 mips | half |
| `ssgiTrace` / `ssgiHit` | RGBA16F (GI, confidence) / RG16F (hit distance, hit fraction) | trace |
| `ssgiHistory[2]` / `ssgiHistoryGeom[2]` | RGBA16F / RGBA16F (depth, oct normal, accumulated length) | trace |
| `ssgiDenoise[2]` | RGBA16F | trace |
| `ssgiScene` | as renderImage | full, used only with a legacy scene buffer or `r_ssgiSource 2` |
| `screenHiZ` (shared) | R32F, 7 mips | full |

"trace" is half resolution (full with `r_ssgiHalfRes 0` or the ultra preset). It is fixed at `vid_restart`.

## Pass order (main world view)

1. `RB_BeginDrawingView` → `RB_ScreenSpaceBeginView`: same view rules as SSR (plain perspective views rendered
   into renderFbo: no portals, mirrors, sky portals, cubemap or shadow views), clears the present attachments.
2. Depth prepass (+ velocity), AO / contact shadows: unchanged.
3. Main pass, **opaque sort**: lightall writes color, glow and the attachments.
4. `RB_RenderScreenSpaceOpaque` (once per view):
   1. shared: MSAA resolve of depth and attachments 2..6, linear depth + closest-depth mips ("Screen geometry")
   2. SSGI: source → trace → temporal → denoise → upsample + composite into color 0
   3. SSR: color pyramid (now containing the GI), trace, resolve, temporal, composite
5. The rest of the main pass: decals, blended surfaces, fog, sun, flares.
6. Post processing unchanged. The `r_ssgiDebug` overlays are drawn at its end.

## Tracing

- Receiver: `albedo.a` and a matching stored view depth (not stale, not the first-person weapon, which is drawn
  with a hacked depth range).
- Directions: `r_ssgiRays` cosine-weighted rays around the normal. Interleaved gradient noise (two decorrelated
  dimensions) is rotated each frame by the R2 sequence, which gives a stable pattern with no visible Bayer
  repetition. The temporal filter integrates it.
- Origin `P + N * max(0.05, 0.002 z)`, clipped in front of the near plane.
- March: the shared `SSRMarchRay`, which is linear (steps denser near the receiver, `pow(t, 1.6)`) or Hi-Z
  (`r_ssgiHiZ`). SSGI has its own distance (`r_ssgiMaxDistance`), thickness (`r_ssgiThickness`), step count and
  distribution. A crossing is refined by a binary search.
- Hit validation / confidence: ray leaves the screen or reaches sky → miss. Ambiguous depth (more than the
  thickness behind) fades out, a back-facing hit surface fades out, self hits (closer than 2× the offset) are
  rejected, and the confidence fades near the screen edges and near the end of the ray.
- Radiance at the hit: the half-resolution source, at the mip of the ray footprint.
- Math: with pdf = cos/π, outgoing diffuse radiance = albedo × mean(L × confidence). There is no extra
  cosine or distance falloff (the solid angle is in the hit probability). The trace output is albedo free, the
  composite multiplies by the receiver's albedo. There is no specular GI (SSR handles reflections).
- Miss / off screen: contributes 0. The GI is only ever added, so a miss can never darken anything.

## Temporal accumulation (`r_ssgiTemporal 1`)

- Reprojection: velocity buffer (NPCs, doors) or the previous camera.
- The four previous trace texels are validated individually by depth (disocclusion), normal (dot ≥ 0.9) and
  receiver state. There is no bleeding across silhouettes, and fewer than 5% valid weight rejects the history.
- History cut (`RB_ScreenHistoryValid`): map load / change, frame gap, `tr.temporalHistoryValid` (motion blur
  reset / cut detection when active), viewport or trace resolution change, camera move > 192 units, rotation > 35°,
  FOV change > 1°. The history is also dropped when temporal is toggled off.
- The history is AABB-clamped to the current 3×3 neighborhood (YCoCg), so light that no current neighbor has (a
  saber that moved away) cannot survive. The weight is `min(r_ssgiHistoryWeight, 1 - 1/(n+1))`, lowered by motion and
  by how much the clamp changed the history. A large change restarts the accumulation.
- `r_ssgiFreezeHistory 1` keeps the last history (debug).

## Denoise and upsample

- `r_ssgiDenoise` à-trous passes (5×5 B3 taps, spacing 1/2/4) at trace resolution. The weights are distance to
  the receiver's tangent plane (tolerance = the pixel footprint), normal `pow(dot, 16)`, and from the second pass
  a luminance stop. The filter does not cross silhouettes, does not blend from the floor onto the wall, and does
  not blend from foreground to background.
- Upsample: the four nearest trace texels, weighted by bilinear × depth similarity × `pow(normal dot, 8)`, the same
  pattern as the SSR resolve.

## Cvars

| cvar | default | |
|---|---|---|
| `r_ssgi` | 0 | archive, latch. Screen-space diffuse GI |
| `r_ssgiSource` | 0 | 0 dynamic lights + emissive (recommended), 1 emissive only, 2 full scene **experimental** |
| `r_ssgiIntensity` | 1 | indirect light scale. 1 = the physically based bounce, not an exposure fix. 0 = passes skipped (attachments still written) |
| `r_ssgiQuality` | 1 | preset, see below |
| `r_ssgiRays` | 0 | rays per traced pixel and frame (1-8), 0 = preset |
| `r_ssgiSteps` | 0 | march steps (Hi-Z: iterations / 3), 0 = preset |
| `r_ssgiMaxDistance` | 256 | ray length, world units |
| `r_ssgiThickness` | 12 | assumed surface thickness, world units (grows with distance) |
| `r_ssgiTemporal` | 1 | temporal accumulation (runtime) |
| `r_ssgiHistoryWeight` | 0.9 | maximum history weight |
| `r_ssgiDenoise` | -1 | à-trous passes 0-4, -1 = preset |
| `r_ssgiHalfRes` | -1 | -1 preset, 0 full, 1 half (applied at `vid_restart`) |
| `r_ssgiHiZ` | -1 | -1 preset, 0 linear, 1 hierarchical |
| `r_ssgiEmissiveScale` | 1 | explicit emissive materials as a source |
| `r_ssgiGlowScale` | 0 | legacy glow / auto-emissive stages as a source (not physical) |
| `r_ssgiCompare` | 0 | split screen: left without, right with SSGI |
| `r_ssgiDebug` | 0 | cheat, see below |
| `r_ssgiFreezeHistory` | 0 | cheat, freeze the temporal history |

Presets change only the cost, never the intensity:

| preset | resolution | rays | steps | refine | Hi-Z | denoise |
|---|---|---|---|---|---|---|
| 0 low | half | 1 | 8 | 3 | no | 1 |
| 1 medium | half | 1 | 12 | 4 | yes | 2 |
| 2 high | half | 2 | 16 | 5 | yes | 2 |
| 3 ultra | full | 2 | 24 | 6 | yes | 3 |

## Debug views (`r_ssgiDebug`)

| | | |
|---|---|---|
| 1 | ray hit mask (green = hits, brighter = more rays; red = all missed; dark gray = not a receiver) | overlay |
| 2 | mean hit distance (blue near → red at `r_ssgiMaxDistance`) | overlay |
| 3 | raw one-frame GI (albedo free) | scene, tone mapped |
| 4 | temporal GI | scene |
| 5 | history weight (from the accumulated length) | overlay |
| 6 | denoised GI | scene |
| 7 | dynamic light source (radiance attachment, dynamic only) | scene |
| 8 | emissive-only source | scene |
| 9 | final indirect contribution (albedo × GI × intensity) | scene |
| 10 | receiver diffuse albedo (after the metalness split) | overlay |

Also available: `r_ssgiCompare 1`, `r_ssgiFreezeHistory 1`, `r_speeds 100` timers.

## GPU cost

GPU timers (`r_speeds 100`): "Screen geometry" (shared MSAA resolve + depth pyramid, counted once even with SSR),
"SSGI source", "SSGI trace", "SSGI temporal", "SSGI denoise", "SSGI composite" (upsample + composite). The MRT
cost of lightall is inside the main pass: compare `r_ssgi 0` with `r_ssgi 1; r_ssgiIntensity 0`.

**Not measured:** the game was not launched for this task (validation = builds + offline GLSL compile). The
expected order on an RTX 2060 class GPU at 1920×1080, medium, estimated from the work per pixel:

| part | estimate |
|---|---|
| lightall MRT (+12 B/px) | 0.1–0.2 ms |
| screen geometry (shared) | 0.1–0.2 ms |
| source + mips | < 0.1 ms |
| trace (half res, 1 ray, Hi-Z) | 0.3–0.6 ms |
| temporal | < 0.1 ms |
| denoise (2 passes) | 0.1–0.2 ms |
| composite | < 0.1 ms |

The trace scales with pixels × rays: roughly ×1.8 at 1440p and ×4 at 4K; ultra (full res, 2 rays) is about 8× the
medium trace. Fill in:

| resolution | quality | geometry | source | trace | temporal | denoise | composite | total |
|---|---|---|---|---|---|---|---|---|
| 1920×1080 | low / medium / high / ultra | | | | | | | |
| 2560×1440 | low / medium / high / ultra | | | | | | | |
| 3840×2160 | low / medium / high / ultra | | | | | | | |

## Validation

Done: MSVC SP + MP builds; the preprocessed lightall without `USE_SSGI` is identical to HEAD (15 permutations of
light type / material / SSAO / shadows / parallax); every SSGI and SSR program (after the shared ray march refactor)
and 100 lightall permutations ({none, SSR, SSGI, both} × per-pixel {lightmap + deluxe, light vector, light vertex}
× {MR, SG, cloth, parallax + skeletal + dlight shadows, vertex animation + SSAO + sun shadows (shadows2)}, plus the
vertex-lit fast-light / unlit paths) compile and link on the Intel UHD and NVIDIA RTX 2060 drivers (232 of 232
compile + link runs OK). gcc 16 also compiles every changed C++ file (SP and MP).

Not done: an in-game run, so there are no screenshots, debug captures or timings yet. In game (`r_ssgi 1`,
`vid_restart`, `r_ssgiCompare 1`, `r_ssgiDebug 1/3/7/9`):

1. white wall + red saber: the wall is lit red directly, the floor next to it gets a weak red bounce
2. saber close to a corner: bounce between both walls, no leak across the corner edge (debug 6)
3. green saber over a floor, moving saber: no trail (debug 4 vs 3, `r_ssgiFreezeHistory` off)
4. blaster bolt travelling down a corridor, explosion: a short bounce flash, no ghosting
5. two differently colored dynamic lights: each surface bounces its own color
6. emissive panel with `emissiveMap`/`emissiveScale`: lights the surroundings; legacy glow panels do not (unless `r_ssgiGlowScale`)
7. **static lightmapped room without dynamic lights: nearly no change** (debug 7 black, debug 9 black)
8. outdoor sunlight: nearly no change (the sun is static light)
9. character next to a bright saber: skin/cloth get the bounce; metal parts, eyes and metallic hair do not
10. moving NPC: history rejected behind it (debug 5), no smearing
11. `r_ssgiSource 2`: shows the double bounce of the baked light (experimental, compare with 0)
12. `r_forwardPlus 0/1` with the same lights: same GI; > 32 lights in Forward+: all of them bounce
13. MSAA on/off, `r_hdr 0/1`, a legacy vs HDR-lightmap map, `r_ssr` on/off, weapon in view (not a receiver)

## Known limitations (screen space)

- Only what is on screen and opaque can bounce or be hit. Off-screen and occluded emitters/lit surfaces contribute
  nothing (fades at the screen edges, so no popping). Glass, particles and additive effects are not in the depth
  buffer.
- The depth buffer has only front faces: thin objects are thickened by `r_ssgiThickness` (false hits behind them
  are reduced by the ambiguity and back-face tests).
- Sources are opaque lightall stages. Additive `generic` glow stages and effect sprites are not sources (sabers
  bounce through their dynamic light, which is what lights the wall).
- The first-person weapon is neither a receiver nor a hit (hacked depth range).
- Only the main view of a scene gets SSGI (no portals, mirrors, sky portals).
- One ray per half-resolution pixel is noisy by nature: the temporal filter and the denoiser trade noise for some
  lag and blur. Rapidly flickering lights restart the accumulation.
- MSAA: the attachments are averaged on silhouettes, and those pixels mostly fail the depth validation (no GI there).

## Future work (not part of this task)

Not implemented, and kept separate from the completed SSGI:

- Miss fallback in an energy-consistent way: light grid, irradiance probes, cubemap diffuse, ambient term.
- Probe-based GI (DDGI-like irradiance volumes) for off-screen and multi-bounce light. No LPV/DDGI exists in this renderer.
- Multi-scale tracing (short high-resolution + long low-resolution rays), only if measurements show a benefit.
- Specular GI beyond SSR, a screen-space emissive layer for additive effects, screen-space skin SSS (to be applied
  after the indirect diffuse without scattering it twice).
