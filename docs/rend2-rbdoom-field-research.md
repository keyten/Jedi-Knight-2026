# RBDOOM area light grids, SH and environment probes: JA requirements

Research date: 2026-10-27; JA baseline `d58f91016`, `rend2-unified-wip`.
Reference source is pinned to RBDOOM-3-BFG
[`ea29c006e84fedcc0e7c173c383a087ce0a8c0d5`](https://github.com/RobertBeckebans/RBDOOM-3-BFG/tree/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5)
(2026-08-07). The [manifest](../tools/rend2/rbdoom_research/sources.json) pins 43
source/documentation files, 1,398,197 bytes, with SHA-256 and Git blob identifiers.
Downloaded source stays under ignored `build/rbdoom-research/reference/`.
No RBDOOM code, assets, executable or dependencies are added to JA.

**Decision:** adopt region-scoped *data semantics* and explicit radiance→irradiance
operators, not the reference engine's renderer/layout. Treat SH L2/L4 as useful
diffuse-field benchmark codecs, not a mandatory canonical format or physical light
model. Keep exact geometry, emitter inference and separately generated transport
fields independent. Explicit validity must replace any inference from RGB darkness.

Related JA work: [GI attribution](rend2-inverse-gi-research.md),
[DDGI comparison](rend2-ddgi-field-research.md),
[regions](rend2-lighting-regions.md), [visibility](rend2-static-visibility.md),
[observation decoding](rend2-observation-decoding.md) and
[physical PBR units](rend2-physical-pbr.md).

## 1. What was actually inspected

The relevant documentation is the repository [README's baked-lighting sections][readme].
Its description is checked against the following current code, not treated as a
complete specification:

| Source | Responsibilities and specific findings |
|---|---|
| [RenderWorld_lightgrid.cpp][grid] | Per-area bounds/spacing, point placement and nudging, metadata/cache I/O, cubemap capture, SH projection and atlas bake. |
| [RenderWorld_local.h][types] / [RenderCommon.h][common] | Grid points, ownership, SH buffers, tile sizes and `STORE_LIGHTGRID_SHDATA`. |
| [SphericalHarmonics.h][sh] | Real basis, degree/count convention, coefficient indexing and diffuse convolution. |
| [RenderWorld_envprobes.cpp][env] | Environment capture and SH diffuse / GGX-style specular prefilter jobs. |
| [ambient_lightgrid_IBL.ps.hlsl][lookup] | Eight-sample grid lookup, normal direction, RGB-darkness rejection, diffuse material application and separate specular probes. |
| [RenderWorld_load.cpp][bounds] / [PointInArea][point] | World-model area bounds and point→BSP-area/solid classification. |
| [tr_frontend_addmodels.cpp][select] / [tr_frontend_main.cpp][frontend] | Surface-area grid selection and fallback; environment-probe selection and connected-area culling. |
| [Game_local.cpp][populate] / [EnvironmentProbe.cpp][entity] | Automatic room-centre probes versus authored `env_probe` entities. |
| [RenderBackend.cpp][backend] | Capture flags, ambient recursion, post-effect exclusions and consumer setup. |
| [Image_load.cpp][images] / [BinaryImage.cpp][binaryimage] | HDR light-probe image handling and runtime compression. |

The source blobs were verified against the pinned Git tree as well as their
download hashes. Source inspection and mathematical controls were performed;
**RBDOOM was not built, its bake commands were not run, and no Doom/JA game assets
were captured**. Algorithm intent is distinguished below from verified runtime
behaviour. The existing JA capture or renderer is not replaced.

## 2. Actual representation: L4 is 25 RGB coefficients, but the atlas is E/pi

### 2.1 Degree and real-basis convention

`SphericalHarmonicsT<T,L>` stores `(L+1)^2` values. Here **L is maximum degree**,
not number of bands. Thus L1 is 4, L2 is 9, L3 is 16, and L4 is **25 coefficients
per channel**. It has five bands, degrees 0 through 4. Both the grid and environment
diffuse jobs use `SphericalHarmonicsT<idVec3,4>`; calling it “four-band SH” would
be incorrect for this version. [Basis/storage][sh] L36–171

The basis is real, orthonormal, with the Condon–Shortley phase, `z` as the polar
axis and index `l*l+l+m`. For example:

```text
(R1) Y00 = 1/sqrt(4*pi)
     Y1,-1 = -sqrt(3/(4*pi))*y
     Y1, 0 =  sqrt(3/(4*pi))*z
     Y1, 1 = -sqrt(3/(4*pi))*x
```

Signed coefficients are normal; rejecting negative SH coefficients as “negative
light intensity” would destroy valid fields. Basis normalization, axis frame,
phase, indexing and degree must be declared by any portable payload.

### 2.2 Capture and convolution

The jobs project captured cubemap radiance with solid-angle weights:

```text
(R2) c_lm = integral_sphere Li(p,w) Y_lm(w) dw
          ~= sum_cubemap_texels L_texel Y_lm(w_texel) deltaOmega_texel
(R3) E(p,n) = integral_sphere Li(p,w) max(0,n·w) dw
     E_L(p,n) = sum_lm A_l c_lm Y_lm(n)
     A_l = 2*pi * integral_0^1 u P_l(u) du
```

Through degree 4, `A = {pi, 2*pi/3, pi/4, 0, -pi/24}`. Degree 3 and all odd
degrees above 1 contribute zero under Lambertian convolution. L4's diffuse
evaluation therefore has 18 potentially useful RGB modes, although its temporary
radiance array contains all 25. L3 can improve radiance while adding nothing over
L2 to this diffuse operator. [Convolution][sh] L196–300

The bake does **not** simply serialize the 25 coefficients as its usual renderer
input. `CalculateLightGridPointJob` evaluates the convolved function in octahedral
normal directions, divides by pi, clamps each output RGB channel to nonnegative,
and writes RGB16F. The actual atlas value is approximately:

```text
(R4) Q(p,n) = max(0, E_L(p,n)/pi)       # channelwise clamp
```

The material shader later multiplies Q by diffuse colour and its other PBR/AO
terms. Division by pi is Lambertian normalization, **not division by observed
texture albedo**. A constant incoming radiance L gives `E=pi*L`, `Q=L` before
the remaining material/consumer factors. [Grid job][grid] L919–1027;
[material application][lookup] L355

`STORE_LIGHTGRID_SHDATA` is **0** in this version: the normal write/job-backup
paths omit SH data. `lightGridPoint_t` nevertheless declares a 25-RGB coefficient
member. A declared struct member is not proof that meaningful SH coefficients
are populated, resident with valid values, or loaded from disk. The ordinary
field representation is the separately loaded atlas plus grid metadata.
[Types][types] L63–83; [compile-time path][common] L478–498

This differs from DDGI's separately factored storage/readout. JA must expose a
canonical decoded quantity such as **E**, not inherit an unnamed pi or 2*pi factor.
`Irradiance`, `IrradianceOverPi` payload response and `IncomingRadianceCoefficients`
cannot be treated as interchangeable encodings without an explicit conversion.

### 2.3 Orientation and texture layout

The projection helper uses face order `+X,-X,+Y,-Y,+Z,-Z`, pixel centres and a
flipped image-v coordinate. It constructs world-space directions before SH
evaluation; the lookup uses the world-space shading normal. `R_SampleCubeMapHDR16F`
also uses the renderer's `cubeAxis` for pixel addressing. Complete capture/face/
shader orientation agreement was not executed here; six axis-colour fixtures
should validate it before a decoder/port reuses a layout. [Projection helpers][grid],
[cube lookup][env] L258–327

Octahedral encoding parameterizes a direction, not a light source. Coefficients
are evaluated to a dense angular function at each spatial support, then atlas
packing combines spatial and angular indices. Linear grid indexing is
`x + Nx*(y + Ny*z)`. The atlas packs x/z horizontally and y vertically:
`width=Nx*Nz*tile`, `height=Ny*tile`.

The header declares tile size `14+2=16`. However, the active `NormalizedOctCoord`
uses margin 2 and an interior of `size-2*margin`, with a source FIXME; the shader's
border transform uses the descriptor's different size/border factors. These
details need a numerical seam/pixel-centre check; do not advertise a verified
14×14 interior solely from the constant name. [Tile constants][common] L480–481;
[oct coordinate helper][env] L456–483; [lookup][lookup] L312–332

## 3. Spatial grids: useful area scope, incomplete geometric guarantee

### 3.1 Bounds, spacing and portals

RBDOOM builds one grid per portal area from that area's **world-model AABB**.
`AddWorldModelEntities` records these bounds; `SetupLightGrid` aligns the first
sample upward and the last sample downward to the spacing. Default preferred
spacing is `(64,64,128)` world units. If the product of dimensions exceeds the
point budget, spacing increases by 16 on successive axes until the count fits.
The default budget is `(2048/16)^2 = 16,384` slots per area, with command overrides.
[Bounds][bounds] L1053; [grid setup][grid] L50–117

This bounds local work and avoids one global AABB forcing every room to coarsen.
It does not remove every empty slot: area AABBs can overlap, contain solid or
other-area space, and poorly fit long/concave rooms. Count bounds are not by
themselves proof that the packed atlas's individual dimensions fit every GPU.
The storage remains a regular dense grid within each selected AABB.

| Property | Original JA/Quake-family observations | Pinned RBDOOM area grids |
|---|---|---|
| Spatial support | Map-wide regular lattice; JA indexed records can be shared by many logical cells. | Separate dense lattice per area AABB, with independently adjusted spacing. |
| Angular evidence | Ambient/directed RGB and one quantized direction; JA retains up to four styles. These are compiler-specific observation operators. | Six rendered HDR views projected to L4, then stored as an angular diffuse response. |
| Position changes | Original baked sample positions must stay fixed. | Invalid capture points can be nudged before rendering new values. |
| Geometric certainty | Neither styles nor nonzero colour prove free space. | BSP point membership, not certification of the full cell or interpolation path. |
| New source information | Missing angles/source identity cannot be recovered merely by changing storage. | Known forward scene lights produce the captures; no emitter localization occurs. |

JA details are established by `Core::GridLayout`, `GridCell` and
`LightgridObservations` in [lightgrid.h](../shared/rd-rend2/lightrecon/lightgrid.h).
The 30-byte JA source record and its indirection must not be replaced by a
RBDOOM-sized angular payload and described as additional original evidence.

BSP portals drive culling/connectivity and bounds ownership. They are not
automatically light-transport interfaces or physical apertures. View-blocking
door portal state may affect environment-probe selection, but does not make a
baked field update itself when a door moves. The inspected grid lookup has no
DDGI-style angular distance moments and no per-contribution optical trace.

### 3.2 Point validity and walls

Placement initially marks a point valid when `PointInArea(point) != -1`.
`PointInArea` traverses BSP nodes and returns -1 for its solid leaf. Placement
tries corner nudges with source loops for 9- and 18-unit displacements when the
base point is invalid. Actual moved origins and validity are saved in metadata.
[Point classifier][point] L1240–1278; [placement][grid] L260–376

Important qualifications in the **actual code**:

- The predicate accepts **any** nonnegative area, not necessarily the grid's
  owning area. It establishes point membership under that BSP, not a free box,
  visibility connection or correctness of every visual/dynamic surface.
- Nudging creates new capture locations and can cross a boundary. The inspected
  nudge loop has an early-exit expression depending on the outer grid index;
  executing both nominal displacement stages for every case is not established
  by reading the loop names alone. This is not a placement routine to copy blindly.
- The shader still computes interpolation from nominal origin/spacing; it does
  not reconstruct an irregular spatial mesh from the saved nudged positions.
- Dark invalid atlas slots are black, but darkness is also a possible valid
  physical signal. The shader rejects a sampled RGB sum below `.0001` as assumed
  wall data, then renormalizes remaining weights. That is a rendering heuristic,
  not geometric classification. [Lookup][lookup] L332–351

For JA, keep explicit per-sample presence, geometric classification, support side
and optical uncertainty. Never reuse the rejected `styles[0]` or RGB-brightness
heuristics. Historical lightgrid coordinates/values remain immutable; only a new
field support can be moved and then reevaluated. Use the existing domain/visual
geometry/region APIs, with local Unknown where certification is absent.

### 3.3 Choosing which grid to sample

The current front end locates a surface centre, seeks a corresponding available
area grid among the entity's references and otherwise uses a first available grid
as a presentation fallback. A model can span areas, hence the surface-level
choice. The back end selects the lightgrid shader when such data exist.
[Selection][select] L815–854; [consumer setup][backend] L1193–1278

This improves on a single object/camera room selection, but a whole surface can
still span portals or a thin partition. It is not per-luxel source visibility.
JA's world-space observations can be associated with **surface sides** and actual
query positions; keep that precision. A first-available-area fallback may be a
documented renderer approximation, never a successful exact API query or fitting
constraint.

## 4. What the bake actually generates

### 4.1 It is forward rendering with known lights

`bakeLightGrids` requires a loaded world/view. It sets up each area's points,
renders six 90-degree HDR faces at every valid capture origin, reads back the
faces, projects SH in CPU jobs and writes each area's atlas. This is **not a
headless CPU inverse solver**, and it does not recover original light definitions
from the captured irradiance. Those lights/materials already exist in Doom's
runtime scene. [Bake command][grid] L1041–1458

The first iteration uses `RDF_NOAMBIENT | RDF_IRRADIANCE`; subsequent iterations
allow ambient reuse. Images are reloaded between outer iterations. The `bounce`
option is therefore an iterative rendered-light reuse count, not a proof of an
exact path-bounce expansion or converged physical transport. The first capture
can already include directly visible emissive/sky radiance as well as reflected
direct lighting, so not every contribution has the same reflection order.

`RDF_IRRADIANCE` selects the HDR target and bypasses normal temporal/post-effect
paths; `RDF_NOAMBIENT` excludes the ambient pass. Generic non-light-dependent
material stages still participate in the scene render. Capture is renderer- and
current scene-state-dependent: taking a view is not proof that NPCs, animated
materials, doors or all transient effects have been removed. The shown code waits
for game-thread work; that is not a general immutable offline scene snapshot.
[Backend capture path][backend] L2208, L5687, L5880–5914

The capture-related ambient path also uses artistic renderer choices, including
a fixed light-scale branch and `r_forceAmbient`; it is not an absolute photometric
measurement. Existing geometry scale/PBR calibration rules in JA cannot identify
the brightness or transfer function of an unknown historical bake by analogy.

### 4.2 Incoming versus outgoing quantities

A cubemap looks outward from probe p and observes outgoing surface radiance
`Lo(y,-w)` along the first visible hit. In vacuum this is incoming radiance
`Li(p,w)`. It already incorporates the hit surface's reflectance, illumination,
emission and view-dependent reflection. It is **not** the outgoing radiance of a
unit-white receiver at p. After convolution, `E(p,n)` describes incident diffuse
illumination for normal n; `rho_receiver*E/pi` describes its Lambertian outgoing
response.

The available JA BSP lightmaps are historical baked multipliers/relative lighting
under an often unknown compiler, not six HDR outgoing-radiance photographs at
every grid cell. JA ambient/directed grid RGB plus one quantized direction cannot
be expanded into 25 measured SH coefficients. Do not project the single direction
as a verified physical source or divide old baked RGB by today's PBR albedo.

### 4.3 Sun, sky, emitters and local lights

The forward capture can contain reflected point/spot/parallel-light effects and
whatever the visible sky/emissive materials render. The README explicitly intends
diffuse, specular and emissive scene contributions to participate. This is not an
automatic per-source separation. Direct sun may be handled as an independent
runtime light while its reflected energy appears in probes; a sky material might
also contain a bright disk. Avoid double counting by recording membership, not
by assuming everything named “ambient” excludes direct sky or sun.

For JA, preserve direct local emitters, direct sun, sky, modelled reflection,
artist ambient and unresolved model discrepancy separately as in the existing
GI decision. A bright capture of a doorway/reflection does not create a physical
point or area emitter. Capturing the current JA render would not be a substitute
for the original CPU BSP observations.

## 5. Environment probes are a different consumer representation

If any authored EnvironmentProbe entity exists, the automatic population routine
returns without filling all remaining areas. Otherwise it tries a snapped AABB
centre per area and skips points not belonging to a valid area. It does not prove
that an arbitrary concave room's centre is a useful capture position. Authored
`env_probe` entities carry their own origin/colour controls. [Population][populate]
L1265–1325; [entity parameters][entity] L79–108

`bakeEnvironmentProbes` captures six HDR faces with ambient excluded, then launches
separate diffuse and glossy jobs:

- **Diffuse:** project L4 radiance, convolve and evaluate/clamp `E/pi` into an
  octahedral map. It is a broad angular response for diffuse shading.
- **Specular:** roughness-dependent GGX-style filtering into packed octahedral mip
  regions, used with a BRDF LUT and reflection direction. It retains more angular
  information but has capture-position/parallax and view-approximation errors.

The glossy job uses the split-sum `V=N` approximation. Its sampled-direction and
packing details need their own numerical validation; the inspected code computes
a reflected L direction but passes H into the source-map sampler. This report
does not claim exact GGX prefilter correctness from a method name. The material
shader blends up to three environment maps; the shown portal-box parallax branch
is disabled, not a proven geometric correction. [Jobs][env] L590–860;
[glossy lookup][lookup] L359–407

Low-order diffuse SH is not a sharp reflection map or directional fog-light
distribution. Environment captures are not physical lamp geometry. The optional
connected-area selection suppresses some behind-door probes, but is view/PVS-like
culling, not exact optical visibility. A global default capture/fallback described
in the README is an artistic compatibility choice, not recovered scene evidence.
JA's new API should report missing data explicitly and preserve the unrelated
legacy renderer independently.

## 6. Memory, disk and streaming implications

### 6.1 Actual storage paths

The text `.lightgrid` contains per-area grid layout, tile/border information and
point validity/origins. Normal SH I/O is disabled; illumination is in separate
`env/<map>/areaN_lightgrid_amb.exr` atlases. The generated `.blightgrid` uses
version/magic and big-endian I/O helpers with timestamp validation. Map CRC checking
is present as commented code; this is not the content-complete invalidation policy
required by JA's reconstruction cache. [I/O][grid] L453–845

HDR bake buffers/output use RGB16F. `TD_HDR_LIGHTPROBE` selects a BC6H runtime image
path; generated image handling can also accept an R11G11B10 alternative. Lossy
runtime compression and bake FP16 rounding are different from SH truncation,
historic LDR quantization and physical calibration. None of the RBDOOM codecs was
executed here. [Image usage][images] L231–235; [binary image][binaryimage]

The loader loops through all portal-area grid images, with deferred image loading;
the inspected path is not evidence of demand-streamed, leased CPU field chunks.
Area boundaries are useful **partition candidates**, not a ready-made JA ownership
or streaming architecture.

### 6.2 Costs (uncompressed payload estimates)

| Representation | RGB bytes/support | Meaning |
|---|---:|---|
| SH L1 FP32 | 48 | 4 RGB coefficients |
| SH L2 FP32 | 108 | 9 RGB coefficients |
| SH L4 FP32 | 300 | 25 RGB coefficients |
| SH L8 FP32 | 972 | 81 RGB coefficients; our mathematical comparison only, not supported by the pinned RBDOOM evaluator |
| 16×16 RGB16F tile | 1,536 | Dense angular atlas tile, including its layout overhead |
| Same 16×16 tile in BC6H | 256 | Block payload only; assumes compatible tile/block arrangement, excludes other resources/mips |

At 16,384 dense slots, RGB16F atlas pixels alone are **24 MiB/area**; 300-byte
SH payloads alone are 4.6875 MiB. The declared point type also has position/validity
and retains its SH member even with serialization disabled (about 316 bytes under
ordinary 4-byte alignment; an estimate, not a measured cross-platform `sizeof`).
Invalid slots still occupy the regular point/atlas layout.

Capturing six RGB16F faces at size S costs `36*S²` bytes/probe before conversion.
For an **illustrative S=256**, that is 2.25 MiB/probe. Holding all 16,384 valid
captures would be **36 GiB** before other allocations. The inspected bake appends
area capture jobs before processing/freeing that area's buffers, so per-area scope
is not a sufficient small-peak-memory guarantee. Actual peak RSS, capture-size
configuration and GPU resources were not measured here; these are layout costs,
not reported full-map performance.

For JA use existing chunked datasets, bounded support batches and geometry/source-
independent caches where justified. Content keys include geometry/material/bake
interpretation, source/calibration generation, support positions, basis/codec,
convolution, components and algorithm version. No filename/timestamp-only acceptance.
Partial/missing chunks must not become valid black lighting.

SH evaluation/storage cost grows as `(L+1)^2`; a fixed basis permits interpolation
of coefficients followed by one angular evaluation. This commutes with *linear*
evaluation, not with per-support clamping. RBDOOM's clamp-before-atlas-interpolation
is an additional operation. SG sums are positive and compact for isolated lobes,
but require axes/widths and lobe correspondence; interpolating those parameters
is not equivalent to linear SH coefficient interpolation.

## 7. Numerical comparison actually executed

The [standard-library prototype](../tools/rend2/rbdoom_research/field_basis.py)
implements a generic associated-Legendre recurrence, not the project's expanded
SH code. Its basis/index/phase is tested against cardinal values and numerical
orthonormality. It compares original incoming radiance and diffuse irradiance
separately, in **synthetic relative-linear RGB**, not stock-map physical units.

The input is three differently coloured normalized spherical-Gaussian lobes with
sharpness 32 plus constant radiance .015. SH projection uses 40×80 sphere nodes;
257 separate deterministic directions measure error. An independent hemisphere
irradiance reference integrates azimuth analytically with I0, then uses Gauss
quadrature in the remaining cosine coordinate. This avoids the clamped-cosine
crease of naive full-sphere quadrature. On checked normals, 48/64-order reference
values differ by at most **3.11e-15**; this is a convergence check, not a universal
certified error theorem.

| Max degree | RGB coefficients | Incoming-radiance relative RMS | Diffuse-irradiance relative RMS |
|---|---:|---:|---:|
| L1 | 4 | 92.93% | 28.45% |
| L2 | 9 | 86.13% | 5.42% |
| L3 | 16 | 76.98% | 5.42% |
| L4 | 25 | 66.71% | 1.75% |
| L8 | 81 | 27.54% | .219% |

These are angular representation errors for one controlled distribution, not a
map benchmark or emitter-location error. Colours remain RGB, not grayscale.
Higher angular order does not repair spatial interpolation through a wall.

Additional controls:

- An angular delta source has exact `E(n)=max(0,n.z)` for unit integrated input.
  Irradiance RMS errors are 35.36%, 8.84%, 4.42%, 1.93% at L1/L2/L4/L8. L1
  predicts about -.249 on the dark side. Finite-order ringing persists at higher
  orders; clipping changes the function and can bias energy rather than restore
  lost angular detail. Pointwise radiance RMS for a delta is undefined, so none
  is reported for this control.
- Three **known** SG lobes plus their known constant reproduce the generating
  radiance exactly by construction. This is an oracle capacity control, not a
  fitted source/unknown-shape comparison or proof that SG is always preferable.
  Minimal axis-angle/width/RGB storage is 21 floats including the constant; explicit
  3-vector axes use more. Axis/type/count discovery is not implemented.
- Rounding SH coefficients to FP16 adds about `8e-5` absolute irradiance RMS in
  this fixture; this does not measure the separate RGB16F atlas or BC6H codec.
- A valid black sample and valid white sample with equal weights interpolate
  to **50** for values `(0,100)`. Brightness rejection plus renormalization yields
  **100**. Explicit availability is therefore required even if the heuristic
  sometimes hides invalid wall points successfully. No-valid-support stays
  unavailable, not a measured zero field.

The independent radiance reference used for SG is
`G(w)=lambda*exp(lambda*(axis·w-1))/(2*pi*(1-exp(-2*lambda)))` with unit sphere
integral. Its amplitude is integrated angular radiance, not physical source
candela. With `nu=n·axis`, its diffuse integral reduces to

```text
(R5) E_G(n) = lambda/(1-exp(-2*lambda)) * integral_0^1
                 u * exp(lambda*(u*nu-1))
                 I0(lambda*sqrt(1-u²)*sqrt(1-nu²)) du
```

This is our numerical reference derivation, not a ported RBDOOM routine. The
source fixture parameters belong to the test harness; no blind solver or JA
consumer receives/uses them.

## 8. Why explicit lights and geometry still matter

Diffuse convolution legitimately smooths angular lighting, explaining why L2
often performs far better for E than for Li. It does not locate physical lights.
At one point, distance/intensity trades can produce identical incoming directions
and irradiance. Reflected surfaces, windows and sky can reproduce lobes without
nearby physical emitters. Multiple coloured directions can coexist in RGB SH;
the limitation is angular/spatial bandwidth and incomplete evidence, not a ban on
multiple colours.

Sharp spot cones create sharp **spatial** changes across receivers. Point lights
also create strong near-field inverse-square/parallax variation, while cast
shadows depend on source/receiver geometry. A diffuse lobe at one point can be
broad even while that spatial signal is sharp. Merely increasing SH degree cannot
restore a missing emitter position, shadow caster, wall, sampling location or
historical bake profile. Low-order SH must not become the sole model for direct
shadow/specular/volumetric consumers.

SH L1/L2/L4, SG or octahedral maps can represent conditional indirect/unresolved
fields; they do not certify recovered point/spot/rectangle/tube/sun sources. Keep
explicit physical sources separate and validate field contributions against the
source/bake/material hypotheses. Do not let a flexible field suppress real emitters
as demonstrated by the earlier GI controls.

## 9. Ideas by subsystem

| Idea | Inverse solver / observations | Future consumers / field storage |
|---|---|---|
| Local bounded grids | Region-scoped work/sampling budgets and coverage diagnostics; raw observations retain identity. | Sparse/local support and independently loadable chunks. |
| BSP area/portal association | Useful hints/acceleration; neither room truth nor optical visibility. Keep exact CPU rays and surface sides. | Support selection and optional state-aware blending, with explicit fallback. |
| SH projection + solid-angle weighting | Testable angular operator for genuinely sampled radiance; never expand one historical grid vector into measured SH. | Compact low-frequency diffuse representation; choose order by error. |
| Radiance→irradiance convolution | Necessary semantic/normalization contract; independent material uncertainty. | Decode to the declared query quantity, avoiding duplicate pi factors. |
| Nudged capture supports | Only new evaluated supports; never move old grid RGB. | May improve coverage with displacement/geometry provenance. |
| Separate diffuse and glossy probes | Prevent using an irradiance fit as proof of specular/source correctness. | Different quantities, bandwidth, roughness and calibration metadata. |
| Renderer cubemap baking | Not original JA observations, not an inverse algorithm. | Optional future *validation/capture* workflow under fixed scene inputs. |
| Darkness rejection, first valid area/global default probe | Inappropriate as valid geometric/photometric evidence. | Presentation fallbacks only, explicitly labelled approximate. |
| GPU atlas, cvars, BSP-world ownership, runtime material pipeline | No reason to copy. | Renderer adapters may choose their own layout after canonical payload validation. |

## 10. Representation-neutral JA requirements

Keep `TransportField`/`Dataset` descriptors and immutable API snapshots. A field
codec must state, without relying on its filename:

1. **Quantity and normalization:** incoming radiance, irradiance, convolved E/pi,
   or roughness-filtered radiance; convolution status, angular basis/directions,
   degree/band count, coefficient ordering/phase and world frame. CPU evaluators
   convert payload conventions to the declared canonical query result.
2. **Units and provenance:** scene-linear RGB/primaries; Relative/Lux/Nits and
   existing calibration ID. Do not infer absolute scale or transfer from EXR/SH.
3. **Spatial support:** actual positions, nominal lattice if applicable, stable
   IDs, bounds, region/surface side and optional displacement. Regular, sparse,
   adaptive and unstructured supports remain possible.
4. **Availability and validity:** absent/invalid/unknown versus valid black;
   geometric certification and optical assumptions separate from RGB confidence.
   No supported samples yields `Unavailable`/explicit approximation, not fake data.
5. **Components:** direct-local/direct-sun/sky roots, reflection orders and
   unresolved ambient/discrepancy membership; Include/Exclude/Unknown states to
   prevent double counting. An irradiance field does not expose physical emitters.
6. **Interpolation policy:** region/side constraints, cross-region openings,
   exact/approximate visibility, actual moved coordinates, boundary handling,
   clipping order and error budget. Unknown region membership must not globally
   prevent valid source→luxel geometry evaluation.
7. **Lifetime/cache:** versioned content keys and bounded chunks/leases; no GPU
   atlas coordinates, handles or source-array indices in canonical identity.
   Geometry/source/material/profile/calibration changes invalidate derived data.
8. **Consumer capability:** metadata presence differs from `IndirectSampling` or
   `IncomingRadianceSampling`; diffuse, glossy, fog and entities independently
   request supported quantities. No mandatory fog/Forward+/PBR switches.

These are requirements for future field codecs/evaluators, not a claim that every
listed metadata member already exists. The current canonical descriptors and
capability boundary remain unchanged; missing evaluators still return Unavailable.

Recommended first experiment for JA: retain a high-quality representation-neutral
CPU reference, benchmark nine-RGB L2 and 25-RGB L4 **diffuse** encodings on certified
local supports, and report angular plus spatial held-out error, ringing, leakage,
missing coverage and cost. Add SG/directional radiance only when measured bandwidth
and consumer needs justify it. This is a benchmark choice, not a canonical SH-only
schema or production implementation commitment.

Region-aware chunks should preserve certified geometry boundaries while allowing
supported transport through openings. A bright room lit through a doorway still
needs no new local emitter; a sealed wall must remain blocked. Retain the existing
exact/unknown geometry policy and conditional GI attribution. The old failed
lightgrid-source reconstruction must not reappear under an SH representation.

## 11. Validation record and remaining questions

Executed: 13 new deterministic standard-library tests, 12 prior DDGI tests and all
17 existing CPU CTest targets. The tests cover basis/count/phase, orthonormality,
convolution, constant E/pi normalization, colour/angular error, ringing, FP16
coefficient error, explicit validity and repeated numerical witnesses. The source
fetch verifier checked all 43 SHA-256 **and** pinned Git blob identifiers.

Commands and source-reading setup are in the
[tool README](../tools/rend2/rbdoom_research/README.md), with numerical data in
[results.json](../tools/rend2/rbdoom_research/results.json). Source files and third-
party game data are not redistributed. The prototype uses generic mathematical
recurrences written here and existing own-code quadrature; RBDOOM implementation
code/assets are not copied. Its repository license and exceptions are retained as
source references, without claiming a code-port licensing decision.

See the pinned [GPL license](https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5/LICENSE.md)
and [exceptions](https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5/LICENSE_EXCEPTIONS.md).
Any later code reuse requires its own licensing review; this deliverable adopts
mathematical ideas and tests their semantics without importing implementation.

Not executed or inferred: RBDOOM build/game/bake success, GPU shader matching,
six-face capture/orientation/seam validation, runtime codec errors, actual large-map
RSS/streaming costs, physical SI calibration of Doom or stock JA, exact multi-bounce
transport, source recovery or a new JA field consumer. No shared production code
changed; no new SP/MP build is required or claimed for this research.

Before field implementation: test area-overlap/concave-centre and nudge cases,
same-area thin walls, cross-area doorways, dynamic door states, valid near-black
lighting, missing chunks, face/phase axes, atlas borders, normalization/clamp order,
finite-source near-field variation and spatial probe density independently of
angular order. Use explicit ground truth and held-out positions. Source inspection
establishes architectural lessons and testable hypotheses, not production accuracy.

[readme]: https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5/README.md#irradiance-volumes-aka-light-grids
[grid]: https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5/neo/renderer/RenderWorld_lightgrid.cpp
[types]: https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5/neo/renderer/RenderWorld_local.h#L63
[common]: https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5/neo/renderer/RenderCommon.h#L478
[sh]: https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5/neo/idlib/math/SphericalHarmonics.h#L36
[env]: https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5/neo/renderer/RenderWorld_envprobes.cpp
[lookup]: https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5/neo/shaders/builtin/lighting/ambient_lightgrid_IBL.ps.hlsl#L235
[bounds]: https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5/neo/renderer/RenderWorld_load.cpp#L1053
[point]: https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5/neo/renderer/RenderWorld.cpp#L1240
[select]: https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5/neo/renderer/tr_frontend_addmodels.cpp#L815
[frontend]: https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5/neo/renderer/tr_frontend_main.cpp#L497
[populate]: https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5/neo/d3xp/Game_local.cpp#L1265
[entity]: https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5/neo/d3xp/EnvironmentProbe.cpp#L79
[backend]: https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5/neo/renderer/RenderBackend.cpp
[images]: https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5/neo/renderer/Image_load.cpp#L231
[binaryimage]: https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/ea29c006e84fedcc0e7c173c383a087ce0a8c0d5/neo/renderer/BinaryImage.cpp
