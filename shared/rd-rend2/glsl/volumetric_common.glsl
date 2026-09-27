/*[Fragment]*/
// Froxel volumetric fog (r_volumetricFog 2), tr_volumetric.cpp, docs/rend2-volumetric-fog.md.
//
// This file is not a program on its own: its fragment block is inserted into the fragment shaders of
// the volumetric_*.glsl programs and, with r_volumetricFog 2 only, of the fog pass, generic and
// surface sprite programs (the legacy fog paths). Everything is inside USE_FROXEL_FOG.
//
// Froxel volume: froxels x, y cover the main view, z is split in slices with exponential distances:
// slice k spans the view depths [B(k), B(k + 1)], B(k) = near * (far / near)^(k / N), and slice 0
// starts at the camera. View depth is the distance along the view forward axis.
//
//   u_FroxelVolume  RGBA16F 3D  rgb = light scattered towards the camera between the camera and
//                               the far side of the slice (B(k + 1)), a = transmittance
//   u_FroxelTail    RGBA16F 2D  rgb = light at the far side of the volume without albedo (baked + sun,
//                               phase included); the media beyond far are integrated analytically
//
// Units: extinction per world unit, as depthToOpaque of the legacy volumetric fog.

#if defined(USE_FROXEL_FOG)
layout(std140) uniform VolumetricFog
{
	mat4 u_FroxelViewProjection;		// froxel camera (main view without jitter)
	mat4 u_FroxelInvViewProjection;		// inverse of the rendered view projection
	mat4 u_FroxelPrevViewProjection;	// froxel camera of the history volume
	vec4 u_FroxelViewOrigin;			// w: 1 = volume available
	vec4 u_FroxelViewForward;
	vec4 u_FroxelRayForward;			// ray of a froxel = rayForward + ndc.x * rayRight + ndc.y * rayUp
	vec4 u_FroxelRayRight;
	vec4 u_FroxelRayUp;
	vec4 u_FroxelViewport;				// view rectangle in render target texture coordinates
	vec4 u_FroxelSliceParams;			// near, far, log2(far / near), sky distance
	vec4 u_FroxelGridSize;				// froxels x, y, z, frame index
	vec4 u_FroxelJitter;				// jitter in froxel units, w: temporal accumulation
	vec4 u_FroxelTemporalParams;		// history weight, history valid, unused, radiance clamp ratio
	vec4 u_FroxelLightParams;			// anisotropy g, sun scale, dynamic light scale, baked light scale
	vec4 u_FroxelSunColor;				// realtime sun radiance, w: cascaded shadow maps available
	vec4 u_FroxelSunDirection;			// towards the sun, w: split light grid available
	vec4 u_FroxelGridOrigin;			// light grid sample origin, w: vertical cell size
	vec4 u_FroxelGridScale;				// world to light grid texture coordinates, w: horizontal cell size
	vec4 u_FroxelShadowParams;			// cascade far distance, shadow map size, dlight shadows, bias
	vec4 u_FroxelDebugParams;			// debug view, bloom, frozen volume, unused
	vec4 u_FroxelHeightFog;				// height fog: base extinction (0 = off), base z, 1 / falloff, log(max scale)
	vec4 u_FroxelHeightFogColor;		// rgb albedo, w: fade out start above the base
	vec4 u_FroxelHeightFogTop;			// x: top above the base (0 = no cutoff)
	vec4 u_FroxelNoiseParams;			// density noise: 1 / macro period, 1 / detail period, macro contrast, detail contrast
	vec4 u_FroxelNoiseMacroOffset;		// wind offset (tile units), w: 1 = height fog is noisy
	vec4 u_FroxelNoiseDetailOffset;		// wind offset (tile units), w: history weight of the noisy media
	vec4 u_FroxelNoiseLod;				// lod offsets (macro, detail), slice thickness / view depth, w: 1 = noise on
	vec4 u_FroxelNoiseNormMacro[4];		// mean normalization at lod 0, 0.5, ..., 7.5
	vec4 u_FroxelNoiseNormDetail[4];
	int u_FroxelNumFogs;
	int u_FroxelLightTile;					// dynamic light lists: froxels per tile side (0 = no lights)
	int u_FroxelLightTilesX;
	int u_FroxelLightTilesY;
	vec4 u_FroxelFogColor[MAX_GPU_FOGS];	// rgb albedo (fog color), a: extinction
	vec4 u_FroxelFogPlane[MAX_GPU_FOGS];
	vec4 u_FroxelFogMins[MAX_GPU_FOGS];		// w: has plane
	vec4 u_FroxelFogMaxs[MAX_GPU_FOGS];		// w: density noise applies

	// local fog volumes (tr_fogvolume.cpp), nearest first. Per slice a packed list of the volumes
	// that overlap it: header = first pool entry | count << 16, pool = 8 bit volume indices.
	vec4 u_FroxelLocalParams;							// count, fade start, 1 / fade length, unused
	vec4 u_FroxelLocalX[MAX_GPU_FOG_VOLUMES];			// world to unit local space rows (xyz, w offset)
	vec4 u_FroxelLocalY[MAX_GPU_FOG_VOLUMES];
	vec4 u_FroxelLocalZ[MAX_GPU_FOG_VOLUMES];
	vec4 u_FroxelLocalPrevX[MAX_GPU_FOG_VOLUMES];		// the same rows in the previous frame
	vec4 u_FroxelLocalPrevY[MAX_GPU_FOG_VOLUMES];
	vec4 u_FroxelLocalPrevZ[MAX_GPU_FOG_VOLUMES];
	vec4 u_FroxelLocalColor[MAX_GPU_FOG_VOLUMES];		// rgb albedo, a: extinction (0: gone this frame)
	vec4 u_FroxelLocalShape[MAX_GPU_FOG_VOLUMES];		// shape (0 ellipsoid, 1 box), inner, 1 / (1 - inner), noisy
	vec4 u_FroxelLocalMotion[MAX_GPU_FOG_VOLUMES];		// changed: 0 no, else 1 + previous shape; previous extinction, inner, 1 / (1 - inner)
	ivec4 u_FroxelLocalSlices[FROXEL_MAX_SLICES / 4];	// slice headers
	ivec4 u_FroxelLocalIndex[FROXEL_LOCAL_POOL / 16];	// index pool, 4 per int

	// BSP fog volumes that may touch a slice (CPU culled): bit i = fog i
	ivec4 u_FroxelFogSlices[FROXEL_MAX_SLICES / 4];
};

