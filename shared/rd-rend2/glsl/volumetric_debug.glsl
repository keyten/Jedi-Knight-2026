/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// r_volumetricFogDebug views of the froxel fog (tr_volumetric.cpp), drawn over the tone mapped frame:
//
//  1  density: optical depth between the camera and the scene (blue 0 .. red 4 and more)
//  2  sun in-scattering without shadows          (the injection keeps only that light term)
//  3  sun in-scattering with the shadow maps
//  4  dynamic light in-scattering
//  5  baked (light grid) in-scattering
//  6  in-scattering of all the lights
//  7  transmittance
//  8  temporal history weight, averaged over the fogged froxels in front of the scene (red: no fog)
//  9  integrated volume: in-scattering over black, faded where transmittance is low
//  10 froxel slice index at the scene depth, froxel grid lines
//  11 density of the BSP fog volumes only       (the injection drops the height fog)
//  12 density of the height fog only            (the injection drops the BSP fog volumes)
//  13 density noise m(p) at the scene surface, per pixel, world space (gray 0..1, 1 = white, then
//     yellow to red up to 4); r_volumetricFogNoise must be on
//  14 extinction of the froxel at the scene depth, without the noise (the injection drops it)
//  15 extinction of the froxel at the scene depth, with the noise
//     (14, 15: heat of the optical depth of 512 units, the scale of view 1)
//  16 density of the local fog volumes only     (the injection drops the BSP fog and the height fog)
//  17 share of the fog along the ray: red = local fog volumes, green = BSP fog / height fog,
//     brightness = opacity
//  18 local fog volume bounds over the frame: outer shell (bright rim) and inner shell where the
//     soft edge starts (thin rim), one hue per volume index, dimmed where behind the scene
//  19 number of local volumes in the XYZ cluster at the scene depth (heat, 8 = red),
//     slice stripes; r_fogvol slices prints the indices
//  20-25 baked light grid terms, as in-scattering (the injection keeps only that term, no sun, no
//     dynamic lights; R_BuildStaticLighting): 20 non-sun baseline B, 21 |M| per channel (the
//     first angular moments, r_volumetricFogStaticDirectional), 22 direction of the luminance moment
//     (rgb = dir * 0.5 + 0.5, dimmed by |M| / B), 23 baked sun S, 24 B + S, 25 100 * |B + S - legacy
//     merged grid| (black)
//  26 density of the FX particle media only    (the injection drops every other medium)
//  27 FX particle media along the ray, opacity weighted: red = history reduction where the particle
//     density changed, green = particle share of the medium
//  28 FX particle proxy bounds over the frame (uploaded particles only): outer shell and inner shell
//     where the soft edge starts, one hue per index (0 = most important), dimmed behind the scene
//  29 dynamic lights listed for the froxel cluster at the scene depth (heat, 8 = red), cyan where
//     spot lights are listed (brighter: more), slice stripes (the slice light mask of the injection)
//  30-34 emission (volumetric_inject.glsl FroxelEmission): 30 scattering source j_s, 31 emissive
//     source j_e, 32 j_s + j_e (30-32: sum of j * length along the ray, the extinction forced to 0),
//     33 emission integrated with the real extinction (self absorption in dense smoke),
//     34 history contribution: red = history part of the scattering, green = emission (no history)
//  35-39 per-medium albedo and anisotropy (volumetric_inject.glsl FroxelMediumSample), opacity
//     weighted along the ray, dark grey without medium: 35 extinction (opacity), 36 single
//     scattering albedo rgb (dark = absorptive), 37 lobes (red = forward g, green = -backward g,
//     blue = share of the backward lobe), 38 effective mixed g (red forward, blue backward),
//     39 phase of the sunlight towards the camera P / (1 + P) (0.5 grey = isotropic)
//  40-50 media self-shadow and multiple scattering (volumetric_inject.glsl)
//  51-56 RGB extinction (r_volumetricFogRGBExtinction; dark magenta when off):
//     51 sigma_t.rgb of the froxel at the scene depth, as the opacity of 512 units per channel
//        1 - exp(-512 sigma_t) (red = absorbs red)
//     52 transmittance T.rgb between the camera and the scene (the color the medium lets through)
//     53 color shift of a white surface: (T.rgb - T_scalar) * 4 + 0.5 (grey = none; the scalar
//        reference uses sigma = mean(sigma_t.rgb), the in-scattering S is the same in both)
//     54 |T.rgb - T_scalar| heat (max channel, 0.25 = red): where RGB and scalar extinction differ
//     55 extinction chroma sigma_t.rgb / mean at the scene depth / 3 (grey = neutral, black = none)
//     56 transmittance of the analytic tail beyond the last slice (white = none; dark blue where
//        the scene is inside the volume)
//  57-58 directional baked light (r_volumetricFogStaticDirectional, black without it), as views 20-25:
//     57 the baked light after the L1 phase B + 3 g (M.v) with the global g, 58 B * |M_c| / B_c (the
//     directional fraction per channel)
//  59-64 liquid media (r_volumetricWater, glsl/liquid_common.glsl; dark magenta when off):
//     59 density of the liquids only            (the injection drops every other medium)
//     60 liquid brushes over the frame: fill and edges, blue water, green slime, orange lava, dimmed
//        behind the scene
//     61 camera contents: the hue of the CPU class (collision, once per view), bottom bar the GPU
//        brushes at the camera, red stripes where they disagree
//     62 liquid boundary: froxels along the ray to the scene cut by a liquid surface (yellow, 2 =
//        full) over the fully covered ones (blue, 8 = full)
//     63 transmittance of the liquids alone between the camera and the scene, exact (rgb with
//        r_volumetricFogRGBExtinction, else the scalar extinction)
//     64 sun under the liquids at the scene surface: transmittance * caustics (grey 0.75 = no
//        attenuation, dark grey: not in a liquid)
//
// r_particleLightingDebug 1-4 (u_ParticleLight.x = 1): the sprite particle light field just in front of
// the scene (all lights, or the term the injection kept: 2 baked, 3 sun, 4 dynamic), tone mapped

