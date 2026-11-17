/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0,
		4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Ray traced shadows (r_sunShadowMode 2, r_dlightMode 3; tr_rtshadows.cpp). Self contained: the GL 4.3
// path compiles this block as a compute shader too (RT_COMPUTE), where implicit derivative lookups of
// shared libraries would not compile.
//
// Raster passes (full screen triangle):
//   RT_LINEARIZE  hardware depth -> linear view depth (R32F, <= 0 sky), read by every later pass
//   RT_TRACE      one shadow ray per trace pixel through the BVH of tr_rtscene.cpp: towards the sun, or
//                 (RT_LIGHTS) towards each of up to four dynamic lights, rgba = their visibility
//   RT_RESOLVE    trace resolution -> full resolution, and the temporal accumulation
//   RT_FILTER     depth aware a-trous step over the accumulated visibility
// Compute (GL 4.3, RT_COMPUTE):
//   RT_CLASSIFY   decides what it can (sky, back faces, out of range, the cascades' clear cases), writes
//                 those pixels and appends the rest to a list; groups of the indirect dispatch grow with it
//   RT_TRACE_LIST traces the listed pixels, one per thread, so tracing threads run densely packed
//
// r_rtShadowsHybrid: the sun cascades hold every caster in mode 2. Where all texels of a small footprint
// agree (nothing in front of the receiver, or an occluder well in front everywhere), the ray would give
// the same answer, and only the pixels near shadow edges are traced.
//
// BVH layout (tr_rtbvh.h), all in RGBA32F buffer textures:
//   node      min.xyz, a | max.xyz, b    a < 0 inner (left child = next node), else first triangle;
//                                        b = skip * 8 + triangle count
//   triangle  v0 | e1 | e2
//   instance  3 rows of world -> local, then (node texel, triangle texel, node count, buffer)
// The nodes are depth first, so a hit inner node continues with the next node and everything else
// with its skip node: no stack.

uniform sampler2D u_ScreenDepthMap;     // RT_LINEARIZE: hardware depth
uniform sampler2D u_RtDepthMap;         // linear view depth, full resolution
uniform sampler2D u_SSRTraceMap;        // input of the resolve / filter pass
uniform sampler2D u_SSRHistoryMap;
uniform sampler2D u_SSRHistoryGeomMap;
uniform sampler2D u_VelocityMap;
uniform samplerBuffer u_RtStaticBuffer;
uniform samplerBuffer u_RtDynamicBuffer;
uniform sampler2DArray u_RtCascadeMap;  // sun cascades, raw depth (layers 0-2)

uniform vec4 u_SSRProjection;           // P[0], P[5], P[8], P[9]
uniform vec4 u_SSRDepthParams;          // P[14], P[10], zFar, view space size of one pixel at depth 1
uniform vec4 u_SSRViewport;             // view rectangle in texture coordinates
uniform vec4 u_SSRTexelSize;            // 1 / full size, 1 / destination size
uniform mat4 u_SSRWorldToView;
uniform mat4 u_SSRReproject;            // view space -> previous frame clip space

uniform vec4 u_RtScene;         // world node count, world triangle texel, top level node count, instance texel
uniform vec4 u_RtParams[4];
// [0] direction to the sun, tangent of the half angle of its disc (0: hard)
// [1] bias (units), bias per unit of view depth, longest ray, frame
// [2] trace pixel step (1, 2), phase x, phase y, debug view
// [3] light count, light source radius, history weight, velocity buffer valid
uniform vec4 u_RtLights[8];     // per light: origin, radius | spot axis, cos outer angle (-2: point)
uniform vec4 u_RtHistory;       // history weight of each channel
uniform mat4 u_RtViewToWorld;
uniform mat4 u_RtCascadeMvp[3];
uniform vec4 u_RtCascadeInfo[3]; // world size of a texel, world units per depth unit, uv of a texel, valid
uniform vec4 u_RtHybrid;         // enabled, footprint radius in texels, extra lit tolerance (world), unused

#define RT_DEPTH_HACK_MAX 0.3001

