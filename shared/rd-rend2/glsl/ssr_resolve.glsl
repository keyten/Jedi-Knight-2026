/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// SSR resolve (tr_ssr.cpp, see ssr_common.glsl): ray hits -> reflected radiance, full resolution.
//
// The radiance comes from the opaque HDR scene pyramid (u_SSRSceneMap), at the mip that matches the
// reflection cone of the roughness where it meets the hit surface. The cone uses the GGX alpha of the
// cubemap prefilter (roughness^2, prefilterEnvMap.glsl), so SSR and cubemap reflections have the same
// sharpness where they blend.
//
// The mips above 0 are premultiplied by their coverage (ssr_downsample.glsl): the first person view model
// and the texels outside the view rectangle are left out, so a rough reflection never picks up the
// weapon or a neighbouring view. A footprint made mostly of such texels falls back to the cubemap.
//
// Half resolution rays (u_SSRSettings.x = 2) are upsampled from the four nearest trace texels (each the
// ray of full resolution pixel 2q + phase), weighted by their bilinear position, depth and normal
// similarity: no reflection leaks across silhouettes.
//
// Output (RGBA16F): rgb = radiance * confidence (premultiplied), a = confidence.
//
// u_SSRSettings: x = trace grid scale (1 or 2), z = coarsest color mip
// u_SSRSettings2: y = trace phase (0..3)
// u_SSRTexelSize.xy = 1 / full resolution size

out vec4 out_Color;

void main()
{
	out_Color = vec4(0.0);

	ivec2 pix = ivec2(gl_FragCoord.xy);
	vec2 uv = (vec2(pix) + 0.5) * u_SSRTexelSize.xy;
	if (!SSRInsideView(uv))
		return;

	float z = texelFetch(u_SSRHiZMap, pix, 0).r;
	vec4 normalRoughness;
	if (!SSRIsReceiver(pix, z, normalRoughness))
		return;

	float coneTangent = SSRConeTangent(normalRoughness.b);
	vec3 P = SSRViewPosition(uv, z);

	if (u_SSRSettings.x < 1.5)
	{
		vec4 hit = texelFetch(u_SSRTraceMap, pix, 0);
		if (hit.w > 0.0)
			out_Color = SSRHitRadiance(hit, P, coneTangent, u_SSRSettings.z);
		return;
	}

	// half resolution: trace texel q holds the ray of full resolution pixel 2q + phase
	int phaseIndex = int(u_SSRSettings2.y);
	vec2 phase = vec2(float(phaseIndex & 1), float(phaseIndex >> 1));
	ivec2 fullSize = textureSize(u_SSRHiZMap, 0);
	ivec2 traceMax = textureSize(u_SSRTraceMap, 0) - ivec2(1);
	vec2 st = (vec2(pix) - phase) * 0.5;
	ivec2 base = ivec2(floor(st));
	vec2 f = st - vec2(base);
	vec3 N = SSRDecodeNormal(normalRoughness.rg);

	vec3 radiance = vec3(0.0);
	float confidenceSum = 0.0;
	float weightSum = 0.0;
	for (int i = 0; i < 4; i++)
	{
		ivec2 offset = ivec2(i & 1, i >> 1);
		ivec2 q = clamp(base + offset, ivec2(0), traceMax);
		ivec2 src = SSRTracePixel(q, 2.0, phase, fullSize);

		float w = (offset.x == 1 ? f.x : 1.0 - f.x) * (offset.y == 1 ? f.y : 1.0 - f.y);
		w = max(w, 0.01);

		float zs = texelFetch(u_SSRHiZMap, src, 0).r;
		vec4 ns = texelFetch(u_SSRNormalMap, src, 0);
		if (!SSRIsSurface(zs) || ns.a < 0.5)
			continue;
		w *= exp(-abs(zs - z) / (0.02 * z + 1.0));
		w *= pow(max(dot(SSRDecodeNormal(ns.rg), N), 0.0), 8.0);

		vec4 hit = texelFetch(u_SSRTraceMap, q, 0);
		if (hit.w > 0.0)
		{
			vec4 h = SSRHitRadiance(hit, P, coneTangent, u_SSRSettings.z);
			radiance += w * h.rgb;
			confidenceSum += w * h.a;
		}
		weightSum += w;
	}

	// no compatible ray: cubemap reflection only
	if (weightSum < 1.0e-4)
		return;

	out_Color = vec4(radiance, confidenceSum) / weightSum;
}
