# JA baked-light forward models, specification v1

Investigation and controlled checks: **2026-10-26**. This specifies compiler-relative
lighting for a future inverse solver. It changes no renderer, decoder, PBR,
brightness, or reconstruction-consumer code. A complete bake emulator and light
reconstruction are outside this implementation.

**There is no verified universal stock-JA bake profile.** The accessible sources
define several different models. Matching RBSP v1, shader keywords, or a game's
release date does not identify a compiler. Select a profile per BSP and its
sidecars, with evidence and uncertainty per feature. In particular, do not use
one fitted brightness gain to disguise an incorrect attenuation law.

## Reference versions and historical confidence

The immutable [source lock](../tools/rend2/bake_model/source-lock.json) records
repositories, commits, paths, and LF-normalized SHA256s. Source files fetched for
inspection are local under ignored `build/bake-model-sources/`; they are not
vendored or required to run the numerical harness.

| Profile/family | Reference and evidence | Confidence and limits |
| --- | --- | --- |
| `Q3MapOriginal` | id Software Quake III source, commit `dbe4ddb10315479fc00086f08e25d968b4b43c49`; `q3map/light.c` | Source-inspected ancestor; **not** a JA RBSP compiler and not a stock-JA identification. Useful counterexamples include grid spot handling and ambient fudge. |
| `Q3MapVlightFragment` | Same release, `q3map/lightv.c` | Source-inspected alternative attenuation fragments only. Different flag meanings; not an executable JA profile. |
| `RavenProductionUnknown` | Raven/SoF2map lineage is a plausible stock-SP/MP candidate. The q3map2 parser explicitly attributes bare `sun` to sof2map; its abstract BSP comments attribute style arrays to sof2map. See [shader parser][N-S], [BSP abstraction][N-BA]. | **Hypothesis about stock production**, not recovered production compiler source, executable, command line, or formula verification. Leave its formula fields unknown. SP and MP may differ, as may levels and patches. |
| `Q3Map2-2.5.16-JA` | id Software GtkRadiant GPL snapshot `0991a5ce8b8ccb86a5b0d54c089369d84f53dc10`; header says 2.5.16, changelog dates it 2004-10-18; `game_ja.h` selects RBSP v1 | Source-supported candidate with exact formulas below; **not executed here** and not proof of any stock bake. This is a later reference, not a recovered 2003 production snapshot. |
| `NetRadiant-d9338309-JA` | Xonotic NetRadiant `d9338309ea8fab75027b7382efaabe034302b862`, 2022-06-28; tested Windows executable `2.5.17n-git-d9338309e`, SHA256 `894049bd136e7828b0f068d12c932833153b3ada87b99655dac90e4a60c85ad4` | High confidence for **specified controlled kernels and options**: 22 actual BSP bakes below. Not full raytracer, patch, style, flood, HDR, or all-options verification. Never assign to stock maps merely because these tests pass. |
| `UnknownProfile` | Missing trustworthy compiler/recipe | Unverified. Preserve encoded observations; evaluate explicit candidate hypotheses separately. |

