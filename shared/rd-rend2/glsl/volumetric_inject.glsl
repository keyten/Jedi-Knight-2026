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
// tr_fogvolume.cpp) listed for this slice, plus the FX particle media. Extinctions add; each medium
// scatters with its own albedo and Henyey-Greenstein g (FroxelMediumSample: up to 3 phase lobes,
// one per distinct g; media without their own g use r_volumetricFogAnisotropy). Without any of
// them there is no medium. The selected media (r_volumetricFogNoise) are multiplied by the world space
// density noise m(p) (mean 1): only the extinction changes, the light does not.
//
// Liquids (r_volumetricWater, USE_LIQUIDS, glsl/liquid_common.glsl): the water / slime / lava brushes of
// the map listed for this slice. Their extinction is the covered fraction of the froxel's depth along
// its ray (exact clip of the convex brushes), so the froxel cut by a water surface integrates its
// share of the water. The sun under a liquid passes the liquid above the froxel (exact path length to
// the surface, rgb transmittance, caustics: LiquidSunTransmittance), every other light is unchanged.
//
// Light (the phase function is 4 pi HG, 1 = isotropic, per lobe of the medium):
//   baked   light grid without the sun (isotropic, legacy brightness)
//   sun     inside the cascaded shadow maps: realtime sun radiance * shadow * phase, beyond them (and
//           where the cascades see no sun in the whole light grid cell) the baked sun part of the
//           light grid * phase
//   dynamic the lights of the froxel's cluster (CPU binned per tile and slice, R_VolumetricBuildLightLists)
//           * attenuation * phase * its shadow map
//
// out_ParticleLight  R11G11B10F  (r_particleLighting, u_ParticleLight.x > 0) the incident light at the
//                       froxel center without extinction and albedo: baked + sun + dynamic, the same
//                       shadows, attenuation and phase as the fog (phase towards the camera: valid for
//                       camera facing sprites seen from this view only). Written in empty space too,
//                       this frame only (no history). Sprite particles are lit with it (generic.glsl).
//
// out_Extinction  RGBA16F  (r_volumetricFogRGBExtinction, USE_FROXEL_RGB) sigma_t.rgb of the media, temporally
//                       filtered like out_Color.a (same weight, same reprojection). Each medium's
//                       sigma_t.rgb = sigma_i * c_i (extinction color, mean 1: out_Color.a stays the scalar
//                       sigma) and its scattering sigma_s.rgb = sigma_t.rgb * albedo.rgb.

uniform sampler3D u_FroxelHistory;
#if defined(USE_FROXEL_RGB)
uniform sampler3D u_FroxelExtinction;	// history of out_Extinction
#endif

#if defined(USE_LIQUIDS)
// the media pass (r_volumetricSelfShadow) is evaluating FroxelMedium: liquids stay out of it while
// the analytic sun path attenuates the sun under them (the self-shadow would count them twice)
bool froxelMediaPass = false;
#endif
uniform sampler3D u_VolumetricStaticGrid;	// non-sun baked baseline B (rgb), sun fraction f (a)
uniform sampler3D u_VolumetricSunGrid;
#if defined(USE_FROXEL_STATIC_RECONSTRUCTION)
// first angular moments of the non-sun baked light per channel (r_volumetricFogStaticDirectional):
// xyz = sum of energy * direction towards the light, |M| <= B
uniform sampler3D u_VolumetricDirMomentR;
uniform sampler3D u_VolumetricDirMomentG;
uniform sampler3D u_VolumetricDirMomentB;
#endif
uniform sampler3D u_VolumetricLegacyGrid;	// merged legacy light grid (debug view 25)
#if defined(USE_SHADOWS2)
uniform sampler2DArray u_ShadowMap;		// raw sun cascade depth
#else
uniform sampler2DArrayShadow u_ShadowMap;	// legacy sun cascades
#endif
uniform sampler2DArrayShadow u_ShadowMap2;	// dynamic light cube faces, 6 layers per light

#if defined(USE_CLOUD_SHADOWS)
// cloud shadows (r_cloudShadows, tr_clouds.cpp): the clouds' sun transmittance on a ground plane around the
// camera; a point below the clouds slides along the sun onto that plane. u_CloudShadow[0]: centre xy, 1 / extent
// (world units), plane z; [1]: sun xy / sun z, strength, enabled
uniform sampler2D u_CloudShadowMap;
uniform vec4 u_CloudShadow[2];

float CloudShadow(in vec3 position)
{
	if (u_CloudShadow[1].w < 0.5)
		return 1.0;
	vec2 q = position.xy - u_CloudShadow[1].xy * (position.z - u_CloudShadow[0].w);
	vec2 uv = (q - u_CloudShadow[0].xy) * u_CloudShadow[0].z + 0.5;
	float T = textureLod(u_CloudShadowMap, uv, 0.0).r;
	vec2 edge = abs(uv - 0.5);
	float inside = 1.0 - smoothstep(0.42, 0.5, max(edge.x, edge.y));
	return mix(1.0, T, inside * u_CloudShadow[1].z);
}
#endif


#if defined(USE_FROXEL_COMPUTE)
int var_Slice;
#else
flat in int var_Slice;	// slice of this layer, -1 = tail pass
#endif

// dynamic light lists: FROXEL_LIGHT_TEXELS per light (origin, radius | color, shadow cube layer |
// spot axis, cos outer | cos inner, projected spot shadow, cookie layer, cookie roll), and per cluster a header (first entry |
// count << 24) followed by the light indexes
#define FROXEL_LIGHT_TEXELS 5
uniform samplerBuffer u_FPlusLights;
uniform usamplerBuffer u_FPlusGridMap;

struct Light
{
	vec4 origin;
	vec3 color;
	float radius;
	vec4 spot;
	vec4 spot2;
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
	mat4 u_SpotShadowVP[MAX_DLIGHTS];	// spot light shadow views, by shadow slot (layer 6 * slot)
};

// fragment outputs are bound to draw buffers by name (shaderOutputNames, tr_glsl.cpp):
// 0 = out_Color, 1 = out_Glow, 2 = out_SSRNormal
#if defined(USE_FROXEL_COMPUTE)
// Per-invocation results of the shared injection math.
vec4 out_Color, out_Glow, out_SSRNormal;
#else
out vec4 out_Color;
out vec4 out_Glow;
out vec4 out_SSRNormal;
#endif
#define out_Dynamic out_Glow
#define out_ParticleLight out_SSRNormal
#if defined(USE_FROXEL_RGB)
// 3 = out_SSRSpecular
#if defined(USE_FROXEL_COMPUTE)
vec4 out_SSRSpecular;
#else
out vec4 out_SSRSpecular;
#endif
#define out_Extinction out_SSRSpecular
#endif

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

// Media self-shadow (r_volumetricSelfShadow, u_FroxelSelfShadow): the light reaching a froxel is
// attenuated by exp(-optical depth) of the media between it and the light. The media pass
// (u_ParticleLight.z = 1, RB_VolumetricBuild) writes the extinction of this frame at the froxel
// centers into u_FroxelMedia (R16F, no jitter, no history: moving smoke shadows where it is now);
// the injection marches it along the light ray. Geometry shadows are applied separately.
uniform sampler3D u_FroxelMedia;

// volume texture coordinates of p (texel centers = froxel centers); false outside the volume
bool FroxelMediaCoord(in vec3 p, out vec3 coord)
{
	vec4 clip = u_FroxelViewProjection * vec4(p, 1.0);
	coord = vec3(0.0);
	if (clip.w <= 1e-3 || clip.w > u_FroxelSliceParams.y)
		return false;
	// clip.w = view depth (as the history reprojection)
	coord = vec3(clip.xy / clip.w * 0.5 + 0.5, FroxelDepthToW(clip.w));
	return all(greaterThanEqual(coord.xy, vec2(0.0))) && all(lessThanEqual(coord.xy, vec2(1.0)));
}

// Optical depth of the media along p + dir * t, t in [0, len]: steps samples, one per interval of
// a quadratic spacing (dense near p, where the shadow detail is), jittered inside its interval
// (jitter in [0, 1), the history averages the offsets). The camera relative volume knows nothing
// outside the view frustum and beyond far: where the ray leaves it the march stops and those media
// count as empty. With u_FroxelSelfShadow.w the height fog, known everywhere, is integrated
// analytically from where the march ended up to total (>= len).
float FroxelMediaOpticalDepth(in vec3 p, in vec3 dir, in float len, in float total, in int steps,
	in float jitter)
{
	float tau = 0.0;
	float tEnd = len;
	float invSteps = 1.0 / float(steps);
	float e0 = 0.0;
	for (int i = 0; i < steps; i++)
	{
		float x = float(i + 1) * invSteps;
		float e1 = len * x * x;
		vec3 coord;
		if (!FroxelMediaCoord(p + dir * mix(e0, e1, jitter), coord))
		{
			tEnd = e0;
			break;
		}
		tau += texture(u_FroxelMedia, coord).r * (e1 - e0);
		e0 = e1;
	}

	if (u_FroxelSelfShadow.w > 0.5 && u_FroxelHeightFog.x > 0.0 && total > tEnd)
	{
		vec3 a = p + dir * tEnd;
		tau += FroxelHeightOpticalDepth(a, p + dir * total, total - tEnd);
	}
	return tau;
}

// march offset of a froxel: interleaved gradient noise, rotated every frame with the temporal
// accumulation, the interval centers without it
float FroxelSelfShadowJitter(in ivec2 cell, in int slice, in float temporal)
{
	if (temporal <= 0.0)
		return 0.5;
	vec2 q = vec2(cell) + float(slice) * vec2(5.588238, 3.137);
	float n = fract(52.9829189 * fract(dot(q, vec2(0.06711056, 0.00583715))));
	return fract(n + u_FroxelGridSize.w * 0.61803399);
}

