/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0,
		4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Ray traced shadows (r_sunShadowMode 2, r_dlightMode 3; tr_rtshadows.cpp). The fragment block of
// ssr_common.glsl is in front of this one (view reconstruction, noise).
//
//   RT_TRACE    one shadow ray per pixel through the BVH of tr_rtscene.cpp: towards the sun, or
//               (RT_LIGHTS) one towards each of up to four dynamic lights, rgba = their visibility
//   RT_RESOLVE  half resolution trace -> full resolution, and the temporal accumulation
//   RT_FILTER   depth aware a-trous step over the accumulated visibility
//
// BVH layout (tr_rtbvh.h), all in RGBA32F buffer textures:
//   node      min.xyz, a | max.xyz, b    a < 0 inner (left child = next node), else first triangle;
//                                        b = skip * 8 + triangle count
//   triangle  v0 | e1 | e2
//   instance  3 rows of world -> local, then (node texel, triangle texel, node count, buffer)
// The nodes are depth first, so a hit inner node continues with the next node and everything else
// with its skip node: no stack.

uniform samplerBuffer u_RtStaticBuffer;
uniform samplerBuffer u_RtDynamicBuffer;
uniform vec4 u_RtScene;         // world node count, world triangle texel, top level node count, instance texel
uniform vec4 u_RtParams[4];
// [0] direction to the sun, tangent of the half angle of its disc (0: hard)
// [1] bias (units), bias per unit of view depth, longest ray, frame
// [2] trace pixel step (1, 2), phase x, phase y, debug view
// [3] light count, light source radius, history weight, velocity buffer valid
uniform vec4 u_RtLights[8];     // per light: origin, radius | spot axis, cos outer angle (-2: point)
uniform vec4 u_RtHistory;       // history weight of each light channel
uniform mat4 u_RtViewToWorld;

out vec4 out_Color;
#if defined(RT_RESOLVE)
out vec4 out_Glow;
#endif

// positive view depth of a pixel, -1: sky. The view model is drawn with glDepthRange(0, 0.3).
float RtViewDepth(ivec2 pix)
{
	float d = texelFetch(u_ScreenDepthMap, pix, 0).r;
	if (d >= 1.0)
		return -1.0;
	if (d <= SSR_DEPTH_HACK_MAX)
		d = d / 0.3;
	return u_SSRDepthParams.x / (2.0 * d - 1.0 + u_SSRDepthParams.y);
}

#if defined(RT_TRACE)

#define RT_MAX_STEPS 768

float g_rtSteps;

bool RtTraceTree(samplerBuffer buf, int nodeTexel, int triTexel, int nodeCount,
	vec3 origin, vec3 dir, float tMin, float tMax)
{
	vec3 safeDir = mix(dir, vec3(1.0e-20), lessThan(abs(dir), vec3(1.0e-20)));
	vec3 invDir = 1.0 / safeDir;
	int i = 0;
	for (int guard = 0; guard < RT_MAX_STEPS; ++guard)
	{
		if (i >= nodeCount)
			break;
		g_rtSteps += 1.0;
		vec4 n0 = texelFetch(buf, nodeTexel + i * 2);
		vec4 n1 = texelFetch(buf, nodeTexel + i * 2 + 1);
		vec3 ta = (n0.xyz - origin) * invDir;
		vec3 tb = (n1.xyz - origin) * invDir;
		vec3 lo = min(ta, tb);
		vec3 hi = max(ta, tb);
		float t0 = max(max(lo.x, lo.y), max(lo.z, tMin));
		float t1 = min(min(hi.x, hi.y), min(hi.z, tMax));
		int link = int(n1.w);
		if (!(t0 <= t1))
		{
			i = link >> 3;
			continue;
		}
		if (n0.w < 0.0)
		{
			i += 1;
			continue;
		}

		int first = triTexel + int(n0.w) * 3;
		int count = link & 7;
		for (int k = 0; k < count; ++k)
		{
			vec3 v0 = texelFetch(buf, first + k * 3).xyz;
			vec3 e1 = texelFetch(buf, first + k * 3 + 1).xyz;
			vec3 e2 = texelFetch(buf, first + k * 3 + 2).xyz;
			vec3 p = cross(dir, e2);
			float det = dot(e1, p);
			if (abs(det) < 1.0e-12)
				continue;
			float invDet = 1.0 / det;
			vec3 s = origin - v0;
			float u = dot(s, p) * invDet;
			if (u < 0.0 || u > 1.0)
				continue;
			vec3 q = cross(s, e1);
			float v = dot(dir, q) * invDet;
			if (v < 0.0 || u + v > 1.0)
				continue;
			float t = dot(e2, q) * invDet;
			if (t > tMin && t < tMax)
				return true;
		}
		i = link >> 3;
	}
	return false;
}

