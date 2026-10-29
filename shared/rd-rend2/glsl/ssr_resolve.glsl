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
// All mips are premultiplied by their coverage (ssr_downsample.glsl): the first person view model
// and the texels outside the view rectangle are left out, so a reflection never picks up the
// weapon or a neighbouring view. A footprint made mostly of such texels falls back to the cubemap.
//
// Full resolution rays: one hit per pixel. Half resolution rays (u_SSRSettings.x = 2) are upsampled
// from the four nearest trace texels, see SSRResolvePixel. The composite does the same work itself
// when nothing needs this image (no temporal accumulation, ssr_composite.glsl FUSED_RESOLVE).
//
// TRACE_RADIANCE (r_ssrResolveCache, half resolution; with SSR_STOCHASTIC also full resolution): the
// radiance of every ray once, at trace resolution, with the roughness of its own receiver (SSR_STOCHASTIC:
// and its hit point and pdf, output 1). UPSAMPLE_RADIANCE then only filters it.
//
// Output (RGBA16F): rgb = radiance * confidence (premultiplied), a = confidence.
//
// u_SSRSettings: x = trace grid scale (1 or 2), z = coarsest color mip, w = coarsest Hi-Z level of
// the footprint depth test (0 = off)
// u_SSRSettings2: y = trace phase (0..3), z = frame (0..63, SSR_STOCHASTIC: the reused 2x2 block of full
// resolution rays), w = 1: the froxel volume is bound (SSR_REFLECTED_FOG, ssr_common.glsl)
//
// SSR_STOCHASTIC (r_ssrStochastic): the rays are GGX samples, see SSRResolveStochastic. u_SSRHitMap = trace.
// u_SSRTexelSize.xy = 1 / full resolution size

out vec4 out_Color;
#if defined(TRACE_RADIANCE) && defined(SSR_STOCHASTIC)
out vec4 out_Glow; // hit point and log2(pdf) of the ray (u_SSRRayMap)
#endif

void main()
{
	out_Color = vec4(0.0);
#if defined(TRACE_RADIANCE) && defined(SSR_STOCHASTIC)
	out_Glow = vec4(0.0); // no ray
#endif

	ivec2 pix = ivec2(gl_FragCoord.xy);
#if defined(TRACE_RADIANCE)
	vec4 hit = SSRTraceHit(texelFetch(u_SSRTraceMap, pix, 0));
	if (hit.w <= 0.0)
		return;
	pix = SSRTracePixel(pix, u_SSRSettings.x, SSRPhaseOffset(u_SSRSettings2.y), textureSize(u_SSRHiZMap, 0));
#endif
	vec2 uv = (vec2(pix) + 0.5) * u_SSRTexelSize.xy;
	if (!SSRInsideView(uv))
		return;

	float z = texelFetch(u_SSRHiZMap, pix, 0).r;
	vec4 normalRoughness;
	if (!SSRIsReceiver(pix, z, normalRoughness))
		return;

#if defined(TRACE_RADIANCE)
	vec3 P = SSRViewPosition(uv, z);
  #if defined(SSR_STOCHASTIC)
	// the pyramid filters the solid angle of the sample, not the whole lobe
	vec3 N = SSRViewNormal(normalRoughness.rg);
	vec3 Q = SSRHitPosition(hit);
	float pdf = SSRGGXPdf(N, -normalize(P), normalize(Q - P), SSRGGXAlpha(normalRoughness.b));
	float coneTangent = SSRStochasticConeTangent(pdf, normalRoughness.b);
	out_Glow = vec4(Q, log2(max(pdf, 1.0e-20)));
  #else
	float coneTangent = SSRConeTangent(normalRoughness.b);
  #endif
	out_Color = SSRHitRadiance(hit, P, coneTangent, u_SSRSettings.z, u_SSRSettings.w);
#elif defined(SSR_STOCHASTIC)
	out_Color = SSRResolveStochastic(pix, uv, z, normalRoughness, u_SSRSettings.x, u_SSRSettings2.y,
		u_SSRSettings2.z, u_SSRSettings.z, u_SSRSettings.w);
#else
	out_Color = SSRResolvePixel(pix, uv, z, normalRoughness, u_SSRSettings.x, u_SSRSettings2.y,
		u_SSRSettings.z, u_SSRSettings.w);
#endif
}