// extinction of the height fog at p, world anchored:
//   sigma0 * min(exp(-(z - base) / falloff), maxScale) * (1 - smoothstep(top - fade, top, z - base))
float FroxelHeightExtinction(in vec3 p)
{
	if (u_FroxelHeightFog.x <= 0.0)
		return 0.0;
	float h = p.z - u_FroxelHeightFog.y;
	if (u_FroxelHeightFogTop.x > 0.0 && h >= u_FroxelHeightFogTop.x)
		return 0.0;
	float extinction = u_FroxelHeightFog.x * exp(min(-h * u_FroxelHeightFog.z, u_FroxelHeightFog.w));
	if (u_FroxelHeightFogTop.x > 0.0)
		extinction *= 1.0 - smoothstep(u_FroxelHeightFogColor.w, u_FroxelHeightFogTop.x, h);
	return extinction;
}

// Average the smooth height profile over the actual integration segment. Keep XY
// jitter (the lighting ray), but not Z jitter: it must not move the slice boundaries.
// y is the extinction threshold for a 1e-5 optical-depth budget over the WHOLE
// column, rather than 1e-5 per slice. Noise and RGB are accounted for by the caller.
vec2 FroxelHeightSlice(in vec2 column, in float slice)
{
	if (u_FroxelHeightFog.x <= 0.0)
		return vec2(0.0);
	vec2 ndc = column / u_FroxelGridSize.xy * 2.0 - 1.0;
	vec3 ray = u_FroxelRayForward.xyz + ndc.x * u_FroxelRayRight.xyz + ndc.y * u_FroxelRayUp.xyz;
	float nearDepth = FroxelWToDepth(slice / u_FroxelGridSize.z);
	float farDepth = FroxelWToDepth((slice + 1.0) / u_FroxelGridSize.z);
	vec3 a = u_FroxelViewOrigin.xyz + ray * nearDepth;
	vec3 b = u_FroxelViewOrigin.xyz + ray * farDepth;
	// Integration uses the unjittered column length; use that same length for
	// the error budget even when XY jitter selects a slightly different ray.
	vec2 centerNdc = (floor(column) + 0.5) / u_FroxelGridSize.xy * 2.0 - 1.0;
	vec3 centerRay = u_FroxelRayForward.xyz + centerNdc.x * u_FroxelRayRight.xyz + centerNdc.y * u_FroxelRayUp.xyz;
	float columnLength = u_FroxelSliceParams.y * length(centerRay);
	// The integral is linear in len: len = 1 directly gives tau / sliceLength.
	return vec2(FroxelHeightOpticalDepth(a, b, 1.0),
		1e-5 / max(columnLength, 1e-6));
}

// Medium sample of the injection (per-medium albedo and anisotropy). Every medium i (BSP fog,
// height fog, local volume, FX particle medium) has an extinction s_i, an albedo a_i (rgb) and a
// Henyey-Greenstein g_i (its own, else the global r_volumetricFogAnisotropy). The overlap:
//   extinction      s = sum s_i
//   scattering      S_i = s_i * a_i (rgb)
//   phase lobes     up to FROXEL_LOBES slots, one per distinct g: the media with the same g share
//                   a slot, S_k = sum S_i, g_k = g
//   source          j = sum_k S_k * L * P(g_k)   per light term (4 pi HG, FroxelPhase)
// This is the exact mixture sum_i S_i P(g_i) as long as a froxel holds at most FROXEL_LOBES
// distinct g (legacy media: all the global g, one slot; a +g / -g overlap: two slots, both peaks
// kept; the default fog + an authored smoke + a back scattering medium: three). Only a further
// distinct g is merged into the slot of the nearest g, with the scattering weighted mean cosine
//   g_k = sum w_i g_i / sum w_i,  w_i = luma(S_i)
// (energy exact: 4 pi P averages 1 for any g; first moment exact; the lobe shape approximated;
// never a plain average of g). The weight is the luminance of S_i, not per channel.
#define FROXEL_LOBES 3

struct FroxelMediumSample
{
	vec3 scatter0;		// S_k of the slots (0 when unused)
	vec3 scatter1;
	vec3 scatter2;
	vec3 g;				// g_k of the slots
	float extinction;	// s
	float lobes;		// slots in use
	float merged;		// 1: more distinct g than slots, some were merged (debug view 37)
#if defined(USE_FROXEL_RGB)
	vec3 extinctionRGB;	// sigma_t.rgb = sum s_i * c_i (mean = extinction)
#endif
};

// lobe accumulators: rgb = sum S_i, a = sum w_i g_i; weight = sum w_i; key = g of the slot
struct FroxelLobes
{
	vec4 scatter[FROXEL_LOBES];
	float weight[FROXEL_LOBES];
	float key[FROXEL_LOBES];
	int count;
	bool merged;
};

void FroxelAddScattering(inout FroxelLobes lobes, in float extinction, in vec3 albedo, in float g)
{
	vec3 s = albedo * extinction;
	float w = Luma(s);
	if (w <= 0.0)
		return;	// black: absorbs only, no phase

	// the slot of this g, else a free one, else the nearest g
	int k = -1;
	for (int i = 0; i < lobes.count; i++)
	{
		if (abs(lobes.key[i] - g) < 1e-3)
			k = i;
	}
	if (k < 0 && lobes.count < FROXEL_LOBES)
	{
		k = lobes.count++;
		lobes.key[k] = g;
	}
	if (k < 0)
	{
		lobes.merged = true;
		k = 0;
		float nearest = 1e9;
		for (int i = 0; i < FROXEL_LOBES; i++)
		{
			float d = abs(lobes.scatter[i].a / max(lobes.weight[i], 1e-20) - g);
			if (d < nearest)
			{
				nearest = d;
				k = i;
			}
		}
	}

	lobes.scatter[k] += vec4(s, w * g);
	lobes.weight[k] += w;
}

// scattering weighted mean of a per-slot value (debug views)
float FroxelLobeMean(in FroxelMediumSample m, in vec3 value)
{
	vec3 w = vec3(Luma(m.scatter0), Luma(m.scatter1), Luma(m.scatter2));
	return dot(w, value) / max(w.x + w.y + w.z, 1e-20);
}

