/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// SSR composite (tr_ssr.cpp, see ssr_common.glsl), drawn additively into color 0 of renderFbo after
// the opaque surfaces.
//
// lightall already added the cubemap reflection C = cubemap radiance * W. Where the SSR confidence is
// c, a part of it is replaced by the screen-space radiance under the same BRDF weight W:
//
//   color += c * (SSR radiance * W - C)
//
// (the SSR input is premultiplied by c). c = 1 shows the SSR instead of the cubemap reflection, c = 0
// leaves the pixel as lightall wrote it: the cubemap reflection is never doubled. Pixels with nothing to
// add are discarded, so most of the screen is not blended at all.
//
// Light sabers and additive effects are neither in the SSR scene (blended, no depth, drawn later) nor
// in the cubemaps, so their reflection is added: color += W * E, E = the reflection ray intersected
// with the capsule / sphere proxies of RB_SSRCollectEmitters, blurred by the roughness cone and hidden
// behind the geometry the SSR rays around the pixel hit first. That is the geometry of the hit, not the
// SSR confidence: a wall at the screen edge or under a grazing ray still hides what is behind it.
//
// FUSED_RESOLVE (r_ssrFusedResolve, no temporal accumulation): the resolve is done here
// (SSRResolvePixel) instead of being written to a full resolution image and read back.
//
// u_SSRTraceMap = final SSR (resolve or temporal accumulation); FUSED_RESOLVE: the trace, or the per-ray
// radiance with UPSAMPLE_RADIANCE. u_SSRHistoryMap = trace (hit points, for the emitter occlusion).
// u_SSRSettings: x = strength (confidence scale), y = split x in window pixels (r_ssrCompare, pixels
// left of it keep the cubemap reflection, -1 = off), z = debug view 7..11 (0 = off), w = pass:
// 0 = signed delta (float target), 1 = replaced cubemap part (subtracted), 2 = added part
// u_SSRSettings2: x = trace grid scale (1 or 2), y = max ray length, z = trace phase (0..3)
// u_SSRSettings3: FUSED_RESOLVE: x = coarsest color mip, y = coarsest Hi-Z level of the footprint depth
// test (0 = off)
// u_SSRTexelSize.xy = 1 / full resolution size
// u_SSREmitters: 3 vec4 per emitter, SSR view space: (a, radius), (b, 1 = sphere), (color, 0)
// u_SSREmitterParams: x = number of emitters, z = max roughness

#define SSR_MAX_EMITTERS 32 // tr_local.h

uniform vec4 u_SSREmitters[SSR_MAX_EMITTERS * 3];
uniform vec4 u_SSREmitterParams;

out vec4 out_Color;

// Where this pixel's ray O + R t passes the geometry another ray hit at Q: Q projected onto the ray. Negative
// when Q is further than reach from the ray: the other ray says nothing about this one. reach = (base, slope):
// base + slope * t (the two rays start a few pixels apart and diverge with their normals). Intersecting the
// plane of the hit surface instead (its normal from the material buffer) is more exact on slanted walls, but
// its fetches cost 1.7 ms on Intel UHD at 1600x900 in this shader (register pressure, even unexecuted).
float OccluderOnRay(vec4 hit, vec3 O, vec3 R, vec2 reach, bool check)
{
	vec3 d = SSRHitPosition(hit) - O;
	float t = dot(d, R);
	if (t <= 0.0)
		return -1.0;
	return (!check || length(d - R * t) <= reach.x + reach.y * t) ? t : -1.0;
}