The upstream [q3map2 changelog][G-H] is particularly useful evidence against
back-projecting modern behavior: sun jitter/`q3map_sunExt` appeared in 2.5.8
(2003-10-02); `_anglescale` in 2.5.11 (2003-12-01); command-line gamma and
unchanged grid direction during bounce in 2.5.12 (2004-01-18); compensation
and per-game defaults in 2.5.13; revised skylight distribution in 2.5.14; and
`-nostyles` in 2.5.16. These dates document that branch's features, not Raven's
internal feature availability or production build dates. Historical pre-2.5.14
skylight cannot simply use the sampler specified here without a distinct
assumption. The supplied [shader manual](https://quake.zone/docs/shader.html)
is useful vocabulary; compiler source takes precedence when behavior differs.

A read-only inventory of the user's licensed installed content checked `hoth2`,
`hoth3`, `mp/ffa1`, `mp/ctf1`, and `mp/duel1`: all have **zero retained entities
whose classname begins with `light`**, and no `_q3map2_version`,
`_q3map2_cmdline`, or `_keepLights` worldspawn keys. Hashes and counts are in
[stock-inventory.json](../tools/rend2/bake_model/stock-inventory.json); no asset,
entity text, texture, or map is redistributed. This is a five-map observation,
not an exhaustive stock audit, evidence that no original lights existed, or a
compiler fingerprint. All five remain unverified `UnknownProfile`.

## Mathematical domain and exact source index

Coordinates and all numeric distances below are **JA world units**, Z up. Let
`x` be the compiler's actual receiver sample, `n` its unit shading normal,
`L` the source origin, `v=L-x`, `r=length(v)`, and `u=v/r` point **toward the
source**. Propagation is `-u`. A spot's unit axis `a` points from its origin
toward its target. `c` is compiler RGB, `I` the authored light scalar, `P` the
internal scalar called `photons`, and `T` the falloff tolerance. These names do
not establish photon counts, watts, candela, lux, or spectral primaries.

Unless a row says otherwise, the following formulas are literal algebraic
transcriptions of the pinned NetRadiant branch for positive, finite inputs,
with binary visibility and no texture filter. Compiler float arithmetic and
truncation can differ from Python doubles by a byte. The linked functions
define ordering, exceptional cases, and geometry tests; formulas are not a
replacement for its trace and sample-placement code.

| Operation | Formula / rule | Exact source location (one-based lines at pinned revision) |
| --- | --- | --- |
| Entity units | `P = I * scale * pointScale / samples`; default `I=300`, `scale=1`, `pointScale=7500`; spots use `spotScale=7500` | [N `light.c` CreateEntityLights, 217-534][N-L]; [N `q3map2.h`, 2345-2373][N-Q] |
| Entity chromaticity | `c = rgb / max(rgb)`; all-zero `ColorNormalize` returns white; default white. Optional sRGB decode precedes normalization; flag 32 bypasses normalization | [N `light.c`, 399-425][N-L]; [N `mathlib.c` ColorNormalize, 204-226][N-M] |
| Point distance | `d=max(16, sqrt(r*r+extraDist*extraDist))`; 2.5.16 uses `max(16,r)` | [N `light.c`, 932-1028][N-L]; [G `light.c`, 843-879][G-L] |
| Standard angle | `A=n dot u` if angle attenuation is enabled, otherwise `1`; two-sided uses its absolute value; then optional `A=min(1,A/_anglescale)` | [N `light.c`, 949-987][N-L]; [G `light.c`, 857-869][G-L] |
| Half Lambert | For enabled angle attenuation: `h(t)=(0.5*min(t,1)+0.5)^2` when `t>0.001`, otherwise `0`; use `h(t)` before `_anglescale` | [N `light.c`, 962-985, 1085-1103][N-L] |
| Inverse square point/spot | `s=max(0,P*A/(d*d))`; RGB `s*c` | [N `light.c`, 989-1028][N-L] |
| Linear point | `s=max(0,A*P/8000 - d*fade)`; **not** `A*max(0,P/8000-d*fade)` | [N `light.c`, 990-1009][N-L]; defaults [N `q3map2.h`, 2373][N-Q] |
| Point envelopes | Default fast cutoff `R=sqrt(P/T)` for inverse square, `(P/8000-T)/fade` for linear; nonfast linear `P/(8000*fade)`; nonfast inverse square uses large world bound | [N `light_ydnar.c` SetupEnvelopes, 3538-3850][N-Y] |
| Spot cone | `z=(x-L) dot a`; `Rz=((radius+16)/targetDistance)*z`; `rho=length(x-L-z*a)`; reject `z<0` or `rho>=Rz`; multiplier `min(1,(Rz-rho)/32)` | [N `light.c`, 452-469, 1031-1063, 1422-1448][N-L] |
| Polygon form factor | `F=(1/(2*pi))*sum_i [ n dot normalize(d_i cross d_(i+1)) * acos(clamp(d_i dot d_(i+1),-1,1)) ]`, `d_i=normalize(w_i-x)` | [N `light.c` PointToPolygonFormFactor, 708-763][N-L]; same signed integral [G `light.c`, 672-724][G-L] |
| Shader area source | `P=V*area*0.25`; `add=V*3*0.25`; exact scalar `s=F*add` | [N `light_bounce.c`, 550-583][N-B]; constants [N `q3map2.h`, 2347, 2372][N-Q] |
| Approximate area | Surface `s=P*A*(-normalEmitter dot u)/d^2` with sidedness checks; grid `s=P/d^2` (no emitter cosine) | [N `light.c`, 849-892, 1352-1362][N-L] |
| Sun | `s=P*A` on surfaces, `s=P` in grid; no distance attenuation. `u=(cos az*cos el,sin az*cos el,sin el)` | [N `shaders.c`, 1267-1337][N-S]; [N `light.c`, 1067-1153, 1450-1473][N-L] |
| Bounce patch | `B=500*shaderBounceScale*0.375`; `P=B*area*globalBounceScale`; `add=B*3*globalBounceScale`; RGB is sampled texture times incoming previous-pass light divided by `255^2` | [N `light_bounce.c` RadSample, 265-419; RadSubdivideDiffuseLight, 430-625][N-B] |
| Grid split | `m=normalize(sum length(C_i)*u_i)`; `q_i=max(0,g0+(g1-g0)*(u_i dot m))`; `D+=q_i*C_i`; `A+=.25*(1-q_i)*C_i + explicitAmbient_i` | [N `light.c` TraceGrid, 1520-1799][N-L] |
| Grid direction encode | `polar=int(acos(m.z)*255/(2*pi)) & 255`; `azimuth=int(atan2(m.y,m.x)*255/(2*pi)) & 255`; polar poles special-cased | [N `mathlib.c` NormalToLatLong, 452-481][N-M] |
| RGB encoding | Gamma -> shared-peak normalization or exposure -> divide compensation -> optional sRGB -> byte conversion | [N `light_ydnar.c` ColorToBytes, 47-149][N-Y]; [G `light_ydnar.c`, 47-92][G-Y] |
| Grid lossy merge | First matching existing record, exact four style bytes; each RGB difference `<=4`; angle difference accepted when `<=4` **or `>=251`** | [N `bspfile_rbsp.c` AddLightGridLumps, 84-199][N-R]; [G same function, 84-195][G-R] |
| Deluxe, modern | Accumulate intensity/colour-weighted incoming direction plus normal bias; normalize and store `trunc(127.5+127.5*m)` | [N `light.c`, 778-791, 1167-1205][N-L]; [N `light_ydnar.c`, 2137-2165][N-Y]; [N `lightmaps_ydnar.c`, 2267-2275][N-LM] |
| Vertex | Direct sample when vertex-lit; otherwise search first nonempty superluxel square and average; accumulate bounce passes, encode with ColorToBytes | [N `light_ydnar.c` IlluminateVertexes, 2739-3117][N-Y] |

The alternative original id **vlight** path must not be confused with
q3map2's `-q3` selector. In its radial surface branch, for positive
`t=min(n dot u,1)`, angle flag 4 changes t to `1-(1-t)^2`; flag 8 changes it to
`1-(1-t)^3` despite the name `LAAT_DOUBLEQUADRATIC`. Then divide by positive
`atten_anglescale` and cap at one. With `d=max(16,r)` and positive
`f=atten_distscale` (default one), radial scalar is `P*A/(d*d*f)`, or
`max(0,A*P/8000-d*f)` for flag 1. Flag 2 selects `s=A`, but this particular
branch subsequently rejects `s<=1`, so ordinary unit-normal samples in this
no-scale mode are discarded; do not turn that fragment into a useful
constant-intensity emitter by silently dropping the threshold. The spot branch
places its cone factor inside the linear intercept, before distance subtraction.
Other vlight branches differ. These are source-supported counterexamples, not
an implemented whole vlight profile or a plausible-by-default stock JA model.
[id q3map/lightv.c 3397-3472, 3538-3585, 5014-5029][Q-V]. A Quake-1-style 1/r
or additive-radius attenuation must likewise be a distinct sourced profile;
none is inferred from q3map2's word "nonlinear" (which means inverse square
in the point formulas inspected here).

## Point, spot and visibility semantics

For q3-style mode (`game_ja.h` has `wolfLight=false`, overridable by `-q3`):
default is inverse square with angle attenuation. Entity spawnflag bit 1
selects linear **and disables angle attenuation**; bit 2 disables angles.
`_anglescale != 0` re-enables angles. In `-wolf`, default is linear without
angles; bit 1 selects inverse square with angles, and bit 2 enables angles.
Both modes keep the same 7500 and 1/8000 constants. `fade` is read for linear
lights, zero means one. Targeted spots force inverse square, angle attenuation,
and fade one even when bit 1 requested linear. Missing targets leave point
lights. Zero radius/target distance fall back to 64. [_Source_: CreateEntityLights][N-L].

Other flags are independently significant: bit 16 disables grid contributions;
`lightJunior*` classnames are grid-only. Modern bit 32 retains raw RGB magnitude.
Bit 64 sets distance attenuation, already on by default. Bits 4/8 set
`LIGHT_DARK`, which is **not** the `LIGHT_NEGATIVE` flag; the inspected lighting
paths do not turn it into subtraction. Negative authored photon/add values are
converted to magnitudes with `LIGHT_NEGATIVE` in SetupEnvelopes, then subtracted
by the accumulation paths. Negative sources are not physical emitters. Do not
import Quake/vlight flag definitions into these profiles. [Flags/defaults][N-Q].

The 16-unit clamp affects only attenuation distance. It does not change `u`,
the raw envelope distance, or spot-cone coordinates. Modern `_extradist` (or
`-extradist`) adds an extra squared-distance term; this is not a geometric
finite-emitter integration. The helper kernels handle ordinary positive inputs;
they do not silently invent behavior for malformed scales or byte casts.