// anything between origin + dir * tMin and origin + dir * tMax
bool RtOccluded(vec3 origin, vec3 dir, float tMin, float tMax)
{
	// the static world
	if (RtTraceTree(u_RtStaticBuffer, 0, int(u_RtScene.y), int(u_RtScene.x), origin, dir, tMin, tMax))
		return true;

	// the instances: brush models, md3 entities, skinned Ghoul2 models
	int nodeCount = int(u_RtScene.z);
	if (nodeCount <= 0)
		return false;
	int instanceTexel = int(u_RtScene.w);
	vec3 safeDir = mix(dir, vec3(1.0e-20), lessThan(abs(dir), vec3(1.0e-20)));
	vec3 invDir = 1.0 / safeDir;
	int i = 0;
	for (int guard = 0; guard < 256; ++guard)
	{
		if (i >= nodeCount)
			break;
		g_rtSteps += 1.0;
		vec4 n0 = texelFetch(u_RtDynamicBuffer, i * 2);
		vec4 n1 = texelFetch(u_RtDynamicBuffer, i * 2 + 1);
		vec3 ta = (n0.xyz - origin) * invDir;
		vec3 tb = (n1.xyz - origin) * invDir;
		vec3 lo = min(ta, tb);
		vec3 hi = max(ta, tb);
		float t0 = max(max(lo.x, lo.y), max(lo.z, tMin));
		float t1 = min(min(hi.x, hi.y), min(hi.z, tMax));
		int link = int(n1.w);
		if (!(t0 <= t1))
		{
			i = link >> 3;
			continue;
		}
		if (n0.w < 0.0)
		{
			i += 1;
			continue;
		}

		int first = instanceTexel + int(n0.w) * 4;
		int count = link & 7;
		for (int k = 0; k < count; ++k)
		{
			vec4 r0 = texelFetch(u_RtDynamicBuffer, first + k * 4);
			vec4 r1 = texelFetch(u_RtDynamicBuffer, first + k * 4 + 1);
			vec4 r2 = texelFetch(u_RtDynamicBuffer, first + k * 4 + 2);
			vec4 info = texelFetch(u_RtDynamicBuffer, first + k * 4 + 3);
			// an affine map keeps the ray parameter: tMin and tMax stay as they are
			vec3 localOrigin = vec3(dot(r0.xyz, origin) + r0.w, dot(r1.xyz, origin) + r1.w, dot(r2.xyz, origin) + r2.w);
			vec3 localDir = vec3(dot(r0.xyz, dir), dot(r1.xyz, dir), dot(r2.xyz, dir));
			bool hit;
			if (info.w > 0.5)
				hit = RtTraceTree(u_RtDynamicBuffer, int(info.x), int(info.y), int(info.z), localOrigin, localDir, tMin, tMax);
			else
				hit = RtTraceTree(u_RtStaticBuffer, int(info.x), int(info.y), int(info.z), localOrigin, localDir, tMin, tMax);
			if (hit)
				return true;
		}
		i = link >> 3;
	}
	return false;
}