// Distance along the ray O + R to the nearest geometry that the reflection rays around this pixel hit (1e30:
// none). Full resolution: the pixel's own ray. Half resolution: the four nearest rays of the same surface
// (depth and normal, as SSRResolvePixel weights them), each brought onto this ray (OccluderOnRay); the nearest
// is kept. When none of them describes this ray, the nearest ray is used without the reach test.
float EmitterOccluderDistance(ivec2 pix, float z, vec3 worldNormal, vec3 O, vec3 R, float coneTangent)
{
	ivec2 traceMax = textureSize(u_SSRHistoryMap, 0) - ivec2(1);
	ivec2 nearest = min(pix, traceMax);
	float gridScale = u_SSRSettings2.x;
	vec2 reach = vec2(2.0 * gridScale * z * u_SSRDepthParams.w + 2.0, max(coneTangent, 0.1));
	if (gridScale > 1.5)
	{
		vec2 phase = SSRPhaseOffset(u_SSRSettings2.z);
		ivec2 fullSize = textureSize(u_SSRHiZMap, 0);
		ivec2 base = ivec2(floor((vec2(pix) - phase) * 0.5));
		float hitDistance = 1.0e30;
		bool described = false;
		for (int i = 0; i < 4; i++)
		{
			ivec2 q = clamp(base + ivec2(i & 1, i >> 1), ivec2(0), traceMax);
			ivec2 src = SSRTracePixel(q, 2.0, phase, fullSize);
			float zs = texelFetch(u_SSRHiZMap, src, 0).r;
			vec4 ns = texelFetch(u_SSRNormalMap, src, 0);
			if (!SSRIsSurface(zs) || ns.a < 0.5 || abs(zs - z) > 0.02 * z + 1.0 ||
				dot(SSRDecodeNormal(ns.rg), worldNormal) < 0.9)
			{
				continue;
			}
			vec4 hit = texelFetch(u_SSRHistoryMap, q, 0);
			if (hit.w <= 0.0)
			{
				described = true; // a miss of the same surface
				continue;
			}
			float t = OccluderOnRay(hit, O, R, reach, true);
			if (t > 0.0)
			{
				described = true;
				hitDistance = min(hitDistance, t);
			}
		}
		if (described)
			return hitDistance;

		nearest = SSRNearestTraceTexel((vec2(pix) + 0.5) * u_SSRTexelSize.xy, 2.0, u_SSRSettings2.z, traceMax + ivec2(1));
	}

	vec4 hit = texelFetch(u_SSRHistoryMap, nearest, 0);
	if (hit.w <= 0.0)
		return 1.0e30;
	float t = OccluderOnRay(hit, O, R, reach, false);
	return t > 0.0 ? t : 1.0e30;
}

// Reflection of the emitters seen along the ray O + R t, t > 0
vec3 EmitterReflection(vec3 O, vec3 R, float roughness, float occluderDistance)
{
	float coneTangent = SSRConeTangent(roughness);
	int count = int(u_SSREmitterParams.x);
	vec3 result = vec3(0.0);

	for (int i = 0; i < SSR_MAX_EMITTERS; i++)
	{
		if (i >= count)
			break;

		vec4 a = u_SSREmitters[i * 3];
		vec4 b = u_SSREmitters[i * 3 + 1];
		vec3 color = u_SSREmitters[i * 3 + 2].rgb;
		float radius = a.w;

		// closest approach of the ray and the segment a b
		vec3 D = b.xyz - a.xyz;
		vec3 W0 = O - a.xyz;
		float dd = dot(D, D);
		float dr = dot(D, R);
		float u = 0.0;
		float denom = dd - dr * dr;
		if (denom > 1.0e-6)
			u = clamp((dot(D, W0) - dr * dot(R, W0)) / denom, 0.0, 1.0);
		float t = dot(a.xyz + D * u - O, R);
		if (dd > 1.0e-6)
			u = clamp(dot(O + R * max(t, 0.0) - a.xyz, D) / dd, 0.0, 1.0);
		t = dot(a.xyz + D * u - O, R);
		if (t <= 0.0)
			continue; // behind the reflecting surface

		float d = length(O + R * t - (a.xyz + D * u));

		// the roughness cone widens the emitter and spreads its energy
		float spread = t * coneTangent;
		float effectiveRadius = sqrt(radius * radius + spread * spread);
		float x = d / effectiveRadius;
		if (x >= 1.0)
			continue;

		float profile = 1.0 - x * x;
		profile *= profile;
		float energy = radius / effectiveRadius;
		if (b.w > 0.5)
			energy *= energy; // spread in two directions

		// behind the geometry the SSR ray hit first, partly where the emitter reaches through it
		float visibility = smoothstep(t - radius, t + radius + 1.0, occluderDistance);
		visibility *= 1.0 - smoothstep(u_SSRSettings2.y, 2.0 * u_SSRSettings2.y, t);

		result += color * (profile * energy * visibility);
	}

	return result;
}