The surface envelope rejects `r>=R`; grid rejects `r>R`. With fast flags, after
cone/angle evaluation a nonpositive scalar or `s<=T` is discarded **before RGB
multiplication**, so colour channels do not each have independent envelopes.
`T=1` by default; entity multisamples divide both I and T by sample count.
`-nofastpoint` alone does not remove the source's default `LIGHT_FAST` flag.
`q3map_noFast`, per-source flags, and SetupEnvelopes determine actual cutoff.
Envelope bounds are also clipped to PVS-visible leaves and source orientation.
Suns have special large bounds, not point-source radii. Fast area envelopes
search along emitter normal starting at 100 in steps of 10 until `abs(F*add)<=T`;
this discrete search is another approximation. [SetupEnvelopes][N-Y].

Even with angular attenuation disabled, the surface path first rejects a
non-sun source origin behind a one-sided receiver plane. Two-sided handling,
surface bounds and emitter-plane tests remain; omitting cosine is not a license
to illuminate a wall through its back. Smooth normals, shader normal maps and
`q3map_shadeAngle`/`-shade` affect the normal used in the bake. Runtime PBR
normals need not equal those compiler normals.

Visibility is compiler tracing, not solely ray/triangle intersection: source
and receiver PVS tests, epsilon clusters, inhibit spheres, nudges, patch-shadow
selection, model shadow flags, sky and material compile flags matter.
`TraceLine` may filter RGB through `C_LIGHTFILTER` texture RGB/255 and
`C_ALPHASHADOW` inverse alpha `(255-alpha)/255`; translucent surfaces without
either flag generally do not shadow. Sun rays must report `C_SKY` and not
opaque; a no-hit ray is not positive sky evidence. `-notrace`, forceSunlight,
`-nosurf`, `-patchshadows`, and `_skybox` can change this. See
[N light_trace.c TraceLine/TraceTriangle, 935-958, 1478-1567, 1723-1779][N-T].
The grid non-sun path tests `passSolid` at the end, while the surface path tests
`passSolid || opaque`; these are separate source paths, not a shared physical
visibility function.

## Surface emission and finite polygon lighting

`q3map_surfacelight V` produces compiler sources from visible compiled surface
triangles/patches. The emission colour comes from `q3map_lightRGB` or the
normalized average of the chosen `q3map_lightImage`/fallback image. This is not
an integral over an arbitrary runtime material graph. Autosprite surface lights
become points at surface bounds centre with `P=V*pointScale`. [CreateSurfaceLights][N-L],
[shader image averaging, 769-817][N-S].

Ordinary surfaces become subdivided polygon lights. `q3map_lightSubdivide`,
compiler subdivision defaults, patch tesselation, gradient subdivision and source
triangulation change the number, positions and visibility rays of those lights.
The default area scale is .25 and the exact-form-factor value scale is 3.
Retain them independently of pointscale. A more physically named setting is
not evidence that V is emitted radiance.

The signed edge formula above skips edges with cross-product length <.0001,
returns zero if the running integral exceeds +/-6.3, and multiplies by the
literal constant .159154942. It does not explicitly clip each edge to the
receiver horizon. Use the winding orientation and the branch's sign handling.
Exact surface code evaluates F with the receiver normal; exact grid code uses
the direction **toward that polygon's source origin** as the integration normal.
This grid quantity is thus not scalar incident irradiance at a known normal.

For area emitters, let `h=x dot normalEmitter-light.dist`, with light origin
already nudged one unit outward and `light.dist=origin dot normalEmitter`.
Surface code rejects one-sided emitters at `h<-1` when `h<3`; rejects nearly
coincident, similarly oriented receivers if `-3<h<3` and normals dot >.9.
For `-8<h<8`, it evaluates F at
`x' = x + (8-h)*normalEmitter`, pushing the receiver onto that offset plane.
The trace still starts from the original sample toward the source centre.
Negative F is rejected unless receiver/source sidedness permits a sign flip;
surface code may then shift the trace endpoint two units across the emitter
plane. Grid uses its own behind-plane/sign rules. [LightContributionToSample/Point][N-L].

Exact polygon lighting uses one visibility ray per subdivided polygon, not
visibility integrated over every emitter point. Subdivision can therefore alter
shadows even though the unoccluded edge integral is additive. `-faster` replaces
the edge integral with the area-at-centre formulas in the table. Modern faster
area attenuation also clamps/softens distance like a point; 2.5.16's surface
faster branch directly divides by raw `r^2` at [G light.c 790-813][G-L]. Its grid
faster branch does clamp to 16. This distinction can create very large near-field
differences; do not fit it with an arealight intensity gain.

Surface emission may additionally create artificial point **backsplash** sources:
on an original unsubdivided triangle, default fraction .05, distance 23 beyond
the already one-unit-nudged area origin, `P_splash=.05*P_area`.
`q3map_backsplash percent distance` changes or disables this. It has no additional
7500 multiplier. Backsplash is direct-bake machinery, not transport bounce.
The area fixture explicitly disables it. [RadSubdivideDiffuseLight][N-B].

## Suns, sky, and ambient contributions

`sun`, `q3map_sun`, and `q3map_sunExt` are compiler directives in the inspected
branches. Parse azimuth/elevation in degrees, then use the vector in the table.
Brightness does not attenuate with map position or distance. Each used shader's
linked sun list is instantiated once, then cleared; separate suns and used sky
shaders can add. It is not one sun per sky triangle. A declaration on an unused
shader is not automatically a world light. `_noshadersun=1` suppresses shader
suns in this path, not the subsequent skylight branch. Targeted entity lights
with `_sun=1` take a separate path and invert their target axis.

**Sun RGB differs across versions.** 2.5.16's shader parser uses Euclidean
`VectorNormalize(rgb)`, while NetRadiant uses `ColorNormalize(rgb)` (max channel).
For white, the same 120 scalar yields `120/sqrt(3)` per channel in the former
and 120 in the latter. Entity suns inherit the entity's normalization, so even
within 2.5.16 entity suns and shader suns need separate conventions.
See [G shaders.c 1232-1297][G-S], [N shaders.c 1272-1337][N-S]. The local runtime
shader parser also uses Euclidean sun normalization; its parsed `tr.sunLight`
is not an oracle for NetRadiant's baked intensities or multiple-sun set.

For sunExt sample count K, shader-sun photons per generated direction are
`brightness*skyScale/max(K,1)`. First sample is the nominal direction. Remaining
directions independently perturb azimuth and elevation by uniform offsets in
`[-deviance,+deviance]`, rejecting offsets outside the 2D disk
`da^2+de^2<=deviance^2`. This is **not uniform solid-angle disk sampling** and
changes distribution near the poles. Filter radius is divided by K. Exact
reproduction needs RNG implementation, seed, iteration/order and binary behavior.
The targeted entity-sun path has already divided intensity by `_samples` before
calling CreateSunLight, which divides again; do not silently fix this apparent
double division when emulating that branch. [N light.c 46-138, 374-386, 472-489][N-L].

Modern `q3map_skyLight V J` is a finite sum of directional suns, independent of
the explicit sun list. With `e=J-1`, `aSteps=4e`,
`N=4e^2+1`, each has `P=V*skyScale/N` and shader-derived colour. For row
`i=0..e-1`, elevation `(i+.5)*90/J` degrees; step azimuth `360/(4e)` degrees
across each row, retaining accumulated angle, then add another azimuth offset
`step/e` between rows. Add a final zenith sun. Each direction has sky visibility
and surface cosine. J is clamped to at least two. No solid-angle or sine-area
weight is added to the samples. J=3 gives **17 suns**, with rings at 15 and 45
degrees and a zenith source. [CreateSkyLights 147-204][N-L]. This models diffuse
outdoor gradients and skylight-aperture illumination without any local point
source. The 2004 change noted above prevents assuming this is the stock sampler.