#if defined(RT_COMPUTE)
layout(local_size_x = RT_GROUP_X, local_size_y = RT_GROUP_Y) in;
#if defined(RT_LIGHTS)
layout(binding = 0, rgba8) writeonly uniform image2D u_RtTraceImage;
#else
layout(binding = 0, r8) writeonly uniform image2D u_RtTraceImage;
#endif
layout(std430, binding = 0) buffer RtList { uint rtPixels[]; };
layout(std430, binding = 1) buffer RtArgs { uint rtGroupsX; uint rtGroupsY; uint rtGroupsZ; uint rtCount; };
#else
out vec4 out_Color;
#if defined(RT_RESOLVE)
out vec4 out_Glow;
#endif
#endif

// linear view depth of a full resolution pixel, <= 0: sky
float RtViewDepth(ivec2 pix)
{
	return texelFetch(u_RtDepthMap, pix, 0).r;
}

vec3 RtViewPosition(vec2 uv, float z)
{
	vec2 ndc = (uv - u_SSRViewport.xy) / u_SSRViewport.zw * 2.0 - 1.0;
	return vec3((ndc + u_SSRProjection.zw) * z / u_SSRProjection.xy, z);
}

bool RtInsideView(vec2 uv)
{
	vec2 rel = (uv - u_SSRViewport.xy) / u_SSRViewport.zw;
	return all(greaterThanEqual(rel, vec2(0.0))) && all(lessThan(rel, vec2(1.0)));
}

float RtNoiseIGN(vec2 pixel)
{
	return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

#if defined(RT_LINEARIZE)

void main()
{
	// the view model is drawn with glDepthRange(0, 0.3)
	float d = texelFetch(u_ScreenDepthMap, ivec2(gl_FragCoord.xy), 0).r;
	float z = -1.0;
	if (d < 1.0)
	{
		if (d <= RT_DEPTH_HACK_MAX)
			d = d / 0.3;
		z = u_SSRDepthParams.x / (2.0 * d - 1.0 + u_SSRDepthParams.y);
	}
	out_Color = vec4(z);
}

#elif defined(RT_TRACE) || defined(RT_CLASSIFY) || defined(RT_TRACE_LIST)

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
	if (!RtInsideView(uv))
		return false;
	position = RtViewPosition(uv, z);

	ivec2 maxPix = ivec2(1.0 / texel + 0.5) - ivec2(1);
	float zl = RtViewDepth(max(pix - ivec2(1, 0), ivec2(0)));
	float zr = RtViewDepth(min(pix + ivec2(1, 0), maxPix));
	float zd = RtViewDepth(max(pix - ivec2(0, 1), ivec2(0)));
	float zu = RtViewDepth(min(pix + ivec2(0, 1), maxPix));
	// the neighbour nearer in depth lies on the same surface
	vec3 dx = (zr > 0.0 && (zl <= 0.0 || abs(zr - z) < abs(z - zl))) ?
		RtViewPosition(uv + vec2(texel.x, 0.0), zr) - position :
		position - RtViewPosition(uv - vec2(texel.x, 0.0), zl > 0.0 ? zl : z);
	vec3 dy = (zu > 0.0 && (zd <= 0.0 || abs(zu - z) < abs(z - zd))) ?
		RtViewPosition(uv + vec2(0.0, texel.y), zu) - position :
		position - RtViewPosition(uv - vec2(0.0, texel.y), zd > 0.0 ? zd : z);
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
		RtNoiseIGN(vec2(pix) + 5.588238 * frame),
		RtNoiseIGN(vec2(pix.yx) + vec2(37.0, 17.0) + 9.123 * frame));
}

void RtBasis(vec3 n, out vec3 t, out vec3 b)
{
	t = normalize(abs(n.z) < 0.99 ? cross(n, vec3(0.0, 0.0, 1.0)) : cross(n, vec3(1.0, 0.0, 0.0)));
	b = cross(n, t);
}

