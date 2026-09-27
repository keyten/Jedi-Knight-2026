/*[Vertex]*/
// every slice in one instanced draw: instance k = slice u_FroxelSlice + k (u_FroxelSlice -1: the tail
// pass, one instance, not layered)
uniform int u_FroxelSlice;
flat out int var_VertexSlice;

void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
	var_VertexSlice = u_FroxelSlice + gl_InstanceID;
}

/*[Geometry]*/
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

flat in int var_VertexSlice[];
flat out int var_Slice;

void main()
{
	for (int i = 0; i < 3; i++)
	{
		gl_Layer = max(var_VertexSlice[0], 0);
		gl_Position = gl_in[i].gl_Position;
		var_Slice = var_VertexSlice[0];
		EmitVertex();
	}
	EndPrimitive();
}

/*[Fragment]*/
// Froxel fog injection (tr_volumetric.cpp): one slice of the froxel volume per draw.
//
// out_Color    RGBA16F  rgb = emission (extinction * albedo * light) of the baked light and the
//                       sun, a = extinction; temporally filtered with the reprojected history
// out_Dynamic  R11G11B10F  emission of the dynamic lights, this frame only (no history, so moving
//                       sabers, bolts and explosions leave no trails)
//
// Media: the BSP fog volumes (axial bounds + the plane of their visible side), extinction and color
// as the legacy volumetric fog, plus the optional height fog (r_volumetricFogHeight*, off by default)
// whose extinction depends on world z, plus the local fog volumes (soft analytic ellipsoids / boxes,
// tr_fogvolume.cpp) listed for this slice. Extinctions add, albedos are extinction weighted. Without
// any of them there is no medium. The selected media (r_volumetricFogNoise) are multiplied by the world space
// density noise m(p) (mean 1): only the extinction changes, the light does not.
//
// Light (the phase function is 4 pi HG, 1 = isotropic):
//   baked   light grid without the sun (isotropic, legacy brightness)
//   sun     inside the cascaded shadow maps: realtime sun radiance * shadow * phase, beyond them (and
//           where the cascades see no sun in the whole light grid cell) the baked sun part of the
//           light grid * phase
//   dynamic the lights of the froxel's cluster (CPU binned per tile and slice, R_VolumetricBuildLightLists)
//           * attenuation * phase * its shadow map

uniform sampler3D u_FroxelHistory;
uniform sampler3D u_VolumetricStaticGrid;
uniform sampler3D u_VolumetricSunGrid;
uniform sampler3D u_VolumetricDirGrid;		// directed non-sun light (rgb), its luminance (a)
uniform sampler3D u_VolumetricDirVecGrid;	// direction towards the light * luminance (rgb)
uniform sampler3D u_VolumetricLegacyGrid;	// merged legacy light grid (debug view 25)
#if defined(USE_SHADOWS2)
uniform sampler2DArray u_ShadowMap;		// raw sun cascade depth
#else
uniform sampler2DArrayShadow u_ShadowMap;	// legacy sun cascades
#endif
uniform sampler2DArrayShadow u_ShadowMap2;	// dynamic light cube faces, 6 layers per light

flat in int var_Slice;	// slice of this layer, -1 = tail pass

// dynamic light lists: 2 texels per light (origin, radius | color, shadow cube layer), and per
// cluster a header (first entry | count << 24) followed by the light indexes
uniform samplerBuffer u_FPlusLights;
uniform usamplerBuffer u_FPlusGridMap;

struct Light
{
	vec4 origin;
	vec3 color;
	float radius;
};

layout(std140) uniform Lights
{
	mat4 u_ShadowMvp;
	mat4 u_ShadowMvp2;
	mat4 u_ShadowMvp3;
	vec4 u_ShadowSplits;
	vec4 u_ShadowBlend;
	vec4 u_ShadowTexelSize;
	vec4 u_ShadowDepthSpan;
	vec4 u_ShadowBias;
	vec4 u_ShadowPcss;
	vec4 u_ShadowDebug;
	int u_NumLights;
	Light u_Lights[MAX_DLIGHTS];
};

// fragment outputs are bound to draw buffers by name (shaderOutputNames, tr_glsl.cpp):
// 0 = out_Color, 1 = out_Glow
out vec4 out_Color;
out vec4 out_Glow;
#define out_Dynamic out_Glow