// the medium of the fog volumes, the height fog and the local fog volumes of the slice at p, and
// the FX particle media (debug views 11, 12, 16 and 26 keep one of them, 14 drops the density
// noise).
// noisyFraction: share of the extinction that comes from noise modulated media (their history weight
// is lowered when the noise moves). localFraction: share of the local volumes (before the noise).
// localChange (only with wantChange): how much the local medium at p changed since the previous
// frame, relative to it (moving, appearing and vanishing volumes; 0 when they are static).
// particleChange: the same for the FX particle media (their own history reduction), particleFraction:
// their share of the extinction (before the noise).
FroxelMediumSample FroxelMedium(in vec3 p, in vec2 heightSample, in int debugView, in bool wantChange, out float noisyFraction,
	out float localFraction, out float localChange, out float particleFraction, out float particleChange)
{
	bool noise = u_FroxelNoiseLod.w > 0.5 && debugView != 14;
	float globalG = u_FroxelLightParams.x;
	float extinction = 0.0;			// with the noise
	float plainExtinction = 0.0;	// without the noise (the fractions)
	float noisyExtinction = 0.0;
	float plainNoisyExtinction = 0.0;
#if defined(USE_FROXEL_RGB)
	vec3 extinctionRGB = vec3(0.0);
#endif
	FroxelLobes lobes;
	for (int i = 0; i < FROXEL_LOBES; i++)
	{
		lobes.scatter[i] = vec4(0.0);
		lobes.weight[i] = 0.0;
		lobes.key[i] = 0.0;
	}
	lobes.count = 0;
	lobes.merged = false;

	// the density noise m(p) of the noisy media, evaluated at the first one (the noise scales their
	// extinction and scattering, not their g)
	float m = -1.0;

	// local fog volumes: the membership mask of this XYZ cluster (CPU culled, tr_fogvolume.cpp)
	float localExtinction = 0.0;
	float localPrevious = 0.0;
	float localDelta = 0.0;
	uvec2 localMask = (u_FroxelLocalParams.x > 0.5 && debugView != 11 && debugView != 12 && debugView != 26 && debugView != 59) ? FroxelLocalCluster(p, var_Slice) : uvec2(0u);
	if (any(notEqual(localMask, uvec2(0u))))
	{
		float fade = FroxelLocalFade(dot(p - u_FroxelViewOrigin.xyz, u_FroxelViewForward.xyz));
		float previousFade = FroxelLocalFade((u_FroxelPrevViewProjection * vec4(p, 1.0)).w);
		while (any(notEqual(localMask, uvec2(0u))))
		{
			int i = FroxelLocalNext(localMask);
			vec4 shape = u_FroxelLocalShape[i];
			vec4 color = u_FroxelLocalColor[i];
			float density = FroxelLocalShapeDensity(u_FroxelLocalX[i], u_FroxelLocalY[i],
				u_FroxelLocalZ[i], shape.x, shape.y, shape.z, p);
			float e = color.a * fade * density;

			if (wantChange)
			{
				vec4 motion = u_FroxelLocalMotion[i];
				float previous = color.a * previousFade * density;
				if (motion.x > 0.5)
				{
					previous = motion.y * previousFade * FroxelLocalShapeDensity(u_FroxelLocalPrevX[i],
						u_FroxelLocalPrevY[i], u_FroxelLocalPrevZ[i], motion.x - 1.0, motion.z, motion.w, p);
				}
				localPrevious += previous;
				// Appearance changes must invalidate history even at unchanged density.
				localDelta += shape.w >= 32.0 ? max(e, previous) : abs(e - previous);
			}

			if (e <= 0.0)
				continue;

			localExtinction += e;
			plainExtinction += e;
			if (noise && FroxelLocalNoisy(shape.w))
			{
				plainNoisyExtinction += e;
				if (m < 0.0)
					m = FroxelNoiseModulation(p, dot(p - u_FroxelViewOrigin.xyz, u_FroxelViewForward.xyz));
				e *= m;
				noisyExtinction += e;
			}
			extinction += e;
			// its own g or the global one (CPU resolved)
#if defined(USE_FROXEL_RGB)
			vec3 c = FroxelLocalExtinctionColor(shape.w);
			extinctionRGB += e * c;
			FroxelAddScattering(lobes, e, color.rgb * c, u_FroxelLocalEmission[i].w);
#else
			FroxelAddScattering(lobes, e, color.rgb, u_FroxelLocalEmission[i].w);
#endif
		}
	}
	localChange = localDelta / max(max(localExtinction, localPrevious), 1e-12);

	// FX particle media: the packed list of this slice (CPU culled and capped, tr_volparticle.cpp)
	float particleExtinction = 0.0;
	float particlePrevious = 0.0;
	float particleDelta = 0.0;
	int particleHeader = (u_FroxelParticleParams.x > 0.5 && debugView != 11 && debugView != 12 &&
		debugView != 16 && debugView != 59) ? FroxelParticleSliceHeader(var_Slice) : 0;
	int particleCount = particleHeader >> 16;
	if (particleCount > 0)
	{
		int first = particleHeader & 0xffff;
		float fade = FroxelParticleFade(dot(p - u_FroxelViewOrigin.xyz, u_FroxelViewForward.xyz));
		for (int j = 0; j < particleCount; j++)
		{
			int i = FroxelParticlePoolIndex(first + j);
			vec4 center = u_FroxelParticleCenter[i];
			vec4 invExtent = u_FroxelParticleInvExtent[i];
			float e = center.w * fade * FroxelParticleDensity(center.xyz, invExtent, p);
			vec4 color = u_FroxelParticleColor[i];

			if (wantChange)
			{
				vec4 prevCenter = u_FroxelParticlePrevCenter[i];
				float previous = e;
				if (FroxelParticleChanged(prevCenter.w))
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
			plainExtinction += e;
			extinction += e;
#if defined(USE_FROXEL_RGB)
			extinctionRGB += vec3(e);	// FX media have no extinction color
#endif
			FroxelAddScattering(lobes, e, color.rgb, FroxelParticleAnisotropy(invExtent.w));
		}
	}
	particleChange = particleDelta / max(max(particleExtinction, particlePrevious), 1e-12);

	// the height fog has no metadata: the global g
	if (u_FroxelHeightFog.x > 0.0 && debugView != 11 && debugView != 16 && debugView != 26 && debugView != 59)
	{
		float e = heightSample.x;
		float plain = e;
		if (noise && u_FroxelNoiseMacroOffset.w > 0.5)
		{
			if (m < 0.0)
				m = FroxelNoiseModulation(p, dot(p - u_FroxelViewOrigin.xyz, u_FroxelViewForward.xyz));
			e *= m;
		}
		// Cull only the height component; never erase local media or pure emission.
		// The normalized RGB extinction can still have one stronger channel.
		float channelScale = 1.0;
#if defined(USE_FROXEL_RGB)
		channelScale = max(u_FroxelHeightFogTop.y, max(u_FroxelHeightFogTop.z, u_FroxelHeightFogTop.w));
#endif
		if (e * channelScale < heightSample.y)
		{
			e = 0.0;
			// A noise void still belongs to the moving medium for history weighting.
			if (!noise || u_FroxelNoiseMacroOffset.w <= 0.5)
				plain = 0.0;
		}
		plainExtinction += plain;
		if (noise && u_FroxelNoiseMacroOffset.w > 0.5)
		{
			plainNoisyExtinction += plain;
			noisyExtinction += e;
		}
		extinction += e;
#if defined(USE_FROXEL_RGB)
		extinctionRGB += e * u_FroxelHeightFogTop.yzw;
		FroxelAddScattering(lobes, e, u_FroxelHeightFogColor.rgb * u_FroxelHeightFogTop.yzw, globalG);
#else
		FroxelAddScattering(lobes, e, u_FroxelHeightFogColor.rgb, globalG);
#endif
	}

	// the fog volumes that may touch this slice (CPU culled); max: see liquidSlice below
	int fogSlice = max(var_Slice, 0);
	int fogMask = (debugView == 12 || debugView == 16 || debugView == 26 || debugView == 59) ? 0 :
		u_FroxelFogSlices[fogSlice >> 2][fogSlice & 3];
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

		// albedo: fogAlbedo or the fog color, g: fogAnisotropy or the global one (CPU resolved)
		vec4 fog = u_FroxelFogColor[i];
		float e = fog.a;
		plainExtinction += e;
		if (noise && maxs.w > 0.5)
		{
			plainNoisyExtinction += e;
			if (m < 0.0)
				m = FroxelNoiseModulation(p, dot(p - u_FroxelViewOrigin.xyz, u_FroxelViewForward.xyz));
			e *= m;
			noisyExtinction += e;
		}
		extinction += e;
#if defined(USE_FROXEL_RGB)
		extinctionRGB += e * u_FroxelFogMedium[i].yzw;
		FroxelAddScattering(lobes, e, fog.rgb * u_FroxelFogMedium[i].yzw, u_FroxelFogMedium[i].x);
#else
		FroxelAddScattering(lobes, e, fog.rgb, u_FroxelFogMedium[i].x);
#endif
	}

#if defined(USE_LIQUIDS)
	// the liquid brushes that may touch this slice (CPU culled, tr_liquid.cpp): the covered fraction
	// of the froxel's depth along its ray, from the slice near to the slice far side (union of the
	// overlapping brushes of a class). Static media without noise: no history reduction.
	// (max: the compute tail pass runs this function's caller with var_Slice -1, and a constant
	// negative index is a compile error on some drivers even in a branch never taken)
	int liquidSlice = max(var_Slice, 0);
	int liquidMask = (u_LiquidParams.x > 0.5 && !(froxelMediaPass && u_LiquidParams.z > 0.5) &&
		debugView != 11 && debugView != 12 && debugView != 16 && debugView != 26) ?
		u_LiquidSlices[liquidSlice >> 2][liquidSlice & 3] : 0;
	if (liquidMask != 0)
	{
		vec3 toP = p - u_FroxelViewOrigin.xyz;
		vec3 dir = toP / max(length(toP), 1e-4);
		float cosView = max(dot(dir, u_FroxelViewForward.xyz), 1e-3);
		float sliceNear = FroxelWToDepth(float(var_Slice) / u_FroxelGridSize.z);
		float sliceFar = FroxelWToDepth(float(var_Slice + 1) / u_FroxelGridSize.z);
		vec3 covered = LiquidCoverage(u_FroxelViewOrigin.xyz, dir, sliceNear / cosView, sliceFar / cosView, liquidMask);
		vec3 fraction = covered * (cosView / max(sliceFar - sliceNear, 1e-4)) *
			LiquidFade(dot(toP, u_FroxelViewForward.xyz));
		for (int c = 0; c < 3; c++)
		{
			vec4 liquid = u_LiquidMaterial[c * 2];
			float e = liquid.a * fraction[c];
			if (e <= 0.0)
				continue;
			plainExtinction += e;
			extinction += e;
			// in-scattering albedo * extinction color in both modes (sigma_s = sigma_t.rgb * albedo)
#if defined(USE_FROXEL_RGB)
			extinctionRGB += e * liquid.rgb;
#endif
			FroxelAddScattering(lobes, e, u_LiquidMaterial[c * 2 + 1].rgb * liquid.rgb, u_LiquidMaterial[c * 2 + 1].a);
		}
	}
#endif

	localFraction = localExtinction / max(plainExtinction, 1e-12);
	particleFraction = particleExtinction / max(plainExtinction, 1e-12);
	// Do not trust old clumps when today's noise is a void. Keep the stronger
	// reduction when a clump dominates today's actual extinction, too.
	noisyFraction = clamp(max(plainNoisyExtinction / max(plainExtinction, 1e-12),
		noisyExtinction / max(extinction, 1e-12)), 0.0, 1.0);

	FroxelMediumSample result;
	result.scatter0 = lobes.scatter[0].rgb;
	result.scatter1 = lobes.scatter[1].rgb;
	result.scatter2 = lobes.scatter[2].rgb;
	result.g = vec3(lobes.scatter[0].a / max(lobes.weight[0], 1e-20),
		lobes.scatter[1].a / max(lobes.weight[1], 1e-20),
		lobes.scatter[2].a / max(lobes.weight[2], 1e-20));
	result.extinction = extinction;
	result.lobes = float(lobes.count);
	result.merged = lobes.merged ? 1.0 : 0.0;
#if defined(USE_FROXEL_RGB)
	result.extinctionRGB = extinctionRGB;
#endif
	return result;
}

// scattering source of the sample for the light of each slot (light0..2 carry the phase of g_k)
vec3 FroxelScatter(in FroxelMediumSample m, in vec3 light0, in vec3 light1, in vec3 light2)
{
	return m.scatter0 * light0 + m.scatter1 * light1 + m.scatter2 * light2;
}