uniform sampler2D u_ScreenDepthMap;
#if defined(USE_FROXEL_RGB)
uniform sampler3D u_FroxelExtinction;	// sigma_t.rgb of this frame (views 51, 55)
#endif
// view 29: the dynamic light lists of the injection (R_VolumetricBuildLightLists)
uniform samplerBuffer u_FPlusLights;
uniform usamplerBuffer u_FPlusGridMap;
#define FROXEL_LIGHT_TEXELS 5
uniform sampler3D u_FroxelMedia;	// extinction of this frame (r_volumetricSelfShadow, view 40)
uniform sampler3D u_FroxelSource;	// injected volume: rgb / a = history weight in view 8

out vec4 out_Color;

vec3 Heat(in float x)
{
	x = clamp(x, 0.0, 1.0);
	return clamp(vec3(1.5 - abs(4.0 * x - 3.0), 1.5 - abs(4.0 * x - 2.0), 1.5 - abs(4.0 * x - 1.0)), 0.0, 1.0);
}

vec3 Display(in vec3 hdr)
{
	return hdr / (1.0 + hdr);
}

vec3 IndexHue(in int i)
{
	float h = fract(float(i) * 0.618034) * 6.0;
	return clamp(vec3(abs(h - 3.0) - 1.0, 2.0 - abs(h - 2.0), 2.0 - abs(h - 4.0)), 0.0, 1.0);
}