Legacy sky `q3map_surfacelight` can also generate polygon/backsplash lighting;
it is not interchangeable with `q3map_skyLight`. Multiple skies, multiple suns,
sun jitter, coloured reflections, fog/filter materials and ambient floors can
all contribute to an outdoor gradient. A single fitted point-like sun is not
a complete candidate forward model.

World `_ambient` (fallback `ambient`) times world `_color` (default white) is
an additive surface/vertex base, not a shadowed sky integral. `_minlight` clamps
the base surface style and supplies defaults for `_minvertexlight` and
`_mingridlight`; the latter independently floors grid ambient, not directed.
These are nuisance/model terms, not inferred hidden emitters.

There is a source-order caveat for NetRadiant: `LightWorld` calls `SetupGrid`
**before** reading world ambient. The grid initializer also uses its existing
`j` when copying ambient rather than a dedicated per-style initialization loop.
Do not infer intended behavior from that comment. In the fresh-process
ambient-only fixture, `_ambient=20` gives floor RGB `(20,20,20)` while interior
grid ambient/directed remain `(0,0,0)`. This observation is verified only for
the tested binary/fixture. The analytic grid reducer's explicit initial ambient
argument is a policy input; it is not assumed to receive world ambient here.
[N light.c 1868-1885, 1911-1932][N-L].

Flood lighting is an additional compiler-specific approximation, absent from
the inspected 2.5.16 implementation. Modern SetupFloodLight constructs 64
directions: azimuth `j*360/16`, normal-relative polar angle `(k+.5)*88/4`
degrees, k=0..3. For high quality, trace all; low quality randomly retains about
10%, making seed relevant. With maximum distance H, each contributes 1 for
sky/translucent/unobstructed, or `min(hitDistance/H,1)` for opaque hits. F is
their arithmetic mean, zero if no vectors. Add `normalizedFloodRGB*intensity*F`.
This is distance-based ambient occlusion fill, not an isotropic physical sky.
Surface flood applies to the normal style, adds normal-directed deluxe weight
according to flood direction scale, and is disabled on bounce passes.
The grid gathers upper and lower hemispheres, each split 75% directional/25%
explicit ambient; **both** contributions use the local `dir=(0,0,1)` in the
inspected code even when gathering the lower hemisphere. Vertex flood has its
own dirt/flood ordering. [N light_ydnar.c 4043-4247, 4423-4486][N-Y],
[N light.c 1633-1690][N-L]. These are source-supported rules, **not numerically
validated flood fixtures in this task**. Shader-local flood and dirt settings
must remain distinct from world flood.

## Direct, bounce, direction and vertex accumulation

The surface lightmap is a sum by style of additive ambient, visible direct
point/spot/sun/polygon/backsplash sources, optional flood, and subsequent
radiosity passes. Receiver albedo is **not multiplied into its own stored
lightmap**. In contrast, its material image/alpha affects light emitted during
the next bounce and the RGB seen by other receivers.

Radiosity in the inspected branch is discrete patch transport:

1. Store/downsample the current floating superluxels into a previous-pass
   radiosity buffer, separately from the accumulating final BSP luxels.
2. Remove the previous source list and create secondary polygon sources from
   material texture times previous-pass lighting, sampled as `RGB/255 * L/255`.
   Lightmapped patches use bounded barycentric sample triples i,j,k=1..5,
   normalized by i+j+k, at most 150 samples. Non-lightmapped patches use vertices.
   Average samples and subdivide on geometric size/colour gradient. Samples
   below .0001 vector magnitude and areas outside [1,20000000] are discarded.
3. Set B/P/add by the table's empirical constants (187.5 at unit bounce scales),
   nudge origin one unit outward, trace them through the same area-source path.
4. Add that pass to final lightmaps/vertex accumulators. Set global ambient to
   zero and disable world flood during bounce; ambient is not re-added each pass.
   `-bounceonly` omits first-pass direct storage, not the direct transport needed
   to generate the first reflection. Material/CLI bounce scales affect emission,
   independently of pointscale. [LightWorld][N-L], [RadSample/RadLight][N-B],
   [StoreSurfaceLightmaps][N-LM].

This is not a converged energy-conserving Lambertian solver; the constants,
sampling, material approximation, light thresholds and patch centre visibility
are part of the authentic model. `-faster` changes the secondary area evaluation
too. Texture data and compile-time shaders can be missing or differ from modern
PBR base colour. Exact reconstruction of bounce from geometry alone is impossible.

The grid begins with `LightContributionToPoint`, with **no receiver cosine**.
Points use the same d/linear laws; spots still have the cone; suns are constant;
areas use the separate formulas above. Collect at most 32767 contributions,
including style. Weight the dominant direction by **Euclidean RGB magnitude**,
not Rec.709 luminance. Default `g0=0`, `g1=1`; CLI
`-gridambientdirectionality` and `-griddirectionality` change them. Clamp q below
at zero, not an invented post-hoc normalization. One direction is shared across
all four style slots. Thus grid ambient is not a pure indirect-light probe and
grid directed is not an individual source intensity.

For 2.5.16, add `.125*D` to ambient after first-pass split; do not repeat that
fudge during bounce. Original id q3map adds `.25*D`, and its grid point/spot
function has **no spot cone** despite surface spots having one. NetRadiant
disables the directed-to-ambient fudge, scales ambient by `gridScale*gridAmbientScale`
and directed by `gridScale`, then forces **each** encoded ambient channel to at
least one if that style's floating directed RGB is nonzero. The one-source
modern grid may therefore store `(1,1,1)` ambient even when its mathematical
split ambient is zero. [G TraceGrid 1392-1459][G-L], [original q3map 1487-1574,
1669-1737][Q-L], [N TraceGrid][N-L].

`-bounce` defaults to zero; **`-bouncegrid` independently defaults false**.
Without it grid data is direct-only even if surface/vertex lighting has bounce.
With it, TraceGrid adds to persistent ambient/directed arrays. NetRadiant's
`gp.dir` persists as the weighted unnormalized sum; this pass normalizes it to
`thisdir` and writes an updated direction. 2.5.16 normalizes its persistent dir
in place and only writes direction before bouncing, so later grid RGB can
contain bounce but stored direction stays direct-derived. These recurrences
must be versioned; calling a one-pass reducer on combined sources is not
equivalent. [N light.c 2053-2084, 1617-1620, 1691-1800][N-L], [G TraceGrid][G-L].

