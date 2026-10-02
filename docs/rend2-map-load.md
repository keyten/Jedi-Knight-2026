# Rend2 map loading: verified findings and implementation plan

Scope: the single-player `code/` engine and `shared/rd-rend2` on this branch. Static-light reconstruction is deliberately outside this plan. All performance estimates in the review are hypotheses until measured on target hardware and content.

## Baseline and profiling

`r_loadProfile 1` prints `[map load]` CPU wall-clock intervals during a local map transition. The new intervals cover client flush, server setup and collision, game spawn, renderer shutdown/init, cgame sounds and graphics, BSP read and processing, cubemap rendering, and `Com_TouchMemory`. Nested totals must **not** be summed. The BSP line distinguishes collision-cache handoff from filesystem reads. Existing world VBO and leaf-merge messages remain available. The numbers include CPU waiting for GL calls; they do not independently measure GPU execution. For GPU-limited stages, add timer queries or a deliberate sync in an experimental build rather than putting `glFinish` in normal loading.

Baseline procedure: run the same build/configuration through cold process start and `map A → B → A` at least five times each. Record median and spread of local-map total and the stage lines. Use representative maps with/without external HDR lightmaps, many patches, PBR materials, and cubemaps. Record `r_cubeMapping`, `r_diffuseIBL`, `r_cubeMappingBounces`, `r_patchStitching`, `r_mergeLeafSurfaces`, `r_pomSilhouette`, resolution, GPU, driver, storage and whether assets are in PK3. Compare screenshots and first-minute frame times as well as loading time. The current output measures readiness through `CL_InitCGame`; loading-screen and first-frame work after that point needs a separate trace if it proves significant.

## Review verification and immediate changes

| Claim | Result in this tree | Action |
| --- | --- | --- |
| Ordinary local `map` shuts Rend2 down while keeping the GL context | Confirmed: `CL_FlushMemory → re.Shutdown(qfalse,qfalse)`; Rend2 deletes textures, FBOs and buffers. GLSL programs are retained on this path. `Hunk_Clear` follows. | Profiled. Full lifetime split below. |
| Collision loader retains main BSP but Rend2 reads it again | Confirmed: `CM_LoadMap_Actual` retains `gpvCachedMapDiskImage`; vanilla checks it, Rend2 did not. | Rend2 now borrows it only for the matching main BSP, protects it from memory-recovery deletion, and keeps filesystem fallback for sub-BSPs and unavailable cache. |
| BRDF integration LUT is rebuilt on every renderer init | Confirmed when `r_cubeMapping` is enabled: 128² × 1024 samples. | Keep the generated CPU half-float table for the renderer module lifetime; GL image is recreated after shutdown. This saves computation, not upload. |
| `.tspace` eliminates VBO tangent calculation | False for current VBO path: the file populates `srfVert_t::tangent`, while VBO packing zeros tangents and runs MikkTSpace over the packed batch. | Do not bypass Mikk without equivalence testing. The `.tspace` file buffer is now freed after surface parsing. |
| Cubemap load is always 120 renders for 10 probes and one bounce | Conditional: `R_RenderAllCubemaps` does `6 × min(probes, MAX_RUNTIME_CUBEMAPS) × (1 + bounces when cube mapping is on)` face renders, then convolution per probe/bounce. With 10 effective probes and one bounce this is 120. | Measure with the actual config/probe count; DDC plan below. |
| Shader-script scanning is repeated | Confirmed: `R_InitShaders` calls `ScanAndLoadShaderFiles`; the script text and index are hunk allocations. Existing GLSL-program persistence does not cover them. | Move with renderer lifetime; separately measure before a standalone cache. |
| Sound/model cache is already persistent | Partly confirmed: `CModelCacheManager` and `SND_RegisterAudio_LevelLoadEnd(false)` retain data with level/pool policies. This does **not** prove all model registration work is free. | Measure cgame sounds and graphics before changing these paths. |
| `Com_TouchMemory` may be unnecessary | Confirmed it iterates zone blocks with a read every 256 bytes. Benefit on modern hardware is unproven. | `com_touchMemoryOnLoad 0/1` now allows A/B, default 1 preserves behavior. |

## 1. Persistent renderer lifetime

**Home:** in-memory renderer lifetime, not disk DDC. **Payoff to measure:** shutdown/init, texture reloads, shader scripts, font/common GPU resource recreation across map changes.

