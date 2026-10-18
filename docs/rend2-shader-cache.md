# Rend2 GLSL program cache (`r_shaderProgramCache`)

Rend2 used to compile every GLSL program at every renderer start: over 500 programs in SP with all features
(112 lightall permutations, 196 surface sprite permutations, generic, fog, post-processing and so on).
`r_shaderProgramCache 1` (default) stores the linked programs on disk (GL_ARB_get_program_binary), so later
starts load them instead of compiling, and `r_shaderProgramLazy 1` (default) builds the per-draw permutations
only when a level needs them.

Code: `shared/rd-rend2/tr_glsl.cpp` (`GLSL_Cache*`, `ShaderProgramBuilder`).

## How it works

- `ShaderProgramBuilder::AddShader` builds the full source of each stage as before, but no longer compiles it
  right away. It hashes the source (64-bit FNV-1a, together with the program name, attributes, transform feedback
  variables and the cache version).
- `ShaderProgramBuilder::Build` looks the hash up in the cache:
  - **Hit:** `glProgramBinary` and a check of the link status. Attribute, fragment output and transform feedback
    locations are part of the binary. Uniform block bindings and sampler units are set after loading, exactly as
    for a compiled program.
  - **Miss, or a binary the driver rejects:** compile and link as before (with
    `GL_PROGRAM_BINARY_RETRIEVABLE_HINT`), then store the binary with `glGetProgramBinary`.
- **Latched cvars:** they become `#define`s in the shader source, so the hash covers them. Each combination
  (for example `r_dlightMode 1` and `r_dlightMode 2`) gets its own entries, and switching back and forth never
  evicts the other combination.
- **Unused defines are dropped before hashing** (`GLSL_PruneUnusedDefines`): a `#define NAME` line of the header
  or the extra defines (everything before the first `#line`) is removed when `NAME` appears nowhere else in the
  stage: not in the libraries, the stage file or another define (a comment counts too). Such a macro cannot
  change the compiled stage. So toggling `r_ssr` changes only the programs that mention `USE_SSR`; before, every
  global define was in every program, and any latched toggle made every program a cache miss.
- **No resolution in the sources:** the window size is the uniform `r_FBufScale`, set once after linking
  (`GLSL_SetFBufScale`), and SMAA's `SMAA_RT_METRICS` is built from it. Before, the size was a `#define` in every
  header, and changing the resolution recompiled everything.

## File

`glslcache/rend2_sp.bin` (SP) / `glslcache/rend2_mp.bin` (MP) in the home path. On Windows this is
`Documents/My Games/OpenJK/base/`.

| Part | Content |
|---|---|
| header | magic `R2GC`, format version, hash of GL_VENDOR / GL_RENDERER / GL_VERSION, generation, entry count |
| entry | key, binary format, length, generation last used, uniform location count, uniform list hash, binary, uniform locations |

- **Driver update:** GL_VERSION usually changes, so the whole file is rebuilt. A binary the driver rejects anyway
  is compiled again.
- **Corrupt files:** a truncated or corrupt file is detected (every size is checked against the file length) and
  never trusted.
- **When it is written:** only when something changed, i.e. programs were compiled, rejected or evicted, or used
  entries need their age refreshed. A start that loads everything from the cache writes nothing.
- **Cleanup:** entries unused for 16 rewrites (sources or cvar combinations you no longer use) are dropped.
  `r_shaderProgramCacheMaxMB` (default 512) caps the size, evicting the least recently used entries first.

- **Uniform locations** (format 2): every entry also holds the `glGetUniformLocation` result of each
  `uniformsInfo` uniform. A program from a binary has the locations of the link that produced it, and querying
  all 289 of them took longer than loading the binary (3.3 ms against 2.7 ms on Intel). They are used when the
  hash of the uniform names matches and two of them (the first and the last active one) match the driver;
  otherwise they are queried and the entry updated (`GLSL_ResolveUniformLocations`).

## Lazy permutations (`r_shaderProgramLazy`)

The families selected per draw have hundreds of permutations (lightall, generic, fog pass, velocity, refraction,
surface sprites; about 450 programs with all features on), and a level binds few of them. With
`r_shaderProgramLazy 1` (default):

- **Startup** builds only the permutations of the usage profile, i.e. those bound in earlier runs. Lightall is
  the exception: its profile spans every map played, so it is built per level instead.