// Emission source j_e at p (radiance per world unit, the unit of the scattering source
// extinction * albedo * light): the local volumes and FX particle media of the slice that emit. Their
// density shape without the noise (stable without history), the same fade. Independent of the
// extinction: a medium with extinction 0 may glow (the integration takes the limit).
vec3 FroxelEmission(in vec3 p, in int debugView)
{
	vec3 emission = vec3(0.0);

	uvec2 localMask = (u_FroxelLocalParams.x > 0.5 && u_FroxelLocalParams.w > 0.5 && debugView != 26) ? FroxelLocalCluster(p, var_Slice) : uvec2(0u);
	if (any(notEqual(localMask, uvec2(0u))))
	{
		float fade = FroxelLocalFade(dot(p - u_FroxelViewOrigin.xyz, u_FroxelViewForward.xyz));
		while (any(notEqual(localMask, uvec2(0u))))
		{
			int i = FroxelLocalNext(localMask);
			vec3 e = u_FroxelLocalEmission[i].rgb;
			if (max(e.r, max(e.g, e.b)) <= 0.0)
				continue;
			vec4 shape = u_FroxelLocalShape[i];
			emission += e * fade * FroxelLocalShapeDensity(u_FroxelLocalX[i], u_FroxelLocalY[i],
				u_FroxelLocalZ[i], shape.x, shape.y, shape.z, p);
		}
	}

	int particleHeader = (u_FroxelParticleParams.x > 0.5 && debugView != 16) ?
		FroxelParticleSliceHeader(var_Slice) : 0;
	int particleCount = particleHeader >> 16;
	if (particleCount > 0)
	{
		int first = particleHeader & 0xffff;
		float fade = FroxelParticleFade(dot(p - u_FroxelViewOrigin.xyz, u_FroxelViewForward.xyz));
		for (int j = 0; j < particleCount; j++)
		{
			int i = FroxelParticlePoolIndex(first + j);
			int slot = FroxelParticleEmissionSlot(u_FroxelParticlePrevCenter[i].w);
			if (slot < 0 || slot >= MAX_GPU_EMISSIVE_PARTICLES)
				continue;
			emission += u_FroxelParticleEmission[slot].rgb * fade *
				FroxelParticleDensity(u_FroxelParticleCenter[i].xyz, u_FroxelParticleInvExtent[i], p);
		}
	}

	return emission * u_FroxelTemporalParams.z;
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

// spot light cone, as lightall.glsl SpotConeAttenuation (tr_spotlight.cpp): L = froxel to light,
// unit; spot = axis, cos outer. Point lights (cos outer -2, cos inner -1): 1
float SpotConeAttenuation(in vec3 L, in vec4 spot, in float cosInner)
{
	return smoothstep(spot.w, cosInner, dot(-L, spot.xyz));
}

// spot light cookies, as SpotCookieUV / SpotCookie of lightall.glsl (tr_lightcookie.cpp): the lod
// comes from the world size of a froxel instead of a pixel, same formula
#define LIGHT_COOKIE_SIZE 256.0
uniform sampler2DArray u_LightCookieMap;
uniform vec4 u_LightCookieParams;	// enabled, rgb, world size of a froxel at distance 1, unused

vec3 SpotCookieUV(in vec3 d, in vec4 spot, in float roll)
{
	vec3 axis = spot.xyz;
	vec3 up0 = abs(axis.z) > 0.99 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.0, 1.0);
	vec3 left = normalize(cross(up0, axis));
	vec3 up = cross(axis, left);
	float c = cos(roll), s = sin(roll);
	float x = dot(d, left), y = dot(d, up);
	float tanOuter = sqrt(max(1.0 - spot.w * spot.w, 1e-6)) / spot.w;
	float r = dot(d, axis) * tanOuter;
	float inv = 0.5 / max(r, 1e-5);
	return vec3(0.5 - (c * x + s * y) * inv, 0.5 - (c * y - s * x) * inv, r);
}

vec3 SpotCookie(in vec3 d, in vec4 spot, in vec4 spot2, in float viewDist)
{
	if (spot2.z < -0.5 || u_LightCookieParams.x < 0.5)
		return vec3(1.0);
	vec3 uvr = SpotCookieUV(d, spot, spot2.w);
	if (uvr.z <= 0.0)
		return vec3(0.0);
	float texels = LIGHT_COOKIE_SIZE * 0.5 * u_LightCookieParams.z * viewDist / uvr.z;
	vec4 cookie = textureLod(u_LightCookieMap, vec3(uvr.xy, spot2.z), log2(max(texels, 1e-4)));
	return u_LightCookieParams.y > 0.5 ? cookie.rgb : vec3(cookie.a);
}

