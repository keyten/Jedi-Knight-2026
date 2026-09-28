# Rend2 GLSL program cache (`r_glslCache`)

Rend2 compiles every GLSL program at every renderer start: about 700 lightall permutations in SP plus generic,
fog, post-processing and so on. `r_glslCache 1` (default) stores the linked programs on disk
(GL_ARB_get_program_binary), so later starts load them instead of compiling.

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

## File

`glslcache/rend2_sp.bin` (SP) / `glslcache/rend2_mp.bin` (MP) in the home path. On Windows this is
`Documents/My Games/OpenJK/base/`.

| Part | Content |
|---|---|
| header | magic `R2GC`, format version, hash of GL_VENDOR / GL_RENDERER / GL_VERSION, generation, entry count |
| entry | key, binary format, length, generation last used, binary |

- **Driver update:** GL_VERSION usually changes, so the whole file is rebuilt. A binary the driver rejects anyway
  is compiled again.
- **Corrupt files:** a truncated or corrupt file is detected (every size is checked against the file length) and
  never trusted.
- **When it is written:** only when something changed, i.e. programs were compiled, rejected or evicted, or used
  entries need their age refreshed. A start that loads everything from the cache writes nothing.
- **Cleanup:** entries unused for 16 rewrites (sources or cvar combinations you no longer use) are dropped.
  `r_glslCacheMaxMB` (default 512) caps the size, evicting the least recently used entries first.

## Cvars

| Cvar | Default | |
|---|---|---|
| `r_glslCache` | 1 | 0 = compile everything as before, no file read or written. Latched. |
| `r_glslCacheMaxMB` | 512 | size limit of the file |

The start-up console shows, after the existing `loaded N GLSL shaders ... in X seconds` line:

```
GLSL cache: 712 from glslcache/rend2_sp.bin, 0 compiled and stored, 0 rejected, 45.3 MB
```

To start over, delete the file (or set `r_glslCache 0`).

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

## Test checklist

1. First start: `0 from ...`, the file appears.
2. Second start: `N from ...`, `0 compiled`, and a much shorter `loaded ... in X seconds`.
3. Change `r_dlightMode` + `vid_restart`: only the affected programs are compiled. Switch back: everything
   comes from the cache.
4. `r_glslCache 0` + `vid_restart`: `GLSL cache: off (r_glslCache 0)`, same image.
5. Replace the file with garbage: a warning, everything is compiled, the file is rebuilt, no crash.
6. During a cold start or `vid_restart` that reloads GLSL programs, the splash
   percentage and bar advance from 0 to 100. With a warm cache, they finish quickly.
   Check SP and MP, with optional AO, SSR/SSGI, silhouette POM and SMAA enabled and
   disabled; there should be no `GLSL startup progress: expected ...` warning.