#if defined(USE_FROXEL_PARTICLES)
// FX particle media (tr_volparticle.cpp, injection and debug views only), most important first. Per
// slice a packed list of the particles that overlap it: header = first pool entry | count << 16,
// pool = 16 bit particle indices.
layout(std140) uniform VolumetricParticles
{
	vec4 u_FroxelParticleParams;										// count, fade start, 1 / fade length, history floor
	vec4 u_FroxelParticleCenter[MAX_GPU_VOL_PARTICLES];				// xyz, w: extinction (0: gone this frame)
	vec4 u_FroxelParticleInvExtent[MAX_GPU_VOL_PARTICLES];			// 1 / extent per world axis, w: inner
	vec4 u_FroxelParticleColor[MAX_GPU_VOL_PARTICLES];				// rgb albedo, w: previous extinction
	vec4 u_FroxelParticlePrevCenter[MAX_GPU_VOL_PARTICLES];			// previous frame, w: 1 = changed
	vec4 u_FroxelParticlePrevInvExtent[MAX_GPU_VOL_PARTICLES];		// previous frame, w: inner
	ivec4 u_FroxelParticleSlices[FROXEL_MAX_SLICES / 4];				// slice headers
	ivec4 u_FroxelParticleIndex[VOL_PARTICLE_POOL / 8];				// index pool, 2 per int
};
#endif

uniform sampler3D u_FroxelVolume;
uniform sampler2D u_FroxelTail;

// 0 = legacy fog, 1 = froxel volume lookup, 2 = none (the composite applied it)
uniform int u_FroxelFogMode;

#define FROXEL_DEPTH_HACK_MAX 0.3001

