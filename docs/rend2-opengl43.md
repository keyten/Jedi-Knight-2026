# Rend2 OpenGL 4.3 and optional modern paths

Both SP and MP rend2 prefer an OpenGL 4.3 Core context. If creation fails,
the SDL window layer retries the same display settings with OpenGL 3.2 Core,
then applies the existing resolution fallback. The renderer/window API and
`glconfig_t` ABI are unchanged. Deploy the rebuilt engine as well as the
renderer DLL to get context negotiation.

`r_gl43 1` (default) requests 4.3 and permits modern paths. `r_gl43 0` requests
3.2 and forces legacy paths. Apply changes with `vid_restart`. A driver may
return a newer context than requested; the console and `gfxinfo` show the
actual version, supported capabilities, and selected policy. A newer version
reported with `r_gl43 0` still uses the legacy policy.

This is infrastructure for optional optimizations. Existing effects retain
their current implementations and GLSL 150 shaders; requesting 4.3 does not
by itself accelerate them. The console says modern paths are **available**,
not that every effect has a compute implementation.

## Adding a fast path

Use the shared `glRefConfig` and `R_HasModernFeatures(requiredMask)`, rather
than parsing GL strings or maintaining another capability manager. The mask
can combine `MODERN_COMPUTE`, `MODERN_SSBO`, and `MODERN_IMAGE_LOAD_STORE`.
Eligibility requires actual GL 4.3+ / GLSL 4.30+, all entry points for the
requested capabilities, and the enabled startup policy. Older contexts keep
the baseline even if they advertise individual ARB extensions.

Check the limits needed by the algorithm before allocating resources:
work-group dimensions/counts, invocations, shared memory, SSBO block size,
binding count/alignment, compute storage blocks, and image units/uniforms
are recorded in `glRefConfig`. Query additional stage-specific limits if
using storage buffers or images from a graphics shader.

Keep a feature-local `modernReady` flag. Set it only after shaders and
resources have initialized successfully. On a capability/limit/initialization
failure, clean up partial resources and continue with the existing algorithm.
Log the chosen implementation once during initialization. Clear readiness
and destroy resources during shutdown; reselect after `vid_restart`.

```cpp
const uint32_t requirements = MODERN_COMPUTE | MODERN_SSBO;
// The feature owns a zero-initialized shaderProgram_t and its buffers.
modernReady = R_HasModernFeatures(requirements) && limitsFit &&
    GLSL_InitComputeShader(&computeProgram, "feature_compute", sourceBody,
                          requirements);
// Also require successful buffer/resource creation before setting ready.

if (modernReady) {
    GLSL_BindProgram(&computeProgram);
    qglBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, outputBuffer);
    qglDispatchCompute(groupsX, groupsY, groupsZ);
    qglMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
} else {
    RunLegacyImplementation();
}
```

`GLSL_InitComputeShader` prepends `#version 430 core` to the supplied body,
compiles and links without a fatal renderer error, and returns false on
failure. It initializes the normal uniform interface and supports
`GLSL_BindProgram` / `GLSL_DeleteGPUShader`. It accepts an empty program;
the feature owns deletion and must not put it into the graphics draw queue.
This initial helper does not use the graphics program binary cache.

Choose barriers for the **next consumer**: shader storage access uses
`GL_SHADER_STORAGE_BARRIER_BIT`, vertex fetch uses
`GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT`, texture sampling uses
`GL_TEXTURE_FETCH_BARRIER_BIT`, and indirect commands use
`GL_COMMAND_BARRIER_BIT`. Combine bits when needed. CPU readback and resource
reuse can also require fences; a memory barrier alone is not a CPU wait.
See the [Khronos memory barrier reference](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glMemoryBarrier.xhtml).

Good candidates for later measured optimizations are Forward+ light-list
construction (currently on the CPU), weather simulation (currently transform
feedback), and image-based Hi-Z/AO or froxel fog passes. Each needs its own
implementation, comparison of output with the baseline, and GPU timings.

## Validation

Build both renderer targets and both engines (the SDL layer lives in the
engines). Exercise default startup, `r_gl43 0; vid_restart`, then
`r_gl43 1; vid_restart`; confirm console / `gfxinfo` policy and actual version.
On a GL 3.2/4.1-only machine confirm automatic context fallback and existing
effects. For each future fast path, test shader/resource failure, boundary
limits, output equivalence, and timing before enabling it by default.

Uniform-block diagnostics now allocate reflection buffers from the driver’s
active-uniform count instead of assuming 128 entries. In particular, the
`Lights` block can exceed that count; the old arrays could overwrite the stack
during startup on either context version. Reflection for diagnostic output
runs only with `developer` enabled. Include `developer 1` startup in regression
checks so large blocks exercise this path.

The specular IBL helper also avoids referencing `u_EnvBrdfMap` when both
cubemaps and SSR are disabled, matching the sampler declaration conditions.

Validation on Intel UHD Graphics, driver 27.20.100.8280:

- Release builds passed for both SP/MP renderers and engines.
- GL 4.3 / GLSL 4.30 context initialization reported compute, SSBO, and image
  load/store support. Corrected lighting variants compiled, including normal,
  parallax, skeletal, and cloth variants. Compilation of the complete default
  graphics shader set was stopped before completion; full GL 4.3 startup and
  rendered scenes remain unverified.
- Forced GL 3.2 completed SP initialization and normal shutdown with exit 0,
  `developer 1`, `r_normalMapping 0`, and `r_specularMapping 0`. All 474 shader
  programs initialized, including a reflected block with 172 active uniforms.
- An independent hidden SDL/GL 4.3 smoke test dispatched a compute shader into
  an SSBO, applied a memory barrier, and correctly read back 32 values. Invalid
  compute source reported a compile failure without a native crash. This
  exercised the GL APIs directly, not an in-game feature using the new helper.
- Automatic fallback after a rejected 4.3 context still needs testing on a
  driver without 4.3 support; the explicit legacy selection was tested above.
