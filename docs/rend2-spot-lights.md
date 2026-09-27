# Rend2 spot lights

A dynamic light can be limited to a cone. Spot lights light surfaces through the legacy dlight loop and through Forward+. They cast a single-perspective shadow, and they light the froxel fog. Surfaces and fog use the same attenuation:

```
light = color * pointAtten(radius) * coneAtten * shadow            (surfaces: * BRDF)
                                                                   (froxels:  * phase)
pointAtten = clamp(0.5 * radius^2 / d^2 - 0.5, 0, 1)                unchanged
coneAtten  = smoothstep(cosOuter, cosInner, dot(-L, dir))
```

Existing lights are unchanged. `AddLightToScene`, `AddAdditiveLightToScene`, saber and blaster lights, and every `.efx` without a `spot` group stay point lights. Spot lights come only from the new API.

## API

The optional renderer extension follows the same pattern as `GetRefVolParticleAPI`. There is no `REF_API_VERSION` bump.

```c
// tr_types.h (MP + SP)
#define SPOTLIGHT_ADDITIVE  1   // as AddAdditiveLightToScene
#define SPOTLIGHT_NOSHADOW  2
typedef struct {
    vec3_t origin;
    vec3_t dir;            // cone axis, away from the light (normalized by the renderer)
    float  radius;         // range, as the intensity of AddLightToScene
    float  color[3];
    float  innerAngle;     // degrees from the axis, full intensity inside
    float  outerAngle;     // degrees, zero outside (clamped 0.5..89)
    int    flags;
} refSpotLight_t;

// tr_public.h: "GetRefSpotLightAPI" -> refSpotLightExport_t { AddSpotLightToScene }
```

How each side reaches it:
- **MP:** the client loads `reSpotLights`. FX calls it directly, and falls back to `AddLightToScene` when the renderer does not export the API.
- **SP:** the new cgame trap is `CG_R_ADDSPOTLIGHT`. The engine sets the ROM cvar `cl_rendererSpotLights` to 1 when the renderer exports the API. The cgame calls the trap only in that case, otherwise it uses the point-light trap. This keeps a new cgame safe on an old engine.

Spot lights are always at least 0.1° soft (inner is clamped to cosOuter + 0.002). There are no hard-edged cones.

## `dlight_t` delta

`areaType` stays `DLIGHT_POINT`. Everything that handles point lights therefore handles spots unchanged: lists, radial culling, the shadow slot budget and flares.

| field | meaning |
|---|---|
| `qboolean spot` | this light is a spot |
| `vec3_t spotDir` | unit cone axis |
| `float spotCosInner, spotCosOuter` | point lights: −1 / −2 (cone factor exactly 1) |
| `qboolean spotNoShadow` | `SPOTLIGHT_NOSHADOW` |
| `int spotShadowSlot` | set by `R_GatherFrameViews`: the slot whose layer 0 holds the projected view, −1 if none |
| `matrix_t spotShadowVP` | world to clip of that view |

## GPU layouts

- **`LightsBlock` (UBO "Lights"; lightall + volumetric_inject):**
  - `Light` grows from 32 to 64 bytes. It adds `vec4 spot` (axis, cosOuter) and `vec4 spot2` (cosInner, shadow −1 none / 0 cube / 1 projected, 0, 0).
  - `mat4 u_SpotShadowVP[MAX_DLIGHT_SHADOWS]` is appended and indexed by shadow slot.
  - The block goes from 1344 to 4416 bytes.
- **Forward+ light TBO:** still 5 texels per light. For point-type lights:
  - texel 2 is now (shadow slot, 0, cosInner, projected).
  - texel 3 is (axis, cosOuter), fetched only when cosInner > −0.5.
  - Area lights are unchanged.
- **Froxel light buffer:** 2 → 4 texels per light: origin, radius | color, shadow layer | axis, cosOuter | cosInner, projected, 0, 0.

## Shadows (`r_dlightMode 2`)

- **Storage:** `pointShadowArrayImage` is reused. Slot *s* keeps its 6 layers. A spot with outer angle ≤ 60° renders **one** view into layer `6*s` and leaves the other 5 unused. No new memory is needed, and layer semantics stay "slot × 6 + face".
- **View:**
  - Forward = dir, with a stable left/up (no roll).
  - fov = 2·outer + 2 texels, capped at 124°.
  - Same near 1, far = radius, polygon offset and `VPT_POINT_SHADOWS` target as a cube face.