// spot light shadow: the one perspective view in layer 6 * slot (u_SpotShadowVP), same depth
// convention as the cube faces; pulled towards the light as DynamicLightShadow
float SpotLightShadow(in vec3 lightOrigin, in vec3 L, in float dist, in int slot, in float cosOuter)
{
	vec3 dir = L / dist;
	vec3 position = lightOrigin - (L - dir * min(2.0 + 0.02 * dist, 0.5 * dist));
	vec4 clip = u_SpotShadowVP[slot] * vec4(position, 1.0);
	if (clip.w <= 1e-3)
		return 1.0;
	vec3 coord = clip.xyz / clip.w * 0.5 + 0.5;
	// the 0.006 radian spread of the cube taps, in the spot view's texture space
	float fovScale = cosOuter / sqrt(max(1.0 - cosOuter * cosOuter, 1e-6));
	float spread = 0.006 * 0.5 * fovScale;
	float layer = float(slot) * 6.0;
	float result = 0.0;
	result += texture(u_ShadowMap2, vec4(coord.xy + vec2( spread,  spread), layer, coord.z));
	result += texture(u_ShadowMap2, vec4(coord.xy + vec2( spread, -spread), layer, coord.z));
	result += texture(u_ShadowMap2, vec4(coord.xy + vec2(-spread,  spread), layer, coord.z));
	result += texture(u_ShadowMap2, vec4(coord.xy + vec2(-spread, -spread), layer, coord.z));
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

// Henyey-Greenstein phase of the four g of the injection: xyz the lobe slots of the medium sample
// (FroxelMediumSample.g), w the global g (sprite particle light field, tail)
vec4 FroxelPhases(in vec4 g, in float cosTheta)
{
	return vec4(FroxelPhase(g.x, cosTheta), FroxelPhase(g.y, cosTheta), FroxelPhase(g.z, cosTheta),
		FroxelPhase(g.w, cosTheta));
}

// Approximate multiple scattering (r_volumetricMultiScatter, u_FroxelMultiScatter): the octave model
// of Wrenninge, Kulla, Lundqvist, "Oz: The Great and Volumetric" (SIGGRAPH 2013), as used by Hillaire
// (Frostbite 2016) and the UE volumetric clouds. A self-shadowed light term L * P(g) * exp(-tau) gets
// the octaves i = 1..N
//   q^i * L_fill * P(c^i g) * exp(-a^i tau)
// (light that reached p after further scatterings: along diffuse paths, so it saw less optical depth,
// a^i, lost energy, q^i, and forgot its direction, c^i). The published model adds b^i even to thin
// media (energy from nowhere at tau -> 0) and ignores the albedo; here the energy per octave is
//   q = b * albedo * (1 - exp(-extinction * l))
// every further scattering keeps only the albedo, and a medium thin at the scale l (fog: extinction *
// l << 1) has no second scattering to speak of: order i fades as (extinction * l)^i. q <= b * albedo
// <= 0.95, so the series is bounded; the caller also clamps single + multiple scattering of a light to
// its unshadowed single scattering at an isotropic or better phase (FroxelMultiScatterClamp).
//   xyz  weights q^i * exp(-a^i tau) of the octaves 1..3 (0 beyond r_volumetricMultiScatterOctaves)
//   w    thickness 1 - exp(-extinction * l)
vec4 FroxelMultiScatterWeights(in float tau, in float extinction, in float albedo)
{
	float thickness = 1.0 - exp(-max(extinction, 0.0) * u_FroxelMultiScatter.w);
	float q = min(u_FroxelMultiScatter.z * clamp(albedo, 0.0, 1.0) * thickness, 0.95);
	vec3 w = vec3(0.0);
	float qi = 1.0;
	float ai = 1.0;
	for (int i = 0; i < 3; i++)
	{
		if (float(i) >= u_FroxelMultiScatter.y)
			break;
		qi *= q;
		ai *= u_FroxelMultiScatter2.x;
		w[i] = qi * exp(-ai * tau);
	}
	return vec4(w, thickness);
}

// sum over the octaves of weight * phase, per g of FroxelPhases (lobe slots, global)
vec4 FroxelMultiScatterPhases(in vec4 weights, in vec4 g, in float cosTheta)
{
	vec4 phases = vec4(0.0);
	float c = 1.0;
	for (int i = 0; i < 3; i++)
	{
		if (weights[i] <= 0.0)
			break;
		c *= u_FroxelMultiScatter2.y;
		phases += weights[i] * FroxelPhases(g * c, cosTheta);
	}
	return phases;
}

// Energy guard of a light term: single + multiple scattering <= the unshadowed light at the phase
// max(P, 1) (1 = isotropic, FroxelPhase is 4 pi HG). In a conservative medium lit from outside the
// radiance inside cannot exceed the incident radiance (maximum principle of a source-free transport):
// the octaves only return light that the media shadow removed, never more than the medium would
// scatter without it, whatever the albedo, density or ray length.
vec3 FroxelMultiScatterClamp(in vec3 single, in vec3 multi, in vec3 unshadowed, in float phase)
{
	return min(multi, max(unshadowed * max(phase, 1.0) - single, vec3(0.0)));
}

// dynamic lights of the cluster at p (viewDir: camera to p, unit), with the phase of each g
// (FroxelPhases): light0..2 for the lobe slots, lightGlobal for the global g. The lights, shadows
// and cookies are evaluated once, each g only costs its phase.
// msMedium: extinction and albedo luma of the medium for the multiple scattering octaves of the
// self-shadowed lights (r_volumetricMultiScatter 2), extinction 0 = none
void DynamicLights(in uint cluster, in vec3 p, in vec3 viewDir, in vec4 g, in float jitter, in vec2 msMedium,
	out vec3 light0, out vec3 light1, out vec3 light2, out vec3 lightGlobal)
{
	light0 = vec3(0.0);
	light1 = vec3(0.0);
	light2 = vec3(0.0);
	lightGlobal = vec3(0.0);
	int first = int(cluster & 0xffffffu);
	int count = int(cluster >> 24);
	for (int j = 0; j < count; j++)
	{
		int i = int(texelFetch(u_FPlusGridMap, first + j).r);
		vec4 originRadius = texelFetch(u_FPlusLights, i * FROXEL_LIGHT_TEXELS);
		vec4 colorLayer = texelFetch(u_FPlusLights, i * FROXEL_LIGHT_TEXELS + 1);
		vec4 spot = texelFetch(u_FPlusLights, i * FROXEL_LIGHT_TEXELS + 2);
		vec4 spot2 = texelFetch(u_FPlusLights, i * FROXEL_LIGHT_TEXELS + 3);
		float radius = originRadius.w;

		// light portal shaft (tr_volrecon.cpp, PortalShape): the light at its source (spot.xyz)
		// where the line source -> p crosses the aperture (center originRadius.xyz, right * half
		// width spot2, up * half height texel 4), point attenuation to |source - center| + range,
		// fading out to range behind the aperture. No shadow map: the aperture is the occluder.
		if (colorLayer.w < -2.5)
		{
			if (u_ShadowDebug.z == 3.0)
				continue;
			vec4 upHeight = texelFetch(u_FPlusLights, i * FROXEL_LIGHT_TEXELS + 4);
			vec3 center = originRadius.xyz;
			float shaftRange = originRadius.w;
			vec3 normal = cross(spot2.xyz, upHeight.xyz);
			float sourceSide = dot(spot.xyz - center, normal);
			float behind = -dot(p - center, normal);
			if (sourceSide <= 0.0 || behind <= 0.0 || behind >= shaftRange)
				continue;
			vec3 toP = p - spot.xyz;
			vec3 q = spot.xyz + toP * (sourceSide / (sourceSide + behind));
			vec3 dq = q - center;
			float softEdge = spot.w * behind + (-colorLayer.w - 3.0);
			float du = abs(dot(dq, spot2.xyz)) - spot2.w;
			float dv = abs(dot(dq, upHeight.xyz)) - upHeight.w;
			float gate = clamp(-du / softEdge, 0.0, 1.0) * clamp(-dv / softEdge, 0.0, 1.0);
			if (gate <= 0.0)
				continue;
			float shaftDist2 = max(dot(toP, toP), 1e-4);
			float reach = sourceSide + shaftRange;
			float shaft = gate * clamp(0.5 * reach * reach / shaftDist2 - 0.5, 0.0, 1.0) *
				(1.0 - smoothstep(0.7 * shaftRange, shaftRange, behind));
			if (shaft <= 0.0)
				continue;
			vec3 toLight = -toP * inversesqrt(shaftDist2);
			vec4 shaftPhase = FroxelPhases(g, dot(toLight, viewDir));
			vec3 shaftLight = colorLayer.rgb * shaft;
			light0 += shaftLight * shaftPhase.x;
			light1 += shaftLight * shaftPhase.y;
			light2 += shaftLight * shaftPhase.z;
			lightGlobal += shaftLight * shaftPhase.w;
			continue;
		}
		bool lineLight = spot2.w < -999.0;

		// r_spotLightDebug 3: no dynamic light in the fog, 4: spot lights only
		float spotDebug = u_ShadowDebug.z;
		if (spotDebug == 3.0 || (spotDebug == 4.0 && (lineLight || spot2.x < -0.5)))
			continue;

		// Saber fog proxy: use the closest point on the blade, while surface
		// lighting continues to evaluate the LTC emitter exactly once.
		vec3 source = originRadius.xyz;
		if (lineLight)
			source += spot.xyz * clamp(dot(p - source, spot.xyz), -spot2.x, spot2.x);
		vec3 L = source - p;
		float sqrDist = max(dot(L, L), 1e-4);
		// CalcLightAttenuation of lightall: zero at the radius
		float attenuation = clamp(0.5 * radius * radius / sqrDist - 0.5, 0.0, 1.0);
		if (attenuation <= 0.0)
			continue;

		float dist = sqrt(sqrDist);
		if (!lineLight)
			attenuation *= SpotConeAttenuation(L / dist, spot, spot2.x);
		if (attenuation <= 0.0)
			continue;
		// light travels from the light (-L) to the camera (-viewDir)
		vec4 phase = FroxelPhases(g, dot(L / dist, viewDir));

		float shadow = 1.0;
		float geometryShadow = 1.0;
		float mediaTau = -1.0;	// < 0: no media shadow, no octaves
		// shadow cube layer (legacy: i, Forward+: slot or -1)
		int shadowLayer = int(colorLayer.w);
		if (u_FroxelShadowParams.z > 0.5 && shadowLayer >= 0)
		{
			if (lineLight)
			{
				vec3 shadowL = originRadius.xyz - p;
				shadow = DynamicLightShadow(shadowL, max(length(shadowL), 1e-2), spot.w, shadowLayer);
			}
			else if (spot2.y > 0.5)
				shadow = SpotLightShadow(originRadius.xyz, L, dist, shadowLayer, spot.w);
			else
				shadow = DynamicLightShadow(L, dist,
					spot2.x < -0.5 && spot2.w > 0.0 ? spot2.w : radius, shadowLayer);
		}
		geometryShadow = shadow;

		// media self-shadow of the strongest few lights (r_volumetricSelfShadow 2, chosen by
		// R_VolumetricBuildLightLists), half the sun samples; the others only get their geometry shadow.
		// The octaves may lighten a geometry shadow a little, so the march also runs in it when they are on.
		bool msLight = u_FroxelMultiScatter.x > 1.5 && msMedium.x > 0.0;
		if (u_FroxelSelfShadow.x > 1.5 && (shadow > 0.0 || (msLight && u_FroxelMultiScatter2.z > 0.0)) && any(equal(vec4(float(i)), u_FroxelSelfShadowLights)))
		{
			int steps = max(3, int(u_FroxelSelfShadow.y) / 2);
			mediaTau = FroxelMediaOpticalDepth(p, L / dist, min(dist, u_FroxelSelfShadow.z), dist, steps, jitter);
			shadow *= exp(-mediaTau);
		}

		// cookie (tr_lightcookie.cpp): the radiance leaving the lamp towards p
		vec3 cookie = SpotCookie(-L / dist, spot, spot2, length(p - u_FroxelViewOrigin.xyz));

		// pointAtten * coneAtten * cookie * shadow * phase
		vec3 unshadowed = colorLayer.rgb * cookie * attenuation;
		vec3 light = unshadowed * shadow;
		vec3 add0 = light * phase.x;
		vec3 add1 = light * phase.y;
		vec3 add2 = light * phase.z;
		vec3 addGlobal = light * phase.w;

		// multiple scattering octaves of a self-shadowed light (r_volumetricMultiScatter 2)
		if (msLight && mediaTau >= 0.0)
		{
			vec4 weights = FroxelMultiScatterWeights(mediaTau, msMedium.x, msMedium.y);
			vec4 msPhase = FroxelMultiScatterPhases(weights, g, dot(L / dist, viewDir));
			vec3 msLightColor = unshadowed * mix(geometryShadow, 1.0, u_FroxelMultiScatter2.z * weights.w);
			add0 += FroxelMultiScatterClamp(add0, msLightColor * msPhase.x, unshadowed, phase.x);
			add1 += FroxelMultiScatterClamp(add1, msLightColor * msPhase.y, unshadowed, phase.y);
			add2 += FroxelMultiScatterClamp(add2, msLightColor * msPhase.z, unshadowed, phase.z);
			addGlobal += FroxelMultiScatterClamp(addGlobal, msLightColor * msPhase.w, unshadowed, phase.w);
		}

		light0 += add0;
		light1 += add1;
		light2 += add2;
		lightGlobal += addGlobal;
	}
}

// Baked light and sun, the phase kept apart for the lobes of the medium: the light terms are
// evaluated once, each g (the three lobe slots, the global g) only costs its phase.
//   baseline        non-sun baked light B (legacy brightness, the mean over all view directions)
//   momentDot       (M_R.v, M_G.v, M_B.v): first angular moments of the confidently attributed baked
//                   light (r_volumetricFogStaticDirectional, USE_FROXEL_STATIC_RECONSTRUCTION) with the
//                   view direction; 0 without them
//   staticG         g of each lobe for the baked light, clamped to +-1/3: the first order (L1) expansion
//                   of Henyey-Greenstein, 1 + 3 g cos, stays >= 0 there since |M| <= B
//   sun             sun radiance * sun scale (shadowed), full HG phase sunPhase
//   sunUnshadowed   the sun without its realtime shadow (debug view 2)
// Debug views 20-25 and 57, 58 replace the baseline with one baked term (no sun, no phase unless stated).
struct FroxelStaticLight
{
	vec3 baseline;
	vec3 momentDot;
	vec4 staticG;
	vec3 sun;
	vec3 sunUnshadowed;
	vec4 sunPhase;
};

// baked + sun of lobe k (0..2 the slots of the medium sample, 3 the global g):
// B + 3 g (M.v), the L1 phase response, never negative
vec3 FroxelStaticLobe(in FroxelStaticLight l, in int k)
{
#if defined(USE_FROXEL_STATIC_RECONSTRUCTION)
	return max(l.baseline + (3.0 * l.staticG[k]) * l.momentDot, vec3(0.0));
#else
	return l.baseline;
#endif
}

vec3 FroxelSunLobe(in FroxelStaticLight l, in int k)
{
	return l.sun * l.sunPhase[k];
}

// The static baked field (baseline, moments) is sampled at the froxel center pc: it is coarse, smooth,
// trilinearly filtered and static, and a directional response of a jittered sample would linger in the
// history. The sun (its shadow and its baked part) is sampled at the jittered p.
FroxelStaticLight BakedAndSunLight(in vec3 p, in vec3 pc, in float temporal, in vec4 g, in int debugView)
{
	FroxelStaticLight l;
	l.momentDot = vec3(0.0);
	l.staticG = clamp(g, vec4(-1.0 / 3.0), vec4(1.0 / 3.0));
	l.sun = vec3(0.0);
	l.sunUnshadowed = vec3(0.0);
	l.sunPhase = vec4(1.0);

	vec3 gridCoordC = (pc - u_FroxelGridOrigin.xyz) * u_FroxelGridScale.xyz;
	vec4 staticGrid = texture(u_VolumetricStaticGrid, gridCoordC);
	l.baseline = staticGrid.rgb * u_FroxelLightParams.w;
	float sunWeight = staticGrid.a;

#if defined(USE_FROXEL_STATIC_RECONSTRUCTION)
	// first angular moments of the attributed baked light (R_BuildStaticLighting), one per
	// channel: red and blue lamps keep their own directions, opposite lamps cancel to isotropic
	vec3 viewDirC = normalize(pc - u_FroxelViewOrigin.xyz);
	vec3 momentR = texture(u_VolumetricDirMomentR, gridCoordC).rgb;
	vec3 momentG = texture(u_VolumetricDirMomentG, gridCoordC).rgb;
	vec3 momentB = texture(u_VolumetricDirMomentB, gridCoordC).rgb;
	// the light travels along -M, towards the camera is -viewDir
	l.momentDot = vec3(dot(momentR, viewDirC), dot(momentG, viewDirC), dot(momentB, viewDirC)) * u_FroxelLightParams.w;
#endif

	vec3 gridCoord = (p - u_FroxelGridOrigin.xyz) * u_FroxelGridScale.xyz;
	if ((debugView >= 20 && debugView <= 25) || debugView == 57 || debugView == 58)
	{
		vec3 bakedSun = (u_FroxelSunDirection.w > 0.5) ? texture(u_VolumetricSunGrid, gridCoordC).rgb : vec3(0.0);
		vec3 reconstructed = staticGrid.rgb + bakedSun;
#if defined(USE_FROXEL_STATIC_RECONSTRUCTION)
		vec3 momentLength = vec3(length(momentR), length(momentG), length(momentB));
		vec3 momentLuma = momentR * 0.2126 + momentG * 0.7152 + momentB * 0.0722;
#else
		vec3 momentLength = vec3(0.0);
		vec3 momentLuma = vec3(0.0);
#endif
		if (debugView == 21)	// |M| per channel
			l.baseline = momentLength * u_FroxelLightParams.w;
		else if (debugView == 22)	// direction of the luminance moment, dimmed by |M| / B
		{
			float lumaB = dot(staticGrid.rgb, vec3(0.2126, 0.7152, 0.0722));
			float lengthLuma = length(momentLuma);
			vec3 dir = lengthLuma > 1e-8 ? momentLuma / lengthLuma : vec3(0.0);
			l.baseline = (dir * 0.5 + 0.5) * (lumaB > 1e-8 ? clamp(lengthLuma / lumaB, 0.0, 1.0) : 0.0) * lumaB * u_FroxelLightParams.w;
		}
		else if (debugView == 23)
			l.baseline = bakedSun * u_FroxelLightParams.w;
		else if (debugView == 24)
			l.baseline = reconstructed * u_FroxelLightParams.w;
		else if (debugView == 25)	// 100 * |B + S - legacy| (the legacy grid of the fog without the split)
			l.baseline = abs(reconstructed - texture(u_VolumetricLegacyGrid, gridCoordC).rgb) * 100.0;
		else if (debugView == 57)	// the baked light after the L1 phase, global g
			l.baseline = max(l.baseline + (3.0 * l.staticG.w) * l.momentDot, vec3(0.0));
		else if (debugView == 58)	// directional fraction |M_c| / B_c
			l.baseline = staticGrid.rgb * u_FroxelLightParams.w *
				clamp(momentLength / max(staticGrid.rgb, vec3(1e-8)), vec3(0.0), vec3(1.0));
		l.momentDot = vec3(0.0);
		return l;
	}

	if (u_FroxelSunDirection.w > 0.5)
	{
		// sunlight travels along -sunDirection, towards the camera is -viewDir
		vec3 viewDir = normalize(p - u_FroxelViewOrigin.xyz);
		l.sunPhase = FroxelPhases(g, dot(u_FroxelSunDirection.xyz, viewDir));
		vec3 bakedSun = texture(u_VolumetricSunGrid, gridCoord).rgb;
		l.sunUnshadowed = bakedSun;
		l.sun = bakedSun;
		if (u_FroxelSunColor.w > 0.5)
		{
			float coverage;
			float shadow = SunShadow(p, temporal, coverage);

			// The sun part of the light grid is S = f D, f = sun alignment * sky visibility of the cell
			// (traced at map load, R_BuildStaticLighting); the other (1 - f) D stays in the
			// baseline. Inside the cascades the realtime sun replaces exactly that part, f * sun, with
			// its shadow: a window edge cell (f 0.2) never gets the full sun on top of 0.8 D, and deep
			// indoors (f 0) the sun stays baked light.
			l.sun = mix(bakedSun, u_FroxelSunColor.rgb * (sunWeight * shadow), coverage);
			l.sunUnshadowed = mix(bakedSun, u_FroxelSunColor.rgb * sunWeight, coverage);

		}
		l.sun *= u_FroxelLightParams.y;
		l.sunUnshadowed *= u_FroxelLightParams.y;
#if defined(USE_CLOUD_SHADOWS)
		// clouds between the medium and the sun (r_cloudShadows): an attenuation of the sun itself, so the
		// "unshadowed" sun of the self-shadow / particle light terms gets it too
		{
			float cloudShadow = CloudShadow(p);
			l.sun *= cloudShadow;
			l.sunUnshadowed *= cloudShadow;
		}
#endif

#if defined(USE_LIQUIDS)
		// under a liquid surface: the sun passed the liquid above p (r_volumetricWaterSunPath), with
		// caustics blurred to the froxel size
		if (u_LiquidParams.x > 0.5)
		{
			float pathLength;
			float viewDepth = max(dot(p - u_FroxelViewOrigin.xyz, u_FroxelViewForward.xyz), 1.0);
			vec3 liquidSun = LiquidSunTransmittance(p, u_FroxelSunDirection.xyz, viewDepth * u_LiquidView.z, pathLength);
			l.sun *= liquidSun;
			l.sunUnshadowed *= liquidSun;
		}
#endif
	}

	return l;
}

// media pass (r_volumetricSelfShadow): the extinction of this frame at the froxel center
float FroxelMediaExtinction(in ivec2 cell, in int slice, in int debugView)
{
	float unused0, unused1, unused2, unused3, unused4;
#if defined(USE_LIQUIDS)
	froxelMediaPass = true;
#endif
	vec3 center = FroxelWorldPosition(vec3(vec2(cell) + 0.5, float(slice) + 0.5));
	// Self-shadow rays are not camera rays: retain point density here.
	float extinction = FroxelMedium(center, vec2(FroxelHeightExtinction(center), 0.0), debugView, false, unused0, unused1, unused2, unused3,
		unused4).extinction;
	if (isnan(extinction) || isinf(extinction))
		extinction = 0.0;
	return max(extinction, 0.0);
}

#if defined(USE_FROXEL_COMPUTE)
void FroxelInject(ivec2 cell)
{
#else
void main()
{
	ivec2 cell = ivec2(gl_FragCoord.xy);
#endif
	float temporal = u_FroxelJitter.w;
	int debugView = int(u_FroxelDebugParams.x);

#if !defined(USE_FROXEL_COMPUTE)
	// media pass of the raster path; compute has its own kernel (USE_FROXEL_MEDIA_PASS)
	if (u_ParticleLight.z > 0.5)
	{
		out_Color = vec4(FroxelMediaExtinction(cell, var_Slice, debugView));
		out_Dynamic = vec4(0.0);
		out_ParticleLight = vec4(0.0);
#if defined(USE_FROXEL_RGB)
		out_Extinction = vec4(0.0);
#endif
		return;
	}
#endif
	// the global g: the tail beyond far and the sprite particle light field (not a medium)
	float g = u_FroxelLightParams.x;

	// tail pass (var_Slice < 0, into u_FroxelTail): the light at the far side of the volume,
	// without albedo, lights the media beyond far (FroxelLookup). No medium test: a fog volume may
	// start beyond far. The tail light is stored with its phase: the media beyond far (BSP fog
	// volumes, height fog) are lit with the global g there, also a fog with fogAnisotropy.
	if (var_Slice < 0)
	{
		vec3 pf = FroxelWorldPosition(vec3(vec2(cell) + 0.5, u_FroxelGridSize.z));
		FroxelStaticLight tail = BakedAndSunLight(pf, pf, 0.0, vec4(g), debugView);
		vec3 sunTail = FroxelSunLobe(tail, 3);
		vec3 light = FroxelStaticLobe(tail, 3) + sunTail;
		if (debugView == 3)
			light = sunTail;
		else if (debugView == 4)
			light = vec3(0.0);
		else if (debugView == 5)
			light -= sunTail;
		else if (debugView == 31 || debugView == 33 || debugView == 34 || (debugView >= 35 && debugView <= 50))
			light = vec3(0.0);
		if (any(isnan(light)) || any(isinf(light)))
			light = vec3(0.0);
		out_Color = vec4(light, 1.0);
		out_Dynamic = vec4(0.0);
		out_ParticleLight = vec4(0.0);
#if defined(USE_FROXEL_RGB)
		out_Extinction = vec4(0.0);
#endif
		return;
	}

	float slice = float(var_Slice);

	// the baked light and the sun are sampled at a jittered position and accumulated over frames, the
	// dynamic lights at the froxel center
	vec3 center = vec3(vec2(cell) + 0.5, slice + 0.5);
	vec3 p = FroxelWorldPosition(center + u_FroxelJitter.xyz * temporal);
	vec3 pc = FroxelWorldPosition(center);

	float noisyFraction, localFraction, localChange, particleFraction, particleChange;
	vec2 heightSample = FroxelHeightSlice(center.xy + u_FroxelJitter.xy * temporal, slice);
	FroxelMediumSample medium = FroxelMedium(p, heightSample, debugView, true, noisyFraction, localFraction, localChange,
		particleFraction, particleChange);

	// FX particles (smoke) change all the time: where their density changed the history keeps only the
	// floor r_volumetricParticlesHistory of its weight (no long smoke ghost), not 0 (the jittered samples of a
	// drifting puff would flicker)
	float particleKeep = mix(1.0, u_FroxelParticleParams.w, smoothstep(0.02, 0.25, particleChange));

	// the medium at the center is only needed where the cluster has dynamic lights
	uint cluster = FroxelLightCluster(cell, var_Slice);
#if defined(USE_FROXEL_RGB)
	FroxelMediumSample mediumCenter = FroxelMediumSample(vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), 0.0, 0.0, 0.0, vec3(0.0));
#else
	FroxelMediumSample mediumCenter = FroxelMediumSample(vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), 0.0, 0.0, 0.0);
#endif
	if ((cluster >> 24) != 0u)
	{
		float unused0, unused1, unused2, unused3, unused4;
		vec2 heightCenter = temporal == 0.0 ? heightSample : FroxelHeightSlice(center.xy, slice);
		mediumCenter = FroxelMedium(pc, heightCenter, debugView, false, unused0, unused1, unused2, unused3, unused4);
	}

	// baked light and sun, with the phases of the lobe slots of the medium and of the global g
	FroxelStaticLight staticLight = FroxelStaticLight(vec3(0.0), vec3(0.0), vec4(0.0), vec3(0.0), vec3(0.0), vec4(1.0));
	if (medium.extinction > 0.0)
		staticLight = BakedAndSunLight(p, pc, temporal, vec4(medium.g, g), debugView);

	// media self-shadow of the sun: the optical depth of this frame's media towards the sun, on top
	// of the geometry (cascade) shadow. Only where the froxel has a medium lit by the sun, or the
	// sprite particle light field needs it.
	float selfShadowJitter = FroxelSelfShadowJitter(cell, var_Slice, temporal);
	float mediaTau = 0.0;
	float mediaT = 1.0;
	vec3 sunGeometry = staticLight.sun;
	if (u_FroxelSelfShadow.x > 0.5 && u_FroxelSunDirection.w > 0.5 &&
		((medium.extinction > 0.0 && any(greaterThan(staticLight.sunUnshadowed, vec3(0.0)))) || u_ParticleLight.x > 0.5))
	{
		mediaTau = FroxelMediaOpticalDepth(p, u_FroxelSunDirection.xyz, u_FroxelSelfShadow.z, 32768.0,
			int(u_FroxelSelfShadow.y), selfShadowJitter);
		mediaT = exp(-mediaTau);
	}
	staticLight.sun *= mediaT;
	// debug views 43 geometry shadow only, 44 media shadow only (45 = both, as view 3)
	if (debugView == 43)
		staticLight.sun = sunGeometry;
	else if (debugView == 44)
		staticLight.sun = staticLight.sunUnshadowed * mediaT;

	// multiple scattering octaves of the sun (r_volumetricMultiScatter, FroxelMultiScatterWeights) from
	// the media optical depth towards the sun. The geometry (cascade) shadow is lightened by at most
	// r_volumetricMultiScatterShadowFill * thickness: a thick cloud in a shadow glows a little, the shadow stays.
	// In the history like the single scattering (same jitter, same radiance clamp).
	vec3 msSun0 = vec3(0.0);
	vec3 msSun1 = vec3(0.0);
	vec3 msSun2 = vec3(0.0);
	vec3 msSunGlobal = vec3(0.0);
	float msThickness = 0.0;
	if (u_FroxelMultiScatter.x > 0.5 && u_FroxelSelfShadow.x > 0.5 && u_FroxelSunDirection.w > 0.5 &&
		medium.extinction > 0.0 && any(greaterThan(staticLight.sunUnshadowed, vec3(0.0))))
	{
		float albedo = Luma(medium.scatter0 + medium.scatter1 + medium.scatter2) / medium.extinction;
		vec4 weights = FroxelMultiScatterWeights(mediaTau, medium.extinction, albedo);
		msThickness = weights.w;
		vec3 viewDir = normalize(p - u_FroxelViewOrigin.xyz);
		vec4 msPhase = FroxelMultiScatterPhases(weights, vec4(medium.g, g), dot(u_FroxelSunDirection.xyz, viewDir));
		vec3 sunFill = mix(sunGeometry, staticLight.sunUnshadowed, u_FroxelMultiScatter2.z * weights.w);
		vec3 sunSingle = staticLight.sun;
		vec3 sunTop = staticLight.sunUnshadowed;
		vec4 sp = staticLight.sunPhase;
		msSun0 = FroxelMultiScatterClamp(sunSingle * sp.x, sunFill * msPhase.x, sunTop, sp.x);
		msSun1 = FroxelMultiScatterClamp(sunSingle * sp.y, sunFill * msPhase.y, sunTop, sp.y);
		msSun2 = FroxelMultiScatterClamp(sunSingle * sp.z, sunFill * msPhase.z, sunTop, sp.z);
		msSunGlobal = FroxelMultiScatterClamp(sunSingle * sp.w, sunFill * msPhase.w, sunTop, sp.w);
	}

	// dynamic lights
	vec3 dynamic0 = vec3(0.0);
	vec3 dynamic1 = vec3(0.0);
	vec3 dynamic2 = vec3(0.0);
	vec3 dynamicGlobal = vec3(0.0);
	if (mediumCenter.extinction > 0.0)
	{
		vec3 viewDir = normalize(pc - u_FroxelViewOrigin.xyz);
		vec2 msMedium = vec2(mediumCenter.extinction,
			Luma(mediumCenter.scatter0 + mediumCenter.scatter1 + mediumCenter.scatter2) / max(mediumCenter.extinction, 1e-12));
		DynamicLights(cluster, pc, viewDir, vec4(mediumCenter.g, g), selfShadowJitter, msMedium, dynamic0, dynamic1, dynamic2,
			dynamicGlobal);
		dynamic0 *= u_FroxelLightParams.z;
		dynamic1 *= u_FroxelLightParams.z;
		dynamic2 *= u_FroxelLightParams.z;
		dynamicGlobal *= u_FroxelLightParams.z;
	}

	// sprite particle light field: the light at the froxel center, also where there is no medium,
	// with the global g (the sprites are no medium of the volume). Baked + sun at the center without
	// the temporal jitter (full shadow filter, no history), reused when the fog evaluated them there
	// already.
	vec3 particleLight = vec3(0.0);
	if (u_ParticleLight.x > 0.5)
	{
		vec3 particleStatic, particleSun;
		if (medium.extinction > 0.0 && temporal == 0.0 && debugView < 20)
		{
			particleStatic = FroxelStaticLobe(staticLight, 3);
			particleSun = FroxelSunLobe(staticLight, 3) + msSunGlobal;
		}
		else
		{
			FroxelStaticLight l = BakedAndSunLight(pc, pc, 0.0, vec4(g), 0);
			particleStatic = FroxelStaticLobe(l, 3);
			particleSun = FroxelSunLobe(l, 3) * mediaT + msSunGlobal;
		}

		vec3 particleDynamic = dynamicGlobal;
		if ((cluster >> 24) != 0u && mediumCenter.extinction <= 0.0)
		{
			vec3 viewDir = normalize(pc - u_FroxelViewOrigin.xyz);
			vec3 unused0, unused1, unused2;
			DynamicLights(cluster, pc, viewDir, vec4(g), selfShadowJitter, vec2(0.0), unused0, unused1, unused2, particleDynamic);
			particleDynamic *= u_FroxelLightParams.z;
		}

		int term = int(u_ParticleLight.y);
		if (term == 2)
			particleLight = particleStatic;
		else if (term == 3)
			particleLight = particleSun;
		else if (term == 4)
			particleLight = particleDynamic;
		else
			particleLight = particleStatic + particleSun + particleDynamic;
		if (any(isnan(particleLight)) || any(isinf(particleLight)))
			particleLight = vec3(0.0);
	}

	// the light of each lobe slot: baked + sun, and the dynamic lights
	vec3 static0 = FroxelStaticLobe(staticLight, 0);
	vec3 static1 = FroxelStaticLobe(staticLight, 1);
	vec3 static2 = FroxelStaticLobe(staticLight, 2);
	vec3 sun0 = FroxelSunLobe(staticLight, 0);
	vec3 sun1 = FroxelSunLobe(staticLight, 1);
	vec3 sun2 = FroxelSunLobe(staticLight, 2);

	// debug views 46 sun single scattering, 47 sun octaves only, 48 both (as view 3); the views of an
	// isolated shadow (2, 43, 44) stay single scattering
	vec3 ssSun0 = sun0;
	vec3 ssSun1 = sun1;
	vec3 ssSun2 = sun2;
	if (debugView == 47)
	{
		sun0 = msSun0;
		sun1 = msSun1;
		sun2 = msSun2;
	}
	else if (debugView != 2 && debugView != 43 && debugView != 44 && debugView != 46)
	{
		sun0 += msSun0;
		sun1 += msSun1;
		sun2 += msSun2;
	}

	// debug views of a single light term
	if (debugView == 2)
	{
		sun0 = staticLight.sunUnshadowed * staticLight.sunPhase.x;
		sun1 = staticLight.sunUnshadowed * staticLight.sunPhase.y;
		sun2 = staticLight.sunUnshadowed * staticLight.sunPhase.z;
		static0 = static1 = static2 = vec3(0.0);
		dynamic0 = dynamic1 = dynamic2 = vec3(0.0);
	}
	else if (debugView == 3 || (debugView >= 43 && debugView <= 48))
	{
		static0 = static1 = static2 = vec3(0.0);
		dynamic0 = dynamic1 = dynamic2 = vec3(0.0);
	}
	else if (debugView == 4)
	{
		static0 = static1 = static2 = vec3(0.0);
		sun0 = sun1 = sun2 = vec3(0.0);
	}
	else if (debugView == 5 || (debugView >= 20 && debugView <= 25) || debugView == 57 || debugView == 58)
	{
		sun0 = sun1 = sun2 = vec3(0.0);
		dynamic0 = dynamic1 = dynamic2 = vec3(0.0);
	}

	// j_scatter = sum_k S_k * L * P(g_k) (FroxelMediumSample)
	vec4 current = vec4(FroxelScatter(medium, static0 + sun0, static1 + sun1, static2 + sun2), medium.extinction);
#if defined(USE_FROXEL_RGB)
	vec3 currentExtinction = medium.extinctionRGB;
#endif

	// emission j_e at the froxel center: no jitter and no history (it goes to the dynamic volume), so
	// a fast fire or explosion leaves no after-image and the history clamp sees scattering only
	vec3 emission = FroxelEmission(pc, debugView);

	// debug view 17: share of the local volumes (red) and of the other media (green), integrated
	// like an emission: the integrated rg is each medium's share of the opacity along the ray
	if (debugView == 17)
		current.rgb = vec3(localFraction, 1.0 - localFraction, 0.0) * medium.extinction;

	// debug view 27: FX particle media, red = history reduction where the particle density changed,
	// green = their share of the medium, integrated like view 17
	if (debugView == 27)
		current.rgb = vec3(1.0 - particleKeep, particleFraction, 0.0) * medium.extinction;

	// debug views 35-39 of the medium sample, integrated like view 17 (value * extinction: the
	// opacity weighted value along the ray, no light):
	//   35 extinction (white = opacity)
	//   36 single scattering albedo sum_k S_k / extinction, rgb (dark = absorptive)
	//   37 lobe slots: red = slots in use / 3, green = more distinct g than slots (merged,
	//      approximated), blue = 1 - the share of the strongest slot in the scattering
	//   38 effective mixed g (scattering weighted mean cosine): red > 0 forward, blue < 0 backward
	//   39 phase of the sunlight towards the camera, the lobe mixture: P / (1 + P), 0.5 grey =
	//      isotropic, brighter forward, darker backward
	//   41 optical depth of the media towards the sun / 8 (r_volumetricSelfShadow)
	//   42 media transmittance towards the sun
	//   49 multiple scattering ratio of the sun: octaves / (single + octaves), luma (r_volumetricMultiScatter)
	//   50 optical depth: red = towards the sun / 8, green = extinction * r_volumetricMultiScatterLength / 8,
	//      blue = thickness 1 - exp(-extinction * l)
	bool mediumDebug = (debugView >= 35 && debugView <= 42 && debugView != 40) || debugView == 49 || debugView == 50;
	if (mediumDebug)
	{
		vec3 scatter = medium.scatter0 + medium.scatter1 + medium.scatter2;
		vec3 value = vec3(0.0);
		if (debugView == 35)
			value = vec3(1.0);
		else if (debugView == 36)
			value = scatter / max(medium.extinction, 1e-12);
		else if (debugView == 37)
		{
			vec3 w = vec3(Luma(medium.scatter0), Luma(medium.scatter1), Luma(medium.scatter2));
			float strongest = max(w.x, max(w.y, w.z)) / max(w.x + w.y + w.z, 1e-20);
			value = vec3(medium.lobes / 3.0, medium.merged, 1.0 - strongest);
		}
		else if (debugView == 41)
			value = vec3(mediaTau * 0.125);
		else if (debugView == 42)
			value = vec3(mediaT);
		else if (debugView == 49)
		{
			float ss = Luma(FroxelScatter(medium, ssSun0, ssSun1, ssSun2));
			float ms = Luma(FroxelScatter(medium, msSun0, msSun1, msSun2));
			value = vec3(ms / max(ss + ms, 1e-12));
		}
		else if (debugView == 50)
			value = vec3(mediaTau, medium.extinction * u_FroxelMultiScatter.w, 8.0 * msThickness) * 0.125;
		else if (debugView == 38)
		{
			float gMixed = FroxelLobeMean(medium, medium.g);
			value = vec3(max(gMixed, 0.0), 0.0, max(-gMixed, 0.0));
		}
		else
		{
			vec3 viewDir = normalize(p - u_FroxelViewOrigin.xyz);
			vec4 phases = FroxelPhases(vec4(medium.g, g), dot(u_FroxelSunDirection.xyz, viewDir));
			float phase = FroxelLobeMean(medium, phases.xyz);
			value = vec3(phase / (1.0 + phase));
		}
		current.rgb = value * medium.extinction;
	}

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
#if defined(USE_FROXEL_RGB)
				vec3 historyExtinction = texture(u_FroxelExtinction, coord).rgb;
				if (any(isnan(historyExtinction)) || any(isinf(historyExtinction)) ||
					any(lessThan(historyExtinction, vec3(0.0))))
					historyExtinction = currentExtinction;
				currentExtinction = mix(currentExtinction, historyExtinction, weight);
#endif
			}
		}
	}

	if (debugView == 8)
		current.rgb = vec3(weight) * current.a;

	// one bad froxel must not poison the next frames (history) nor the integrated column
	vec4 dynamicEmission = vec4(FroxelScatter(mediumCenter, dynamic0, dynamic1, dynamic2), 1.0);
	if (debugView == 17 || mediumDebug)
		dynamicEmission.rgb = vec3(0.0);
	if (mediumDebug)
		emission = vec3(0.0);

	// j_total = j_scatter + j_emissive. Debug views: 30 scattering source, 31 emissive source,
	// 32 both (integrated with the extinction forced to 0: the sum of j * length along the ray),
	// 33 emission integrated with the real extinction (self absorption), 34 history contribution
	// (red: the history part of the scattering, green: the emission, never from history)
	if (debugView == 30)
		emission = vec3(0.0);
	else if (debugView == 31 || debugView == 33)
	{
		current.rgb = vec3(0.0);
		dynamicEmission.rgb = vec3(0.0);
	}
	else if (debugView == 34)
	{
		vec3 luma = vec3(0.2126, 0.7152, 0.0722);
		current.rgb = vec3(weight * dot(current.rgb, luma), 0.0, 0.0);
		dynamicEmission.rgb = vec3(0.0);
		emission = vec3(0.0, dot(emission, luma), 0.0);
	}
	dynamicEmission.rgb += emission;

	if (any(isnan(current)) || any(isinf(current)))
		current = vec4(0.0);
	if (any(isnan(dynamicEmission)) || any(isinf(dynamicEmission)))
		dynamicEmission = vec4(0.0, 0.0, 0.0, 1.0);

