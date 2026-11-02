# Performance, loading and diagnostics

[All rendering features](../rendering-features.md)

Controls marked `vid_restart` need a renderer restart. Debug views use 0 to return to normal unless stated otherwise. Performance labels are rough relative costs, not benchmark guarantees.

## Shader program cache

*Requires driver support for program binaries, because the cached data is the driver’s linked shader program.*

Makes later starts and renderer restarts faster after a first compilation. It does not change the image or per-frame quality.

**Main controls**

- `r_shaderProgramCache 0 | 1` — compile every time / use the on-disk program cache. Requires `vid_restart`.
- `r_shaderProgramCacheMaxMB <MB>` — maximum cache size; least-recently-used entries are evicted first.

**Debug**

- No separate debug controls.

Linked programs are saved on disk and checked against driver and shader identity before reuse. Unsupported drivers compile normally. [Details](../rend2-shader-cache.md).

## Modern OpenGL paths

*Requires GL 4.3-capable hardware for compute paths, because older contexts cannot execute those shaders.*

Can build light lists and fog work more efficiently while preserving the same features. `r_gl43 0` selects the older paths after restart.

**Main controls**

- `r_gl43` — Prefer GL 4.3 and allow modern paths; 0 forces legacy paths after vid_restart. Default `1`; `vid_restart`.
- `r_forwardPlusCompute` — Forward+: 1 = cluster lists built by a GL 4.3 compute shader when available (r_gl43), 0 = built on the CPU. Default `1`.
- `r_volumetricFogLightCompute` — Build froxel dynamic-light cluster lists on GL 4.3 compute. Default `1`.
- `r_volumetricFogLightTile` — Froxel fog: fog cells per side of a dynamic light tile (4, 8, 16), see r_vfogLightStats. Default `8`.

**Debug**

- `r_forwardPlusVerify` — Compare GPU and CPU cluster lists.
- `r_vfogLightStats` — Print froxel dynamic-light-list sizes and costs.
- `r_vfogLightVerify` — Compare GPU and CPU froxel light-list construction.

The renderer requests a modern context and uses compute shaders where supported, falling back when unavailable. Verification commands compare CPU and GPU light lists rather than enabling extra visuals. [Details](../rend2-opengl43.md).

## Loading and texture preparation

Reduces loading work through on-demand shader compilation, worker threads, texture prefetch and prepared-material caches. These options mainly affect when a map becomes ready, not how a finished frame looks.

**Main controls**

- `com_touchMemoryOnLoad` — Touch process memory at map load; 0 skips that SP load stage. Default `1`.
- `fs_looseImageIndex` — Windows loose JPG/PNG/TGA negative-lookup index; rebuilt at map/search-path changes. Default `1`.
- `r_genNormalMapsLazy` — generate normal maps (r_genNormalMaps) only for lit stages that need them. Default `0`.
- `r_loadPrefetch` — Decode predicted textures on worker threads during map load. Default `1`.
- `r_loadPrefetchMB` — Memory for textures decoded ahead of use, in MB. Default `512`.
- `r_loadProfile` — Print nested map-load CPU timings from the engine, renderer and matching SP game module. Default `0`.
- `r_loadThreads` — Worker threads for load-time texture work: -1 = automatic, 0 = none. Default `-1`.
- `r_materialDDCObjects` — Cache prepared NPC, object and other non-BSP textures on disk. Default `1`.
- `r_materialDDCWorld` — Cache prepared BSP surface textures on disk. Default `0`.
- `r_shaderCompileProcesses` — Parallel shader helper processes: -1 automatic, 0 compile inside the game. Default `-1`.
- `r_shaderProgramLazy` — Compile shader variants when a map or draw first needs them, instead of all at startup. Default `1`; `vid_restart`.
- `r_uploadPBO` — Use a staging buffer for texture upload. Default `1`.
- `ui_lazyMenuAssets` — defer menu asset loading until required. Default `1`.

**Debug**

- `r_imageChecksums` — Write a hash of every loaded texture to imagechecksums/<map>.txt after registration. Default `0`.
- `glslusage` — Print shader variant usage.
- `imagechecksums` — Write loaded-image checksums for comparison.

Reusable work is cached or prepared ahead of use. Separate budgets bound worker/predecoded data; world and object material caches remain independent. Nested load timings overlap and must not be added together. [Details](../rend2-map-load.md).

## Capability and capture diagnostics

Reports which optional renderer interfaces are available and supports hidden-window capture. These are integration tools rather than visual quality presets.

**Main controls**

- `r_offscreenPresent` — Enable captures from a hidden game window; intended for scripted tests. Default `0`; `vid_restart`.

**Debug**

- `cl_rendererLensWater` — Engine-owned flag: the active renderer exposes lens-water events. Default `0`.
- `cl_rendererSpotLights` — Engine-owned flag: the active renderer exposes the optional spot-light API. Default `0`.
- `cl_rendererWaterInteraction` — Engine-owned flag: the active renderer exposes water interaction events. Default `0`.
- `r_ext_multisample_default_fb` — Internal default-framebuffer multisample state; not a user quality setting. Default `1`.
- `cl_rendererFoliageWaves` — Read-only: Force-wave submission interface is available.
- `cl_rendererFoliageCut` — Read-only: saber-cut submission interface is available.

The engine writes read-only capability flags for game code. The offscreen-present option renders to an intermediate image before copying it to the window.
