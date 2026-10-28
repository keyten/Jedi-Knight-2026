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
// TRACE_RADIANCE (r_ssrResolveCache, half resolution): the radiance of every ray once, at trace
// resolution, with the roughness of its own receiver. UPSAMPLE_RADIANCE then only filters it.
//
// Output (RGBA16F): rgb = radiance * confidence (premultiplied), a = confidence.
//
// u_SSRSettings: x = trace grid scale (1 or 2), z = coarsest color mip, w = coarsest Hi-Z level of
// the footprint depth test (0 = off)
// u_SSRSettings2: y = trace phase (0..3)
// u_SSRTexelSize.xy = 1 / full resolution size

out vec4 out_Color;

void main()
{
	out_Color = vec4(0.0);

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
	out_Color = SSRHitRadiance(hit, SSRViewPosition(uv, z), SSRConeTangent(normalRoughness.b),
		u_SSRSettings.z, u_SSRSettings.w);
#else
	out_Color = SSRResolvePixel(pix, uv, z, normalRoughness, u_SSRSettings.x, u_SSRSettings2.y,
		u_SSRSettings.z, u_SSRSettings.w);
#endif
}