// The world origin of the shadow ray of a trace pixel, its geometric normal and the start offset.
// False: sky or outside of the view.
bool RtPixelRay(ivec2 tracePix, out ivec2 pix, out vec3 origin, out vec3 normal, out float offset)
{
	int stride = int(u_RtParams[2].x + 0.5);
	ivec2 fullSize = ivec2(1.0 / u_SSRTexelSize.xy + 0.5);
	pix = tracePix;
	if (stride > 1)
		pix = min(tracePix * stride + ivec2(u_RtParams[2].yz), fullSize - ivec2(1));
	vec3 viewPosition, viewNormal;
	float z;
	origin = vec3(0.0);
	normal = vec3(0.0, 0.0, 1.0);
	offset = 0.0;
	if (!RtReceiver(pix, viewPosition, viewNormal, z))
		return false;
	vec3 position = (u_RtViewToWorld * vec4(viewPosition, 1.0)).xyz;
	normal = normalize(mat3(u_RtViewToWorld) * viewNormal);
	offset = u_RtParams[1].x + z * u_RtParams[1].y;
	origin = position + normal * offset;
	return true;
}

#if defined(RT_LIGHTS)
// -1: trace, else the visibility it is known to have (out of range, facing away, outside of the cone)
float RtLightKnown(int light, vec3 origin, vec3 normal)
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
	return -1.0;
}

