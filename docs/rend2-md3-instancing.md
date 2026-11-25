# Repeated rigid MD3 instance draws

`r_md3Instancing` defaults to **0**; `1` batches eligible repeated rendered MD3 entities. `0` restores
individual submission without a restart. The current implementation requires the
renderer modern tier (`r_gl43 1` and SSBO capability), although instance data itself
uses a portable std140 uniform block. It does not batch decor baked into BSP geometry.

Each group reuses the existing model VBO and IBO. It issues one actual
`glDrawElementsInstancedBaseVertex` with 2–32 instances. No mesh vertex/index data is
copied per entity. A 10,752-byte block carries 32 complete 336-byte `EntityBlock`
records: matrices, ambient/directed light, light origins/directions, grid/SH lighting,
split sunlight and the other entity constants. The vertex shader uses `gl_InstanceID`;
the fragment shader receives the same flat instance index. Differing lighting does
not force one shared constant or sacrifice per-entity appearance.

The ordinary generic/lightall shaders retain their original preprocessed path.
Only an actual repeated group lazily requests `GLSLV_MD3_INSTANCING`. No additional
instance indexing or fragment branch is added to ordinary draws. Variant compile
failure, full upload buffers or insufficient per-frame CPU allocation leave every
original draw intact. The shader cache includes the variant define and source.

## Admission and ordering

Admission requires a one-frame MD3, stable model buffers, frame zero, an opaque
single-stage material and a positive uniform transform. Different translations,
rotations, uniform scales and lighting origins are supported. Mirrored, nonuniform,
sheared, singular and nonfinite transforms retain the ordinary path. Animated poses,
Ghoul2/gore, plant/cut geometry, foliage motion, deforms, silhouette POM, tessellation,
alpha test, blending, particle ownership and personal/depth-hack/volumetric renderfx
are excluded. Lighting-origin and no-shadow flags are permitted.

After recording, only **consecutive compatible** draws can group. Equality includes
program, exact indexed geometry range and attributes, render/depth/cull state,
textures, all other uniform-block bindings and exact material uniform data. Cubemap
selection, dynamic-light masks and material parameters consequently remain distinct
when required. Hash collisions always undergo exact equality. Groups never cross an
intervening opaque material, preserving the winner of coplanar depth ties. Capacity
splits preserve every member once. Large-divisor constant MD3 colour/UV attributes
are allowed only when the whole bounded group still reads element zero.

The main view's generic/lightall colour and legacy depth programs are supported.
The default fast depth and motion-vector programs, previous-frame transforms,
shadows, portal/sky views, no-world views and other program families remain individual.
They use the same mesh and entity data as before. The instance vertex path uses the
original shader expression/order for model and view-projection multiplication.
Current-frame model occlusion is separately limited to Ghoul2; it does not suppress
members of an MD3 group.

## Verification

`BuildMd3InstancingTests=ON` builds `rend2_md3_instancing_test`. It exercises the actual
planner and transform policy: all eligibility gates, reflection/nonuniform/shear/NaN
rejection, independent transforms/lighting, material hash collisions, intervening-draw
barriers, 32-member splits, exact upload capacity and integer-overflow failure.

`tools/rend2/test_md3_instancing_gl.py` passed on Intel UHD Graphics
(27.20.100.8280) and RTX 2060 (616.92). Six ordinary/instance shader pairs compile
and link: generic RGB/opaque, lightall grid, physical PBR with split sun, height grid
and reflected SH. The instance UBO is 10,752 bytes; ordinary programs have no instance
block. The production generic shaders render 2, 17 and 32 differently transformed,
uniformly scaled and lit instances with byte-identical pixels to individual draws,
using respectively 1 instead of 2, 17 and 32 GL indexed draw calls.

SP and MP Release renderer builds and the CPU test pass. The private runtime harness
`tools/rend2/test_md3_instancing_runtime.py` uses an external entity override containing
40 actual misc_model_static MD3s plus two excluded transforms, private binaries/home,
read-only installed assets and a real UI/FX/shader-clock freeze. It compares enabled,
disabled/repeated, default fast depth, velocity and moved-camera legacy depth captures.
The Intel runtime completed with **40 colour draws reduced to 2**, saving 38 calls
per sampled frame and uploading 21,504 bytes. The 40 fused depth/velocity draws
remain individual. Enabled/disabled/repeated, initialized velocity and moved-camera
legacy-depth captures all have **zero differing channels**. The group split is the
real 32 + 8 overflow case; two nonuniform/mirrored fixture transforms stay individual.

The bounded warmed 90-frame ABBA medians were 3.5347 / 3.4067 ms disabled and
3.7877 / 3.6267 ms enabled at 640x360. These full-frame intervals include driver,
game and presentation; resident MSBuild workers and background desktop activity
limit interpretation. They do **not** establish a net win. Consequently the feature
stays opt-in, pending a more profitable submission workload or batching refinement.
No FPS improvement is claimed. See the [machine-readable receipt](rend2-md3-instancing-validation.json).

`r_md3InstancingStats 2` additionally explains the first rejected program; `1` samples the completed frame once per second: tagged draws,
material/pass fallbacks, groups, instances, saved calls, capacity fallbacks and uploaded
bytes. These are actual grouped submissions, not an assumption based on shared VBOs.
No FPS percentage is inferred from the call count.