// edges of the cube [-size, size]^3 along the ray o + t * d (unit local space): front and back
// edges, dimmed behind the scene. -1: the ray misses the cube.
float BoxOutline(in vec3 o, in vec3 d, in float size, in float sceneDistance, in float width)
{
	vec3 invD = 1.0 / mix(d, vec3(1e-8), lessThan(abs(d), vec3(1e-8)));
	vec3 t0 = (-size - o) * invD;
	vec3 t1 = ( size - o) * invD;
	vec3 tMin = min(t0, t1);
	vec3 tMax = max(t0, t1);
	float tNear = max(max(tMin.x, tMin.y), tMin.z);
	float tFar = min(min(tMax.x, tMax.y), tMax.z);
	if (tNear > tFar || tFar <= 0.0)
		return -1.0;

	float coverage = 0.0;
	for (int k = 0; k < 2; k++)
	{
		float t = (k == 0) ? tNear : tFar;
		if (t <= 0.0)
			continue;
		vec3 edge = smoothstep(1.0 - width, 1.0, abs(o + d * t) / size);
		float line = max(edge.x * edge.y, max(edge.y * edge.z, edge.x * edge.z));
		coverage = max(coverage, line * ((t < sceneDistance) ? 1.0 : 0.35));
	}
	return coverage;
}

// view 18: outline of the local volume i along the ray origin + t * dir (t in world units).
// rgb: color, a: coverage. sceneDistance: the scene along the ray (hidden parts are dimmed).
vec4 LocalVolumeOutline(in int i, in vec3 origin, in vec3 dir, in float sceneDistance)
{
	vec4 rx = u_FroxelLocalX[i];
	vec4 ry = u_FroxelLocalY[i];
	vec4 rz = u_FroxelLocalZ[i];
	vec3 o = vec3(dot(rx.xyz, origin) + rx.w, dot(ry.xyz, origin) + ry.w, dot(rz.xyz, origin) + rz.w);
	vec3 d = vec3(dot(rx.xyz, dir), dot(ry.xyz, dir), dot(rz.xyz, dir));
	float inner = u_FroxelLocalShape[i].y;
	vec3 hue = IndexHue(i);
	float dd = max(dot(d, d), 1e-12);

	if (u_FroxelLocalShape[i].x < 0.5)
	{
		// closest approach of the ray to the center in the unit sphere space: rims at 1 and inner
		float t = -dot(o, d) / dd;
		if (t <= 0.0)
			return vec4(0.0);
		float b = length(o + d * t);
		float visible = (t < sceneDistance) ? 1.0 : 0.35;
		float outer = 1.0 - smoothstep(0.0, 0.03, abs(b - 1.0));
		float soft = (1.0 - smoothstep(0.0, 0.015, abs(b - inner))) * 0.6;
		float fill = (b < 1.0) ? 0.08 : 0.0;
		return vec4(hue, max(max(outer, soft), fill) * visible);
	}

	// box: the outer and the inner cube, edges where two coordinates reach the faces
	float outer = BoxOutline(o, d, 1.0, sceneDistance, 0.04);
	if (outer < 0.0)
		return vec4(0.0);
	float soft = (inner > 0.05) ? BoxOutline(o, d, inner, sceneDistance, 0.02) * 0.6 : 0.0;
	return vec4(hue, max(max(outer, soft), 0.08));
}

// view 28: outline of the FX particle proxy i (ellipsoid along the world axes), as the local
// ellipsoids of view 18
vec4 ParticleOutline(in int i, in vec3 origin, in vec3 dir, in float sceneDistance)
{
	vec4 center = u_FroxelParticleCenter[i];
	vec4 invExtent = u_FroxelParticleInvExtent[i];
	vec3 o = (origin - center.xyz) * invExtent.xyz;
	vec3 d = dir * invExtent.xyz;
	float dd = max(dot(d, d), 1e-12);
	float t = -dot(o, d) / dd;
	if (t <= 0.0)
		return vec4(0.0);
	float b = length(o + d * t);
	float visible = (t < sceneDistance) ? 1.0 : 0.35;
	float outer = 1.0 - smoothstep(0.0, 0.04, abs(b - 1.0));
	float soft = (1.0 - smoothstep(0.0, 0.02, abs(b - FroxelParticleInner(invExtent.w)))) * 0.6;
	float fill = (b < 1.0) ? 0.06 : 0.0;
	return vec4(IndexHue(i), max(max(outer, soft), fill) * visible);
}

