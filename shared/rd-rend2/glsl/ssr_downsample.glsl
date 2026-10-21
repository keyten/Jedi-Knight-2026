/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// SSR scene color pyramid (tr_ssr.cpp): the next mip of the opaque HDR scene, sampled by rough
// reflections, a 4x4 box around the 2x2 source block: the overlapping footprints keep the cone blur of
// rough surfaces from showing blocks. u_SSRSceneMap has BASE_LEVEL = MAX_LEVEL = the source level.
// u_SSRTexelSize.xy = 1 / source size, zw = 1 / destination size.
//
// The mips are premultiplied by their coverage (alpha): texels of the first person view model (its
// color is in the scene, but no ray hits it) and texels outside the view rectangle do not count.
// MASK_LEVEL builds masked mip 0 from the resolved scene, one color/depth fetch
// per pixel. Every downsample is then four bilinear taps on premultiplied data.

out vec4 out_Color;

void main()
{
#if defined(MASK_LEVEL)
	ivec2 p = ivec2(gl_FragCoord.xy);
	vec2 uv = (vec2(p) + 0.5) * u_SSRTexelSize.xy;
	out_Color = vec4(0.0);
	if (SSRInsideView(uv) && texelFetch(u_SSRHiZMap, p, 0).r != SSR_DEPTH_VIEWMODEL)
		out_Color = vec4(texelFetch(u_SSRSceneMap, p, 0).rgb, 1.0);
#else
	vec2 uv = gl_FragCoord.xy * u_SSRTexelSize.zw;
	vec2 offset = u_SSRTexelSize.xy;

	vec4 color  = textureLod(u_SSRSceneMap, uv + vec2(-offset.x, -offset.y), 0.0);
	color      += textureLod(u_SSRSceneMap, uv + vec2( offset.x, -offset.y), 0.0);
	color      += textureLod(u_SSRSceneMap, uv + vec2(-offset.x,  offset.y), 0.0);
	color      += textureLod(u_SSRSceneMap, uv + vec2( offset.x,  offset.y), 0.0);
	out_Color = color * 0.25;
#endif
}
