# Loading, OpenGL and diagnostics

[All rendering settings](../rendering-features.md)

Settings that change how fast the game starts and loads maps, which OpenGL paths are used, and tools for testing. Modern OpenGL and the reviewed rendering fast paths are enabled by default; shader caching and the map-loading options below remain opt-in. Existing explicit settings in your config are preserved.

To also enable shader caching and faster loading:

~~~text
seta r_shaderProgramCache 1
seta r_shaderProgramLazy 1
seta r_shaderCompileProcesses -1
seta r_loadPrefetch 1
seta r_loadThreads -1
seta r_materialDDCObjects 1
seta r_uploadPBO 1
seta r_gl43 1
seta fs_looseImageIndex 1
seta ui_lazyMenuAssets 1
~~~

## Shader compilation

Rend2 builds hundreds of shader programs for the video driver. Compiling them all at startup is slow, so this build compiles them on first use, in parallel helper processes, and saves the results on disk for the next start.

- `r_shaderProgramCache 0` *(restart)* — 1: save compiled programs in `glslcache/`; later starts and `vid_restart` skip compiling. Needs driver support; otherwise programs are just compiled.
- `r_shaderProgramCacheMaxMB 512` — cache size limit; least-recently-used programs go first.
- `r_shaderProgramLazy 0` *(restart)* — 1: compile a program when a map or effect first needs it instead of all at startup. The first appearance of a new effect can hitch briefly until it is cached.
- `r_shaderCompileProcesses 0` — helper processes for compiling: -1 automatic (half the CPU threads, at most 8), 0 compile inside the game.

**Debug**

- `glslusage` — print which shader variants are used.

Design notes: [rend2-shader-cache.md](../rend2-shader-cache.md).

## Map loading

Textures are decoded ahead of time on worker threads, prepared material textures are cached on disk, and several slow load steps are skipped or deferred.

- `r_loadPrefetch 0` — 1: decode the textures a map will need on worker threads while it loads.
- `r_loadPrefetchMB 512` — memory for those textures.
- `r_loadThreads 0` — worker threads: -1 automatic, 0 none.
- `r_materialDDCObjects 0` — 1: cache prepared textures of characters, objects and other models on disk.
- `r_materialDDCWorld 0` — 1: also cache the map's own surface textures.
- `r_uploadPBO 0` — 1: upload textures through a staging buffer.
- `r_genNormalMapsLazy 0` — experimental: with `r_genNormalMaps 1`, generate normal maps only for surfaces that are actually lit by them.
- `fs_looseImageIndex 0` — Windows, 1: remember which loose image files (outside pk3s) don't exist, so they aren't searched for again.
- `com_touchMemoryOnLoad 1` — SP: 0 skips the step that walks through all game memory at the end of loading (1 is what the original game does).
- `ui_lazyMenuAssets 0` — 1: load menu art only when a menu needs it.

**Debug**

- `r_loadProfile 1` — print nested load timings from engine, renderer and game; nested times overlap, don't add them up.
- `r_imageChecksums 1`, `imagechecksums` — write a hash of every loaded texture to `imagechecksums/<map>.txt`, to compare loads.

Design notes: [rend2-map-load.md](../rend2-map-load.md), [rend2-material-ddc.md](../rend2-material-ddc.md).

## Modern OpenGL

On cards with OpenGL 4.3 some work moves to compute shaders: building light lists for [Forward+](lighting.md#many-dynamic-lights-forward) and [volumetric fog](fog.md#volumetric-fog), and [water tessellation](water.md#wave-geometry). Features stay the same; older cards fall back automatically.

- `r_gl43 1` *(restart)* — use the OpenGL 4.3 paths where the card has them; 0 forces the legacy policy. Context/capability failures retain the existing fallback.
- `r_forwardPlusCompute 1` — build Forward+ light lists on the GPU; 0 on the CPU.
- `r_volumetricFogLightCompute 1` — build volumetric fog light lists on the GPU.
- `r_volumetricFogLightTile 8` — fog cells per side of a light tile (4, 8, 16).

**Debug**

- `r_forwardPlusVerify`, `r_vfogLightVerify` — compare GPU and CPU light lists.
- `r_vfogLightStats` — fog light-list sizes and cost.

Design notes: [rend2-opengl43.md](../rend2-opengl43.md).

## Rendering fast paths

These reduce submission or shading work. They are already enabled for fresh settings; older saved values are kept. Use 0 on an individual switch to return to its reference path. Supported materials and hardware use the faster path; other draws fall back automatically. The benefit depends on the scene and is not a promise of higher FPS everywhere.

- `r_depthPrepassFast 1` — use lighter depth-only programs when the camera prepass needs no motion vectors.
- `r_worldSurfaceList 1` — submit visible BSP surfaces from a compact ordered list instead of scanning the whole array. Mode 2 checks membership against the reference.
- `r_modelOcclusion 1` — skip eligible opaque Ghoul2 surface draws hidden by the current frame's depth. Nearby bodies and uncertain bounds keep drawing; queries never wait for the GPU. Mode 2 keeps the queries without rejecting draws.
- `r_surfaceSpritesCompute 2` — GPU culling and per-plant detail for supported grass. Mode 1 keeps the original whole-surface detail; 0 uses direct submission. Needs modern OpenGL; blended and special stages retain direct draws. Distant bend/card detail can change in mode 2.
- `r_md3Instancing 1` — group compatible repeated rigid MD3 props into instance draws, keeping each prop's own lighting. Animated, translucent and other unsupported models retain individual draws. Needs modern OpenGL.
- `r_lightallSpecialize 1` — lazily use a simpler material program when that draw receives no local light. Baked light, sun, materials and emission are retained. Switch it live; a failed compile uses the original program.

Sun-cache mode 1, point-shadow traversal/layered submission and guarded skyportal culling are also enabled; see [Shadows](shadows.md) and [Sky](sky.md). Forward+, both point-shadow caches, water tessellation and experimental sun-cache modes 2/3 remain opt-in.

**Debug**

- `r_modelOcclusionDebug 1` — query coverage and confirmed skipped draws.
- `r_depthPrepassVerify 1` — compare the two prepass program families (slow).

Design notes: [optimization defaults](../rend2-optimization-defaults.md), [enabled batching defaults](../rend2-enabled-batching-defaults.md), [lightall specialization](../rend2-lightall-specialization.md).

## Testing and internal flags

- `r_offscreenPresent 0` *(restart)* — 1: render into an off-screen image first, so screenshots work from a hidden window (scripted tests).

Set by the engine; read-only, not settings:

- `cl_rendererLensWater`, `cl_rendererSpotLights`, `cl_rendererWaterInteraction`, `cl_rendererFoliageWaves`, `cl_rendererFoliageCut` — tell the game which optional renderer features are available.
- `r_ext_multisample_default_fb` — internal multisampling state.