- **Wider cones** (> 60°) keep the 6 cube faces and the cube lookup.
- **Receiver bias:** same `r_dlightShadowBias` modes. The texel size scales by 1 / tan(fov/2), and the PCF disk covers the same angle as on a cube face.
- **Froxels:** 4 taps, pulled towards the light like the cube taps.
- **Budget:** unchanged (`r_dynamicShadowMaxLights` under Forward+). `SPOTLIGHT_NOSHADOW` and `r_spotShadows 0` spots are never given a slot.
- Unrelated fix: `shadowCubeFbo` was created with `PSHADOW_MAP_SIZE` metadata; it now uses `DSHADOW_MAP_SIZE`.

## Culling

- **Froxel and Forward+ cluster ranges:** a spot with outer ≤ 60° uses the smallest sphere through its apex that holds the cone. Its centre is `origin + dir·R/(2cosθ)` and its radius is `R/(2cosθ)`. Wider spots use the light sphere.
- **Legacy entity mask (`R_DLightsForPoint`):** adds a conservative sphere/cone test.
- **World surfaces:** keep the sphere test.
- The CPU harness found 0 misses against brute force.

## EFX syntax

This is an optional group of a `Light` primitive. MP and SP parse it identically.

```
Light
{
	size { start 700 }
	rgb  { start 1 0.92 0.8 }
	spot
	{
		innerAngle	16		// degrees, range allowed (16 20)
		outerAngle	26		// degrees, max 89
		direction	1 0 0	// optional: the cone axis in the effect's axes
		shadows		1		// optional, 0 = never shadowed
	}
}
```

The direction comes from the effect's own axes. `ax` is the play direction, or the bolt axis for relative effects. It never comes from world +X.
- **MP:** bolted (`FX_RELATIVE`) spots re-read the bolt axis every frame in `CLight::Update`.
- **SP:** stock point lights never followed bolts, and they still don't. Spot lights now do, in two cases:
  - bolt-relative effects, through the client's ghoul2 bolt;
  - client-id effects, through the client's muzzle point and direction.

A renderer without the API gets the plain point light.

## Cookies (gobos)

A spot light can carry a texture that modulates the light it sends in each direction. It is applied where the cone is, before the BRDF on surfaces and before the phase function in the froxel injection, so walls and fog show the same pattern:

```
light = color * pointAtten * coneAtten * cookie * shadow
```

The cookie never replaces the geometry shadow: both multiply. Module: `shared/rd-rend2/tr_lightcookie.cpp`.

**Projection.** The cookie has its own analytic projection, because unshadowed spots have no shadow view:
- basis: the axis plus the stable left / up of `R_SpotShadowAxis`, turned by a roll;
- `uv = 0.5 - 0.5 * (dot(d, left), dot(d, up)) / (dot(d, axis) * tan outer)`, with `d` the unit direction from the lamp;
- the outer cone maps to the disc inscribed in the texture. The rim of the pattern is where the cone factor reaches 0, and the corners are never sampled. Behind the lamp the factor is 0;
- seen from behind the lamp, the image is not mirrored: `up` is the top, right is right;
- the roll comes from `refSpotLight_t::up`, measured against the stable basis. An explicit up therefore stays continuous where that basis switches (axis near vertical). The CPU harness measured a largest uv step of 2e-16 through straight down.

**Storage (GL 3.2: no bindless, no per-light samplers).**
- One `GL_TEXTURE_2D_ARRAY`, `tr.lightCookieArray`: 256² RGBA8, 16 layers (one per registered cookie), full mip chain, trilinear, clamp to edge. It is about 5.6 MB and is created on the first registration.
- Unit `TB_LIGHTCOOKIES` = 25, bound once for lightall draws with dlights and for the froxel inject. It needs `GL_MAX_TEXTURE_IMAGE_UNITS > 25`, otherwise cookies are ignored with a warning.
- Every light samples it with `textureLod(u_LightCookieMap, vec3(uv, layer), lod)`. The sampler is uniform and the layer is data, so there is no dynamic sampler indexing.
- The lod is explicit, because the Forward+ loop is non-uniform control flow. Surfaces and fog use the same formula, `log2(256 * 0.5 * footprint * viewDist / (t * tan outer))`, where footprint is the world size of a pixel (surfaces) or a froxel (fog) at distance 1. The fog therefore samples a blurrier mip, about 2–3 levels up at 1080p, which is what keeps it from shimmering.
- A light without a cookie has layer −1. The shaders skip the fetch and use a factor of exactly 1.

