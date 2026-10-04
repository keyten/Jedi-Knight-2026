/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Cloud temporal resolve (r_clouds, tr_clouds.cpp): the cloud history of its own (not the froxel or SMAA
// history), at the march resolution. A pixel's cloud point (transmittance weighted distance of the march) moved
// with the wind; it is reprojected into the previous frame, the history there (bilinear over pixels that saw
// sky) is clamped to the 3x3 neighbourhood of this frame's march and blended with u_Cloud[16].y (0 = no history:
// camera cut, map change, parameter change, first frame).
//
// out_Color: resolved scattering (rgb) + transmittance (a); debug 7 = the history weight used
// out_Glow:  r = resolved cloud distance (km, CLOUD_NO_SKY where the pixel has no sky)

uniform sampler2D u_CloudCurrentMap;
uniform sampler2D u_CloudCurrentDepthMap;
uniform sampler2D u_CloudHistoryMap;
uniform sampler2D u_CloudHistoryDepthMap;
uniform mat4 u_CloudInvViewProjection;	// clip -> world offset from the camera (no translation)
uniform mat4 u_CloudPrevViewProjection;

// the neighbourhood box is widened by this share of its size on both sides
#ifndef CLOUD_CLAMP_WIDEN
#define CLOUD_CLAMP_WIDEN 0.5
#endif

out vec4 out_Color;
out vec4 out_Glow;

void main()
{
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	vec4 current = texelFetch(u_CloudCurrentMap, pixel, 0);
	float currentDistance = texelFetch(u_CloudCurrentDepthMap, pixel, 0).r;
	int debugView = int(u_Cloud[16].x);

	if (currentDistance < 0.0)
	{
		out_Color = debugView > 0 ? vec4(0.0) : vec4(0.0, 0.0, 0.0, 1.0);
		out_Glow = vec4(CLOUD_NO_SKY, 0.0, 0.0, 0.0);
		return;
	}

	float weight = u_Cloud[16].y;
	vec4 history = current;
	float historyDistance = currentDistance;
	if (weight > 0.0)
	{
		// the cloud point of this pixel, where it was one frame ago (wind), in the previous view
		vec2 tc = CloudLowToTc(gl_FragCoord.xy);
		vec2 ndc = (tc - u_Cloud[14].xy) / u_Cloud[14].zw * 2.0 - 1.0;
		vec4 nearPoint = u_CloudInvViewProjection * vec4(ndc, -1.0, 1.0);
		vec4 farPoint = u_CloudInvViewProjection * vec4(ndc, 1.0, 1.0);
		vec3 dir = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);
		float worldDistance = currentDistance / max(u_Cloud[11].z, 1e-12);
		vec3 point = u_Cloud[19].xyz + dir * worldDistance;
		point.xy -= u_Cloud[18].zw * u_Cloud[19].w;
		vec4 clip = u_CloudPrevViewProjection * vec4(point, 1.0);

		bool valid = clip.w > 1e-6;
		vec2 uv = valid ? clip.xy / clip.w * 0.5 + 0.5 : vec2(-1.0);
		valid = valid && all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));
		if (valid)
			valid = CloudFetchBilinear(u_CloudHistoryMap, u_CloudHistoryDepthMap,
				u_Cloud[15].xy + uv * u_Cloud[15].zw, history, historyDistance);

		if (valid)
		{
			// neighbourhood of this frame's march (pixels with sky)
			vec4 lo = current;
			vec4 hi = current;
			ivec2 lowMin = ivec2(u_Cloud[15].xy);
			ivec2 lowMax = lowMin + ivec2(u_Cloud[15].zw) - 1;
			for (int k = 0; k < 9; k++)
			{
				ivec2 c = clamp(pixel + ivec2(k % 3, k / 3) - 1, lowMin, lowMax);
				if (texelFetch(u_CloudCurrentDepthMap, c, 0).r < 0.0)
					continue;
				vec4 v = texelFetch(u_CloudCurrentMap, c, 0);
				lo = min(lo, v);
				hi = max(hi, v);
			}
			// widened box: the march noise is high frequency, a tight box would keep most of it
			vec4 widen = (hi - lo) * CLOUD_CLAMP_WIDEN;
			history = clamp(history, lo - widen, hi + widen);
		}
		else
			weight = 0.0;
	}

	if (debugView == 7)
	{
		out_Color = vec4(CloudHeat(weight), 0.0);
		out_Glow = vec4(currentDistance, 0.0, 0.0, 0.0);
		return;
	}

	out_Color = mix(current, history, weight);
	out_Glow = vec4(mix(currentDistance, historyDistance, weight), 0.0, 0.0, 0.0);
}