// view depth of the slice coordinate w (0 = camera, 1 = far). Slice 0 spans [0, B(1)] linearly, as its
// integration domain (the camera to B(1)); the others are exponential, B(k) = near * (far / near)^(k / N).
float FroxelWToDepth(in float w)
{
	float s = w * u_FroxelGridSize.z;
	float firstBoundary = u_FroxelSliceParams.x * exp2(u_FroxelSliceParams.z / u_FroxelGridSize.z);
	if (s <= 1.0)
		return max(s, 0.0) * firstBoundary;
	return u_FroxelSliceParams.x * exp2(w * u_FroxelSliceParams.z);
}

// slice coordinate of the view depth d (inverse of FroxelWToDepth)
float FroxelDepthToW(in float d)
{
	float firstBoundary = u_FroxelSliceParams.x * exp2(u_FroxelSliceParams.z / u_FroxelGridSize.z);
	if (d <= firstBoundary)
		return max(d, 0.0) / firstBoundary / u_FroxelGridSize.z;
	return log2(d / u_FroxelSliceParams.x) / u_FroxelSliceParams.z;
}

// Henyey-Greenstein phase function times 4 pi: 1 for isotropic scattering (g = 0), so the anisotropy
// redistributes the scattered light without changing its average. cosTheta: angle between the light
// propagation direction and the direction towards the camera.
float FroxelPhase(in float g, in float cosTheta)
{
	float g2 = g * g;
	float denom = max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4);
	return (1.0 - g2) / (denom * sqrt(denom));
}

// Density (0..1) of a local fog volume at p. rows: world to unit local space (q = rows * p), the
// shape is the unit sphere or the unit cube. The density fades out between the inner shell and the
// boundary with a smoothstep: value and slope are 0 at the boundary, so the edge is never hard. The
// box multiplies the fades of its three axes, which also rounds its corners.
float FroxelLocalShapeDensity(in vec4 rx, in vec4 ry, in vec4 rz, in float shape, in float inner,
	in float invWidth, in vec3 p)
{
	vec3 q = vec3(dot(rx.xyz, p) + rx.w, dot(ry.xyz, p) + ry.w, dot(rz.xyz, p) + rz.w);
	if (shape < 0.5)
	{
		float r2 = dot(q, q);
		if (r2 >= 1.0)
			return 0.0;
		float t = clamp((sqrt(r2) - inner) * invWidth, 0.0, 1.0);
		return 1.0 - t * t * (3.0 - 2.0 * t);
	}

	vec3 a = abs(q);
	if (max(a.x, max(a.y, a.z)) >= 1.0)
		return 0.0;
	vec3 t = clamp((a - vec3(inner)) * invWidth, 0.0, 1.0);
	vec3 f = 1.0 - t * t * (3.0 - 2.0 * t);
	return f.x * f.y * f.z;
}

// packed list of the local volumes of a slice: header = first pool entry | count << 16
int FroxelLocalSliceHeader(in int slice)
{
	return u_FroxelLocalSlices[slice >> 2][slice & 3];
}

int FroxelLocalPoolIndex(in int entry)
{
	return (u_FroxelLocalIndex[entry >> 4][(entry >> 2) & 3] >> ((entry & 3) * 8)) & 255;
}

// local volumes fade out before the last slice: the tail beyond far (FroxelTailMedium) does not
// contain them, so they must not end with a hard cut at far
float FroxelLocalFade(in float viewDepth)
{
	float t = clamp((viewDepth - u_FroxelLocalParams.y) * u_FroxelLocalParams.z, 0.0, 1.0);
	return 1.0 - t * t * (3.0 - 2.0 * t);
}

#if defined(USE_FROXEL_PARTICLES)
// Density (0..1) of an FX particle proxy at p: an ellipsoid along the world axes (invExtent.xyz), full
// density inside the inner shell (invExtent.w), smoothstep to 0 at the boundary.
float FroxelParticleDensity(in vec3 center, in vec4 invExtent, in vec3 p)
{
	vec3 q = (p - center) * invExtent.xyz;
	float r2 = dot(q, q);
	if (r2 >= 1.0)
		return 0.0;
	float t = clamp((sqrt(r2) - invExtent.w) / max(1.0 - invExtent.w, 1e-3), 0.0, 1.0);
	return 1.0 - t * t * (3.0 - 2.0 * t);
}