#if defined(USE_FROXEL_RGB)
	// the debug views that integrate a value * extinction measure a scalar opacity
	if (debugView == 8 || debugView == 17 || debugView == 27 || mediumDebug)
		currentExtinction = vec3(current.a);
	if (any(isnan(currentExtinction)) || any(isinf(currentExtinction)))
		currentExtinction = vec3(0.0);
	out_Extinction = vec4(max(currentExtinction, vec3(0.0)), current.a);
#endif

	out_Color = current;
	out_Dynamic = dynamicEmission;
	out_ParticleLight = vec4(particleLight, 1.0);
}

#if defined(USE_FROXEL_COMPUTE)
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
layout(rgba16f, binding = 0) uniform restrict writeonly image3D u_InjectOutput;
layout(r11f_g11f_b10f, binding = 1) uniform restrict writeonly image3D u_DynamicOutput;
layout(r11f_g11f_b10f, binding = 2) uniform restrict writeonly image3D u_ParticleOutput;
#if defined(USE_FROXEL_RGB)
layout(rgba16f, binding = 3) uniform restrict writeonly image3D u_ExtinctionOutput;
#endif
layout(rgba16f, binding = 4) uniform restrict writeonly image2D u_TailOutput;
layout(r16f, binding = 5) uniform restrict writeonly image3D u_MediaOutput;