// view space position and geometric normal (towards the camera) of a full resolution pixel
bool RtReceiver(ivec2 pix, out vec3 position, out vec3 normal, out float z)
{
	z = RtViewDepth(pix);
	position = vec3(0.0);
	normal = vec3(0.0, 0.0, -1.0);
	if (z <= 0.0)
		return false;
	vec2 texel = u_SSRTexelSize.xy;
	vec2 uv = (vec2(pix) + 0.5) * texel;
	if (!SSRInsideView(uv))
		return false;
	position = SSRViewPosition(uv, z);

	ivec2 maxPix = textureSize(u_ScreenDepthMap, 0) - ivec2(1);
	float zl = RtViewDepth(max(pix - ivec2(1, 0), ivec2(0)));
	float zr = RtViewDepth(min(pix + ivec2(1, 0), maxPix));
	float zd = RtViewDepth(max(pix - ivec2(0, 1), ivec2(0)));
	float zu = RtViewDepth(min(pix + ivec2(0, 1), maxPix));
	// the neighbour nearer in depth lies on the same surface
	vec3 dx = (zr > 0.0 && (zl <= 0.0 || abs(zr - z) < abs(z - zl))) ?
		SSRViewPosition(uv + vec2(texel.x, 0.0), zr) - position :
		position - SSRViewPosition(uv - vec2(texel.x, 0.0), zl > 0.0 ? zl : z);
	vec3 dy = (zu > 0.0 && (zd <= 0.0 || abs(zu - z) < abs(z - zd))) ?
		SSRViewPosition(uv + vec2(0.0, texel.y), zu) - position :
		position - SSRViewPosition(uv - vec2(0.0, texel.y), zd > 0.0 ? zd : z);
	vec3 n = cross(dx, dy);
	float len = length(n);
	if (len > 1.0e-12)
	{
		n /= len;
		normal = dot(n, position) > 0.0 ? -n : n;
	}
	return true;
}

vec2 RtNoise(ivec2 pix)
{
	float frame = u_RtParams[1].w;
	return vec2(
		SSRInterleavedGradientNoise(vec2(pix) + 5.588238 * frame),
		SSRInterleavedGradientNoise(vec2(pix.yx) + vec2(37.0, 17.0) + 9.123 * frame));
}

void RtBasis(vec3 n, out vec3 t, out vec3 b)
{
	t = normalize(abs(n.z) < 0.99 ? cross(n, vec3(0.0, 0.0, 1.0)) : cross(n, vec3(1.0, 0.0, 0.0)));
	b = cross(n, t);
}

#if defined(RT_LIGHTS)
float RtLightVisibility(int light, vec3 origin, vec3 normal, float offset, vec2 noise)
{
	vec4 source = u_RtLights[light * 2];
	vec4 spot = u_RtLights[light * 2 + 1];
	vec3 toLight = source.xyz - origin;
	float dist = length(toLight);
	if (dist >= source.w || dist < 1.0e-3)
		return 1.0;
	vec3 dir = toLight / dist;
	// a surface facing away gets no light from it, and one outside of a spot cone neither
	if (dot(normal, dir) <= -0.15)
		return 1.0;
	if (spot.w > -1.5 && dot(-dir, spot.xyz) < spot.w - 0.05)
		return 1.0;
	float sourceRadius = u_RtParams[3].y;
	if (sourceRadius > 0.0)
	{
		// a point of the disc of the source seen from the receiver
		vec3 t, b;
		RtBasis(dir, t, b);
		float r = sqrt(noise.x) * min(sourceRadius, 0.5 * dist);
		float a = 6.2831853 * fract(noise.y + 0.31 * float(light));
		toLight += (t * cos(a) + b * sin(a)) * r;
		dist = length(toLight);
		dir = toLight / dist;
	}
	return RtOccluded(origin, dir, 0.5 * offset, max(dist - offset, 0.5 * offset)) ? 0.0 : 1.0;
}
#endif

