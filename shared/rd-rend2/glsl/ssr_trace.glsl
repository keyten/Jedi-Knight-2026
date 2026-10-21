/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
#if defined(CLASSIFY)
	// nearest depth: marks the pixels that need a ray
	gl_Position = vec4(position, -1.0, 1.0);
#else
	// in front of the cleared depth, behind the marked pixels: with a depth test the pixels without a ray
	// are rejected before the fragment shader runs (the trace neither discards nor writes depth)
	gl_Position = vec4(position, 0.0, 1.0);
#endif
}

/*[Fragment]*/
// SSR ray march (tr_ssr.cpp, see ssr_common.glsl).
//
// For every receiver pixel (every pixel, or one of each 2x2 block at half resolution, the block phase
// rotating over the frames with the temporal accumulation) the mirror reflection ray is marched through
// the linear depth buffer: in view space, projected to the screen with perspective correct depth, the steps
// evenly spread over the pixels the ray covers (SSRMarchRay, shared with SSGI; USE_HIZ walks the closest
// depth mips). The first crossing of a depth buffer surface (within its assumed thickness) is refined with
// a binary search.
//
// Hit cache: the hit of the previous frame (u_SSRPrevHitMap, at the reprojected receiver) is reused
// without a march when it is still the reflection of this frame's ray: on the ray within the roughness
// cone, still the visible surface there, not moved (velocity buffer), nothing new in between. Valid cached
// trace cells are refreshed every 4 frames (every phase within 16 frames with rotating half resolution).
//
// CLASSIFY: the early depth pass of the trace (r_ssrReceiverCull). Writes the nearest depth where a pixel needs a
// ray (receiver, glossy enough, visible specular weight), discards the others.
//
// Output (RGBA16): see ssr_common.glsl. The confidence fades the SSR towards the cubemap reflection where
// the hit is doubtful: near the screen edges, the end of the ray, back facing or ambiguous (thick) hits,
// rays towards the camera, grazing rays and rough surfaces.
//
// u_SSRSettings:  x = max steps, y = refine steps, z = max ray length, w = thickness
// u_SSRSettings2: x = max roughness, y = edge fade, z = pixel scale of the trace grid (1 or 2), w = frame
// u_SSRSettings3: x = coarsest Hi-Z level, y = min specular weight, z = near plane, w = Hi-Z iterations
// u_SSRSettings4: x = hit cache valid, y = velocity buffer valid, z = refresh slot (0..3), w = phase (0..3)
// u_SSRSettings5: x = previous trace phase
// u_SSRTexelSize.xy = 1 / full resolution size

out vec4 out_Color;

// receiver tests shared by the classification and the trace
bool NeedsRay(ivec2 pix, out float z, out vec4 normalRoughness)
{
	z = 0.0;
	normalRoughness = vec4(0.0);

	vec2 uv = (vec2(pix) + 0.5) * u_SSRTexelSize.xy;
	if (!SSRInsideView(uv))
		return false;

	z = texelFetch(u_SSRHiZMap, pix, 0).r;
	if (!SSRIsReceiver(pix, z, normalRoughness))
		return false;

	if (normalRoughness.b >= u_SSRSettings2.x)
		return false;

	vec3 weight = SSRSpecularWeight(pix);
	return max(weight.r, max(weight.g, weight.b)) >= u_SSRSettings3.y;
}

#if !defined(CLASSIFY)
// Conservative visibility of O -> cached Q. Unlike a new-hit test, ANY front
// surface behind which the segment passes invalidates the cache, regardless of
// thickness. A small Hi-Z budget bounds the cost; uncertainty causes a fresh
// trace, never acceptance of an untested tail. Linear mode only reads mip 0.
bool CachedPathClear(vec3 O, vec3 Q)
{
	if (!SSRInsideView(SSRProjectToUV(O)))
		return false;
	float sMin, sMax, screenLength;
	if (!SSRSetupRay(O, Q, sMin, sMax, screenLength))
		return false;
	// Exclude the receiver and the hit's own pixel footprint.
	float end = min(sMax, 1.0 - 1.5 / screenLength);
	float s = max(sMin, 1.5 / screenLength);
	int maxLevel = int(u_SSRSettings3.x);
	int level = min(maxLevel, max(0, int(ceil(log2(max(screenLength, 1.0)))) - 2));
	float eps = 0.02 / screenLength;
	vec2 dirStep = vec2(g_D.x >= 0.0 ? 1.0 : 0.0, g_D.y >= 0.0 ? 1.0 : 0.0);
	for (int i = 0; i < 24; i++)
	{
		if (s >= end)
			return true;
		float cellSize = exp2(float(level));
		vec2 cell = floor(SSRRayPixel(s) / cellSize);
		vec2 boundary = (cell + dirStep) * cellSize;
		float sx = g_D.x != 0.0 ? (boundary.x - g_S0.x) / g_D.x : 1.0e30;
		float sy = g_D.y != 0.0 ? (boundary.y - g_S0.y) / g_D.y : 1.0e30;
		float exitS = min(min(sx, sy), end);
		ivec2 size = textureSize(u_SSRHiZMap, level);
		float zs = texelFetch(u_SSRHiZMap, clamp(ivec2(cell), ivec2(0), size - ivec2(1)), level).r;
		float zFar = max(SSRRayDepth(s), SSRRayDepth(exitS));
		if (!SSRIsSurface(zs) || zFar <= zs + 0.02 * zs + 1.0)
		{
			s = exitS + eps;
			level = min(level + 1, maxLevel);
		}
		else if (level > 0)
			level--;
		else
			return false;
	}
	return s >= end;
}