NormalToLatLong uses a **255-step multiplier**, truncated toward zero and masked,
with exact poles `(0,0)` for +Z and `(128,0)` for -Z. A zero vector also takes
the latter pole branch; it is not reliable directional evidence. Engine ideal
decoding uses a 256-step denominator:
`polar=b0*2*pi/256`, `az=b1*2*pi/256`,
`u=(cos(az)*sin(polar),sin(az)*sin(polar),cos(polar))`.
The actual legacy entity path additionally uses its 1024-entry/1023-denominator
sine table. Bake encoding, stored lossy merge, ideal observation decoding and
runtime table decoding are four separate stages. Do not make the compiler
encode with 256 just because the decoder uses it.

Modern deluxe accumulation differs again. For a visible positive direct source
with scalar s and source RGB c, add
`u*s*(.2989*c.r+.5870*c.g+.1140*c.b)/255` to its direction accumulator.
The base is `n*max(gray(ambient)/255,1/256)`. Ordinary bounce directions have an
additional .25 multiplier; opposite-side two-sided lighting can add colour
without a deluxe contribution. The sun branch has its own bounce minimum
direction weight. Texture shadow filtering can reduce RGB without an equivalent
chromatic update to direction contribution. Filter/downsample direction vectors
before final normalization. Store model/world-space mode 0 or transform with
the compiler tangent basis for mode 1, then RGB8 encode; do not gamma-decode
direction channels. Base style only has one deluxe accumulator, and the
illumination loop can add contributions across styles. It is not four independent
direction fields. [N LightContributionToSample][N-L], [N IlluminateRawLightmap][N-Y],
[N StoreSurfaceLightmaps 2833-2900][N-LM].

2.5.16 instead accumulates post-trace RGB-weighted direction using
`.3R+.59G+.11B`, with the fixed `n/256` bias and its own filtering path; no modern
.25 bounce-direction suppression. Zero direction storage differs as well:
2.5.16 only writes when normalization succeeds, modern writes centred bytes
for zero. These deluxe maps cannot be treated as exact first angular moments
of the incident physical radiance field. [G light_ydnar.c 1897-1908, 1983-1990][G-Y],
[G lightmaps_ydnar.c 2153-2176][G-LM].

Vertex lighting has two important paths. For vertex-lit geometry, sample at its
normal/position, with fallback nudges when invalid/dark, per-style accumulation,
dirt/flood and minVertexLight, then encode with vertexScale. For lightmapped
geometry, reconstruct vertex colour from the first nonempty expanding square of
valid floating superluxels, average by luxel sample counts, or use ambient if
none. It is not a new physical measurement independent of the lightmap.
`-cpma` can force the direct vertex path and change tracing; `-novertex` and
`q3map_noVertexLight` can preserve authored vertex RGB. Alpha may carry material
blending, not light energy. [IlluminateVertexes][N-Y].

## Lightstyles, binary representation and sampling pipeline

Retained entities are incomplete evidence. In accessible q3map2,
SetLightStyles strips `light*` entities without targetname unless world
`_keepLights=1` or BSP `-keeplights` preserves them. Targetnamed switched lights
are retained for runtime control. The LIGHT stage normally rereads source
`.map` lights because ordinary sources were stripped from the BSP; with
`_keepLights=1` it uses the retained BSP lights instead. Targetnamed BSP lights
are not duplicated from the reloaded map. Surface lights, generated secondary
patches, backsplash and sky samples are compiler-created sources, not a complete
entity list waiting to be decoded. A later `-onlyents`/entity patch or changed
shader can also leave entities/materials inconsistent with the original bake.
Retained entity presence is neither completeness nor proof of matching current
intensities. [N SetLightStyles][N-W], [N light.c 3045-3054, 246-253][N-L].

Local SP and MP `qfiles.h` agree on the relevant JA format:

| Data | Layout / interpretation |
| --- | --- |
| Header | `RBSP`, version 1, 18 little-endian offset/length lumps; format is not compiler provenance |
| Surface (lump 13) | 148-byte record; four lightmap styles, four vertex styles; four lightmap page indices/offset pairs, dimensions, origin/vectors and patch dimensions |
| Vertex (lump 10) | 80 bytes: xyz, texture UV, four lightmap UVs, shading normal, four RGBA8 colours |
| Embedded lightmap (lump 14) | RGB8 pages, normally 128*128*3 bytes for JA; no stored exposure/gamma/units flag; external maps may differ |
| Grid records (lump 15) | 30 bytes: ambient RGB[4] (12), directed RGB[4] (12), style[4] (4), polar/azimuth (2) |
| Grid array (lump 17) | Dense uint16 record references; first axis X then Y then Z; index zero is valid |

Style ID 0 is normal; 255 is no-style. Source `_style`/`style`, targetname-based
switched allocation and shader `q3map_lightStyle` determine IDs. SetLightStyles
can allocate switched IDs `32+j` by targetname/style and retain `switch_style`.
Only four slots exist. The surface/vertex sample paths skip a new style when
full, whereas grid overflow falls back to slot zero. Light order can therefore
matter. At render time style colours/animation modulate stored slots; weights
are not baked into RGB8. `-nostyles` and shader-style hacks are versioned compiler
features; inspect their path-specific implementation rather than changing IDs
in decoded evidence. Animated styles were not compiled/tested in this suite.
[N writebsp.c 348-433][N-W], [N LightingAtSample][N-L].

Compiler sample locations differ from geometric texel centres:

1. Compile brush/model/patch geometry, UV projection and smooth/perturbed normals.
   Determine sample size and per-shader/entity lightmap scale. Expand to
   superSample resolution, rasterize/remap, find PVS cluster, reject or nudge
   samples. Axial surfaces reconstruct projected coordinates; other surfaces
   interpolate explicit positions. Default lightmap sample offset is **one unit
   along the projection axis with normal sign**, or along normal for nonaxial
   maps; not invariably `x+n`. [MapSingleLuxel 443-719][N-Y].
2. Construct light lists and envelopes; evaluate direct contributions and
   adaptive samples with compiler visibility. `-samples`, random samples,
   `-super`, per-light filter radius and derivative tests change sampling.
3. Per-light filters use an integer luxel box with half weight on outer X/Y
   edges (corners .25), ignore invalid clusters and renormalize. Dirt/flood,
   hole filling, `-filter`, splotch/dark seam policies and adjacent-surface
   stitching operate at their designated stages; a single final Gaussian blur
   is not equivalent. [IlluminateRawLightmap 1808-1930, 2413-2538, 2551-2720][N-Y].
4. Average valid supersamples, preserve previous-pass radiosity buffers and
   accumulate final float lightmaps. Solid colour lightmaps can collapse to
   shared texels; equivalent maps can share pages and be stitched/packed.
   `-nocollapse` and `-approx` change observations/representation. Fill atlas
   padding after writing; padding is not a real receiver. [StoreSurfaceLightmaps][N-LM].
5. Apply output floors/brightness, encode RGB; independently normalize/encode
   deluxe vectors. Grid has its own placement, colour floors/scales, direction
   encode, then **lossy** record merging before writing.