void main()
{
	int stride = int(u_RtParams[2].x + 0.5);
	ivec2 fullSize = textureSize(u_ScreenDepthMap, 0);
	ivec2 pix = ivec2(gl_FragCoord.xy);
	if (stride > 1)
		pix = min(pix * stride + ivec2(u_RtParams[2].yz), fullSize - ivec2(1));

	g_rtSteps = 0.0;
	vec3 viewPosition, viewNormal;
	float z;
	if (!RtReceiver(pix, viewPosition, viewNormal, z))
	{
		out_Color = vec4(1.0);
		return;
	}
	vec3 position = (u_RtViewToWorld * vec4(viewPosition, 1.0)).xyz;
	vec3 normal = normalize(mat3(u_RtViewToWorld) * viewNormal);
	float offset = u_RtParams[1].x + z * u_RtParams[1].y;
	vec3 origin = position + normal * offset;
	vec2 noise = RtNoise(pix);

#if defined(RT_LIGHTS)
	// one function call per light and a mask write: a dynamically indexed write into the vec4 inside a
	// loop with early outs came out as no shadow at all on the Intel UHD driver
	vec4 visibility = vec4(1.0);
	int count = int(u_RtParams[3].x + 0.5);
	for (int light = 0; light < 4; ++light)
	{
		if (light >= count)
			break;
		float v = RtLightVisibility(light, origin, normal, offset, noise);
		visibility = mix(visibility, vec4(v), vec4(equal(ivec4(0, 1, 2, 3), ivec4(light))));
	}
	out_Color = visibility;
#else
	vec3 dir = normalize(u_RtParams[0].xyz);
	float visibility = 1.0;
	if (dot(normal, dir) <= -0.15)
	{
		visibility = 0.0;
	}
	else
	{
		if (u_RtParams[0].w > 0.0)
		{
			vec3 t, b;
			RtBasis(dir, t, b);
			float r = sqrt(noise.x) * u_RtParams[0].w;
			float a = 6.2831853 * noise.y;
			dir = normalize(dir + (t * cos(a) + b * sin(a)) * r);
		}
		visibility = RtOccluded(origin, dir, 0.5 * offset, u_RtParams[1].z) ? 0.0 : 1.0;
	}
	out_Color = vec4(visibility);
#endif

	// r_rtShadowsDebug 4: nodes visited
	if (u_RtParams[2].w > 3.5 && u_RtParams[2].w < 4.5)
		out_Color = vec4(clamp(g_rtSteps / 256.0, 0.0, 1.0));
}

#elif defined(RT_RESOLVE)

bool RtSameSurface(float a, float b)
{
	return b > 0.0 && abs(a - b) <= 0.02 * a + 1.0;
}