#if defined(USE_FROXEL_MEDIA_PASS)
// Media pass kernel: only the medium evaluation, so its register use is not
// sized by the lighting path of the full injection.
void main()
{
	ivec3 cell = ivec3(gl_GlobalInvocationID);
	if (any(greaterThanEqual(cell, ivec3(u_FroxelGridSize.xyz))))
		return;
	var_Slice = cell.z;
	imageStore(u_MediaOutput, cell, vec4(FroxelMediaExtinction(cell.xy, cell.z, int(u_FroxelDebugParams.x))));
}
#else
void main()
{
	ivec3 cell = ivec3(gl_GlobalInvocationID);
	if (any(greaterThanEqual(cell, ivec3(u_FroxelGridSize.xyz))))
		return;
	var_Slice = cell.z;
	FroxelInject(cell.xy);
	imageStore(u_InjectOutput, cell, out_Color);
	imageStore(u_DynamicOutput, cell, out_Dynamic);
	if (u_ParticleLight.x > 0.5)
		imageStore(u_ParticleOutput, cell, out_ParticleLight);
#if defined(USE_FROXEL_RGB)
	imageStore(u_ExtinctionOutput, cell, out_Extinction);
#endif
	// The first invocation in each column also writes its analytic tail light.
	if (cell.z == 0)
	{
		var_Slice = -1;
		FroxelInject(cell.xy);
		imageStore(u_TailOutput, cell.xy, out_Color);
	}
}
#endif
#endif