Grid spacing defaults `(64,64,128)`, overridable by world `gridsize` (and compiler
grid-bound/size rules); it is not inferred from record count. Origin is component
`spacing*ceil(modelMin/spacing)`, max `spacing*floor(modelMax/spacing)`, count
`(max-origin)/spacing+1`; ordinal `x+nx*(y+ny*z)`. Logical positions in solids can
be sampled at undisclosed nearby points: 2.5.16 tries fixed +/-9, +/-18 corners;
NetRadiant uses random offsets at increasing fractions of grid spacing. Neither
RGB nor style nor direction independently certifies occupancy. There is no
dedicated validity bit. The repository's observation API correctly leaves
geometric validity unknown. [TraceGrid/SetupGrid][N-L], [G TraceGrid][G-L].

Both accessible RBSP writers greedily merge a cell into the **first** preceding
record with identical styles, all 24 RGB bytes within 4, and direction-byte
differences <=4 or >=251. The latter is the exact comparison, not modulo-256
geodesic distance; even polar bytes use it. Store the existing record unchanged.
Maximum dictionary allocation is `min(logicalCells,0xffff)`; exact replay also
requires the writer's full traversal and exhaustion behavior. This is not lossless
deduplication. A source-informed fit should use the cell's shared-record
uncertainty (plus quantization) or replay the ordered merge; it should not force
its uncompressed prediction to equal every stored grid tuple exactly. No claim
is made that Raven's stock compressor used these same tolerances. [AddLightGridLumps][N-R].

## Colour encoding, radiometric meaning, and renderer boundary

For each non-direction RGB vector `L`, output scale s, gamma g, exposure e,
compensation k, the pinned modern ColorToBytes does:

```text
s = (s > 0 ? s : 1)
t_i = 255 * (max(0, s*L_i)/255)^(1/g)
M = max(t)
e == 0: t *= min(1,255/M)       # identity when M=0
e != 0: t *= 255*(1-exp(-M/e))/M  # zero when M=0
t /= k
optional -sRGBlight: t_i = floor(255*linearToSrgb(t_i/255)+.5)
b_i = C byte conversion(t_i)    # truncation in ordinary in-range nonnegative case
```

2.5.16 has gamma, shared-peak normalization, compensation and truncation; it
does not have this exposure/sRGB-output extension. Per-shader
`q3map_lightmapGamma` in this branch is a **brightness multiplier alias**, not
the command-line power gamma. JA defaults in both inspected game headers are
gamma 1 and compensate 1; modern exposure 0, sRGB flags false. Those defaults
do not verify a stock bake's settings.

Modern exposure here is a **nonlinear RGB-peak tone curve in compiler units**,
not photographic stops and not the existing observation decoder's
`preExposureStops`. Do not map `-exposure 255` to `2^255` or a source transfer
power. A profile with this exposure needs the coupled peak curve forward model;
it cannot be represented by a scalar per-channel inverse transfer alone.
`-sRGBcolor` decodes authored colours; `-sRGBtex` affects material image use;
`-sRGBlight` encodes output. These flags are independent. sRGB encode is the
standard .0031308/12.92 branch and 1.055*x^(1/2.4)-.055 otherwise. Primaries
remain an assumption, not encoded in the BSP. See [ColorToBytes][N-Y].

Peak clipping is chromaticity-preserving **coupled** compression, e.g.
`(510,255,51) -> (255,127,25)`, not `(255,255,51)`. Even nonsaturated channels
then lost absolute magnitude. k>1 can hide prior clipping below code 255.
For g=k=1,e=0 without clipping, `b<=L<b+1` for truncation. Code 255 has an
unknown upper bound, as do coupled channels when clipping history is unknown.
Gamma/exposure/floors, RGBE quantization, compression and spatial filtering
create additional uncertainty; raw zero does not prove zero light. Invalid
settings producing out-of-range float-to-byte casts are not portable specified
behaviour and are rejected by the Python helper rather than modeled as wrapping.

Stored lightmap RGB is best described as a **legacy illumination colour
multiplier**, with an irradiance-like angular integral for some direct sources.
The receiver's texture is applied later by `$lightmap`/blendFunc or runtime
lighting. It is not measured incident irradiance or outgoing radiance in SI.
Grid A/D are coefficients for the legacy `A + D*max(n dot m,0)` model, after
empirical reduction, not independent physical ambient/directed radiance fields.
Deluxe is a normalized aggregate direction without stored strength; vertex RGB
can be illumination, authored colour, or a material mix. Bounce introduces
other surfaces' texture colours, but never turns the receiver's direct
lightmap into its already-albedo-multiplied outgoing radiance.

Relevant local contracts inspected:

- `code/qcommon/qfiles.h` and `codemp/qcommon/qfiles.h`: binary structures above.
- `shared/rd-rend2/tr_bsp.cpp`: load-time colour shifts, LDR/HDR sidecars, styles,
  raw observation hooks and grid layout; `tr_light.cpp`: style-weighted grid
  interpolation/direction decode. `tr_shader.cpp` parses runtime sun and surface
  hints, skips most compile-only q3map directives, and is **not** a compiler.
- `shared/rd-rend2/lightrecon/{lightmaps,lightgrid,surfaces,decode,evidence,offline_bsp}.cpp`:
  original-source preservation, sample mapping, explicit uncertain transfer and
  evidence filtering. See [observation decoding](rend2-observation-decoding.md),
  [lightmaps](rend2-lightmap-observations.md), [grid](rend2-lightgrid-observations.md).
- `shared/rd-rend2/tr_pbr_contract.h`, `glsl/lightall.glsl`, and
  [physical PBR contract](rend2-physical-pbr.md): metres/world conversion,
  candela/illuminance/radiance semantics and presentation calibration.

The existing offline toolchain `tools/rend2/datasets/run_pipeline.py` authors
two distinct kinds of fixtures: its independent CPU reference baker uses the
physical/scene-equivalent contract, while optional q3map2 recipes use legacy
compiler units and record source answers separately. Their point/spot/area/sun
numbers are not interchangeable. Original raw datasets, mapped world samples,
private source answers and public assumed transfer profiles already have
separate ownership in the toolchain; this work does not relabel its CPU fixtures
as authentic stock bakes or feed original source answers into solver inputs.

Current byte upload in `tr_bsp.cpp` multiplies by
`2^max(0,r_mapOverBrightBits-tr.overbrightBits)` then peak-normalizes to bytes.
Its float path permits a signed exponent and may retain over-one values.
LDR-to-HDR fallback has a .499 byte floor and dim grayscale heuristic before
upload, then renderer linearization. These **presentation** changes must not
be treated as original observations or baked source scaling. Historical
`-compensate` is likewise distinct from current runtime overbright settings.

HDR is a separate format/provenance axis. This renderer can read external
Radiance `.hdr` colour images decoded to float RGB, and headerless
`maps/<map>/lightgrid.raw` with six float32 values per **logical** grid cell
(ambient RGB then directed RGB), not per shared LDR record. Optional vertex
float sidecars also need their own provenance; LDR/HDR are alternatives, not
additive measurements. Do not infer the sidecar producer or SI scale from its
existence, or assume every compiler supports these extensions. The accessible
NetRadiant bakes here produce LDR; **no HDR compiler variant is verified**.