Introduce explicit `R_BeginLevel` / `R_EndLevel` behind the renderer API. Make `CL_FlushMemory` call level teardown for map changes and reserve `RE_Shutdown` for renderer/video/process teardown. This cannot be done by merely skipping `R_DeleteTextures`: current image, shader, model and world pointers are mixed with `Hunk_Alloc` storage that `Hunk_Clear` destroys. First inventory every pointer rooted in `tr`, `backEnd`, image/shader pools and registration handles. Move persistent allocations to an explicit renderer-owned arena/zone (or stable individually owned allocations), and keep level allocations on a distinct level arena. The existing backend frame data and per-frame allocator are hunk allocated and must move or be recreated. Maintain stable registration handles or remap them before cgame/UI use; preserve current `vid_restart`, context-loss, GL-option and error-recovery semantics.

Keep GL context, shader programs, shader-script index, fonts, built-in textures and images, shared material textures, and reusable global buffers/FBO infrastructure across maps. FBO attachments tied to resolution or feature settings still rebuild on relevant changes. Destroy world BSP, world VBO/IBO, lightmaps, cubemap probes, weather zones, level area lights, POM shells, static world state and other world-dependent resources at `R_EndLevel`. Add explicit ownership for each texture (global/shared, level, transient); `*` names are not a safe lifetime key because different maps use the same names. Budget shared texture memory and evict only unreferenced entries after a level transition. Make `RE_Shutdown` fully release GL resources on context destruction and clear all handles. Verify A→B→A, same-map restart, `vid_restart`, error drop during load, and low-memory recovery with handle/pointer assertions and GL leak checks.

## 2. Shared texture and generated-material cache

**Home:** GPU image reuse belongs to persistent renderer lifetime; expensive deterministic preprocessing belongs to rend2 DDC. **Dependencies:** complete image ownership/handle work from plan 1 before retaining GPU objects across `Hunk_Clear`.

Current `R_GetLoadedImage` avoids duplicates only within one renderer session. `R_FindImageFile` decodes source images and computes extra metadata (`emissiveColor`, `heightRange`), and generated normal/height, specular/ORMS and auto-roughness maps add CPU work. Build a cache key from the *resolved VFS asset bytes* (not only filename or timestamp), image type, flags, picmip, color/linear processing, generated-map algorithm version and any input assets. The same name may resolve differently after a PK3/mod change; purge/rekey on FS search-path change. Keep lightmaps, video/render targets, and map-scoped generated images out of the shared GPU LRU unless the key explicitly includes their map.

Start DDC with a versioned preprocessed CPU image payload plus width/height, format/mips and metadata, validated before allocation. Use atomic writes, size limits and a miss fallback. A GPU-ready compressed path is a later phase: KTX2 or DDS requires loader integration, format/capability negotiation and sRGB/normal-map correctness; driver compression output is not automatically portable across machines. Measure per-image hits, decode/preprocess/upload time and cache bytes. Test source replacement, changed settings, corrupted/truncated cache, and visual/mipmap equivalence.

## 3. BSP-derived world geometry

**Home:** rend2 DDC for deterministic CPU conversion; current-map GPU VBO/IBO objects remain level lifetime. **Payoff to measure:** surfaces/patches, light-direction calculation, packed VBOs and MikkTSpace, leaf merging.

Cache versioned, pointer-free arrays and descriptors, not `world_t`: packed vertices and indices, surface-to-batch offsets, merged-leaf index data, and optionally POM silhouette derived geometry. Reconstruct runtime `VBO_t`/`IBO_t` and surface pointers during load. Begin with packed world VBO data after the normal BSP parse; if this wins and the earlier patch stage remains significant, add a second layer for parsed/stiched patch geometry. The first layer avoids Mikk and packing while preserving runtime BSP structures needed by visibility, collision-adjacent features and fallback surfaces. Do not assume all geometry is purely BSP keyed: sort/grouping uses shader properties (portal, sky, CPU deforms and sorted index), cubemap assignment comes from `env.json`/entity probes, and POM shells use material state.

