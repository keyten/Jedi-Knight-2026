# Rend2 sun shadows 2.0

`r_sunShadowMode` selects the complete sun-shadow implementation at renderer
startup. It is latched because it changes both the GLSL sampler type and the GL
texture comparison state.

- `0`: legacy three-cascade fitting, snapping, hardware comparison texture and
  the existing `r_shadowFilter` PCF path.
- `1`: stable cascade spheres, independent texel snapping, explicit split
  blending, manual raw-depth filtering and optional directional-light PCSS.

Dynamic-light cube shadow maps are unchanged. Ghoul2 caster/receiver behaviour, the caster LOD option and the
dynamic light cube bias option are described in [rend2-character-shadows.md](rend2-character-shadows.md).

## Modern cascade fit

The split distances retain the existing `CalcSplit()` and
`r_shadowCascadeZBias` policy. Each transition expands both adjacent render
ranges by half of `r_shadowCascadeBlend` times the smaller adjacent split span.

For each expanded slice, the renderer computes the analytic bounding sphere of
the symmetric perspective frustum slice. The radius is independent of camera
rotation and translation and is quantized to 1/16 world unit. The light-view
origin is projected onto the two shadow-plane axes and snapped independently to
that cascade's world-units-per-texel. Modern light-space depth is rebased to a
stable interval enclosing the BSP plus a dynamic-caster margin, including when
`r_sunShadowCache 0` uses a full redraw. This keeps the comparison projection
the same for every cache mode;
this makes stored depth values reusable after XY scrolling and keeps PCSS
blocker separation in one consistent light-space interval.

The old implementation used function-static texel-size values inside the
cascade loop. They were initialized by the first cascade and then incorrectly
reused by the other two cascades. Legacy mode preserves that behavior for an
exact A/B path; modern mode calculates both axes for every cascade and snaps the
view before its matrix and frustum are built.

## Filtering and bias

Modern mode creates the sun array as `GL_DEPTH_COMPONENT24` without
`GL_TEXTURE_COMPARE_MODE`. Shaders bind it as `sampler2DArray`, fetch raw depth
with `texelFetch`, and perform comparisons manually. Legacy mode retains the
16-bit `sampler2DArrayShadow` texture. No sampler-object dependency is added.

The modern comparison depth combines:

1. a constant receiver bias expressed in world units;
2. a geometric-normal offset expressed in cascade texels;
3. a receiver-plane correction for every kernel offset.

The receiver-plane gradient is solved from screen-space derivatives of shadow
UV and depth. The receiver position is differentiated once, before the cascade
branch; because the sun projection is orthographic, each evaluated cascade gets
its shadow-space derivatives from the 3x3 part of its matrix instead of
projecting the pixel into all three cascades. Corrections are clamped in world
units to prevent derivatives at geometry discontinuities from producing extreme
bias.

PCSS uses a stable world-space Vogel pattern with per-tap `sqrt`/`cos`/`sin`.
That is deliberate: on the Intel UHD (mp/ffa3, 960x540, r_sunShadowCache 0) a
dynamically indexed const table of the disc points made the main view about 5x
slower, and advancing each tap by a golden-angle complex multiply (one
`sin`/`cos` per pixel) made it 7.0 -> 8.2-8.7 ms. A
kernel never leaves its cascade: near the map edge the disc radius shrinks
instead of clamping taps onto (and repeating) the border texels.

1. Search raw depth and collect depths less than the bias-corrected receiver
   depth. The search radius is `maxSeparation * tan(sunAngularDiameter / 2)`,
   at most `r_shadowPCSSMaxPenumbra` and at least two texels, where
   `maxSeparation` is the receiver's distance below the map's light-side bound
   (`u_ShadowDepthSpan.w`): no blocker further away can widen its penumbra.
2. Average the blocker depths.
3. Convert receiver/blocker depth separation back to world units and compute
   `penumbra = separation * tan(sunAngularDiameter / 2)`.
4. Clamp the penumbra in world units, convert it to the current cascade's UV
   scale, and run variable-radius manual PCF. A penumbra of at most one texel
   (contact-hard, the common case at a 0.53 degree sun) uses a 2x2 bilinear
   PCF instead: 4 fetches rather than the preset's 8-32, and a smoother edge
   than random taps on nearest texels.

Because both separation and penumbra are in world units, cascade resolution
does not change the apparent light size. Only pixels in a configured split
overlap evaluate two cascades. Preset sample counts are:

| Quality | Blocker search | PCF |
| --- | ---: | ---: |
| low | 8 | 8 |
| high (default) | 12 | 16 |
| ultra | 24 | 32 |

### Character layers

