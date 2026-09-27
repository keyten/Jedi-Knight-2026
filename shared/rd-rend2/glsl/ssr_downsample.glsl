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
// FIRST_LEVEL builds mip 1 from the plain scene copy, 16 texel taps masked with the linear depth. The
// other levels are four bilinear taps on the texel corners (premultiplied: a linear filter).

out vec4 out_Color;

void main()
{
#if defined(FIRST_LEVEL)
	ivec2 srcMax = textureSize(u_SSRSceneMap, 0) - ivec2(1);
	ivec2 src = ivec2(gl_FragCoord.xy) * 2 - ivec2(1);

	vec4 sum = vec4(0.0);
	for (int y = 0; y < 4; y++)
	{
		for (int x = 0; x < 4; x++)
		{
			ivec2 p = src + ivec2(x, y);
			vec2 uv = (vec2(p) + 0.5) * u_SSRTexelSize.xy;
			if (any(lessThan(p, ivec2(0))) || any(greaterThan(p, srcMax)) || !SSRInsideView(uv))
				continue;
			if (texelFetch(u_SSRHiZMap, p, 0).r == SSR_DEPTH_VIEWMODEL)
				continue;
			sum += vec4(texelFetch(u_SSRSceneMap, p, 0).rgb, 1.0);
		}
	}
	out_Color = sum * (1.0 / 16.0);
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