int FroxelParticleSliceHeader(in int slice)
{
	return u_FroxelParticleSlices[slice >> 2][slice & 3];
}

int FroxelParticlePoolIndex(in int entry)
{
	return (u_FroxelParticleIndex[entry >> 3][(entry >> 1) & 3] >> ((entry & 1) * 16)) & 0xffff;
}

// as FroxelLocalFade
float FroxelParticleFade(in float viewDepth)
{
	float t = clamp((viewDepth - u_FroxelParticleParams.y) * u_FroxelParticleParams.z, 0.0, 1.0);
	return 1.0 - t * t * (3.0 - 2.0 * t);
}
#endif

#if defined(USE_FROXEL_NOISE)
// Density noise (r_volumetricFogNoise): a tiling 64^3 texture sampled in world space, r = macro field,
// g = detail field, both uniformly distributed (histogram equalized). The modulation
//   f(n; c) = (1 + c) n^c
// has a mean of 1 for any contrast c; N(lod) removes the deviation of the filtered texture per mip level.
uniform sampler3D u_FroxelNoise;

// table of 16 values at lod 0, 0.5, ..., 7.5
float FroxelNoiseNorm(in vec4 t0, in vec4 t1, in vec4 t2, in vec4 t3, in float lod)
{
	float l = clamp(lod, 0.0, 6.0) * 2.0;
	int i = int(l);
	vec4 a = (i < 4) ? t0 : ((i < 8) ? t1 : ((i < 12) ? t2 : t3));
	int j = i + 1;
	vec4 b = (j < 4) ? t0 : ((j < 8) ? t1 : ((j < 12) ? t2 : t3));
	return mix(a[i & 3], b[j & 3], l - float(i));
}

float FroxelNoiseContrast(in float n, in float c)
{
	return (1.0 + c) * pow(max(n, 1e-4), c);
}

// density modulation m(p), world anchored. viewDepth selects the mip level from the slice thickness
// (0: the finest level).
float FroxelNoiseModulation(in vec3 p, in float viewDepth)
{
	float footprint = log2(max(viewDepth * u_FroxelNoiseLod.z, 1e-6));
	float m = 1.0;

	float macroContrast = u_FroxelNoiseParams.z;
	if (macroContrast > 0.0)
	{
		float lod = max(footprint + u_FroxelNoiseLod.x, 0.0);
		vec3 uvw = p * u_FroxelNoiseParams.x - u_FroxelNoiseMacroOffset.xyz;
		float n = textureLod(u_FroxelNoise, uvw, lod).r;
		m *= FroxelNoiseContrast(n, macroContrast) *
			FroxelNoiseNorm(u_FroxelNoiseNormMacro[0], u_FroxelNoiseNormMacro[1],
				u_FroxelNoiseNormMacro[2], u_FroxelNoiseNormMacro[3], lod);
	}

	float detailContrast = u_FroxelNoiseParams.w;
	if (detailContrast > 0.0)
	{
		// rotated by 30 degrees around z and offset: the two tiles share no axis or origin
		float lod = max(footprint + u_FroxelNoiseLod.y, 0.0);
		vec3 q = p * u_FroxelNoiseParams.y;
		q.xy = vec2(0.8660254 * q.x - 0.5 * q.y, 0.5 * q.x + 0.8660254 * q.y);
		vec3 uvw = q + vec3(0.37, 0.61, 0.23) - u_FroxelNoiseDetailOffset.xyz;
		float n = textureLod(u_FroxelNoise, uvw, lod).g;
		m *= FroxelNoiseContrast(n, detailContrast) *
			FroxelNoiseNorm(u_FroxelNoiseNormDetail[0], u_FroxelNoiseNormDetail[1],
				u_FroxelNoiseNormDetail[2], u_FroxelNoiseNormDetail[3], lod);
	}

	return m;
}
#endif