void main()
{
	vec2 tc = gl_FragCoord.xy / r_FBufScale;
	float depth = texture(u_ScreenDepthMap, tc).r;
	vec3 worldPos = FroxelSceneWorldPosition(tc, depth);
	vec4 fog = FroxelFog(worldPos);

	int view = int(u_FroxelDebugParams.x);
	vec3 color = vec3(0.0);

	if (u_ParticleLight.x > 0.5)
	{
		// a little in front of the surface: the froxel there is in the air, not behind the wall
		vec3 toScene = worldPos - u_FroxelViewOrigin.xyz;
		vec3 airPos = u_FroxelViewOrigin.xyz + toScene * 0.97;
		out_Color = vec4(Display(ParticleLightLookup(airPos)), 1.0);
		return;
	}

	if (view == 1 || view == 11 || view == 12 || view == 16 || view == 26 || view == 59)
	{
		color = Heat(-log(max(fog.a, 1e-4)) / 4.0);
	}
	else if ((view >= 2 && view <= 6) || (view >= 20 && view <= 25) || view == 57 || view == 58 || (view >= 30 && view <= 34) || (view >= 43 && view <= 48))
	{
		color = Display(fog.rgb);
	}
	else if (view == 7)
	{
		color = vec3(fog.a);
	}
	else if (view == 8)
	{
		vec4 clip = u_FroxelViewProjection * vec4(worldPos, 1.0);
		vec2 uv = clamp(clip.xy / max(clip.w, 1e-3) * 0.5 + 0.5, 0.0, 1.0);
		float d = dot(worldPos - u_FroxelViewOrigin.xyz, u_FroxelViewForward.xyz);
		int numSlices = int(u_FroxelGridSize.z);
		float sum = 0.0;
		float count = 0.0;
		for (int k = 0; k < numSlices; k++)
		{
			if (FroxelWToDepth(float(k) / float(numSlices)) > d)
				break;
			vec4 froxel = texture(u_FroxelSource, vec3(uv, (float(k) + 0.5) / float(numSlices)));
			if (froxel.a > 0.0)
			{
				sum += froxel.r / froxel.a;
				count += 1.0;
			}
		}
		color = (count > 0.0) ? vec3(sum / count) : vec3(0.5, 0.0, 0.0);
	}
	else if (view == 9)
	{
		color = Display(fog.rgb) + vec3(0.0, 0.0, 0.15) * (1.0 - fog.a);
	}
	else if (view == 13)
	{
		float m = (u_FroxelNoiseLod.w > 0.5) ? FroxelNoiseModulation(worldPos, 0.0) : 1.0;
		color = (m <= 1.0) ? vec3(m) : mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), clamp((m - 1.0) / 3.0, 0.0, 1.0));
	}
	else if (view == 14 || view == 15)
	{
		vec4 clip = u_FroxelViewProjection * vec4(worldPos, 1.0);
		vec2 uv = clamp(clip.xy / max(clip.w, 1e-3) * 0.5 + 0.5, 0.0, 1.0);
		float d = dot(worldPos - u_FroxelViewOrigin.xyz, u_FroxelViewForward.xyz);
		float w = FroxelDepthToW(min(d, u_FroxelSliceParams.y));
		float slice = min(floor(w * u_FroxelGridSize.z), u_FroxelGridSize.z - 1.0);
		float extinction = texture(u_FroxelSource, vec3(uv, (slice + 0.5) / u_FroxelGridSize.z)).a;
		color = Heat(extinction * 512.0 / 4.0);
	}
	else if (view == 17)
	{
		float opacity = 1.0 - fog.a;
		color = vec3(fog.r, fog.g, 0.0) / max(fog.r + fog.g, 1e-4) * sqrt(opacity);
	}
	else if (view == 27)
	{
		float opacity = 1.0 - fog.a;
		color = vec3(fog.r, fog.g, 0.0) / max(opacity, 1e-4) * sqrt(opacity);
	}
	else if (view == 35)
	{
		// extinction: the opacity of the medium along the ray
		color = vec3(1.0 - fog.a);
	}
	else if (view == 40)
	{
		// extinction of this frame (media pass) at the surface slice, as view 14
		vec4 clip = u_FroxelViewProjection * vec4(worldPos, 1.0);
		vec2 uv = clamp(clip.xy / max(clip.w, 1e-3) * 0.5 + 0.5, 0.0, 1.0);
		float d = dot(worldPos - u_FroxelViewOrigin.xyz, u_FroxelViewForward.xyz);
		float w = FroxelDepthToW(min(d, u_FroxelSliceParams.y));
		float slice = min(floor(w * u_FroxelGridSize.z), u_FroxelGridSize.z - 1.0);
		float extinction = texture(u_FroxelMedia, vec3(uv, (slice + 0.5) / u_FroxelGridSize.z)).r;
		color = (u_FroxelSelfShadow.x > 0.5) ? Heat(extinction * 512.0 / 4.0) : vec3(0.3, 0.0, 0.3);
	}
	else if ((view >= 36 && view <= 42) || view == 49 || view == 50)
	{
		// opacity weighted mean of the medium value along the ray (the injection writes value *
		// extinction), dark grey where there is (almost) no medium
		float opacity = 1.0 - fog.a;
		vec3 value = fog.rgb / max(opacity, 1e-4);
		color = mix(vec3(0.05), clamp(value, 0.0, 1.0), smoothstep(0.0, 0.05, opacity));
	}
	else if (view == 28)
	{
		// drawn blended over the frame (RB_VolumetricDebugOverlay)
		vec3 toScene = worldPos - u_FroxelViewOrigin.xyz;
		float sceneDistance = length(toScene);
		vec3 dir = toScene / max(sceneDistance, 1e-4);
		vec4 outline = vec4(0.0);
		int count = min(int(u_FroxelParticleParams.x), MAX_GPU_VOL_PARTICLES);
		for (int i = 0; i < count; i++)
		{
			if (u_FroxelParticleCenter[i].w <= 0.0)
				continue;	// gone this frame (kept one frame for the history)
			vec4 o = ParticleOutline(i, u_FroxelViewOrigin.xyz, dir, sceneDistance);
			if (o.a > outline.a)
				outline = o;
		}
		out_Color = outline;
		return;
	}
	else if (view == 18)
	{
		// drawn blended over the frame (RB_VolumetricDebugOverlay)
		vec3 toScene = worldPos - u_FroxelViewOrigin.xyz;
		float sceneDistance = length(toScene);
		vec3 dir = toScene / max(sceneDistance, 1e-4);
		vec4 outline = vec4(0.0);
		int count = int(u_FroxelLocalParams.x);
		for (int i = 0; i < count; i++)
		{
			if (u_FroxelLocalColor[i].a <= 0.0)
				continue;	// gone this frame (kept one frame for the history)
			vec4 o = LocalVolumeOutline(i, u_FroxelViewOrigin.xyz, dir, sceneDistance);
			if (o.a > outline.a)
				outline = o;
		}
		out_Color = outline;
		return;
	}
	else if (view == 19)
	{
		float d = dot(worldPos - u_FroxelViewOrigin.xyz, u_FroxelViewForward.xyz);
		int slice = int(min(floor(FroxelDepthToW(min(d, u_FroxelSliceParams.y)) * u_FroxelGridSize.z),
			u_FroxelGridSize.z - 1.0));
		uvec2 mask = u_FroxelLocalParams.x > 0.5 ? FroxelLocalCluster(worldPos, slice) : uvec2(0u);
		int count = 0;
		while (any(notEqual(mask, uvec2(0u))))
		{
			FroxelLocalNext(mask);
			count++;
		}
		color = (count > 0) ? Heat(float(count) / 8.0) : vec3(0.12);
		if ((slice & 1) != 0)
			color *= 0.75;
	}
	else if (view == 29)
	{
		vec4 clip = u_FroxelViewProjection * vec4(worldPos, 1.0);
		vec2 uv = clamp(clip.xy / max(clip.w, 1e-3) * 0.5 + 0.5, 0.0, 1.0);
		float d = dot(worldPos - u_FroxelViewOrigin.xyz, u_FroxelViewForward.xyz);
		int slice = int(min(floor(FroxelDepthToW(min(d, u_FroxelSliceParams.y)) * u_FroxelGridSize.z),
			u_FroxelGridSize.z - 1.0));
		color = vec3(0.12);
		if (u_FroxelLightTile > 0)
		{
			ivec2 cell = min(ivec2(uv * u_FroxelGridSize.xy), ivec2(u_FroxelGridSize.xy) - 1);
			ivec2 tile = cell / u_FroxelLightTile;
			int cluster = (slice * u_FroxelLightTilesY + tile.y) * u_FroxelLightTilesX + tile.x;
			uint header = texelFetch(u_FPlusGridMap, cluster).r;
			int first = int(header & 0xffffffu);
			int count = int(header >> 24);
			int spots = 0;
			for (int j = 0; j < count; j++)
			{
				int i = int(texelFetch(u_FPlusGridMap, first + j).r);
				if (texelFetch(u_FPlusLights, i * FROXEL_LIGHT_TEXELS + 3).x > -0.5)
					spots++;
			}
			if (count > 0)
				color = Heat(float(count) / 8.0);
			if (spots > 0)
				color = mix(color, vec3(0.0, 1.0, 1.0), clamp(0.4 + 0.2 * float(spots), 0.0, 1.0));
		}
		if ((slice & 1) != 0)
			color *= 0.75;
	}
	else if (view >= 51 && view <= 56)
	{
#if defined(USE_FROXEL_RGB)
		vec3 T;
		FroxelFogRGB(worldPos, T);
		vec4 clip = u_FroxelViewProjection * vec4(worldPos, 1.0);
		vec2 uv = clamp(clip.xy / max(clip.w, 1e-3) * 0.5 + 0.5, 0.0, 1.0);
		float d = dot(worldPos - u_FroxelViewOrigin.xyz, u_FroxelViewForward.xyz);
		float w = FroxelDepthToW(min(d, u_FroxelSliceParams.y));
		float slice = min(floor(w * u_FroxelGridSize.z), u_FroxelGridSize.z - 1.0);
		vec3 sigma = texture(u_FroxelExtinction, vec3(uv, (slice + 0.5) / u_FroxelGridSize.z)).rgb;
		if (view == 51)
			color = vec3(1.0) - exp(-sigma * 512.0);
		else if (view == 52)
			color = T;
		else if (view == 53)
			color = clamp((T - vec3(fog.a)) * 4.0 + 0.5, 0.0, 1.0);
		else if (view == 54)
		{
			vec3 delta = abs(T - vec3(fog.a));
			color = Heat(max(delta.r, max(delta.g, delta.b)) * 4.0);
		}
		else if (view == 55)
		{
			float mean = dot(sigma, vec3(1.0 / 3.0));
			color = (mean > 1e-7) ? sigma / mean / 3.0 : vec3(0.0);
		}
		else
		{
			float farZ = u_FroxelSliceParams.y;
			color = vec3(0.0, 0.0, 0.25);
			if (d > farZ)
			{
				vec3 toPos = worldPos - u_FroxelViewOrigin.xyz;
				vec3 a = u_FroxelViewOrigin.xyz + toPos * (farZ / d);
				float len = (d - farZ) * length(toPos) / max(d, 1e-3);
				vec3 tau;
				FroxelTailMediumRGB(a, toPos / max(length(toPos), 1e-6), len, tau);
				color = exp(-tau);
			}
		}
#else
		color = vec3(0.3, 0.0, 0.3);
#endif
	}
	else if (view >= 60 && view <= 64)
	{
#if defined(USE_LIQUIDS)
		vec3 origin = u_FroxelViewOrigin.xyz;
		vec3 toScene = worldPos - origin;
		float sceneDistance = length(toScene);
		vec3 dir = toScene / max(sceneDistance, 1e-4);
		int n = LiquidCount();
		if (view == 60)
		{
			// drawn blended over the frame (RB_VolumetricDebugOverlay)
			vec4 outline = vec4(0.0);
			for (int i = 0; i < n; i++)
			{
				vec2 interval = LiquidClip(i, origin, dir, 0.0, 1e6);
				if (interval.x >= interval.y)
					continue;
				float visible = (interval.x < sceneDistance) ? 1.0 : 0.35;
				float width = 0.75 + 0.003 * interval.x;
				float edge = (LiquidNearPlanes(i, origin + dir * interval.x, width) >= 2) ? 1.0 : 0.0;
				float a = max(0.18, edge) * visible;
				if (a > outline.a)
					outline = vec4(LiquidClassHue(LiquidClassOf(i)), a);
			}
			out_Color = outline;
			return;
		}
		else if (view == 61)
		{
			int cpuClass = int(u_LiquidParams.y);
			int gpuClass = LiquidPointClass(origin);
			color = LiquidClassHue(cpuClass) * ((cpuClass >= 0) ? 0.6 : 1.0);
			if (tc.y < 0.06)
				color = LiquidClassHue(gpuClass);
			else if (cpuClass != gpuClass && fract((gl_FragCoord.x + gl_FragCoord.y) / 24.0) < 0.5)
				color = vec3(1.0, 0.0, 0.0);
		}
		else if (view == 62)
		{
			float sceneDepth = dot(toScene, u_FroxelViewForward.xyz);
			float cosView = max(dot(dir, u_FroxelViewForward.xyz), 1e-3);
			int numSlices = int(u_FroxelGridSize.z);
			float partial = 0.0;
			float full = 0.0;
			for (int k = 0; k < numSlices; k++)
			{
				float d0 = FroxelWToDepth(float(k) / float(numSlices));
				if (d0 > sceneDepth)
					break;
				int mask = u_LiquidSlices[k >> 2][k & 3];
				if (mask == 0)
					continue;
				float d1 = min(FroxelWToDepth(float(k + 1) / float(numSlices)), sceneDepth);
				vec3 covered = LiquidCoverage(origin, dir, d0 / cosView, d1 / cosView, mask);
				float f = (covered.x + covered.y + covered.z) * cosView / max(d1 - d0, 1e-4);
				if (f > 0.98)
					full += 1.0;
				else if (f > 0.02)
					partial += 1.0;
			}
			color = vec3(0.08) + vec3(0.0, 0.2, 0.6) * min(full / 8.0, 1.0) + vec3(1.0, 0.85, 0.0) * min(partial / 2.0, 1.0);
		}
		else if (view == 63)
		{
			vec3 covered = LiquidCoverage(origin, dir, 0.0, sceneDistance, -1);
#if defined(USE_FROXEL_RGB)
			color = exp(-LiquidOpticalDepth(covered));
#else
			float tau = covered.x * u_LiquidMaterial[0].a + covered.y * u_LiquidMaterial[2].a +
				covered.z * u_LiquidMaterial[4].a;
			color = vec3(exp(-tau));
#endif
		}
		else
		{
			// a little in front of the surface (a lake bed lies on its brush boundary)
			float pathLength;
			vec3 T = LiquidSunTransmittance(worldPos - dir * 0.5, u_FroxelSunDirection.xyz,
				max(dot(toScene, u_FroxelViewForward.xyz), 1.0) * u_LiquidView.z / 8.0, pathLength);
			color = (pathLength > 0.0) ? T * 0.75 : vec3(0.1);
		}
#else
		color = vec3(0.3, 0.0, 0.3);
#endif
	}
	else if (view == 10)
	{
		vec4 clip = u_FroxelViewProjection * vec4(worldPos, 1.0);
		vec2 uv = clamp(clip.xy / max(clip.w, 1e-3) * 0.5 + 0.5, 0.0, 1.0);
		float d = dot(worldPos - u_FroxelViewOrigin.xyz, u_FroxelViewForward.xyz);
		float slice = floor(FroxelDepthToW(min(d, u_FroxelSliceParams.y)) * u_FroxelGridSize.z);
		color = Heat(slice / u_FroxelGridSize.z);
		if (mod(slice, 2.0) > 0.5)
			color *= 0.7;
		vec2 cell = fract(uv * u_FroxelGridSize.xy);
		if (any(lessThan(cell, vec2(0.06))))
			color *= 0.5;
	}

	out_Color = vec4(color, 1.0);
}