- **End of a level load** (`RE_EndRegistration` -> `GLSL_EndRegistration`) builds the lightall permutations the
  registered shader stages can select: the stage's own index, its skeletal / vertex animated variants for grid
  lit stages (models), and the unlit depth prepass / shadow variants (tcGen / tcMod, parallax).
- **Any other permutation** is built when a draw first selects it (`GLSL_EnsureProgram`, from
  `GLSL_BindProgram` and `UniformDataWriter::Start`). With a warm cache this is one `glProgramBinary`; cold, it
  is a compile, i.e. a hitch.
- The cache binaries of the permutations not built at startup stay in memory: their keys are computed at
  startup without compiling (`GLSL_LazyCollectKeys`), every other entry is freed as before. Programs built
  later are merged into the file at the end of each level load and at shutdown.
- `glslcache/rend2_sp_used.txt` / `rend2_mp_used.txt` is the usage profile: `<family> <permutation>` lines,
  extended with every lazy permutation bound. Deleting it is harmless.

`glslusage` prints the loaded programs per family, how many were bound since loading (`glslusage reset` clears
that, `glslusage all` lists them) and how many were built on demand. With `developer 1` every on-demand build
is logged with its time.

## Helper processes (`r_shaderCompileProcesses`)

The Intel Windows driver compiles one GLSL program at a time per process: several threads with shared contexts
are slower than one, but separate processes compile in parallel (12 lightall permutations: 1 process 21.7 s,
3 processes 9.3 s, 6 processes 6.5 s). So a batch of programs that are not in the cache, the startup programs
and the lightall permutations of a level (`GLSL_EndRegistration`), is linked in `rend2_glslc` processes
(`shared/rd-rend2/glslc`, `tr_glslhelper.cpp`):

- the batch is first collected without linking anything. At startup every loader runs once as a dry run:
  `ShaderProgramBuilder::Build` and `GLSL_InitComputeShader` turn an uncached program into a job and give the
  loader a trivial linked program, so its uniform setup is harmless, and those programs are cleared again
  (`GLSL_DryRunEnd`, about 40-60 ms on a cached start). A level batch runs the lightall loader in `prepare` mode.
  A job is the key, the stage sources and the attribute / output / transform feedback bindings;
- `rend2_glslc.exe`, next to the renderer DLL, is started up to `r_shaderCompileProcesses` times (default -1 =
  half the CPU threads, at most 8). It opens a hidden window with the renderer's context version and reports
  its GL_VENDOR / GL_RENDERER / GL_VERSION; a helper on another GPU or driver is not used for the rest of the
  session. Longest sources go first. The helpers exit at the end of the batch;
- the returned binaries become cache entries, and the normal pass loads them with `glProgramBinary`. A program a
  helper could not link, or a binary the driver rejects, is compiled in the game as before, which reports the
  error. A helper that does not answer for two minutes is stopped.

Single programs built on demand are still compiled in the game process. Without `rend2_glslc.exe` (or with
`r_shaderCompileProcesses 0`) everything is compiled in the game process. A batch takes at least as long as its
longest program: at startup that is the froxel fog compute kernels.

`rend2_glslc.exe` is built with the renderers (CMake target `rend2_glslc`, a dependency of both) and must be
copied next to `rdsp-rend2_x86_64.dll` / `rd-rend2_x86_64.dll`; it uses the `SDL2.dll` there.

## Cvars

| Cvar | Default | |
|---|---|---|
| `r_shaderProgramCache` | 1 | 0 = compile everything as before, no file read or written. Latched. |
| `r_shaderProgramCacheMaxMB` | 512 | size limit of the file |
| `r_shaderProgramLazy` | 1 | 0 = build every permutation at startup, as before. Latched. |
| `r_shaderCompileProcesses` | -1 | helper processes for batches of uncached programs; -1 = half the CPU threads (at most 8), 0 = none |

The start-up console shows, after the existing `loaded N GLSL shaders ... in X seconds` line:

```
GLSL cache: 712 from glslcache/rend2_sp.bin, 0 compiled and stored, 0 rejected, 45.3 MB
```

To start over, delete the file (or set `r_shaderProgramCache 0`).

## Startup progress

The splash screen shows `SHADERS: N%` and a progress bar in its lower-left corner
while GLSL programs are prepared. It counts completed programs, including cache
hits, against the valid permutations and enabled shader families for this start.
This is work completed, not a remaining-time estimate: individual programs can
take very different amounts of time to compile.