Physically, a point of intensity I_cd has irradiance
`E=I_cd*max(n dot u,0)/r_metres^2`, and a Lambertian receiver has
`L_out=rho*E/pi`. Historical 7500-scaled map values, 16-world-unit clamp,
linear intercept loss, half Lambert, cone edge, finite thresholds, .25/3 area
factors, backsplash, ambient fudge, bounce .375, RGB compression and lossy grid
storage are not implied by that physical law. The renderer's physical path
also has its own finite source radius and smooth finite-range window. A
calibrated far-field inverse-square candidate can resemble the direct compiler
law away from these approximations, but a cd scale cannot make all paths
physically identical. Keep `HistoricalBakeUnits`, `RelativeLinear`, and
`SceneLinearEquivalent`/physical calibration distinct. No renderer or PBR math
was changed to close any of these gaps.

## Versioned BakeModelProfile and future evaluator interface

The [profile catalogue](../tools/rend2/bake_model/profiles.json) supplies initial
data-only candidates. It is **not integrated into** existing Decode::Profile
or the on-disk observation schema. A future profile should contain:

```cpp
template<class T> struct Known {
    T value; Knowledge knowledge; // Unknown, Assumed, SourceSupported, Measured
    EvidenceRef evidence;          // immutable source/recipe/test + feature scope
};
struct BakeModelProfile {
    uint32_t schemaVersion, formulaVersion;
    ProfileId id; ProfileStatus status; // Unknown, Candidate, VerifiedSubset
    CompilerIdentity compiler;          // repo+commit, exe hash, banner, build
    DatasetIdentity dataset;            // original BSP/shader/image/sidecar hashes
    Known<PointRule> point;             // units/scales, mode, min/extra distance
    Known<AngleRule> angle; Known<SpotRule> spot;
    Known<AreaRule> area;               // exact/faster, subdivision, backsplash
    Known<SunRule> sun; Known<SkySampler> sky;
    Known<AmbientFloodDirt> fill;
    Known<BounceRule> bounce;           // passes, scales, material sampler
    Known<GridRule> grid;               // reduction/recurrence/merge/placement
    Known<DeluxeRule> deluxe; Known<VertexRule> vertex;
    Known<StylePolicy> styles;
    Known<SamplingTraceRule> sampling;   // normals, epsilons, filters, RNG/order
    Known<EncodingPipeline> output[4];  // lightmap, grid A, grid D, vertex
    Known<PhysicalCalibration> absolute; // normally Unknown
    FeatureMask verifiedFeatures;
    std::vector<Uncertainty> unknowns;
};
struct BakeEvaluation {
    StyleRgb surface; GridCoefficients grid; DirectionMoments deluxe;
    StyleRgb vertex; EncodedOutputs encoded;
    ErrorBudget uncertainty; EvaluationStatus status;
};
BakeEvaluation EvaluateBake(const BakeModelProfile&, const BakeScene&,
                            const SampleBatch&, const TraceProvider&,
                            const MaterialProvider&, const SamplingState&);
```

Evaluate separate stages: expand authored sources; place receivers; trace
uncompressed direct contributions; transport bounce; reduce by observation
type/style; filter/resample; encode; then run ordered RBSP grid merging.
Expose pre/post-shadow RGB, per-source direction contribution, per-pass terms,
quantization intervals and shared-record errors for audit. Surface queries need
geometric and shading normals, sidedness, shader/material/style IDs, UVs and
actual compiler offsets. Grid reduction is a batch/stateful operation, not just
`PointIrradiance(x)`. Full-bake operations require the complete sample ordering
for compression and RNG replay. SamplingState must pin the seed and draw order
or explicitly report them unknown. Hash formula/settings versions in cache keys.

`UnknownProfile` conservative behavior:

- Keep original bytes/sidecars/styles/directions and provenance. Source transfer,
  colour primaries, absolute scale, exposure, attenuation family, gamma,
  compensate, bounce count, grid fudge and sky sampler all start **unknown**.
- Produce no verified linear/SI value solely from RGB8. This matches the
  existing decoder's unknown-transfer default. Missing source entities mean
  unknown sources, not zero direct lighting.
- Geometry and canonical 256-step grid decoding are available but remain
  quantized/aggregate direction evidence, with unknown compressor and occupancy.
  Preserve clamped/black/censored/shared samples; do not interpret zero as a wall.
- For optional exploratory fitting, instantiate **separate assumed candidates**:
  max(16,r) inverse square with cosine, q3-style linear without cosine, and any
  provenance-supported variants; explicit g=1/k=1/e=0 is an assumption. Retain
  source scales as relative, keep grid and surface residuals separate, and allow
  nonnegative unexplained fill/bounce residuals. Do not assign one gain to hide
  incompatible distance ratios. These are hypotheses, not verified defaults.
- If a required feature is unknown, return `UnspecifiedModel` or an explicitly
  bounded ensemble/hypothesis result, not silently fill a modern default. A
  controlled-kernel test can set only its verified feature bits; it cannot
  promote an unknown stock dataset to verified.

## Reproduction, numerical fixtures and results

The standard-library harness is [tools/rend2/bake_model](../tools/rend2/bake_model/README.md).
It creates its own sealed brush rooms, TGA/shaders and light entities, compiles
BSP/VIS/LIGHT, independently maps floor texel centres through BSP triangle UVs,
and compares predicted **encoded bytes**. No game installation, graphics driver,
fitted gain, original light reconstruction, or existing renderer changes are
needed. `test_kernels.py` has seven numerical tests, including distinct falloff
ratios, clamp/softening, linear-angle intercept ordering, spot edge width,
coupled clipping/exposure, grid reduction/version fudge, polygon edge integral,
and 255-step direction encoding. The full-bake harness uses actual compiler
data as the independent oracle.

```powershell
python tools/rend2/bake_model/test_kernels.py -v
$q3 = Resolve-Path build/toolchain/netradiant-20220628/portable/netradiant_1.5.0-20220628-windows-amd64/q3map2.exe
python tools/rend2/bake_model/compare_bakes.py --q3map2 $q3 --output build/my-bake-model-run
```

Use a fresh output folder; prior witnesses are never overwritten. The final
local run is `build/bake-model-comparison-final`, with full argv, logs, authored
maps, raw compiled BSP hashes and per-sample examples. Compiler installation
and verified official package checksum are documented in
[the existing q3map2 validation](rend2-q3map2-validation.md). The final run used
Python 3.12 from the existing bundled runtime. No new compiler was installed.

All cases use `-game ja -threads 1`, BSP `-meta -leaktest -noflares -samplesize 16`,
VIS, and LIGHT `-deluxe -deluxemode 0 -gamma 1 -compensate 1 -exposure 0
-bounce 0 -super 1 -nocollapse`, with each case's independent overrides.
`-nocollapse` keeps the constant ambient fixture spatially mapped; it is a
declared fixture recipe, not a proposed stock default.

