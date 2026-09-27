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
// leaves the pixel as lightall wrote it: the cubemap reflection is never doubled.
//
// Light sabers and additive effects are neither in the SSR scene (blended, no depth, drawn later) nor
// in the cubemaps, so their reflection is added: color += W * E, E = the reflection ray intersected
// with the capsule / sphere proxies of RB_SSRCollectEmitters, blurred by the roughness cone and hidden
// behind the surface the SSR ray hit first.
//
// u_SSRTraceMap = final SSR (resolve or temporal accumulation), u_SSRHistoryMap = trace (hit point and
// confidence, for the emitter occlusion).
// u_SSRSettings: x = strength (confidence scale), y = split x in window pixels (r_ssrCompare, pixels
// left of it keep the cubemap reflection, -1 = off), z = debug view 7..11 (0 = off), w = pass:
// 0 = signed delta (float target), 1 = replaced cubemap part (subtracted), 2 = added part
// u_SSRSettings2: x = trace grid scale (1 or 2), y = max ray length
// u_SSREmitters: 3 vec4 per emitter, SSR view space: (a, radius), (b, 1 = sphere), (color, 0)
// u_SSREmitterParams: x = number of emitters, z = max roughness

#define SSR_MAX_EMITTERS 32 // tr_local.h

uniform vec4 u_SSREmitters[SSR_MAX_EMITTERS * 3];
uniform vec4 u_SSREmitterParams;

out vec4 out_Color;

// Reflection of the emitters seen along the ray O + R t, t > 0
vec3 EmitterReflection(vec3 O, vec3 R, float roughness, float occluderDistance, float occluderConfidence)
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

		// behind the surface the SSR ray hit first
		float visibility = t > occluderDistance ? 1.0 - occluderConfidence : 1.0;
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
	vec4 ssr = texelFetch(u_SSRTraceMap, pix, 0);
	vec3 weight = SSRSpecularWeight(pix);
	vec3 cubemap = texelFetch(u_SSRCubemapMap, pix, 0).rgb;

	float strength = u_SSRSettings.x;
	vec3 ssrPart = ssr.rgb * weight * strength;
	vec3 cubemapPart = cubemap * (ssr.a * strength);

	float z = texelFetch(u_SSRHiZMap, pix, 0).r;
	vec4 normalRoughness;
	bool receiver = SSRIsReceiver(pix, z, normalRoughness);

	// light sabers and effects, glossy surfaces only (the dynamic light of
	// a saber already gives rough surfaces their highlight)
	vec3 emitterPart = vec3(0.0);
	float maxRoughness = u_SSREmitterParams.z;
	if (receiver && u_SSREmitterParams.x > 0.5 && normalRoughness.b < maxRoughness)
	{
		vec2 uv = (vec2(pix) + 0.5) / vec2(textureSize(u_SSRHiZMap, 0));
		vec3 P = SSRViewPosition(uv, z);
		vec3 N = SSRViewNormal(normalRoughness.rg);
		vec3 V = -normalize(P);
		if (dot(N, V) > 0.0)
		{
			vec3 R = reflect(-V, N);
			vec3 O = P + N * max(0.05, 0.002 * z);

			vec4 hit = texelFetch(u_SSRHistoryMap, min(pix / int(u_SSRSettings2.x), textureSize(u_SSRHistoryMap, 0) - ivec2(1)), 0);
			float occluderDistance = hit.w > 0.0 ? length(SSRHitPosition(hit) - O) : 1.0e30;

			float fade = 1.0 - smoothstep(0.5, 1.0, normalRoughness.b / maxRoughness);
			emitterPart = EmitterReflection(O, R, normalRoughness.b, occluderDistance, hit.w) *
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

	int pass = int(u_SSRSettings.w);
	if (pass == 1)
		out_Color = vec4(cubemapPart, 0.0);
	else if (pass == 2)
		out_Color = vec4(ssrPart + emitterPart, 0.0);
	else
		out_Color = vec4(ssrPart - cubemapPart + emitterPart, 0.0);
}