The splash has its own small built-in font so it can display progress before the
game UI and fonts have initialized. Updates are limited to once per 100 ms;
one slow driver compilation can keep the displayed percentage still until that
program finishes. Progress stays below 100% until the disk cache has been saved.
This covers the GLSL preparation stage, not the entire engine or map load.

## Measurements (offline, GL 3.2 core context, binary saved and loaded in separate processes)

| Driver, lightall permutation | compile + link | load from binary | binary |
|---|---|---|---|
| Intel UHD, heavy (lightmap, deluxe, parallax, SSR, sun + point shadows, skeletal) | 1246 ms | 2.7 ms | 72 KB |
| Intel UHD, light grid + point shadows | 194 ms | 1.5 ms | 26 KB |
| NVIDIA RTX 2060, heavy | 2019 ms (cold) | 10 ms | 149 KB |
| NVIDIA RTX 2060, light grid + point shadows | 402 ms (cold) | 3.3 ms | 61 KB |

The NVIDIA driver keeps its own GLSL cache, so warm starts there were already faster than these cold numbers.
The rend2 cache also covers drivers without such a cache (Intel). The first start after a shader change, a driver
update or a new cvar combination still compiles; only the programs that changed are compiled.

## Measurements in game (2026-10-16, SP, Intel UHD, the author's config: HDR, SSR, GTAO, skin SSS, F+, LTC, froxel fog)

Programs bound while looking around a level (`glslusage`): vjun2 44 of 518, t2_trip 44, t3_hevil 45, yavin1b 47.
The lazy families together used 23 different programs on these four maps: lightall 9 of 112, generic 8 of 64,
surface sprites 3 of 196. No spec-gloss or cloth lightall permutation was used.

| | before | `r_shaderProgramLazy 1` |
|---|---|---|
| startup, nothing cached | 518 programs, 448 s | 74 programs, 15 s (+ 22 s at the first level load for 7 lightall programs) |
| startup, cached | 3.7-5.5 s | 0.34 s + 0.15 s for the lazy keys |
| level load, cached | - | 7 lightall programs from the cache in 69 ms |
| `r_ssr 0` (latched) | every program compiled again | 3 of 74 startup programs compiled, the rest cached |
| other resolution | every program compiled again | nothing compiled |

With the uniform locations in the cache (format 2) the cached start went from 0.34 s to 0.18 s and the cached
level batch from 69 ms to 40 ms. With `rend2_glslc` (6 helper processes), everything uncached (`glslcache`
deleted, t2_trip): startup 15.2 s -> 10.3 s (bounded by the volumetric compute kernels), the level's 7 lightall
programs 15.3 s -> 7.3 s (another run: 35.7 s -> 9.0 s; the laptop's timings vary with heat). The screenshots of
both are the same.

Most of the old cached start was not the driver: the stage source was assembled with `Q_strcat`, whose
`strncpy` fills the rest of the (lightall sized) buffer with zeros on every call, tens of MB per stage
(`GLSL_HeaderCat` now).

## Test checklist

1. First start: `0 from ...`, the file appears.
2. Second start: `N from ...`, `0 compiled`, and a much shorter `loaded ... in X seconds`.
3. Change `r_dlightMode` + `vid_restart`: only the affected programs are compiled. Switch back: everything
   comes from the cache.
4. `r_shaderProgramCache 0` + `vid_restart`: `GLSL cache: off (r_shaderProgramCache 0)`, same image.
5. Replace the file with garbage: a warning, everything is compiled, the file is rebuilt, no crash.
6. During a cold start or `vid_restart` that reloads GLSL programs, the splash
   percentage and bar advance from 0 to 100. With a warm cache, they finish quickly.
   Check SP and MP, with optional AO, SSR/SSGI, silhouette POM and SMAA enabled and
   disabled; there should be no `GLSL startup progress: expected ...` warning.
7. During a cold start, click the window while shaders compile. On a driver
   reporting `GLSL parallel shader compile: available`, the window should continue
   handling messages even while one shader or program is still compiling.
8. `r_shaderProgramLazy 1`, load a level: `GLSL: N lightall permutations for this level, M built (K from cache)`;
   play, then `glslusage`: few or no programs built on demand after the level load (`developer 1` logs each).
   Restart: the lazy programs of the profile load at startup, the level ones from the cache.
9. `r_shaderProgramLazy 0` + `vid_restart`: every permutation is built at startup as before, same image.
10. Change `r_customwidth` / `r_customheight` + `vid_restart`: `0 compiled`.