// Integral of exp(-h / H) along a path of length len whose height above the base goes from h0 to h1
// (a straight segment): H / k * (exp(-h0 / H) - exp(-h1 / H)), k = (h1 - h0) / len. Nearly level
// segments use the midpoint (the difference would cancel).
float FroxelExpIntegral(in float h0, in float h1, in float len, in float invFalloff)
{
	float dh = (h1 - h0) * invFalloff;
	if (abs(dh) < 1e-3)
		return len * exp(-0.5 * (h0 + h1) * invFalloff);
	return len / dh * (exp(-h0 * invFalloff) - exp(-h1 * invFalloff));
}

// Optical depth of the height fog along the segment a -> b (length len), analytic: the density cap
// below the base is constant, above it exponential; the soft top is cut in the middle of its fade.
float FroxelHeightOpticalDepth(in vec3 a, in vec3 b, in float len)
{
	float h0 = a.z - u_FroxelHeightFog.y;
	float h1 = b.z - u_FroxelHeightFog.y;

	if (u_FroxelHeightFogTop.x > 0.0)
	{
		float top = 0.5 * (u_FroxelHeightFogColor.w + u_FroxelHeightFogTop.x);
		if (h0 >= top && h1 >= top)
			return 0.0;
		// keep the part below the top
		float t = (h1 != h0) ? clamp((top - h0) / (h1 - h0), 0.0, 1.0) : 1.0;
		if (h0 > top)
		{
			h0 = top;
			len *= 1.0 - t;
		}
		else if (h1 > top)
		{
			h1 = top;
			len *= t;
		}
	}

	// sigma = sigma0 * min(exp(-h / H), maxScale): capped (constant) below hCap
	float invFalloff = u_FroxelHeightFog.z;
	float hCap = -u_FroxelHeightFog.w / invFalloff;
	float maxScale = exp(u_FroxelHeightFog.w);
	float lo = min(h0, h1);
	float hi = max(h0, h1);
	float integral;
	if (lo >= hCap)
		integral = FroxelExpIntegral(h0, h1, len, invFalloff);
	else if (hi <= hCap)
		integral = len * maxScale;
	else
	{
		float capLen = len * (hCap - lo) / (hi - lo);
		integral = capLen * maxScale + FroxelExpIntegral(hCap, hi, len - capLen, invFalloff);
	}
	return u_FroxelHeightFog.x * integral;
}

// The medium beyond the slices, along the segment a -> b (direction dir, length len), analytic:
// the BSP fog volumes clipped to their bounds and visible side, and the height fog. rgb: optical
// depth weighted albedo, a: optical depth. The local fog volumes have faded out before far.
vec4 FroxelTailMedium(in vec3 a, in vec3 dir, in float len)
{
	int debugView = int(u_FroxelDebugParams.x);
	vec4 result = vec4(0.0);

	if (u_FroxelHeightFog.x > 0.0 && debugView != 11 && debugView != 16)
	{
		float tau = FroxelHeightOpticalDepth(a, a + dir * len, len);
		result += vec4(u_FroxelHeightFogColor.rgb * tau, tau);
	}

	int numFogs = (debugView == 12 || debugView == 16) ? 0 : u_FroxelNumFogs;
	vec3 invDir = 1.0 / mix(dir, vec3(1e-8), lessThan(abs(dir), vec3(1e-8)));
	for (int i = 0; i < numFogs; i++)
	{
		// slab test of the bounds
		vec3 t0 = (u_FroxelFogMins[i].xyz - a) * invDir;
		vec3 t1 = (u_FroxelFogMaxs[i].xyz - a) * invDir;
		vec3 tMin = min(t0, t1);
		vec3 tMax = max(t0, t1);
		float enter = max(max(max(tMin.x, tMin.y), tMin.z), 0.0);
		float leave = min(min(min(tMax.x, tMax.y), tMax.z), len);
		if (enter >= leave)
			continue;

		// the visible side of the fog plane (inFog of CalcFog): dot(p, n) - dist >= 0
		if (u_FroxelFogMins[i].w > 0.5)
		{
			vec4 plane = u_FroxelFogPlane[i];
			float f0 = dot(a, plane.xyz) - plane.w;
			float k = dot(dir, plane.xyz);
			if (abs(k) < 1e-8)
			{
				if (f0 < 0.0)
					continue;
			}
			else if (k > 0.0)
				enter = max(enter, -f0 / k);
			else
				leave = min(leave, -f0 / k);
			if (enter >= leave)
				continue;
		}

		vec4 fog = u_FroxelFogColor[i];
		float tau = fog.a * (leave - enter);
		result += vec4(fog.rgb * tau, tau);
	}

	return result;
}