// The hit of the previous frame, if it is still the hit of the ray O + R t. Q = hit point (view space),
// hitPixel / zScene as the march returns them.
bool CachedHit(vec3 P, vec2 uv, vec3 O, vec3 R, float rayLength, float coneTangent, float startOffset,
	out vec3 Q, out vec2 hitPixel, out float zScene)
{
	Q = vec3(0.0);
	hitPixel = vec2(0.0);
	zScene = 0.0;

	// the receiver in the previous frame
	vec2 prevUV;
	if (u_SSRSettings4.y > 0.5)
	{
		prevUV = uv - texture(u_VelocityMap, uv).rg;
	}
	else
	{
		vec4 prevClip = u_SSRReproject * vec4(P, 1.0);
		if (prevClip.w <= 0.0)
			return false;
		prevUV = u_SSRViewport.xy + (prevClip.xy / prevClip.w * 0.5 + 0.5) * u_SSRViewport.zw;
	}
	if (!SSRInsideView(prevUV))
		return false;

	ivec2 traceSize = textureSize(u_SSRPrevHitMap, 0);
	ivec2 q = SSRNearestTraceTexel(prevUV, u_SSRSettings2.z, u_SSRSettings5.x, traceSize);
	vec4 cached = texelFetch(u_SSRPrevHitMap, q, 0);
	if (cached.w <= 0.0)
		return false;

	// the hit point is fixed in the world: previous view space -> this view space
	Q = (u_SSRPrevViewToView * vec4(SSRPrevHitPosition(cached), 1.0)).xyz;
	if (Q.z <= u_SSRSettings3.z * 1.5)
		return false;

	// still on the ray, within the roughness cone (at least a couple of trace pixels)
	vec3 d = Q - O;
	float t = dot(d, R);
	if (t <= 2.0 * startOffset || t > rayLength)
		return false;
	float pixelAngle = u_SSRDepthParams.w * u_SSRSettings2.z;
	float tolerance = t * max(0.5 * coneTangent, 2.0 * pixelAngle) + 0.5;
	if (length(d - R * t) > tolerance)
		return false;

	// still the visible surface there
	vec2 hitUV = SSRProjectToUV(Q);
	if (!SSRInsideView(hitUV))
		return false;
	hitPixel = hitUV / u_SSRTexelSize.xy;
	zScene = SSRSceneDepth(hitPixel);
	if (!SSRIsSurface(zScene))
		return false;
	if (abs(zScene - Q.z) > max(0.03 * zScene + 2.0, 0.25 * SSRThickness(zScene, u_SSRSettings.w)))
		return false;

	// not a moving object: its previous position must be where the hit was
	if (u_SSRSettings4.y > 0.5)
	{
		vec2 prevHitUV = hitUV - texture(u_VelocityMap, hitUV).rg;
		if (length((prevHitUV - cached.xy) / u_SSRTexelSize.xy) > 1.0)
			return false;
	}

	return CachedPathClear(O, Q);
}
#endif

