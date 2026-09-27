/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// SSR temporal accumulation (tr_ssr.cpp, r_ssrTemporal, see ssr_common.glsl).
//
// The receiver point is reprojected into the previous frame: with the velocity buffer of the depth
// prepass when there is one (moving doors, NPCs), else with the previous camera (u_SSRReproject). The
// history is rejected when
//   - the reprojected point is off screen or the history is invalid (camera cut, teleport, map load,
//     FOV change: u_SSRSettings.x = 0),
//   - the previous surface there has another depth (disocclusion), normal or roughness (the depth
//     tolerance of moving surfaces grows with their screen motion only),
//   - the reflected point moved: the ray hit of this frame (u_SSRHitMap) and the one of the history
//     (u_SSRPrevHitMap, brought into this view) are further apart than the reflection cone or two trace
//     pixels, or one of them is a hit and the other a miss. The receiver alone does not tell: a mirror
//     under a moving camera keeps its depth and normal while it reflects something else,
// and it is clamped to the range of the current 3x3 neighborhood (YCoCg), so reflections of moving
// things do not smear. Large motion lowers the history weight.
//
// u_SSRTraceMap = resolve of this frame, u_SSRHistoryMap / u_SSRHistoryGeomMap = previous frame.
// Output 0: accumulated premultiplied radiance, confidence. Output 1: geometry for the next frame
// (view depth, octahedral world normal, roughness; roughness -1 = no receiver).
//
// u_SSRSettings: x = history valid, y = history weight, z = velocity buffer valid, w = trace grid scale

out vec4 out_Color;
out vec4 out_Glow;

vec3 RGBToYCoCg(vec3 c)
{
	return vec3(
		 0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
		 0.5  * c.r             - 0.5  * c.b,
		-0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

vec3 YCoCgToRGB(vec3 c)
{
	return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

void main()
{
	ivec2 pix = ivec2(gl_FragCoord.xy);
	vec2 uv = (vec2(pix) + 0.5) * u_SSRTexelSize.xy;

	float z = texelFetch(u_SSRHiZMap, pix, 0).r;
	vec4 normalRoughness = vec4(0.0);
	bool receiver = SSRInsideView(uv) && SSRIsReceiver(pix, z, normalRoughness);

	out_Glow = receiver ? vec4(z, normalRoughness.rg, normalRoughness.b) : vec4(0.0, 0.5, 0.5, -1.0);

	vec4 current = texelFetch(u_SSRTraceMap, pix, 0);
	out_Color = current;
	if (!receiver || u_SSRSettings.x < 0.5)
		return;

	// range of the current frame around the pixel
	ivec2 maxCoord = textureSize(u_SSRTraceMap, 0) - ivec2(1);
	vec4 lo = vec4(1.0e30);
	vec4 hi = vec4(-1.0e30);
	for (int y = -1; y <= 1; y++)
	{
		for (int x = -1; x <= 1; x++)
		{
			vec4 c = texelFetch(u_SSRTraceMap, clamp(pix + ivec2(x, y), ivec2(0), maxCoord), 0);
			vec4 c2 = vec4(RGBToYCoCg(c.rgb), c.a);
			lo = min(lo, c2);
			hi = max(hi, c2);
		}
	}

	// previous frame position and the depth it would have there if static
	vec3 P = SSRViewPosition(uv, z);
	vec4 prevClip = u_SSRReproject * vec4(P, 1.0);
	if (prevClip.w <= 0.0)
		return;
	vec2 prevUV = u_SSRViewport.xy + (prevClip.xy / prevClip.w * 0.5 + 0.5) * u_SSRViewport.zw;
	float tolerance = 0.03 * z + 2.0;

	if (u_SSRSettings.z > 0.5)
	{
		vec2 objectUV = uv - texture(u_VelocityMap, uv).rg;
		// a moving surface: the static depth prediction is off by about as much as it moved (its screen
		// motion in world units at its depth), never more than the old blanket tolerance
		float objectMotion = length((objectUV - prevUV) / u_SSRTexelSize.xy);
		if (objectMotion > 1.0)
			tolerance = min(tolerance + 2.0 * objectMotion * z * u_SSRDepthParams.w, 0.25 * z + 8.0);
		prevUV = objectUV;
	}

	if (!SSRInsideView(prevUV))
		return;

	vec4 prevGeom = texelFetch(u_SSRHistoryGeomMap, ivec2(prevUV / u_SSRTexelSize.xy), 0);
	if (prevGeom.w < 0.0)
		return;
	if (abs(prevGeom.x - prevClip.w) > tolerance)
		return;
	if (dot(SSRDecodeNormal(prevGeom.yz), SSRDecodeNormal(normalRoughness.rg)) < 0.9)
		return;
	if (abs(prevGeom.w - normalRoughness.b) > 0.1)
		return;

	// the reflected point: this frame's hit against the history's
	float gridScale = u_SSRSettings.w;
	ivec2 hitMax = textureSize(u_SSRHitMap, 0) - ivec2(1);
	vec4 hitNow = texelFetch(u_SSRHitMap, clamp(pix / int(gridScale), ivec2(0), hitMax), 0);
	vec4 hitPrev = texelFetch(u_SSRPrevHitMap, clamp(ivec2(prevUV / (u_SSRTexelSize.xy * gridScale)), ivec2(0), hitMax), 0);
	float hitAgreement = 1.0;
	if ((hitNow.w > 0.0) != (hitPrev.w > 0.0))
	{
		// a hit appeared or vanished (faint ones blend, the clamp handles them)
		hitAgreement = 1.0 - smoothstep(0.1, 0.4, max(hitNow.w, hitPrev.w));
	}
	else if (hitNow.w > 0.0)
	{
		vec3 Qnow = SSRHitPosition(hitNow);
		vec3 Qprev = (u_SSRPrevViewToView * vec4(SSRHitPosition(hitPrev), 1.0)).xyz;
		float apart = Qprev.z > 1.0 ? length((SSRProjectToUV(Qprev) - hitNow.xy) / u_SSRTexelSize.xy) : 1.0e6;
		float footprint = length(Qnow - P) * SSRConeTangent(normalRoughness.b) / (max(Qnow.z, 1.0) * u_SSRDepthParams.w);
		float allowed = max(2.0 * gridScale, footprint);
		hitAgreement = 1.0 - smoothstep(allowed, 2.0 * allowed, apart);
	}
	if (hitAgreement <= 0.0)
		return;

	vec4 history = textureLod(u_SSRHistoryMap, prevUV, 0.0);
	vec4 history2 = clamp(vec4(RGBToYCoCg(history.rgb), history.a), lo, hi);
	history = vec4(YCoCgToRGB(history2.rgb), history2.a);

	float motion = length((uv - prevUV) / u_SSRTexelSize.xy);
	float weight = u_SSRSettings.y * (1.0 - 0.5 * smoothstep(2.0, 24.0, motion)) * hitAgreement;
	out_Color = mix(current, history, weight);
}