float RtLightVisibility(int light, vec3 origin, vec3 normal, float offset, vec2 noise)
{
	float known = RtLightKnown(light, origin, normal);
	if (known >= 0.0)
		return known;
	vec3 toLight = u_RtLights[light * 2].xyz - origin;
	float dist = length(toLight);
	vec3 dir = toLight / dist;
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

// one call per light and a mask write: a dynamically indexed write into the vec4 inside a loop with
// early outs came out as no shadow at all on the Intel UHD driver
vec4 RtLightsVisibility(vec3 origin, vec3 normal, float offset, vec2 noise)
{
	vec4 visibility = vec4(1.0);
	int count = int(u_RtParams[3].x + 0.5);
	for (int light = 0; light < 4; ++light)
	{
		if (light >= count)
			break;
		float v = RtLightVisibility(light, origin, normal, offset, noise);
		visibility = mix(visibility, vec4(v), vec4(equal(ivec4(0, 1, 2, 3), ivec4(light))));
	}
	return visibility;
}

bool RtLightsNeedTrace(vec3 origin, vec3 normal)
{
	int count = int(u_RtParams[3].x + 0.5);
	for (int light = 0; light < 4; ++light)
	{
		if (light >= count)
			break;
		if (RtLightKnown(light, origin, normal) < 0.0)
			return true;
	}
	return false;
}
#else
// r_rtShadowsHybrid: -1 trace; 1 lit or 0 shadowed when every texel of the footprint in the finest
// cascade covering it agrees. The receiver's own surface is in the footprint too: in front of the
// receiver by at most its slope over the footprint, which is the lit tolerance.
float RtSunHybrid(vec3 origin, vec3 normal, vec3 dir)
{
	if (u_RtHybrid.x < 0.5)
		return -1.0;
	float cosTheta = clamp(dot(normal, dir), 0.05, 1.0);
	float slope = min(sqrt(1.0 - cosTheta * cosTheta) / cosTheta, 8.0);
	float radius = u_RtHybrid.y;
	for (int c = 0; c < 3; ++c)
	{
		vec4 info = u_RtCascadeInfo[c];
		if (info.w < 0.5)
			continue;
		vec4 q = u_RtCascadeMvp[c] * vec4(origin, 1.0);
		vec3 p = q.xyz / q.w * 0.5 + 0.5;
		float margin = (radius + 1.0) * info.z;
		if (any(lessThan(p, vec3(margin, margin, 0.0))) || any(greaterThan(p, vec3(1.0 - margin, 1.0 - margin, 1.0))))
			continue;
		float litTolerance = info.x * (1.0 + radius) * (1.0 + slope) + u_RtHybrid.z;
		float minSep = 1.0e9;
		float maxSep = -1.0e9;
		for (int y = -1; y <= 1; ++y)
		for (int x = -1; x <= 1; ++x)
		{
			vec2 uv = p.xy + vec2(float(x), float(y)) * radius * info.z;
			float d = textureLod(u_RtCascadeMap, vec3(uv, float(c)), 0.0).r;
			float sep = (p.z - d) * info.y;
			minSep = min(minSep, sep);
			maxSep = max(maxSep, sep);
		}
		if (maxSep < litTolerance)
			return 1.0;
		// every texel has an occluder clearly in front, and the sun disc's penumbra at that distance
		// stays inside the footprint
		if (minSep > 2.0 * litTolerance && maxSep * u_RtParams[0].w < 0.5 * radius * info.x)
			return 0.0;
		return -1.0;
	}
	return -1.0;
}

float RtSunVisibility(vec3 origin, vec3 normal, float offset, vec2 noise)
{
	vec3 dir = normalize(u_RtParams[0].xyz);
	if (dot(normal, dir) <= -0.15)
		return 0.0;
	if (u_RtParams[0].w > 0.0)
	{
		vec3 t, b;
		RtBasis(dir, t, b);
		float r = sqrt(noise.x) * u_RtParams[0].w;
		float a = 6.2831853 * noise.y;
		dir = normalize(dir + (t * cos(a) + b * sin(a)) * r);
	}
	return RtOccluded(origin, dir, 0.5 * offset, u_RtParams[1].z) ? 0.0 : 1.0;
}
#endif

#if defined(RT_TRACE)

void main()
{
	g_rtSteps = 0.0;
	ivec2 pix;
	vec3 origin, normal;
	float offset;
	if (!RtPixelRay(ivec2(gl_FragCoord.xy), pix, origin, normal, offset))
	{
		out_Color = vec4(1.0);
		return;
	}
	vec2 noise = RtNoise(pix);
#if defined(RT_LIGHTS)
	out_Color = RtLightsVisibility(origin, normal, offset, noise);
#else
	vec3 sunDir = normalize(u_RtParams[0].xyz);
	float known = dot(normal, sunDir) <= -0.15 ? 0.0 : RtSunHybrid(origin, normal, sunDir);
	if (known >= 0.0)
	{
		// r_rtShadowsDebug 5: decided by the cascades (black / white), traced (grey)
		out_Color = vec4(known);
		return;
	}
	out_Color = vec4(RtSunVisibility(origin, normal, offset, noise));
	if (u_RtParams[2].w > 4.5 && u_RtParams[2].w < 5.5)
		out_Color = vec4(0.5);
#endif
	// r_rtShadowsDebug 4: nodes visited
	if (u_RtParams[2].w > 3.5 && u_RtParams[2].w < 4.5)
		out_Color = vec4(clamp(g_rtSteps / 256.0, 0.0, 1.0));
}

#elif defined(RT_CLASSIFY)

shared uint s_rtCount;
shared uint s_rtBase;

void main()
{
	if (gl_LocalInvocationIndex == 0u)
		s_rtCount = 0u;
	barrier();

	ivec2 tracePix = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = imageSize(u_RtTraceImage);
	bool inside = all(lessThan(tracePix, size));
	bool trace = false;
	if (inside)
	{
		ivec2 pix;
		vec3 origin, normal;
		float offset;
		vec4 known = vec4(1.0);
		if (RtPixelRay(tracePix, pix, origin, normal, offset))
		{
#if defined(RT_LIGHTS)
			trace = RtLightsNeedTrace(origin, normal);
#else
			vec3 sunDir = normalize(u_RtParams[0].xyz);
			float k = dot(normal, sunDir) <= -0.15 ? 0.0 : RtSunHybrid(origin, normal, sunDir);
			trace = k < 0.0;
			known = vec4(k);
#endif
		}
		if (!trace)
			imageStore(u_RtTraceImage, tracePix, known);
		else if (u_RtParams[2].w > 4.5 && u_RtParams[2].w < 5.5)
		{
			imageStore(u_RtTraceImage, tracePix, vec4(0.5));
			trace = false;
		}
	}

	uint local = 0u;
	if (trace)
		local = atomicAdd(s_rtCount, 1u);
	barrier();
	if (gl_LocalInvocationIndex == 0u && s_rtCount > 0u)
	{
		s_rtBase = atomicAdd(rtCount, s_rtCount);
		atomicMax(rtGroupsX, (s_rtBase + s_rtCount + 63u) / 64u);
	}
	barrier();
	if (trace)
		rtPixels[s_rtBase + local] = uint(tracePix.x) | (uint(tracePix.y) << 16);
}

#elif defined(RT_TRACE_LIST)

void main()
{
	uint index = gl_GlobalInvocationID.x;
	if (index >= rtCount)
		return;
	uint entry = rtPixels[index];
	ivec2 tracePix = ivec2(int(entry & 0xffffu), int(entry >> 16));
	g_rtSteps = 0.0;
	ivec2 pix;
	vec3 origin, normal;
	float offset;
	if (!RtPixelRay(tracePix, pix, origin, normal, offset))
		return;
	vec2 noise = RtNoise(pix);
#if defined(RT_LIGHTS)
	vec4 v = RtLightsVisibility(origin, normal, offset, noise);
#else
	vec4 v = vec4(RtSunVisibility(origin, normal, offset, noise));
#endif
	if (u_RtParams[2].w > 3.5 && u_RtParams[2].w < 4.5)
		v = vec4(clamp(g_rtSteps / 256.0, 0.0, 1.0));
	imageStore(u_RtTraceImage, tracePix, v);
}

#endif

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
	if (z <= 0.0 || !RtInsideView(uv))
		return;

	// this frame's rays
	int stride = int(u_RtParams[2].x + 0.5);
	vec4 current;
	vec4 lowest, highest;
	ivec2 traceMax = textureSize(u_SSRTraceMap, 0) - ivec2(1);
	if (stride <= 1)
	{
		// the cross: a 3x3 box costs four more fetches for the same clamp in practice
		current = texelFetch(u_SSRTraceMap, pix, 0);
		vec4 a = texelFetch(u_SSRTraceMap, clamp(pix + ivec2(1, 0), ivec2(0), traceMax), 0);
		vec4 b = texelFetch(u_SSRTraceMap, clamp(pix - ivec2(1, 0), ivec2(0), traceMax), 0);
		vec4 c = texelFetch(u_SSRTraceMap, clamp(pix + ivec2(0, 1), ivec2(0), traceMax), 0);
		vec4 d = texelFetch(u_SSRTraceMap, clamp(pix - ivec2(0, 1), ivec2(0), traceMax), 0);
		lowest = min(min(min(a, b), min(c, d)), current);
		highest = max(max(max(a, b), max(c, d)), current);
	}
	else
	{
		// the four rays around this pixel, those of its own surface
		ivec2 phase = ivec2(u_RtParams[2].yz);
		ivec2 fullMax = textureSize(u_RtDepthMap, 0) - ivec2(1);
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

	// r_rtShadowsDebug 3 / 5: the rays of this frame
	if (u_RtParams[2].w > 2.5)
		return;

	vec4 historyWeight = u_RtHistory;
	if (max(max(historyWeight.x, historyWeight.y), max(historyWeight.z, historyWeight.w)) <= 0.0)
		return;

	vec3 receiver = RtViewPosition(uv, z);
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
	if (!RtInsideView(prevUV))
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
	vec4 center = texelFetch(u_SSRTraceMap, pix, 0);
	out_Color = center;

	int stride = int(u_RtParams[2].x + 0.5);
	// most of the screen is plainly lit or plainly shadowed: if the cross agrees there is nothing to
	// filter (and no depth to read)
	vec4 a = texelFetch(u_SSRTraceMap, clamp(pix + ivec2(stride, 0), ivec2(0), maxPix), 0);
	vec4 b = texelFetch(u_SSRTraceMap, clamp(pix - ivec2(stride, 0), ivec2(0), maxPix), 0);
	vec4 c = texelFetch(u_SSRTraceMap, clamp(pix + ivec2(0, stride), ivec2(0), maxPix), 0);
	vec4 d = texelFetch(u_SSRTraceMap, clamp(pix - ivec2(0, stride), ivec2(0), maxPix), 0);
	vec4 spread = max(max(abs(a - center), abs(b - center)), max(abs(c - center), abs(d - center)));
	if (max(max(spread.x, spread.y), max(spread.z, spread.w)) < 1.0 / 255.0)
		return;

	float z = RtViewDepth(pix);
	if (z <= 0.0)
		return;
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