**Image contents.**
- An opaque image is an intensity: white is full light. The alpha channel holds its luminance and rgb its colour.
- An image with alpha is an **occluder mask**, as on an alpha-tested surface: opaque texels block the light, and transmission is 1 − alpha, in grey. So the stock alpha-tested grates and window frames work as gobos unchanged.
- `r_lightCookies 1` uses the intensity; `2` uses the colour.
- The source is box filtered down to 256² (4×4 taps per texel).

**GPU data (no size growth).**
- UBO `Light.spot2.zw` = cookie layer, roll.
- Forward+ TBO, point/spot lights: texel 2 `.y` is the cookie layer (previously 0). Texel 4 `.x` is the roll, fetched only when the layer is ≥ 0.
- Froxel light buffer: texel 3 `.zw` = cookie layer, roll.

**API.**
- `refSpotLight_t` gains `int cookie` (handle, 0 = none) and `vec3_t up` (zero = the stable basis).
- `refSpotLightExport_t` gains `RegisterLightCookie(name)`. It returns layer + 1, or 0 when unavailable.
- SP gets the new trap `CG_R_REGISTERLIGHTCOOKIE`. The engine sets `cl_rendererSpotLights` to **2** when the renderer registers cookies, and the cgame calls the trap only then, so a new cgame still runs on an older engine.

**EFX.** These are keys of the `spot` group only. A cookie is a property of the cone, and point lights have no projection, so there is no point-light cookie.

```
spot
{
	innerAngle	22
	outerAngle	26
	cookie		textures/imperial/grate02	// image, registered when the effect loads
	cookieRoll	0							// optional, degrees around the axis
}
```