One averaged blocker depth gives one penumbra per pixel. A character standing
in the soft shadow of a tree top therefore used to get the tree top's blur on
its own shadow edge. With `r_shadowPCSSCharacterLayers 1` the renderer passes
the conservative model spheres of the eight Ghoul2 entities nearest the camera
(`u_ShadowCharacters`, the spheres the sun cache restore also uses). A pixel
whose search disc a sphere can reach, with the sphere between the receiver and
the sun, gets that sphere's light-space depth range. Blockers inside the range
are averaged and filtered with their own penumbra, the rest with theirs, and
the two visibilities multiply. Pixels with only one kind of blocker, or with no
character nearby, run the ordinary single PCSS. Only characters get this split:
general depth layering would run two PCF passes over most foliage. Other
blockers count as the character only inside both its depth range and its
sphere's disc in shadow uv, so canopy or walls at the character's height keep
their soft penumbra. A layer of at most one texel uses the bilinear 2x2 filter.

It is a latched shader permutation (`USE_PCSS_CHARACTER_LAYERS`): 0 compiles
the previous lightall exactly. `r_shadowDebug 13` shows the split pixels red.

On by default (set 0 to get the previous lightall back). The first version (character classification inside the
blocker search loop) cost about 18-20% of the Intel UHD main view even with no
character in view (6.9 -> 8.5-9.1 ms), from register pressure of the larger
lightall. The search loop is now unchanged and a second pass over the same
(cached) taps runs only next to a character; that measured +0.4-1.2 ms, but in
runs shared with another game process, so it is not a reliable number.

Checked on yavin1 (forced sun, player in third person in the soft shadow of the
canyon wall / canopy, view 6): with 0 the character's ground shadow is blurred
away and its self shadow is grey; with 1 both are crisp, and the canopy pattern
around it is unchanged (no halo).

## Screen-space contacts

The contact pass reconstructs the view-space receiver position and a
full-resolution geometric normal from the main depth buffer. It marches a
bounded ray towards the sun, rejects self hits with a normal/light origin
offset, applies a configurable depth thickness, fades the hit near maximum ray
length and fades towards viewport edges. A fixed midpoint step pattern avoids
screen-locked random jitter during motion. Its visibility multiplies the CSM or
PCSS visibility; it never replaces it.

## Controls

| Cvar | Default | Meaning |
| --- | ---: | --- |
| `r_sunShadowMode` | 1 | 0 legacy, 1 shadows 2.0; latched |
| `r_sunShadowCache` | 0 | 0 off, 1 exact reuse, 2 scrolling clipmaps (experimental), 3 persistent world-space disk pages (experimental); latched |
| `r_sunShadowAlphaCasters` | 1 | sun-shadow cutouts/receivers for `q3map_alphashadow` foliage and `surfaceSprites`; latched |
| `r_shadowCascadeBlend` | 0.10 | overlap as fraction of smaller adjacent span |
| `r_shadowDepthBias` | 0.15 | constant world-space receiver bias |
| `r_shadowNormalBias` | 0.75 | normal offset in cascade texels |
| `r_shadowSlopeBias` | 1.0 | receiver-plane correction multiplier |
| `r_shadowReceiverBiasClamp` | 4.0 | derivative correction clamp in world units |
| `r_shadowPCSS` | 1 | enable sun PCSS |
| `r_shadowPCSSQuality` | 1 | 0 low, 1 high, 2 ultra |
| `r_shadowSunAngularDiameter` | 0.53 | apparent source diameter in degrees |
| `r_shadowPCSSMaxPenumbra` | 32 | maximum world-space penumbra/search radius |
| `r_shadowPCSSCharacterLayers` | 1 | Ghoul2 character blockers get their own penumbra where their shadow meets softer shadows; latched |
| `r_shadowSinglePass` | 1 | gather all six point-light cube faces in one scene walk; 0 keeps six independent walks |
| `r_shadowLayered` | 1 | submit a supported point-light cube through its layered FBO; unsupported hardware/surfaces fall back automatically |
| `r_shadowCache` | 1 | reuse unchanged point/spot shadow slots; conservative time-dependent casters force a redraw |
| `r_shadowDebug` | 0 | debug view, listed below |

The contact controls are `r_contactShadows`, `r_contactShadowLength`,
`r_contactShadowSteps`, `r_contactShadowThickness`, and
`r_contactShadowStrength`.

`r_shadowDebug` values are: 1 cascade colors, 2 raw depth, 3 fixed-radius PCF,
4 average blocker depth, 5 penumbra radius, 6 PCSS visibility, 7 contact-only,
8 final CSM/PCSS times contact visibility, 9 effective bias, 10 dynamic light cube
shadows only, 11 Ghoul2 receivers (see rend2-character-shadows.md), 12 model
sun split, and 13 PCSS character layers (red where a character's blockers got
their own penumbra).

### Alpha-tested foliage casters