| Fixture group | Verified result |
| --- | --- |
| Inverse square, no angle, linear, linear+angle, linear fade .5, angle scale .5 | Encoded floor and interior single-source grid RGB match exactly; tested independently at many distances |
| Wolf default linear / bit-1 inverse square | Exact RGB matches; flag meanings are opposite to q3 mode |
| Pointscale .5, extra distance 64, half Lambert | Exact RGB matches, no refitted scale |
| Spot with requested linear flag | Forced inverse square and 32-unit edge match exactly |
| Colour gamma 2.2 | Exact RGB matches, with `_color=(.2,.5,1)` |
| Colour clipping at light=3000 | Surface/grid RGB error at most one byte; deluxe exact; grid direction error up to four bins from approximate record merging |
| Vertical sun, two suns, 17-direction skylight, exact shader area | Interior floor RGB exact; grid not asserted for these multisource/area fixtures |
| World ambient 20 alone | Exact floor RGB20, interior grid all zero; source-order caveat above |
| Bounce1 vs direct, bouncegrid1 vs direct | Floor red mean increases **5.3314285714** byte units in both; resolved grid byte-identical only without bouncegrid. This is a paired behavior check, **not exact bounce replay** |
| Retained source (`-keeplights` BSP option) | One source retained, direct byte predictions exact; other fixtures retain zero light entities |
| Deluxe and grid directions | Single-source point/spot/control deluxe predictions match exactly; ordinary grid direction angles exact; clipped case affected by source-defined merge tolerance |

The versioned [validation witness](../tools/rend2/bake_model/validation.json)
records exact counts, hashes, maxima and per-case options. The inverse-square
and linear tests also run the **wrong law without refitting**: RMSE must exceed
5 bytes, while the correct law is byte-exact. For example at I=100 and distances
64/128/256, raw inverse-square values are
183.10546875 / 45.7763671875 / 11.444091796875; raw linear values are
29.75 / 0 / 0. A scalar cannot reconcile these ratios.

One explicit limit: the multiple-sun analytic box-visibility comparison excludes
a **two-world-unit band** around its predicted sky/wall shadow boundary. There
are 105 unchecked RGB channels (35 texels), maximum discrepancy 28; the remaining
interior channels match exactly. The raw mismatch is retained in reports. A
full compiler trace/triangle epsilon replay is required there. All other cases
retain their interior sample checks. No broader claim of exact stock visibility
or boundary sampling is made.

The follow-up [X04 boundary/footprint validation](rend2-shadow-footprints-x04.md)
reproduces the unchanged35-texel witness and explains this pinned fixture using
clipped trace-tree triangles and source-defined barycentric slack. Its restricted
replay matches all checked floor bytes, including the old excluded row. The
historical witness remains unchanged; this is not general Raven trace validation.

## Historical unknowns and next implementation boundaries

The missing production compiler/recipe is a substantive result, not an assumed
equivalence. Still unverified for stock JA: exact Raven/compiler revisions per
SP/MP level; sun colour normalization; pre-2004 sky sampler/area-sky behavior;
linear/inverse-square defaults and flag interpretation; light/spot/area scales;
ambient/grid fudge and grid lossy merging; bounce count/grid inclusion and
direction recurrence; gamma/compensation/exposure and any custom output steps;
shadow/sample/filter/tesselation options; style allocation/cap behavior; and
post-bake processing, retained shader/image versions or map patches.

Unvalidated accessible paths include full flood/dirt bakes, animated styles,
curved patches, two-sided horizon-crossing emitters, near-solid nudges, external
HDR producers, RNG-identical jitter and full vertex/trace replay. Sources give
rules for implementing these; the fixture witness verifies only listed features.
The repository's earlier controlled twelve-map/SP observation-validation suite
provides complementary raw export/visibility/bounce evidence, not a substitute
for attenuation-specific numerical tests.

Even known source geometry cannot undo clipped RGB, quantization, approximate
grid merging, normalized direction strength loss, style overflow, baked-in
ambient/flood heuristics, missing original material images, discrete bounce
patches, patch-centre visibility, supersampling/filtering/atlas sharing, altered
compile geometry or undisclosed random sample locations. Recovering one possible
source set is therefore not proof of recovering the authored source set.

### Pinned source references

The follow-up [X03 identifiability experiments](rend2-bake-identifiability-x03.md)
add fitted nuisance comparisons and an explicit UnknownProfile strategy. Their
conditional rankings do not identify a stock JA production compiler.

[N-L]: https://github.com/xonotic/netradiant/blob/d9338309ea8fab75027b7382efaabe034302b862/tools/quake3/q3map2/light.c
[N-Y]: https://github.com/xonotic/netradiant/blob/d9338309ea8fab75027b7382efaabe034302b862/tools/quake3/q3map2/light_ydnar.c
[N-B]: https://github.com/xonotic/netradiant/blob/d9338309ea8fab75027b7382efaabe034302b862/tools/quake3/q3map2/light_bounce.c
[N-S]: https://github.com/xonotic/netradiant/blob/d9338309ea8fab75027b7382efaabe034302b862/tools/quake3/q3map2/shaders.c
[N-Q]: https://github.com/xonotic/netradiant/blob/d9338309ea8fab75027b7382efaabe034302b862/tools/quake3/q3map2/q3map2.h
[N-T]: https://github.com/xonotic/netradiant/blob/d9338309ea8fab75027b7382efaabe034302b862/tools/quake3/q3map2/light_trace.c
[N-M]: https://github.com/xonotic/netradiant/blob/d9338309ea8fab75027b7382efaabe034302b862/libs/mathlib/mathlib.c
[N-R]: https://github.com/xonotic/netradiant/blob/d9338309ea8fab75027b7382efaabe034302b862/tools/quake3/q3map2/bspfile_rbsp.c
[N-LM]: https://github.com/xonotic/netradiant/blob/d9338309ea8fab75027b7382efaabe034302b862/tools/quake3/q3map2/lightmaps_ydnar.c
[N-W]: https://github.com/xonotic/netradiant/blob/d9338309ea8fab75027b7382efaabe034302b862/tools/quake3/q3map2/writebsp.c
[N-BA]: https://github.com/xonotic/netradiant/blob/d9338309ea8fab75027b7382efaabe034302b862/tools/quake3/q3map2/bspfile_abstract.c
[G-L]: https://github.com/id-Software/GtkRadiant/blob/0991a5ce8b8ccb86a5b0d54c089369d84f53dc10/tools/quake3/q3map2/light.c
[G-Y]: https://github.com/id-Software/GtkRadiant/blob/0991a5ce8b8ccb86a5b0d54c089369d84f53dc10/tools/quake3/q3map2/light_ydnar.c
[G-S]: https://github.com/id-Software/GtkRadiant/blob/0991a5ce8b8ccb86a5b0d54c089369d84f53dc10/tools/quake3/q3map2/shaders.c
[G-R]: https://github.com/id-Software/GtkRadiant/blob/0991a5ce8b8ccb86a5b0d54c089369d84f53dc10/tools/quake3/q3map2/bspfile_rbsp.c
[G-LM]: https://github.com/id-Software/GtkRadiant/blob/0991a5ce8b8ccb86a5b0d54c089369d84f53dc10/tools/quake3/q3map2/lightmaps_ydnar.c
[G-H]: https://github.com/id-Software/GtkRadiant/blob/0991a5ce8b8ccb86a5b0d54c089369d84f53dc10/tools/quake3/q3map2/changelog.q3map2.txt
[Q-L]: https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/q3map/light.c
[Q-V]: https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/q3map/lightv.c