The cookie's top is the effect's up axis (the bolt's up for bolted spots, updated every frame), projected across the cone and turned by `cookieRoll`. For client-muzzle spots in SP, the up comes from `MakeNormalVectors` of the muzzle direction. It is deterministic but not tied to the weapon's roll. Effects without these keys are unchanged.

**Debug.**
- `r_lightCookieDebug` (cheat) replaces lit surfaces that receive a cookie light (unlit output, weighted by the cone):
  - 1: projected uv (fract, red/green);
  - 2: cookie factor;
  - 3: cookie × shadow.
- Final volumetric injection: `r_volumetricFogDebug 4` (dynamic-light in-scattering), with `r_spotLightDebug 4` to keep only spot lights in the fog.
- The `r_spotLightDebug 1` list prints the cookie's layer, name and roll.
- `r_spot cookie <i> <image|none>` puts a cookie on a renderer-only test spot. `r_spot attach` spots use the view up, so the pattern turns with the camera.

## Test asset and commands

**Asset:** `build/test-assets/zz_spotlight_test.pk3` (source dir alongside). The base assets were scanned: 72 efx files have a `Light`, and none is directional (muzzle flashes, `env/beam_lights`, sparks), so none was copied. The pk3 contains:
- `effects/test/volumetric_spot.efx`: 16/26°, radius 700, shadowed, 15 s
- `effects/test/volumetric_spot_noshadow.efx`: the same cone, unshadowed
- `effects/test/volumetric_spot_wide.efx`: 50/70°, radius 500, uses the cube-face shadow path
- `effects/test/volumetric_spot_cookie.efx`: the shadowed 22/26° spot with the cookie `textures/imperial/grate02`. This is a stock alpha-tested 8×8 window grate from `assets1.pk3`, used as an occluder mask, so no new texture was needed.

Each file uses the existing `gfx/effects/blasterFrontFlash` sprite as the lamp.

**Commands:**
- `fxplay <efx> [distance]` (MP engine, SP cgame; needs `developer 1`): plays the effect at the camera plus forward·distance, pointing along the view.
- SP only, `fxplay <efx> muzzle`: attaches the effect to the player's weapon muzzle, so the spot is bolted and turns with the view.
- `r_spot add|attach|spin|noshadow|list|clear`: renderer-only spots, no asset needed.
  - `attach` makes a camera flashlight.
  - `spin <i> <deg/s>` rotates a spot.

**Cvars:**

| cvar | effect |
|---|---|
| `r_spotLights 1` | 0 submits spots as point lights |
| `r_spotShadows 1` | 0 turns off spot shadows |
| `r_spotLightDebug` (cheat) | 1 cones plus a per-second list (type, dir, cone, shadow mode); 2 adds the shadow frustums; 3 surfaces lit by spots only, fog without dlights; 4 fog lit by spots only, no spots on surfaces |
| `r_volumetricFogDebug 29` | lights per froxel cluster at the scene depth (heat), cyan where spots are listed: the slice light mask |

## Validation

Done:
- MSVC builds of both renderers, both engines, `jagamex86_64` and `cgamex86_64`.
- Offline GLSL on Intel UHD and RTX 2060:
  - lightall: 12 permutations, including DSHADOWS, LTC, POM, FPLUS_DEBUG, cloth, SSS and SSGI.
  - froxel programs: 48 cases.
  - 0 failures.
- lightall compile time (min of 5, shadowed permutation):
  - Intel: 549 → 519 ms. The shadow lookup now has one call site instead of two.
  - NVIDIA: 474 → 467 ms.
  - Unshadowed permutation: unchanged.
- CPU harness (`spottest/harness.py`), all 0 failures:
  - the point sentinel factor is exactly 1;
  - the cone is 1 inside inner, 0 outside outer, monotonic, with no jumps;
  - the cone bounding sphere contains the cone;
  - the sphere/cone reject has no false culls in 8941 culled cases;
  - projected depth equals the cube `getLightDepth`, and the rim stays inside the shadow map.

**Not done: the game was not launched.** No screenshots, no timings. Run these `spot:` rows in the game (deploy both renderer DLLs, `openjk*.exe`, `jagamex86_64.dll`, `cgamex86_64.dll` and the pk3):

| row | how | expect |
|---|---|---|
| spot: empty room | `r_spot add 600 30 20` facing a wall | soft round pool, no light behind the lamp |
| spot: cone vs wall | walk around, `r_spotLightDebug 1` | cone lines match the lit pool |
| spot: geometry cuts cone | `r_dlightMode 2`, object in the beam | sharp shadow; `r_spotLightDebug 2` frustum covers the cone |
| spot: doorway + fog | `r_volumetricFog 2`, spot through a doorway | visible cone in the fog, shadowed by the door frame; `r_volumetricFogDebug 29` cyan clusters |
| spot: moving / rotating | `r_spot attach`, `r_spot spin 0 45` | shadow and fog cone follow every frame, no lag |
| spot: bolted efx | SP `fxplay effects/test/volumetric_spot muzzle` | cone follows the weapon |
| spot: efx at camera | `fxplay effects/test/volumetric_spot 16` (MP and SP) | the same as `r_spot add` |
| spot: point regression | sabers, blasters, `r_spotLights 0` A/B | identical point lights |
| spot: shadow off/on | `r_spotShadows 0/1`, `_noshadow` efx | only spot shadows toggle |
| spot: wide | `volumetric_spot_wide` | cube-face shadow, `r_spotLightDebug 1` says "cube" |
| cookie: checker on wall + fog | `r_volumetricFog 2`, `fxplay effects/test/volumetric_spot_cookie 16` facing a wall | 8×8 panes on the wall and the same shafts in the fog |
| cookie: debug | `r_lightCookieDebug 1/2/3` | uv gradient inside the disc; the pattern; the pattern cut by geometry shadow |
| cookie: shadow overlap | `r_dlightMode 2`, an object in the beam | object shadow and cookie both visible, neither replaces the other |
| cookie: moving | `r_spot attach`, `r_spot cookie 0 textures/imperial/grate02`, turn and roll the view | pattern stays locked to the lamp, no flip looking straight down |
| cookie: near/far | walk from 50 to 700 units | the pattern scales with the cone; no shimmer far away (explicit lod) |
| cookie: fog only | `r_volumetricFogDebug 4`, `r_spotLightDebug 4` | cookie shafts in the injected light |
| cookie: A/B | `r_lightCookies 0/1/2` | 0 = the plain spot; 2 = colour (grey for the grate) |
| spot: timings | `r_forwardPlusBenchmark` and the froxel timers, 0 vs 4 spots | record ms |