float Luma(in vec3 c)
{
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// froxel coordinates (x, y in froxels, z in slices) to world space
vec3 FroxelWorldPosition(in vec3 froxel)
{
	vec2 ndc = froxel.xy / u_FroxelGridSize.xy * 2.0 - 1.0;
	float d = FroxelWToDepth(froxel.z / u_FroxelGridSize.z);
	vec3 ray = u_FroxelRayForward.xyz + ndc.x * u_FroxelRayRight.xyz + ndc.y * u_FroxelRayUp.xyz;
	return u_FroxelViewOrigin.xyz + ray * d;
}

// extinction of the height fog at p, world anchored:
//   sigma0 * min(exp(-(z - base) / falloff), maxScale) * (1 - smoothstep(top - fade, top, z - base))
float FroxelHeightExtinction(in vec3 p)
{
	float h = p.z - u_FroxelHeightFog.y;
	float extinction = u_FroxelHeightFog.x * exp(min(-h * u_FroxelHeightFog.z, u_FroxelHeightFog.w));
	if (u_FroxelHeightFogTop.x > 0.0)
		extinction *= 1.0 - smoothstep(u_FroxelHeightFogColor.w, u_FroxelHeightFogTop.x, h);
	return extinction;
}

// extinction (a) and albedo (rgb) of the fog volumes, the height fog and the local fog volumes of
// the slice at p, and the FX particle media (debug views 11, 12, 16 and 26 keep one of them, 14 drops
// the density noise).
// noisyFraction: share of the extinction that comes from noise modulated media (their history weight
// is lowered when the noise moves). localFraction: share of the local volumes (before the noise).
// localChange (only with wantChange): how much the local medium at p changed since the previous
// frame, relative to it (moving, appearing and vanishing volumes; 0 when they are static).
// particleChange: the same for the FX particle media (their own history reduction), particleFraction:
// their share of the extinction (before the noise).
vec4 FroxelMedium(in vec3 p, in int debugView, in bool wantChange, out float noisyFraction,
	out float localFraction, out float localChange, out float particleFraction, out float particleChange)
{
	bool noise = u_FroxelNoiseLod.w > 0.5 && debugView != 14;
	float extinction = 0.0;
	vec3 albedo = vec3(0.0);
	float noisyExtinction = 0.0;
	vec3 noisyAlbedo = vec3(0.0);

	// local fog volumes: the packed list of this slice (CPU culled, tr_fogvolume.cpp)
	float localExtinction = 0.0;
	float localPrevious = 0.0;
	float localDelta = 0.0;
	int localHeader = (u_FroxelLocalParams.x > 0.5 && debugView != 11 && debugView != 12 && debugView != 26) ?
		FroxelLocalSliceHeader(var_Slice) : 0;
	int localCount = localHeader >> 16;
	if (localCount > 0)
	{
		int first = localHeader & 0xffff;
		float fade = FroxelLocalFade(dot(p - u_FroxelViewOrigin.xyz, u_FroxelViewForward.xyz));
		for (int j = 0; j < localCount; j++)
		{
			int i = FroxelLocalPoolIndex(first + j);
			vec4 shape = u_FroxelLocalShape[i];
			vec4 color = u_FroxelLocalColor[i];
			float e = color.a * fade * FroxelLocalShapeDensity(u_FroxelLocalX[i], u_FroxelLocalY[i],
				u_FroxelLocalZ[i], shape.x, shape.y, shape.z, p);

			if (wantChange)
			{
				vec4 motion = u_FroxelLocalMotion[i];
				float previous = e;
				if (motion.x > 0.5)
				{
					previous = motion.y * fade * FroxelLocalShapeDensity(u_FroxelLocalPrevX[i],
						u_FroxelLocalPrevY[i], u_FroxelLocalPrevZ[i], motion.x - 1.0, motion.z, motion.w, p);
				}
				localPrevious += previous;
				localDelta += abs(e - previous);
			}

			if (e <= 0.0)
				continue;

			localExtinction += e;
			if (noise && shape.w > 0.5)
			{
				noisyExtinction += e;
				noisyAlbedo += color.rgb * e;
			}
			else
			{
				extinction += e;
				albedo += color.rgb * e;
			}
		}
	}
	localChange = localDelta / max(max(localExtinction, localPrevious), 1e-12);

	// FX particle media: the packed list of this slice (CPU culled and capped, tr_volparticle.cpp)
	float particleExtinction = 0.0;
	float particlePrevious = 0.0;
	float particleDelta = 0.0;
	int particleHeader = (u_FroxelParticleParams.x > 0.5 && debugView != 11 && debugView != 12 &&
		debugView != 16) ? FroxelParticleSliceHeader(var_Slice) : 0;
	int particleCount = particleHeader >> 16;
	if (particleCount > 0)
	{
		int first = particleHeader & 0xffff;
		float fade = FroxelParticleFade(dot(p - u_FroxelViewOrigin.xyz, u_FroxelViewForward.xyz));
		for (int j = 0; j < particleCount; j++)
		{
			int i = FroxelParticlePoolIndex(first + j);
			vec4 center = u_FroxelParticleCenter[i];
			float e = center.w * fade * FroxelParticleDensity(center.xyz, u_FroxelParticleInvExtent[i], p);
			vec4 color = u_FroxelParticleColor[i];

			if (wantChange)
			{
				vec4 prevCenter = u_FroxelParticlePrevCenter[i];
				float previous = e;
				if (prevCenter.w > 0.5)
				{
					previous = color.w * fade * FroxelParticleDensity(prevCenter.xyz,
						u_FroxelParticlePrevInvExtent[i], p);
				}
				particlePrevious += previous;
				particleDelta += abs(e - previous);
			}

			if (e <= 0.0)
				continue;

			particleExtinction += e;
			extinction += e;
			albedo += color.rgb * e;
		}
	}
	particleChange = particleDelta / max(max(particleExtinction, particlePrevious), 1e-12);

	if (u_FroxelHeightFog.x > 0.0 && debugView != 11 && debugView != 16 && debugView != 26)
	{
		float e = FroxelHeightExtinction(p);
		if (noise && u_FroxelNoiseMacroOffset.w > 0.5)
		{
			noisyExtinction += e;
			noisyAlbedo += u_FroxelHeightFogColor.rgb * e;
		}
		else
		{
			extinction += e;
			albedo += u_FroxelHeightFogColor.rgb * e;
		}
	}

	// the fog volumes that may touch this slice (CPU culled)
	int fogMask = (debugView == 12 || debugView == 16 || debugView == 26) ? 0 :
		u_FroxelFogSlices[var_Slice >> 2][var_Slice & 3];
	int numFogs = (fogMask != 0) ? u_FroxelNumFogs : 0;
	for (int i = 0; i < numFogs; i++)
	{
		if (((fogMask >> i) & 1) == 0)
			continue;

		vec4 mins = u_FroxelFogMins[i];
		vec4 maxs = u_FroxelFogMaxs[i];
		if (any(lessThan(p, mins.xyz)) || any(greaterThan(p, maxs.xyz)))
			continue;

		// same test as CalcFog (inFog) of the legacy fog
		vec4 plane = u_FroxelFogPlane[i];
		if (mins.w > 0.5 && dot(p, plane.xyz) - plane.w < 0.0)
			continue;

		vec4 fog = u_FroxelFogColor[i];
		if (noise && maxs.w > 0.5)
		{
			noisyExtinction += fog.a;
			noisyAlbedo += fog.rgb * fog.a;
		}
		else
		{
			extinction += fog.a;
			albedo += fog.rgb * fog.a;
		}
	}

	localFraction = localExtinction / max(extinction + noisyExtinction, 1e-12);
	particleFraction = particleExtinction / max(extinction + noisyExtinction, 1e-12);

	noisyFraction = 0.0;
	if (noisyExtinction > 0.0)
	{
		float viewDepth = dot(p - u_FroxelViewOrigin.xyz, u_FroxelViewForward.xyz);
		float m = FroxelNoiseModulation(p, viewDepth);
		noisyExtinction *= m;
		extinction += noisyExtinction;
		albedo += noisyAlbedo * m;
		noisyFraction = noisyExtinction / max(extinction, 1e-12);
	}

	return vec4(albedo / max(extinction, 1e-12), extinction);
}

// one hardware filtered tap with temporal accumulation (the jittered positions soften the beams),
// four otherwise
#if defined(USE_SHADOWS2)
float FroxelSunCompare(in float layer, in vec2 uv, in float ref)
{
	ivec2 shadowSize = textureSize(u_ShadowMap, 0).xy;
	ivec2 p = clamp(ivec2(uv * vec2(shadowSize)), ivec2(0), shadowSize - ivec2(1));
	return ref <= texelFetch(u_ShadowMap, ivec3(p, int(layer)), 0).r ? 1.0 : 0.0;
}
#endif

float SunShadowTap(in float layer, in vec3 shadowPos, in float bias, in float temporal)
{
	float ref = shadowPos.z - bias;
	#if defined(USE_SHADOWS2)
	if (temporal > 0.5)
		return FroxelSunCompare(layer, shadowPos.xy, ref);

	float texel = 0.75 / u_FroxelShadowParams.y;
	float result = 0.0;
	result += FroxelSunCompare(layer, shadowPos.xy + vec2(-texel, -texel), ref);
	result += FroxelSunCompare(layer, shadowPos.xy + vec2( texel, -texel), ref);
	result += FroxelSunCompare(layer, shadowPos.xy + vec2(-texel,  texel), ref);
	result += FroxelSunCompare(layer, shadowPos.xy + vec2( texel,  texel), ref);
	#else
	if (temporal > 0.5)
		return texture(u_ShadowMap, vec4(shadowPos.xy, layer, ref));

	float texel = 0.75 / u_FroxelShadowParams.y;
	float result = 0.0;
	result += texture(u_ShadowMap, vec4(shadowPos.xy + vec2(-texel, -texel), layer, ref));
	result += texture(u_ShadowMap, vec4(shadowPos.xy + vec2( texel, -texel), layer, ref));
	result += texture(u_ShadowMap, vec4(shadowPos.xy + vec2(-texel,  texel), layer, ref));
	result += texture(u_ShadowMap, vec4(shadowPos.xy + vec2( texel,  texel), layer, ref));
	#endif
	return result * 0.25;
}

#if defined(USE_SHADOWS2)
float SunShadowCascade(in mat4 shadowMvp, in float layer, in vec3 p,
	in float bias, in float temporal)
{
	vec4 projected = shadowMvp * vec4(p, 1.0);
	vec3 shadowPos = projected.xyz / projected.w * 0.5 + 0.5;
	if (any(lessThan(shadowPos, vec3(0.0))) || any(greaterThan(shadowPos, vec3(1.0))))
		return 1.0;
	return SunShadowTap(layer, shadowPos, bias, temporal);
}
#endif

// Sun visibility from the cascaded shadow maps (cascade selection as sunShadow() of lightall).
// coverage: 1 inside the cascades, fading to 0 at their far end (then the baked sun is used).
float SunShadow(in vec3 p, in float temporal, out float coverage)
{
	#if defined(USE_SHADOWS2)
	float bias = u_FroxelShadowParams.w;
	float viewDepth = dot(p - u_FroxelViewOrigin.xyz, normalize(u_FroxelRayForward.xyz));
	float split0 = u_ShadowSplits.x;
	float split1 = u_ShadowSplits.y;
	float half0 = u_ShadowBlend.x;
	float half1 = u_ShadowBlend.y;
	float result;

	if (half0 > 0.0 && viewDepth >= split0 - half0 && viewDepth <= split0 + half0)
	{
		float nearResult = SunShadowCascade(u_ShadowMvp, 0.0, p, bias, temporal);
		float farResult = SunShadowCascade(u_ShadowMvp2, 1.0, p, bias, temporal);
		result = mix(nearResult, farResult, smoothstep(split0 - half0, split0 + half0, viewDepth));
	}
	else if (half1 > 0.0 && viewDepth >= split1 - half1 && viewDepth <= split1 + half1)
	{
		float nearResult = SunShadowCascade(u_ShadowMvp2, 1.0, p, bias, temporal);
		float farResult = SunShadowCascade(u_ShadowMvp3, 2.0, p, bias, temporal);
		result = mix(nearResult, farResult, smoothstep(split1 - half1, split1 + half1, viewDepth));
	}
	else if (viewDepth < split0)
		result = SunShadowCascade(u_ShadowMvp, 0.0, p, bias, temporal);
	else if (viewDepth < split1)
		result = SunShadowCascade(u_ShadowMvp2, 1.0, p, bias, temporal);
	else
		result = SunShadowCascade(u_ShadowMvp3, 2.0, p, bias, temporal);

	coverage = 1.0 - smoothstep(u_ShadowSplits.w, u_ShadowSplits.z, viewDepth);
	return mix(1.0, result, coverage);
	#else
	coverage = 1.0;
	float bias = u_FroxelShadowParams.w;
	float edge = 0.5 - 2.0 / u_FroxelShadowParams.y;
	vec4 shadowPos = u_ShadowMvp * vec4(p, 1.0);
	shadowPos.xyz = shadowPos.xyz / shadowPos.w * 0.5 + 0.5;
	if (all(lessThanEqual(abs(shadowPos.xyz - vec3(0.5)), vec3(edge))))
		return SunShadowTap(0.0, shadowPos.xyz, bias, temporal);

	shadowPos = u_ShadowMvp2 * vec4(p, 1.0);
	shadowPos.xyz = shadowPos.xyz / shadowPos.w * 0.5 + 0.5;
	if (all(lessThanEqual(abs(shadowPos.xyz - vec3(0.5)), vec3(edge))))
		return SunShadowTap(1.0, shadowPos.xyz, bias, temporal);

	shadowPos = u_ShadowMvp3 * vec4(p, 1.0);
	shadowPos.xyz = shadowPos.xyz / shadowPos.w * 0.5 + 0.5;
	if (all(lessThanEqual(abs(shadowPos.xyz - vec3(0.5)), vec3(0.5))))
	{
		float cameraDistance = length(p - u_FroxelViewOrigin.xyz);
		coverage = 1.0 - clamp(cameraDistance / u_FroxelShadowParams.x * 10.0 - 9.0, 0.0, 1.0);
		return SunShadowTap(2.0, shadowPos.xyz, bias, temporal);
	}

	coverage = 0.0;
	return 1.0;
	#endif
}

// cube map face lookup of the dynamic light shadow maps, as lightall.glsl
vec3 FroxelSampleCube(in vec3 v)
{
	vec3 vAbs = abs(v);
	float ma = 0.0;
	vec2 uv = vec2(0.0);
	float faceIndex = 0.0;
	if (vAbs.z >= vAbs.x && vAbs.z >= vAbs.y)
	{
		faceIndex = v.z < 0.0 ? 5.0 : 4.0;
		ma = 0.5 / vAbs.z;
		uv = vec2(v.z < 0.0 ? -v.x : v.x, -v.y);
	}
	else if (vAbs.y >= vAbs.x)
	{
		faceIndex = v.y < 0.0 ? 3.0 : 2.0;
		ma = 0.5 / vAbs.y;
		uv = vec2(v.x, v.y < 0.0 ? -v.z : v.z);
	}
	else
	{
		faceIndex = v.x < 0.0 ? 1.0 : 0.0;
		ma = 0.5 / vAbs.x;
		uv = vec2(v.x < 0.0 ? v.z : -v.z, -v.y);
	}
	return vec3(uv * ma + 0.5, faceIndex);
}

// depth of the dynamic light shadow maps (perspective, near 1, far = light radius), as lightall.glsl
float FroxelLightDepth(in vec3 v, in float f)
{
	vec3 absV = abs(v);
	float z = max(absV.x, max(absV.y, absV.z));
	const float n = 1.0;
	float normZ = (f + n) / (f - n) - 2.0 * f * n / (z * (f - n));
	return (normZ + 1.0) * 0.5;
}

// L: froxel to light
float DynamicLightShadow(in vec3 L, in float dist, in float radius, in int lightIndex)
{
	// move towards the light: no surface normal to offset along
	vec3 dir = L / dist;
	vec3 sampleVector = L - dir * min(2.0 + 0.02 * dist, 0.5 * dist);
	float depth = FroxelLightDepth(sampleVector, radius);

	// 4 taps around the direction, a fixed pattern (no history for dynamic lights)
	vec3 up = abs(dir.z) < 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	vec3 t = normalize(cross(up, dir));
	vec3 b = cross(dir, t);
	const float spread = 0.006;
	float layer = float(lightIndex) * 6.0;
	float result = 0.0;
	result += texture(u_ShadowMap2, vec4(FroxelSampleCube(dir + (t + b) * spread) + vec3(0.0, 0.0, layer), depth));
	result += texture(u_ShadowMap2, vec4(FroxelSampleCube(dir + (t - b) * spread) + vec3(0.0, 0.0, layer), depth));
	result += texture(u_ShadowMap2, vec4(FroxelSampleCube(dir + (-t + b) * spread) + vec3(0.0, 0.0, layer), depth));
	result += texture(u_ShadowMap2, vec4(FroxelSampleCube(dir + (-t - b) * spread) + vec3(0.0, 0.0, layer), depth));
	return result * 0.25;
}

// cluster header of the froxel cell in the slice: first entry | count << 24, 0 = no lights
uint FroxelLightCluster(in ivec2 cell, in int slice)
{
	if (u_FroxelLightTile <= 0)
		return 0u;
	ivec2 tile = cell / u_FroxelLightTile;
	int cluster = (slice * u_FroxelLightTilesY + tile.y) * u_FroxelLightTilesX + tile.x;
	return texelFetch(u_FPlusGridMap, cluster).r;
}

// dynamic lights of the cluster at p (viewDir: camera to p, unit)
vec3 DynamicLights(in uint cluster, in vec3 p, in vec3 viewDir, in float g)
{
	vec3 light = vec3(0.0);
	int first = int(cluster & 0xffffffu);
	int count = int(cluster >> 24);
	for (int j = 0; j < count; j++)
	{
		int i = int(texelFetch(u_FPlusGridMap, first + j).r);
		vec4 originRadius = texelFetch(u_FPlusLights, i * 2);
		vec4 colorLayer = texelFetch(u_FPlusLights, i * 2 + 1);
		float radius = originRadius.w;

		vec3 L = originRadius.xyz - p;
		float sqrDist = max(dot(L, L), 1e-4);
		// CalcLightAttenuation of lightall: zero at the radius
		float attenuation = clamp(0.5 * radius * radius / sqrDist - 0.5, 0.0, 1.0);
		if (attenuation <= 0.0)
			continue;

		float dist = sqrt(sqrDist);
		// light travels from the light (-L) to the camera (-viewDir)
		float phase = FroxelPhase(g, dot(L / dist, viewDir));

		float shadow = 1.0;
		// shadow cube layer (legacy: i, Forward+: slot or -1)
		int shadowLayer = int(colorLayer.w);
		if (u_FroxelShadowParams.z > 0.5 && shadowLayer >= 0)
			shadow = DynamicLightShadow(L, dist, radius, shadowLayer);

		light += colorLayer.rgb * attenuation * phase * shadow;
	}

	return light;
}

// Baked light (returned: isotropic part + directed part with its phase) and sun (phase included) at
// p. sunUnshadowed: the sun without its realtime shadow (debug view 2). Debug views 20-25 replace the
// returned light with one baked term (no sun, no phase unless stated).
vec3 BakedAndSunLight(in vec3 p, in float temporal, in float g, in int debugView, out vec3 sunLight,
	out vec3 sunUnshadowed)
{
	sunLight = vec3(0.0);
	sunUnshadowed = vec3(0.0);

	vec3 viewDir = normalize(p - u_FroxelViewOrigin.xyz);
	vec3 gridCoord = (p - u_FroxelGridOrigin.xyz) * u_FroxelGridScale.xyz;
	vec4 staticGrid = texture(u_VolumetricStaticGrid, gridCoord);
	vec3 isotropic = staticGrid.rgb * u_FroxelLightParams.w;
	float trust = staticGrid.a;

	// directed (non-sun) part of the light grid (R_BuildVolumetricLightGrid). The direction is
	// weighted by the luminance: between cells lit from different directions the filtered vector
	// is shorter than the luminance, the phase fades to isotropic in proportion (coherence).
	vec4 dirGrid = texture(u_VolumetricDirGrid, gridCoord);
	vec3 directed = dirGrid.rgb * u_FroxelLightParams.w;
	vec3 dirVec = texture(u_VolumetricDirVecGrid, gridCoord).rgb;
	float dirLength = length(dirVec);
	float coherence = (dirGrid.a > 1e-6) ? clamp(dirLength / dirGrid.a, 0.0, 1.0) : 0.0;
	vec3 lightDir = (dirLength > 1e-8) ? dirVec / dirLength : vec3(0.0, 0.0, 1.0);
	vec3 directedPhased = directed;
	if (u_FroxelDebugParams.w > 0.5)
	{
		// the light travels along -lightDir, towards the camera is -viewDir
		directedPhased *= mix(1.0, FroxelPhase(g, dot(lightDir, viewDir)), coherence);
	}
	vec3 staticLight = isotropic + directedPhased;

	if (debugView >= 20 && debugView <= 25)
	{
		vec3 bakedSun = (u_FroxelSunDirection.w > 0.5) ? texture(u_VolumetricSunGrid, gridCoord).rgb : vec3(0.0);
		if (debugView == 20)
			return isotropic;
		if (debugView == 21)
			return directed;
		if (debugView == 22)	// direction towards the light, dimmed by the incoherence
			return (lightDir * 0.5 + 0.5) * coherence * dot(directed, vec3(0.2126, 0.7152, 0.0722));
		if (debugView == 23)
			return bakedSun * u_FroxelLightParams.w;
		vec3 reconstructed = staticGrid.rgb + dirGrid.rgb + bakedSun;
		if (debugView == 24)
			return reconstructed * u_FroxelLightParams.w;
		// 25: 100 * |I + D + B - legacy| (the legacy grid of the fog without the split)
		return abs(reconstructed - texture(u_VolumetricLegacyGrid, gridCoord).rgb) * 100.0;
	}

	if (u_FroxelSunDirection.w > 0.5)
	{
		// sunlight travels along -sunDirection, towards the camera is -viewDir
		float phase = FroxelPhase(g, dot(u_FroxelSunDirection.xyz, viewDir));
		vec3 bakedSun = texture(u_VolumetricSunGrid, gridCoord).rgb;
		sunUnshadowed = bakedSun;
		sunLight = bakedSun;
		if (u_FroxelSunColor.w > 0.5)
		{
			float coverage;
			float shadow = SunShadow(p, temporal, coverage);

			// The split light grid only knows the light direction: a lamp straight above can
			// look like a high sun. The baked sun part is trusted only where the sky is visible from
			// the light grid cell (traced at map load, R_BuildVolumetricLightGrid); deep in shadow
			// (indoors) it stays baked light.
			coverage *= trust;
			sunLight = mix(bakedSun, u_FroxelSunColor.rgb * shadow, coverage);
			sunUnshadowed = mix(bakedSun, u_FroxelSunColor.rgb, coverage);
		}
		sunLight *= phase * u_FroxelLightParams.y;
		sunUnshadowed *= phase * u_FroxelLightParams.y;
	}

	return staticLight;
}

void main()
{
	ivec2 cell = ivec2(gl_FragCoord.xy);
	float temporal = u_FroxelJitter.w;
	int debugView = int(u_FroxelDebugParams.x);
	float g = u_FroxelLightParams.x;

	// tail pass (var_Slice < 0, into u_FroxelTail): the light at the far side of the volume,
	// without albedo, lights the media beyond far (FroxelLookup). No medium test: a fog volume may
	// start beyond far.
	if (var_Slice < 0)
	{
		vec3 pf = FroxelWorldPosition(vec3(vec2(cell) + 0.5, u_FroxelGridSize.z));
		vec3 sunTail, unusedSun;
		vec3 light = BakedAndSunLight(pf, 0.0, g, debugView, sunTail, unusedSun) + sunTail;
		if (debugView == 3)
			light = sunTail;
		else if (debugView == 4)
			light = vec3(0.0);
		else if (debugView == 5)
			light -= sunTail;
		if (any(isnan(light)) || any(isinf(light)))
			light = vec3(0.0);
		out_Color = vec4(light, 1.0);
		out_Dynamic = vec4(0.0);
		return;
	}

	float slice = float(var_Slice);

	// the baked light and the sun are sampled at a jittered position and accumulated over frames, the
	// dynamic lights at the froxel center
	vec3 center = vec3(vec2(cell) + 0.5, slice + 0.5);
	vec3 p = FroxelWorldPosition(center + u_FroxelJitter.xyz * temporal);
	vec3 pc = FroxelWorldPosition(center);

	float noisyFraction, localFraction, localChange, particleFraction, particleChange;
	vec4 medium = FroxelMedium(p, debugView, true, noisyFraction, localFraction, localChange,
		particleFraction, particleChange);

	// FX particles (smoke) change all the time: where their density changed the history keeps only the
	// floor r_volParticlesHistory of its weight (no long smoke ghost), not 0 (the jittered samples of a
	// drifting puff would flicker)
	float particleKeep = mix(1.0, u_FroxelParticleParams.w, smoothstep(0.02, 0.25, particleChange));

	// the medium at the center is only needed where the cluster has dynamic lights
	uint cluster = FroxelLightCluster(cell, var_Slice);
	vec4 mediumCenter = vec4(0.0);
	if ((cluster >> 24) != 0u)
	{
		float unused0, unused1, unused2, unused3, unused4;
		mediumCenter = FroxelMedium(pc, debugView, false, unused0, unused1, unused2, unused3, unused4);
	}

	// baked light and sun
	vec3 staticLight = vec3(0.0);
	vec3 sunLight = vec3(0.0);
	vec3 sunUnshadowed = vec3(0.0);
	if (medium.a > 0.0)
		staticLight = BakedAndSunLight(p, temporal, g, debugView, sunLight, sunUnshadowed);

	// dynamic lights
	vec3 dynamicLight = vec3(0.0);
	if (mediumCenter.a > 0.0)
	{
		vec3 viewDir = normalize(pc - u_FroxelViewOrigin.xyz);
		dynamicLight = DynamicLights(cluster, pc, viewDir, g) * u_FroxelLightParams.z;
	}

	// debug views of a single light term
	if (debugView == 2)
	{
		sunLight = sunUnshadowed;
		staticLight = vec3(0.0);
		dynamicLight = vec3(0.0);
	}
	else if (debugView == 3)
	{
		staticLight = vec3(0.0);
		dynamicLight = vec3(0.0);
	}
	else if (debugView == 4)
	{
		staticLight = vec3(0.0);
		sunLight = vec3(0.0);
	}
	else if (debugView == 5 || (debugView >= 20 && debugView <= 25))
	{
		sunLight = vec3(0.0);
		dynamicLight = vec3(0.0);
	}

	vec4 current = vec4(medium.rgb * medium.a * (staticLight + sunLight), medium.a);

	// debug view 17: share of the local volumes (red) and of the other media (green), integrated
	// like an emission: the integrated rg is each medium's share of the opacity along the ray
	if (debugView == 17)
		current.rgb = vec3(localFraction, 1.0 - localFraction, 0.0) * medium.a;

	// debug view 27: FX particle media, red = history reduction where the particle density changed,
	// green = their share of the medium, integrated like view 17
	if (debugView == 27)
		current.rgb = vec3(1.0 - particleKeep, particleFraction, 0.0) * medium.a;

	// temporal accumulation with the reprojected history. Noise modulated media that move with the
	// wind use a lower weight (R_VolumetricNoise), so the drifting density leaves no trail. Where a
	// local volume moved, appeared or vanished the history is dropped in proportion to the change of
	// its density: no smoke ghost behind a moving volume, full history for static ones.
	float weight = u_FroxelTemporalParams.x;
	float froxelWeight = mix(weight, u_FroxelNoiseDetailOffset.w, noisyFraction);
	froxelWeight *= 1.0 - smoothstep(0.02, 0.25, localChange);
	froxelWeight *= particleKeep;
	if (weight > 0.0)
	{
		vec4 prevClip = u_FroxelPrevViewProjection * vec4(pc, 1.0);
		weight = 0.0;
		if (prevClip.w > 0.0)
		{
			vec3 coord = vec3(prevClip.xy / prevClip.w * 0.5 + 0.5, FroxelDepthToW(prevClip.w));
			// disocclusion: outside of the previous volume
			if (all(greaterThanEqual(coord, vec3(0.0))) && all(lessThanEqual(coord, vec3(1.0))))
			{
				vec4 history = texture(u_FroxelHistory, coord);

				// a corrupt history (NaN, Inf) would be fed back forever: drop it
				if (any(isnan(history)) || any(isinf(history)) || history.a < 0.0)
					history = current;

				// the light may have changed (moving shadows, switched lights): clamp the radiance
				// of the history to a range around the current one
				if (current.a > 0.0 && history.a > 0.0)
				{
					float k = u_FroxelTemporalParams.w;
					vec3 radiance = current.rgb / current.a;
					vec3 historyRadiance = clamp(history.rgb / history.a, radiance / k, radiance * k + 1e-4);
					history.rgb = historyRadiance * history.a;
				}

				weight = froxelWeight;
				current = mix(current, history, weight);
			}
		}
	}

	if (debugView == 8)
		current.rgb = vec3(weight) * current.a;

	// one bad froxel must not poison the next frames (history) nor the integrated column
	vec4 dynamicEmission = vec4(mediumCenter.rgb * mediumCenter.a * dynamicLight, 1.0);
	if (debugView == 17)
		dynamicEmission.rgb = vec3(0.0);
	if (any(isnan(current)) || any(isinf(current)))
		current = vec4(0.0);
	if (any(isnan(dynamicEmission)) || any(isinf(dynamicEmission)))
		dynamicEmission = vec4(0.0, 0.0, 0.0, 1.0);

	out_Color = current;
	out_Dynamic = dynamicEmission;
}