Key by BSP content hash, `.tspace`/vertex-light sidecar hashes when used, shader/material script content or an explicit material-derived group signature, `env.json` and other probe definitions when batching depends on them, `r_subdivisions`, `r_patchStitching`, `r_mergeLeafSurfaces`, relevant POM settings, packed-vertex layout/endianness and a cache schema/algorithm version. Split keys by layer so changing a POM option need not invalidate base geometry. Store counts/offsets with checked arithmetic and validate against source surface counts to prevent bad-cache out-of-bounds reads. Write atomically; silently rebuild on a cache miss/corruption. Verify `r_mergeLeafSurfaces` on/off, patch seams/LOD, shader overrides, env probe edits, mirrors/portals, sub-BSPs and pixel-equivalent tangent-space normal mapping.

`.tspace` fast path is an optional experiment only. Mikk currently works over a *whole packed batch* after surface sorting; a `.tspace` value per original drawvert need not equal Mikk on a subdivided/stiched patch or across UV seams. Gate an experiment to planar/triangle surfaces, compare packed tangent/sign and rendered normals against the current path, then benchmark. If equivalence or gain is weak, omit it and rely on geometry DDC.

## 4. Runtime cubemap probes

**Home:** probe images are level lifetime in VRAM; rendered/convolved results belong to rend2 DDC. An in-process A→B→A GPU cache is optional after renderer lifetime is established, with an explicit memory budget.

Capture exactly the image products consumed after `R_RenderAllCubemaps`: raw faces/mips if later bounces need them, prefiltered specular chain, diffuse irradiance or directional-IBL data, and probe average. Cache all effective probes/bounces as a coherent set; no partial hit that mixes old and new lighting. Key by BSP and effective probe positions/order, `env.json`, shader/material/texture content affecting the scene, lighting/environment sidecars, rendering algorithm version, cube resolution/format, `r_cubeMapping`, `r_diffuseIBL`, bounce count and other appearance-affecting renderer settings. Dynamic entities/weather or temporal state must be excluded from capture or make the cache ineligible. Use a canonical portable half-float payload initially; GPU compression is optional. Validate probe count, dimensions and mip chain; fall back to rendering on any mismatch. Benchmark with cubemaps disabled and enabled, and compare saved probes/screenshots after asset changes.

## 5. Shader-script database

**Home:** persistent renderer lifetime. Disk DDC is unnecessary until profiling shows parse cost still matters after the lifetime split.

`ScanAndLoadShaderFiles` lists `.shader`, prefers corresponding `.mtr`, validates and merges text, compresses it and builds `shaderTextHashTable`. Move its text and index off the hunk; separate this immutable database from parsed `shader_t` instances and map-specific image references. Refresh on VFS mount/order or script-content change, `vid_restart` only if settings alter parsing, and renderer version changes. If a standalone disk cache is justified later, serialize text/index offsets rather than pointers and key by ordered resolved file content, including `.mtr` precedence. Test mod/PK3 overrides, missing files, live asset reload and malformed scripts.

## 6. Lightmaps and other map images

**Home:** level lifetime GPU images; optional rend2 DDC for expensive external HDR decode/conversion. Internal BSP lightmaps can be uploaded from BSP data and should be optimized only if the profile shows material time.

External HDR lightmaps, deluxe maps, vertex-light sidecars and light-grid textures can change independently of BSP. A DDC key must include their resolved content, atlas layout, GL format/linear-light settings and conversion algorithm version. Store validated half-float or packed CPU upload data, then recreate map-scoped GL objects. Keep filenames unique per map or key by map hash to avoid collisions in the image pool. Benchmark HDR-heavy maps before implementing. Do not retain stale world lightmaps merely because the texture name repeats.

## Priority gates

1. Use `r_loadProfile 1` on representative maps and retain A/B numbers for the immediate fixes. `com_touchMemoryOnLoad 0` is accepted only if total load improves and first-minute frame times do not regress.
2. Implement renderer lifetime/ownership with a narrow initial slice (global shader scripts and shared textures), preserving the existing full `RE_Shutdown` path for video restart. Expand only after map transitions and context teardown are correct.
3. Choose geometry DDC versus material DDC from measured CPU costs. Add one cache layer at a time with hit/miss logging and corruption fallback.
4. Build cubemap DDC when cubemap stage is materially costly in the configurations people actually use. Keep it independent of geometry DDC because its invalidation inputs differ.

No change here serializes static-light reconstruction or changes model/sound caching policy.