// In-scattering (rgb) and transmittance (a) between the camera and the view depth d of worldPos,
// along the ray through uv (froxel volume texture coordinates). rayScale: path length per unit of
// view depth.
vec4 FroxelLookup(in vec2 uv, in float d, in float rayScale, in vec3 worldPos)
{
	float numSlices = u_FroxelGridSize.z;
	float farZ = u_FroxelSliceParams.y;
	float firstBoundary = FroxelWToDepth(1.0 / numSlices);

	float dc = clamp(d, 0.0, farZ);
	// texel k holds the value at B(k + 1)
	float b = FroxelDepthToW(max(dc, firstBoundary)) * numSlices;
	vec4 fog = texture(u_FroxelVolume, vec3(uv, (b - 0.5) / numSlices));

	// the first slice starts at the camera
	if (dc < firstBoundary)
		fog = mix(vec4(0.0, 0.0, 0.0, 1.0), fog, dc / firstBoundary);

	// beyond the slices: the media between far and d (bounded fog volumes end or begin there), lit
	// by the light at the far side of the volume (u_FroxelTail, without albedo)
	if (d > farZ)
	{
		vec3 toPos = worldPos - u_FroxelViewOrigin.xyz;
		vec3 a = u_FroxelViewOrigin.xyz + toPos * (farZ / d);
		float len = (d - farZ) * rayScale;
		vec4 medium = FroxelTailMedium(a, toPos / max(length(toPos), 1e-6), len);
		if (medium.a > 0.0)
		{
			vec3 light = texture(u_FroxelTail, uv).rgb;
			float t = exp(-medium.a);
			fog.rgb += fog.a * (medium.rgb / medium.a) * light * (1.0 - t);
			fog.a *= t;
		}
	}

	return fog;
}

// In-scattering (rgb) and transmittance (a) between the camera and worldPos
vec4 FroxelFog(in vec3 worldPos)
{
	vec4 clip = u_FroxelViewProjection * vec4(worldPos, 1.0);
	if (clip.w <= 0.0)
		return vec4(0.0, 0.0, 0.0, 1.0);

	vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
	vec3 toPos = worldPos - u_FroxelViewOrigin.xyz;
	float d = dot(toPos, u_FroxelViewForward.xyz);
	float rayScale = length(toPos) / max(d, 1e-3);

	// a frozen volume (r_volumetricFogFreeze) only covers its own frustum
	if (u_FroxelDebugParams.z > 0.5 && (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))))
		return vec4(0.0, 0.0, 0.0, 1.0);

	return FroxelLookup(clamp(uv, 0.0, 1.0), d, rayScale, worldPos);
}

// World position of the depth buffer sample at render target coordinates tc. The first person view
// model (depth hack range) is moved back to its real depth, the sky is at the sky distance.
vec3 FroxelSceneWorldPosition(in vec2 tc, in float depth)
{
	vec2 ndc = (tc - u_FroxelViewport.xy) / u_FroxelViewport.zw * 2.0 - 1.0;

	if (depth >= 1.0)
	{
		vec4 farPoint = u_FroxelInvViewProjection * vec4(ndc, 1.0, 1.0);
		vec3 dir = farPoint.xyz / farPoint.w - u_FroxelViewOrigin.xyz;
		dir /= max(dot(dir, u_FroxelViewForward.xyz), 1e-6);
		return u_FroxelViewOrigin.xyz + dir * u_FroxelSliceParams.w;
	}

	if (depth <= FROXEL_DEPTH_HACK_MAX)
		depth /= 0.3;

	vec4 p = u_FroxelInvViewProjection * vec4(ndc, depth * 2.0 - 1.0, 1.0);
	return p.xyz / p.w;
}
#endif