With `r_sunShadowMode 1` and `r_sunShadowAlphaCasters 1`, the runtime keeps the
legacy `q3map_alphashadow` keyword as an explicit caster hint. Such materials
may enter the sun cascade pass even when their visible sort is blended. The
shadow draw uses the authored `alphaFunc`; when the material only declares
alpha blending, it uses a 0.5 cutout. Non-converted blended materials disable
blending and force depth writes only for depth-only draws. Glass, smoke and
other unmarked blended materials remain excluded.

Simple one-stage unlit foliage such as `models/map_objects/yavin/fern3b` is
converted from alpha blend to an alpha-tested, depth-writing cutout. Its color,
camera depth and sun CSM use the same 0.5 silhouette, so later water cannot
paint over visible leaf pixels. The base stage uses the lightall vertex-light
path to receive CSM without sampling the world entity's light grid at the
origin. This intentionally loses the legacy soft alpha fringe; setting
`r_sunShadowAlphaCasters 0` restores the original blend after renderer restart.
Other marked blended materials still receive a 0.5 fallback in depth passes,
so their partially transparent fringes may not occlude later surfaces.

Generated `surfaceSprites` use their normal alpha test and retain the visible
camera's billboard axes, distance fade and wind phase in every cascade. This
prevents the light view from rotating or distance-culling grass independently
in each shadow map. Setting `r_sunShadowAlphaCasters 0` and restarting the
renderer disables these paths; legacy sun shadows and dynamic-light shadow maps
are unchanged.

## Static sun-shadow cache

Modern mode can keep a persistent D24 array for conservative static BSP
casters. On a rebuild, that depth layer initializes the existing working
cascade. On exact hits, only the previous dynamic-caster footprint is restored
from static depth; with no previous casters there is no copy. Moving/animated
casters are then rendered over it with the
normal depth test. Lighting, volumetrics, cascade blending and PCSS continue to
sample only `sunShadowArrayImage`; no PCSS tap gains a second texture lookup.
Caching is opt-in: copying depth and redrawing strips can cost more GPU time
than a full static draw on bandwidth-limited GPUs, even when CPU traversal drops.

Only opaque, simple-depth world faces/grids/triangles/VBO meshes enter the
persistent layer. Alpha-tested materials, shader deforms, resolved foliage motion,
surface sprites, POM shells, water/distortion, Force Sight, polygon-offset surfaces and all
entities stay in the dynamic overlay. Shader remaps invalidate both the static
depth and the compact list of dynamic world surfaces. The dynamic overlay culls
that compact list directly instead of walking the whole BSP every cascade.

Raw classifier evidence such as a two-sided material is not itself motion.
The working-layer restore uses conservative Ghoul2 culling spheres, MD3 frame
bounds (both animation frames), and BSP bounds, with a four-texel raster guard.
Footprints are kept as up to eight separate rectangles (touching ones merge;
past eight, the pair with the smallest union grows), so characters far apart
restore only their own areas; past half the layer one full copy is used. World
overlay surfaces without animation (no deforms, tcMod, animMap, video or
time-varying alphaGen, no Force Sight, known through the sort key) write the
same depth on every exact hit and are left out of the footprint.
Unknown/deformed casters fall back to a complete restore. Same-format depth
copies use optional `glCopyImageSubData`, with FBO blits on older GL drivers.
PCSS shader taps are unchanged.

Mode 1 uses the original one-pass redraw while the snapped projection changes.
It builds a static layer after four consecutive matching projections, then
reuses it. This avoids building/copying a throwaway cache on every moving frame.
If a caster has no safe bounds (including moving foliage/deforms), mode 1
returns that cascade to single-pass redraw on subsequent frames instead of
continually copying a full layer; it retries after projection/caster invalidation.
Mode 2 instead treats every cascade as a ping-pong shadow
clipmap: an integer-texel camera movement blits the overlapping depth into the
other bank, then renders only the newly exposed horizontal/vertical strips.
Each strip has a matching narrow culling frustum and a scissor rectangle,
while retaining the full cascade's raster projection and viewport. This avoids
the additional clipping/interpolation seams of a one-pixel projection and
reduces BSP traversal and fragment work together. Sun-direction, projection-extent, shader-generation,
map or non-integral-grid changes fall back to a full conservative rebuild.

At 1024, mode 1 adds about 12 MiB of D24 depth storage; mode 2 adds about
24 MiB. `r_sunShadowCache 0` selects a one-pass-per-cascade full redraw with the
same stable modern projection for visual and performance A/B tests.