void main()
{
	if (gl_FragCoord.x < u_SSRSettings.y)
		discard;

	ivec2 pix = ivec2(gl_FragCoord.xy);
	vec2 uv = (vec2(pix) + 0.5) * u_SSRTexelSize.xy;
	float z = texelFetch(u_SSRHiZMap, pix, 0).r;
	vec4 normalRoughness;
	bool receiver = SSRIsReceiver(pix, z, normalRoughness);

	// a non-receiver adds nothing (neither SSR nor emitters): leave before the material fetches. The scene
	// debug views 7-11 draw it black instead
	if (!receiver && u_SSRSettings.z == 0.0)
		discard;

#if defined(FUSED_RESOLVE)
	vec4 ssr = vec4(0.0);
	if (receiver && SSRInsideView(uv))
	{
		ssr = SSRResolvePixel(pix, uv, z, normalRoughness, u_SSRSettings2.x, u_SSRSettings2.z,
			u_SSRSettings3.x, u_SSRSettings3.y);
	}
#else
	vec4 ssr = texelFetch(u_SSRTraceMap, pix, 0);
#endif
	vec3 weight = SSRSpecularWeight(pix);
	vec3 cubemap = texelFetch(u_SSRCubemapMap, pix, 0).rgb;

	float strength = u_SSRSettings.x;
	vec3 ssrPart = ssr.rgb * weight * strength;
	vec3 cubemapPart = cubemap * (ssr.a * strength);

	// light sabers and effects, glossy surfaces only (the dynamic light of
	// a saber already gives rough surfaces their highlight)
	vec3 emitterPart = vec3(0.0);
	float maxRoughness = u_SSREmitterParams.z;
	if (receiver && u_SSREmitterParams.x > 0.5 && normalRoughness.b < maxRoughness)
	{
		vec3 P = SSRViewPosition(uv, z);
		vec3 N = SSRViewNormal(normalRoughness.rg);
		vec3 V = -normalize(P);
		if (dot(N, V) > 0.0)
		{
			vec3 R = reflect(-V, N);
			vec3 O = P + N * max(0.05, 0.002 * z);
			float occluderDistance = EmitterOccluderDistance(pix, z, SSRDecodeNormal(normalRoughness.rg), O, R,
				SSRConeTangent(normalRoughness.b));

			float fade = 1.0 - smoothstep(0.5, 1.0, normalRoughness.b / maxRoughness);
			emitterPart = EmitterReflection(O, R, normalRoughness.b, occluderDistance) *
				weight * (fade * strength);
		}
	}

	int debugView = int(u_SSRSettings.z);
	if (debugView != 0)
	{
		// the reflection terms alone, through the normal tone mapping
		vec3 result = vec3(0.0);
		if (receiver)
		{
			if (debugView == 7)      // raw SSR radiance
				result = ssr.a > 0.0 ? ssr.rgb / ssr.a : vec3(0.0);
			else if (debugView == 8) // cubemap reflection
				result = cubemap;
			else if (debugView == 9) // final hybrid reflection
				result = cubemap + ssrPart - cubemapPart + emitterPart;
			else if (debugView == 10) // replaced part
				result = abs(ssrPart - cubemapPart);
			else                     // light saber / effect reflections
				result = emitterPart;
		}
		out_Color = vec4(result, 1.0);
		return;
	}

	// nothing replaced or added: leave the target alone (no blend)
	if (max(ssr.a, max(ssr.r, max(ssr.g, ssr.b))) <= 0.0 &&
		max(emitterPart.r, max(emitterPart.g, emitterPart.b)) <= 0.0)
	{
		discard;
	}

	int pass = int(u_SSRSettings.w);
	if (pass == 1)
		out_Color = vec4(cubemapPart, 0.0);
	else if (pass == 2)
		out_Color = vec4(ssrPart + emitterPart, 0.0);
	else
		out_Color = vec4(ssrPart - cubemapPart + emitterPart, 0.0);
}