void main()
{
	out_Color = vec4(0.0);

	float gridScale = u_SSRSettings2.z;
	ivec2 fullSize = textureSize(u_SSRHiZMap, 0);
	ivec2 traceTexel = ivec2(gl_FragCoord.xy);
	ivec2 pix = SSRTracePixel(traceTexel, gridScale, SSRPhaseOffset(u_SSRSettings4.w), fullSize);

	float z;
	vec4 normalRoughness;
#if defined(PRECLASSIFIED)
	// The classification depth test already proved eligibility. Only inputs
	// needed by the ray itself are fetched again, not stored depth/specular W.
	z = texelFetch(u_SSRHiZMap, pix, 0).r;
	normalRoughness = texelFetch(u_SSRNormalMap, pix, 0);
#else
	bool needsRay = NeedsRay(pix, z, normalRoughness);
#endif

#if defined(CLASSIFY)
	if (!needsRay)
		discard;
	out_Color = vec4(1.0);
#else
  #if !defined(PRECLASSIFIED)
	if (!needsRay)
		return;
  #endif

	vec2 uv = (vec2(pix) + 0.5) * u_SSRTexelSize.xy;
	float roughness = normalRoughness.b;
	float maxRoughness = u_SSRSettings2.x;

	vec3 P = SSRViewPosition(uv, z);
	vec3 N = SSRViewNormal(normalRoughness.rg);
	vec3 V = -normalize(P);
	float NV = dot(N, V);
	if (NV <= 0.0)
		return;
	vec3 R = reflect(-V, N);

	// start slightly above the surface, stop in front of the near plane
	float startOffset = max(0.05, 0.002 * z);
	vec3 O = P + N * startOffset;
	float rayLength = u_SSRSettings.z * mix(1.0, 0.35, roughness / maxRoughness);
	float nearZ = u_SSRSettings3.z * 1.5;
	if (R.z < 0.0)
		rayLength = min(rayLength, (O.z - nearZ) / -R.z);
	if (rayLength <= 1.0 || O.z <= nearZ)
		return;

	vec3 Q = vec3(0.0);
	vec2 hitPixel = vec2(0.0);
	float zRay = 0.0;
	float zScene = 0.0;
	bool reused = false;

	// the refresh slot of this pixel traces even with a valid cached hit
	bool refresh = int(u_SSRSettings4.z) == ((traceTexel.x & 1) | ((traceTexel.y & 1) << 1));
	if (u_SSRSettings4.x > 0.5 && !refresh)
	{
		reused = CachedHit(P, uv, O, R, rayLength, SSRConeTangent(roughness), startOffset, Q, hitPixel, zScene);
		zRay = Q.z;
	}

	if (!reused)
	{
		vec3 E = O + R * rayLength;

		float sMin, sMax, screenLength;
		if (!SSRSetupRay(O, E, sMin, sMax, screenLength))
			return; // along the view direction: nothing to find on the screen

		float jitter = SSRInterleavedGradientNoise(gl_FragCoord.xy + 5.588238 * u_SSRSettings2.w);

		float sHit;
		if (!SSRMarchRay(sMin, sMax, screenLength, jitter,
			u_SSRSettings.x, 1.0, u_SSRSettings.w, u_SSRSettings.y,
			u_SSRSettings3.x, u_SSRSettings3.w, sHit))
		{
			return;
		}

		hitPixel = SSRRayPixel(sHit);
		zRay = SSRRayDepth(sHit);
		zScene = SSRSceneDepth(hitPixel);
		if (!SSRIsSurface(zScene))
			return;

		// view space hit point on the ray (perspective correct)
		Q = mix(O * g_k0, E * g_k1, sHit) * zRay;
	}

	vec2 hitUV = hitPixel * u_SSRTexelSize.xy;
	float hitDistance = length(Q - O);
	if (hitDistance < 2.0 * startOffset)
		return; // the surface itself

	float confidence = 1.0;

	// ambiguous hit: the ray ended up far from the surface it crossed
	confidence *= 1.0 - smoothstep(0.25, 1.0, abs(zRay - zScene) / SSRThickness(zScene, u_SSRSettings.w));

	// the ray sees the back of the surface it hit
	vec4 hitNormalRoughness;
	if (SSRIsReceiver(ivec2(hitPixel), zScene, hitNormalRoughness))
		confidence *= 1.0 - smoothstep(0.0, 0.35, dot(SSRViewNormal(hitNormalRoughness.rg), R));

	// screen edges: the reflection continues off screen
	if (u_SSRSettings2.y > 0.0)
	{
		vec2 rel = (hitUV - u_SSRViewport.xy) / u_SSRViewport.zw;
		float edge = min(min(rel.x, 1.0 - rel.x), min(rel.y, 1.0 - rel.y));
		confidence *= smoothstep(0.0, u_SSRSettings2.y, edge);
	}

	// near the end of the ray, where a miss starts
	confidence *= 1.0 - smoothstep(0.6, 1.0, hitDistance / rayLength);

	// back towards the camera: the reflected scene is mostly behind the view
	confidence *= 1.0 - smoothstep(0.4, 0.9, dot(R, V));

	// grazing views: the ray skims the (normal mapped) surface
	confidence *= smoothstep(0.0, 0.08, NV);

	// rough surfaces fade to the prefiltered cubemap
	confidence *= 1.0 - smoothstep(maxRoughness * 0.7, maxRoughness, roughness);

	if (confidence <= 0.0)
		return;

	// the surface point (not the ray point behind it): fixed in the world, the next frame's cache
	out_Color = vec4(clamp(hitUV, 0.0, 1.0), SSREncodeHitDepth(zScene), SSREncodeConfidence(confidence, reused));
#endif
}
