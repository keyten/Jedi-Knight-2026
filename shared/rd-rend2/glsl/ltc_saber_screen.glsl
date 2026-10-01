/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0,
		4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Stochastic saber visibility over the pre-light Hi-Z pyramid. The temporal
// pass reprojects the two saber channels, and the final pass filters by depth.
uniform vec4 u_LtcScreenParams; // frame, 1/full width, 1/full height, history/velocity
uniform vec4 u_LtcSaber0;       // center.xyz, half length
uniform vec4 u_LtcSaberAxis0;   // unit axis.xyz, temporal weight
uniform vec4 u_LtcSaber1;
uniform vec4 u_LtcSaberAxis1;

out vec4 out_Color;
#if defined(LTC_SABER_TEMPORAL)
out vec4 out_Glow;
#endif

#if defined(LTC_SABER_TRACE)
vec2 SaberTrace(vec3 receiver, vec4 saber, vec4 axis, ivec2 pixel, float z)
{
	if (saber.w <= 0.0)
		return vec2(1.0, 0.0);
	float random = SSRInterleavedGradientNoise(vec2(pixel) +
		vec2(u_LtcScreenParams.x * 0.7549, u_LtcScreenParams.x * 0.5698));
	vec3 worldSample = saber.xyz + axis.xyz * ((2.0 * random - 1.0) * saber.w);
	vec3 endpoint = (u_SSRWorldToView * vec4(worldSample, 1.0)).xyz;
	if (endpoint.z <= 1.0 || receiver.z <= 0.0)
		return vec2(1.0, 0.0);
	vec3 delta = endpoint - receiver;
	float lengthRay = length(delta);
	if (lengthRay < 2.0)
		return vec2(1.0, 0.0);
	vec3 origin = receiver + delta * (max(1.0, 0.002 * z) / lengthRay);
	float sMin, sMax, screenLength;
	if (!SSRSetupRay(origin, endpoint, sMin, sMax, screenLength))
		return vec2(1.0, 0.0);
	float hitPosition;
	bool hit = SSRMarchRay(sMin, sMax, screenLength, random,
		32.0, 1.0, 1.0, 4.0, 5.0, 96.0, hitPosition);
	if (sMax < 0.995)
		return vec2(1.0, 0.0);
	vec2 uv = (vec2(pixel) + 0.5) * u_LtcScreenParams.yz;
	vec2 edge = min((uv - u_SSRViewport.xy) / u_SSRViewport.zw,
		(u_SSRViewport.xy + u_SSRViewport.zw - uv) / u_SSRViewport.zw);
	float confidence = clamp(min(edge.x, edge.y) * 20.0, 0.0, 1.0);
	return vec2(hit && hitPosition < 0.99 ? 0.0 : 1.0, confidence);
}

void main()
{
	ivec2 fullSize = textureSize(u_SSRHiZMap, 0);
	ivec2 phase = ivec2(int(u_LtcScreenParams.x) & 1,
		(int(u_LtcScreenParams.x) >> 1) & 1);
	ivec2 pixel = min(ivec2(gl_FragCoord.xy) * 2 + phase, fullSize - ivec2(1));
	vec2 uv = (vec2(pixel) + 0.5) * u_LtcScreenParams.yz;
	float z = texelFetch(u_SSRHiZMap, pixel, 0).r;
	if (!SSRInsideView(uv) || !SSRIsSurface(z))
	{
		out_Color = vec4(1.0, 0.0, 1.0, 0.0);
		return;
	}
	vec3 receiver = SSRViewPosition(uv, z);
	vec2 a = SaberTrace(receiver, u_LtcSaber0, u_LtcSaberAxis0, pixel, z);
	vec2 b = SaberTrace(receiver, u_LtcSaber1, u_LtcSaberAxis1, pixel, z);
	out_Color = vec4(a, b);
}
#elif defined(LTC_SABER_TEMPORAL)
void main()
{
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	vec2 uv = (vec2(pixel) + 0.5) * u_LtcScreenParams.yz;
	float z = texelFetch(u_SSRHiZMap, pixel, 0).r;
	out_Glow = vec4(SSRIsSurface(z) ? z : 0.0, 0.0, 0.0, 0.0);
	vec4 current = texture(u_SSRTraceMap, uv);
	out_Color = current;
	if (!SSRIsSurface(z) || u_LtcScreenParams.w < 0.5)
		return;
	vec3 receiver = SSRViewPosition(uv, z);
	vec4 prevClip = u_SSRReproject * vec4(receiver, 1.0);
	if (prevClip.w <= 0.0)
		return;
	vec2 prevUV = u_SSRViewport.xy +
		(prevClip.xy / prevClip.w * 0.5 + 0.5) * u_SSRViewport.zw;
	if (u_LtcScreenParams.w > 1.5)
		prevUV = uv - texture(u_VelocityMap, uv).rg;
	if (!SSRInsideView(prevUV))
		return;
	float previousDepth = texture(u_SSRHistoryGeomMap, prevUV).r;
	if (abs(previousDepth - prevClip.w) > 0.03 * prevClip.w + 2.0)
		return;
	vec4 previous = texture(u_SSRHistoryMap, prevUV);
	if (current.y > 0.0 && previous.y > 0.0)
	{
		float w = clamp(u_LtcSaberAxis0.w, 0.0, 0.9);
		out_Color.x = mix(current.x, previous.x, w);
		out_Color.y = max(current.y, previous.y * w);
	}
	if (current.w > 0.0 && previous.w > 0.0)
	{
		float w = clamp(u_LtcSaberAxis1.w, 0.0, 0.9);
		out_Color.z = mix(current.z, previous.z, w);
		out_Color.w = max(current.w, previous.w * w);
	}
}
#elif defined(LTC_SABER_FILTER)
void main()
{
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	ivec2 maxPixel = textureSize(u_SSRTraceMap, 0) - ivec2(1);
	float centerDepth = texelFetch(u_SSRHiZMap, pixel, 0).r;
	vec4 center = texelFetch(u_SSRTraceMap, pixel, 0);
	if (!SSRIsSurface(centerDepth))
	{
		out_Color = vec4(1.0, 0.0, 1.0, 0.0);
		return;
	}
	vec2 visible = vec2(0.0);
	vec2 weight = vec2(0.0);
	for (int y = -1; y <= 1; ++y)
	for (int x = -1; x <= 1; ++x)
	{
		ivec2 q = clamp(pixel + ivec2(x, y), ivec2(0), maxPixel);
		float depth = texelFetch(u_SSRHiZMap, q, 0).r;
		if (!SSRIsSurface(depth) ||
			abs(depth - centerDepth) > 0.02 * centerDepth + 1.0)
			continue;
		vec4 sampleValue = texelFetch(u_SSRTraceMap, q, 0);
		float spatial = 1.0 / (1.0 + float(x * x + y * y));
		vec2 w = sampleValue.yw * spatial;
		visible += sampleValue.xz * w;
		weight += w;
	}
	out_Color = vec4(weight.x > 0.0 ? visible.x / weight.x : center.x,
		max(center.y, min(weight.x / 4.0, 1.0)),
		weight.y > 0.0 ? visible.y / weight.y : center.z,
		max(center.w, min(weight.y / 4.0, 1.0)));
}
#endif