void main()
{
	ivec2 pix = ivec2(gl_FragCoord.xy);
	vec2 uv = (vec2(pix) + 0.5) * u_SSRTexelSize.xy;
	float z = RtViewDepth(pix);
	out_Glow = vec4(max(z, 0.0), 0.0, 0.0, 0.0);
	out_Color = vec4(1.0);
	if (z <= 0.0 || !SSRInsideView(uv))
		return;

	// this frame's rays
	int stride = int(u_RtParams[2].x + 0.5);
	vec4 current;
	vec4 lowest, highest;
	ivec2 traceMax = textureSize(u_SSRTraceMap, 0) - ivec2(1);
	if (stride <= 1)
	{
		current = texelFetch(u_SSRTraceMap, pix, 0);
		lowest = highest = current;
		for (int y = -1; y <= 1; ++y)
		for (int x = -1; x <= 1; ++x)
		{
			vec4 s = texelFetch(u_SSRTraceMap, clamp(pix + ivec2(x, y), ivec2(0), traceMax), 0);
			lowest = min(lowest, s);
			highest = max(highest, s);
		}
	}
	else
	{
		// the four rays around this pixel, those of its own surface
		ivec2 phase = ivec2(u_RtParams[2].yz);
		ivec2 fullMax = textureSize(u_ScreenDepthMap, 0) - ivec2(1);
		vec2 f = (vec2(pix) - vec2(phase)) / float(stride);
		ivec2 base = ivec2(floor(f));
		vec2 w = f - vec2(base);
		vec4 sum = vec4(0.0);
		float weight = 0.0;
		lowest = vec4(1.0);
		highest = vec4(0.0);
		vec4 nearest = vec4(1.0);
		float nearestDelta = 1.0e30;
		for (int corner = 0; corner < 4; ++corner)
		{
			ivec2 o = ivec2(corner & 1, corner >> 1);
			ivec2 q = clamp(base + o, ivec2(0), traceMax);
			float zq = RtViewDepth(min(q * stride + phase, fullMax));
			vec4 s = texelFetch(u_SSRTraceMap, q, 0);
			lowest = min(lowest, s);
			highest = max(highest, s);
			float delta = zq > 0.0 ? abs(zq - z) : 1.0e29;
			if (delta < nearestDelta)
			{
				nearestDelta = delta;
				nearest = s;
			}
			if (!RtSameSurface(z, zq))
				continue;
			float bw = (o.x == 0 ? 1.0 - w.x : w.x) * (o.y == 0 ? 1.0 - w.y : w.y) + 0.01;
			sum += s * bw;
			weight += bw;
		}
		current = weight > 0.0 ? sum / weight : nearest;
	}
	out_Color = current;

	// r_rtShadowsDebug 3: the rays of this frame
	if (u_RtParams[2].w > 2.5)
		return;

	vec4 historyWeight = u_RtHistory;
	if (max(max(historyWeight.x, historyWeight.y), max(historyWeight.z, historyWeight.w)) <= 0.0)
		return;

	vec3 receiver = SSRViewPosition(uv, z);
	vec4 prevClip = u_SSRReproject * vec4(receiver, 1.0);
	if (prevClip.w <= 0.0)
		return;
	vec2 prevUV = u_SSRViewport.xy + (prevClip.xy / prevClip.w * 0.5 + 0.5) * u_SSRViewport.zw;
	float expectedDepth = prevClip.w;
	if (u_RtParams[3].w > 0.5)
	{
		vec2 moved = uv - texture(u_VelocityMap, uv).rg;
		// a moving surface: where it was is known, how deep it was is not
		if (any(greaterThan(abs(moved - prevUV), u_SSRTexelSize.xy)))
			expectedDepth = z;
		prevUV = moved;
	}
	if (!SSRInsideView(prevUV))
		return;
	float previousDepth = texture(u_SSRHistoryGeomMap, prevUV).r;
	if (abs(previousDepth - expectedDepth) > 0.03 * expectedDepth + 2.0)
		return;
	vec4 previous = texture(u_SSRHistoryMap, prevUV);
	// outside of this frame's penumbra the history has nothing to add (and would ghost)
	previous = clamp(previous, lowest, highest);
	out_Color = mix(current, previous, clamp(historyWeight, 0.0, 0.95));
}

#elif defined(RT_FILTER)

void main()
{
	ivec2 pix = ivec2(gl_FragCoord.xy);
	ivec2 maxPix = textureSize(u_SSRTraceMap, 0) - ivec2(1);
	float z = RtViewDepth(pix);
	vec4 center = texelFetch(u_SSRTraceMap, pix, 0);
	out_Color = center;
	if (z <= 0.0)
		return;

	int stride = int(u_RtParams[2].x + 0.5);
	vec4 sum = center;
	float weight = 1.0;
	for (int y = -1; y <= 1; ++y)
	for (int x = -1; x <= 1; ++x)
	{
		if (x == 0 && y == 0)
			continue;
		ivec2 q = pix + ivec2(x, y) * stride;
		if (any(lessThan(q, ivec2(0))) || any(greaterThan(q, maxPix)))
			continue;
		float zq = RtViewDepth(q);
		if (zq <= 0.0)
			continue;
		// the tolerance follows the pixel footprint: a floor seen at a grazing angle keeps its taps
		float tolerance = 0.5 + z * (0.01 + 2.0 * float(stride) * u_SSRDepthParams.w);
		float w = (x == 0 || y == 0 ? 0.5 : 0.25) * exp(-abs(zq - z) / tolerance);
		sum += texelFetch(u_SSRTraceMap, q, 0) * w;
		weight += w;
	}
	out_Color = sum / weight;
}

#endif
