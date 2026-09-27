/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// r_ssrDebug 1-6 (tr_ssr.cpp, see ssr_common.glsl), drawn over the final image. Pixels that do not
// receive SSR (not an opaque PBR surface) are black, except in the hit/miss view (dark gray).
//
//   1 material normal (world, * 0.5 + 0.5)    4 ray hit (green, brighter = more confident) / miss (red)
//   2 roughness                               5 hit distance (blue near .. red at r_ssrMaxDistance)
//   3 specular reflectance W                  6 final confidence
//  12 hit cache: green = hit reused from the previous frame, red = hit traced this frame, dark = miss
//
// u_SSRTraceMap = trace, u_SSRHistoryMap = final SSR (premultiplied radiance, confidence)
// u_SSRSettings: x = debug view, y = trace grid scale (1 or 2), z = max ray length

out vec4 out_Color;

void main()
{
	ivec2 pix = ivec2(gl_FragCoord.xy);
	int view = int(u_SSRSettings.x);

	float z = texelFetch(u_SSRHiZMap, pix, 0).r;
	vec4 normalRoughness;
	vec3 color = vec3(0.0);

	if (SSRIsReceiver(pix, z, normalRoughness))
	{
		if (view == 1)
		{
			color = SSRDecodeNormal(normalRoughness.rg) * 0.5 + 0.5;
		}
		else if (view == 2)
		{
			color = vec3(normalRoughness.b);
		}
		else if (view == 3)
		{
			color = SSRSpecularWeight(pix);
		}
		else if (view == 6)
		{
			color = vec3(texelFetch(u_SSRHistoryMap, pix, 0).a);
		}
		else
		{
			vec4 hit = texelFetch(u_SSRTraceMap, min(pix / int(u_SSRSettings.y), textureSize(u_SSRTraceMap, 0) - ivec2(1)), 0);
			if (view == 4)
				color = hit.w > 0.0 ? vec3(0.1, 0.3 + 0.7 * hit.w, 0.1) : vec3(0.6, 0.05, 0.05);
			else if (view == 12)
				color = hit.w > 0.0 ? (SSRHitReused(hit.w) ? vec3(0.1, 0.8, 0.1) : vec3(0.8, 0.1, 0.1)) : vec3(0.08);
			else if (hit.w > 0.0)
			{
				vec2 uv = (vec2(pix) + 0.5) / vec2(textureSize(u_SSRHiZMap, 0));
				float d = clamp(length(SSRHitPosition(hit) - SSRViewPosition(uv, z)) / u_SSRSettings.z, 0.0, 1.0);
				color = vec3(d, 1.0 - abs(2.0 * d - 1.0), 1.0 - d);
			}
		}
	}
	else if (view == 4 || view == 12)
	{
		color = vec3(0.04);
	}

	out_Color = vec4(color, 1.0);
}