Mode 3 keeps the mode-2 GPU clipmaps and adds a lazy 256x256 world-space page
cache under `shadowcache/<map>/`. Complete missing pages are built in a 288x288
scratch FBO with a 16-texel guard band; only the central 256x256 is saved. This
keeps viewport clipping away from the persisted edges and completes the pages
partially covered by a cascade. Warmup is bounded to one page per cascade per
scene. A repeated camera window can then hit disk without first exploring
beyond all four borders. A later launch (or a
large teleport) assembles a cascade from disk when every page covering its
window is present; otherwise it falls back atomically to the normal full/strip
render and fills the missing pages. CPU page data is retained for one scene
only, so walking a large map does not create an unbounded RAM cache.

The page layout key contains a version, map name and allocation size, the
positions and indices of all conservative static casters, their resolved
shader names/cull modes, the sun basis, stable depth range, cascade level,
texel scale and resolution. Map geometry, relevant shader/remap, sun, FOV,
split or resolution changes therefore select a different cache. Files have a
validated header, a payload checksum and finite normalized depth; an incomplete
or corrupt page is treated as a miss. A page counts as saved only after its
file reads back at full size, so a failed write is retried; the saved-page
list is reset with the cache (map load). World/curve/culling settings also select a different
cache. Mode 3 is experimental and opt-in because page creation includes synchronous page-depth
readback, bounded extra page renders, and uncompressed float pages on disk.

## Performance and validation

The local runtime results and remaining experimental scrolling/page mismatches
are recorded in
[sun-shadow cache validation](rend2-sun-shadow-cache-validation.md).

GPU timers label each sun cascade render separately, the main-view sample cost
is visible in the main-view timer, and the contact pass is labelled
`AO composite/contact`. Compare those timers with `r_sunShadowMode 0/1`, PCSS
off/on, each quality preset, and contact shadows off/on. A split-overlap pixel
can execute twice the normal sun sample count; pixels outside overlaps never
sample a second cascade.

`r_shadowStats 1` prints the per-frame CPU cost (front end and command
submission) of each view class once per second, including separate `sun static`
and `sun dynamic` rows. It also prints cache hits, clipmap scrolls, disk-window
hits, full rebuilds, disk pages read/written and the number of static
megapixels updated/restored per frame and baseline bypasses, plus point/spot
cache hits, misses and slots dirtied by time-dependent casters. Point light shadow cubes are built
from one scene walk per light by default (`r_shadowSinglePass 1`), with layered
submission where supported (`r_shadowLayered 1`). The fallbacks remain available,
and `r_shadowVerify 1` checks the optimized result on one frame. Details and measurements are in
[rend2-gl43-plan-4-layered-shadows.md](rend2-gl43-plan-4-layered-shadows.md).

Recommended visual checks are slow forward/strafe motion, both transition
bands, close contacts, long shadows, sloped floors, rails and foliage,
wide/narrow FOV, several shadow-map sizes, and low sun angles. Use debug views
1, 3, 5, 7, and 8 to isolate the common failure modes.

`r_sunShadowVerify 1` compares the next scene's three cached working cascades
with full world/entity renders at the identical stable projection;
`r_sunShadowVerify 2` repeats continuously. Both perform slow synchronous
readbacks and must be off for performance measurements. After rendering the
reference, the actual depth is restored to the working texture so subsequent
verified frames also exercise partial restoration. Persistent banks are untouched. The report
keeps the exact differing-texel count, the count above two D24 steps and maximum
normalized depth error. PASS checks a symmetric one-texel depth footprint with
depth tolerance max(8 D24 steps, 5% of that cascade's world texel size). This is
a geometric tolerance check, not a claim of bit-exact scroll or page rendering:
GPU clipping/interpolation can change depth at triangle edges after translation.
Disk uploads explicitly select the texture unit and preserve unpack-PBO state;
all synchronous depth reads preserve pack-PBO state, including when an
asynchronous screenshot is pending.

The Windows integration harness `tools/rend2/test_sunshadow_cache_runtime.py`
uses locally built engine/game/renderer binaries, reads installed assets, and
writes configs, screenshots, logs, pages and JSON timing summaries to its own
`build/sun-shadow-runtime` directory. Its default sequence is 0, 1, 2, 3, 3;
the last launch reuses the previous mode-3 home to exercise persistent pages.
It checks stationary views, small and diagonal steps, movement, large
teleports/returns and world/curve/culling toggles. GPU timings are sampled with
verification disabled. Example:

```powershell
python tools/rend2/test_sunshadow_cache_runtime.py --installation C:/path/to/game --binaries build/sun-shadow-cache/Release
```

`r_sunShadowBenchmark 600` measures 600 complete wall-clock frame intervals
(CPU, GPU waits, swap and game), prints mean/median/p95 and average FPS, then
resets to zero. Disable verification, stats, per-pass logging, vsync and all
focused/unfocused/minimized FPS caps. The harness supports
`--no-verify --benchmark-frames 600 --modes 0 1 0 1` (uncapped by default).
Its private startup config avoids the SP engine's 32-command startup limit.
