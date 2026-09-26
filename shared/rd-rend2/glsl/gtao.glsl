/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Ground Truth Ambient Occlusion, main pass.
//
// Horizon-based AO after Jimenez et al., "Practical Real-Time Strategies for
// Accurate Indirect Occlusion" (SIGGRAPH 2016). The slice integration, the
// distance falloff, the depth MIP selection and FastACos follow XeGTAO:
//
//   XeGTAO, Copyright (C) 2016-2021, Intel Corporation
//   SPDX-License-Identifier: MIT
//   Permission is hereby granted, free of charge, to any person obtaining a
//   copy of this software and associated documentation files (the
//   "Software"), to deal in the Software without restriction, including
//   without limitation the rights to use, copy, modify, merge, publish,
//   distribute, sublicense, and/or sell copies of the Software, and to permit
//   persons to whom the Software is furnished to do so, subject to the
//   following conditions: The above copyright notice and this permission
//   notice shall be included in all copies or substantial portions of the
//   Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
//   NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
//   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
//   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
//   USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// Differences to XeGTAO: OpenGL 3.2 fragment shader instead of a compute
// shader, no temporal noise (a fixed 4x4 pattern that the spatial denoiser
// fully covers, so the result does not shimmer without TAA), view normals
// reconstructed with second order depth extrapolation, and first person
// (depth hack) pixels excluded.
//
// View space here: x right, y up, z = positive distance along the view
// direction (same orientation as XeGTAO's view space).
//
// Output: r = visibility (1 = unoccluded), gba = view space normal * 0.5 + 0.5
// BENT_NORMAL: out_BentNormal.rg = octahedral view space bent normal (the
// visibility weighted mean unoccluded direction, XeGTAO's "Algorithm 2"
// extension), for indirect lighting only

uniform sampler2D u_AODepthMap;  // linear view depth, AO resolution, AO_DEPTH_MIPS levels

uniform vec4 u_AOProjection;     // P[0], P[5], P[8], P[9]
uniform vec4 u_AODepthParams;    // P[14], P[10], zFar, sky threshold
uniform vec4 u_AOViewport;       // view rectangle in texture coordinates
uniform vec4 u_AOTexelSize;      // 1 / AO texture size (xy)
uniform vec4 u_AOSettings;       // slices, steps per side, radius, falloff range (world units)
uniform vec4 u_AOSettings2;      // thin occluder compensation, final power, max radius (px), view size of a pixel at depth 1

out vec4 out_Color;
#if defined(BENT_NORMAL)
// output 1 (bound by name as out_Glow): octahedral view space bent normal
out vec4 out_Glow;
#define out_BentNormal out_Glow
#endif

#define AO_PI      3.1415926535897932
#define AO_HALF_PI 1.5707963267948966
#define MAX_MIP    3.0 // AO_DEPTH_MIPS - 1

// Eberly's polynomial acos approximation (as used by XeGTAO)
float FastAcos(float x)
{
	float res = -0.156583 * abs(x) + AO_HALF_PI;
	res *= sqrt(1.0 - abs(x));
	return x >= 0.0 ? res : AO_PI - res;
}

vec3 ViewPosition(vec2 uv, float z)
{
	vec2 ndc = (uv - u_AOViewport.xy) / u_AOViewport.zw * 2.0 - 1.0;
	return vec3((ndc + u_AOProjection.zw) * z / u_AOProjection.xy, z);
}

float FetchDepth(ivec2 p)
{
	p = clamp(p, ivec2(0), textureSize(u_AODepthMap, 0) - ivec2(1));
	return texelFetch(u_AODepthMap, p, 0).r;
}

vec3 FetchViewPosition(ivec2 p, float z)
{
	return ViewPosition((vec2(p) + 0.5) * u_AOTexelSize.xy, z);
}

// View normal from the depth buffer. For each axis the side whose depth is
// best predicted by linear extrapolation of the next pixel is used, so the
// derivative never spans a foreground/background edge. A true surface normal
// (G-buffer) can replace this function without touching the rest.
vec3 ReconstructNormal(ivec2 pix, vec3 P, float z)
{
	float l1 = FetchDepth(pix + ivec2(-1, 0)), l2 = FetchDepth(pix + ivec2(-2, 0));
	float r1 = FetchDepth(pix + ivec2( 1, 0)), r2 = FetchDepth(pix + ivec2( 2, 0));
	float d1 = FetchDepth(pix + ivec2(0, -1)), d2 = FetchDepth(pix + ivec2(0, -2));
	float u1 = FetchDepth(pix + ivec2(0,  1)), u2 = FetchDepth(pix + ivec2(0,  2));

	// invalid neighbours (depth hack) must never be chosen
	float errL = l1 < 0.0 ? 1e30 : abs(2.0 * l1 - l2 - z);
	float errR = r1 < 0.0 ? 1e30 : abs(2.0 * r1 - r2 - z);
	float errD = d1 < 0.0 ? 1e30 : abs(2.0 * d1 - d2 - z);
	float errU = u1 < 0.0 ? 1e30 : abs(2.0 * u1 - u2 - z);

	vec3 dx = errL < errR
		? P - FetchViewPosition(pix + ivec2(-1, 0), l1)
		: FetchViewPosition(pix + ivec2(1, 0), r1) - P;
	vec3 dy = errD < errU
		? P - FetchViewPosition(pix + ivec2(0, -1), d1)
		: FetchViewPosition(pix + ivec2(0, 1), u1) - P;

	vec3 N = cross(dy, dx);
	float len = length(N);
	if (len < 1e-8)
		return normalize(-P);
	N /= len;
	// face the camera
	return dot(N, -P) < 0.0 ? -N : N;
}

#if defined(BENT_NORMAL)
// View space octahedral encoding. Bent normals face the camera (-z), so z is
// flipped to keep them in the unfolded half of the octahedron (ao_composite.glsl
// decodes the same way).
vec2 ViewOctEncode(vec3 n)
{
	n.z = -n.z;
	n /= abs(n.x) + abs(n.y) + abs(n.z);
	vec2 p = n.xy;
	if (n.z < 0.0)
		p = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
	return p * 0.5 + 0.5;
}

// rotation taking (0, 0, -1) to V applied to a (XeGTAO_RotFromToMatrix,
// Rodrigues form). V always faces the camera, so the rotation is never 180
// degrees.
vec3 RotateFromViewAxis(vec3 V, vec3 a)
{
	const vec3 from = vec3(0.0, 0.0, -1.0);
	float e = dot(from, V);
	if (e > 1.0 - 0.0003)
		return a;
	vec3 v = cross(from, V);
	return e * a + cross(v, a) + (dot(v, a) / (1.0 + e)) * v;
}
#endif

// 4x4 ordered pattern, 0..15. Fixed in screen space: every 4x4 block holds
// all slice rotations, which the denoiser then averages.
float Bayer4(ivec2 p)
{
	p &= 3;
	int b = ((p.x ^ p.y) & 1) * 8 + (p.y & 1) * 4 + ((p.x ^ p.y) & 2) + ((p.y & 2) >> 1);
	return float(b);
}

void main()
{
	ivec2 pix = ivec2(gl_FragCoord.xy);
	float z = texelFetch(u_AODepthMap, pix, 0).r;
	vec2 uv = gl_FragCoord.xy * u_AOTexelSize.xy;

	// sky, far plane and depth hack pixels: unoccluded
	if (z < 0.0 || z >= u_AODepthParams.w)
	{
		out_Color = vec4(1.0, 0.5, 0.5, 0.0);
#if defined(BENT_NORMAL)
		out_BentNormal = vec4(ViewOctEncode(vec3(0.0, 0.0, -1.0)), 0.0, 0.0);
#endif
		return;
	}

	vec3 P = ViewPosition(uv, z);
	vec3 V = normalize(-P);
	vec3 N = ReconstructNormal(pix, P, z);

	int sliceCount = int(u_AOSettings.x);
	int stepCount = int(u_AOSettings.y);
	float effectRadius = u_AOSettings.z;
	float falloffRange = max(u_AOSettings.w, 1e-3);
	float falloffFrom = effectRadius - falloffRange;
	float falloffMul = -1.0 / falloffRange;
	float falloffAdd = falloffFrom / falloffRange + 1.0;
	float thinCompensation = u_AOSettings2.x;

	// effect radius in AO pixels
	float screenRadius = effectRadius / (z * u_AOSettings2.w);
	float radiusFade = clamp(screenRadius - 1.0, 0.0, 1.0);
	if (radiusFade <= 0.0)
	{
		out_Color = vec4(1.0, N * 0.5 + 0.5);
#if defined(BENT_NORMAL)
		out_BentNormal = vec4(ViewOctEncode(N), 0.0, 0.0);
#endif
		return;
	}
	screenRadius = min(screenRadius, u_AOSettings2.z);

	// never sample the center texel itself
	const float minOffset = 1.3;

	float pattern = Bayer4(pix);
	float sliceNoise = (pattern + 0.5) / 16.0;
	float stepNoise = fract(pattern * 0.6180339887 + 0.3);

	vec2 viewportMin = u_AOViewport.xy;
	vec2 viewportMax = u_AOViewport.xy + u_AOViewport.zw;

	float visibility = 0.0;
#if defined(BENT_NORMAL)
	vec3 bentNormal = vec3(0.0);
#endif
	for (int slice = 0; slice < sliceCount; slice++)
	{
		float phi = (float(slice) + sliceNoise) * (AO_PI / float(sliceCount));
		vec2 omega = vec2(cos(phi), sin(phi));

		vec3 directionVec = vec3(omega, 0.0);
		vec3 orthoDirectionVec = directionVec - dot(directionVec, V) * V;
		vec3 axisVec = normalize(cross(orthoDirectionVec, V));
		vec3 projectedNormalVec = N - axisVec * dot(N, axisVec);

		float signNorm = sign(dot(orthoDirectionVec, projectedNormalVec));
		float projectedNormalVecLength = length(projectedNormalVec);
		float cosNorm = clamp(dot(projectedNormalVec, V) / max(projectedNormalVecLength, 1e-6), 0.0, 1.0);
		float n = signNorm * FastAcos(cosNorm);

		// lowest possible horizons: the tangent plane
		float lowHorizonCos0 = cos(n + AO_HALF_PI);
		float lowHorizonCos1 = cos(n - AO_HALF_PI);
		float horizonCos0 = lowHorizonCos0;
		float horizonCos1 = lowHorizonCos1;

		for (int stepIdx = 0; stepIdx < stepCount; stepIdx++)
		{
			// quadratic distribution: more samples close to the center
			float s = (float(stepIdx) + stepNoise) / float(stepCount);
			s *= s;
			vec2 offset = round(omega * (s * screenRadius + minOffset));
			float mip = clamp(log2(length(offset)) - 3.3, 0.0, MAX_MIP);
			vec2 uvOffset = offset * u_AOTexelSize.xy;

			vec2 uv0 = uv + uvOffset;
			vec2 uv1 = uv - uvOffset;

			// positive direction
			if (all(greaterThanEqual(uv0, viewportMin)) && all(lessThan(uv0, viewportMax)))
			{
				float z0 = textureLod(u_AODepthMap, uv0, mip).r;
				if (z0 > 0.0)
				{
					vec3 delta = ViewPosition(uv0, z0) - P;
					float dist = length(delta);
					float shc = dot(delta, V) / dist;
					float falloffDist = length(vec3(delta.xy, delta.z * (1.0 + thinCompensation)));
					float weight = clamp(falloffDist * falloffMul + falloffAdd, 0.0, 1.0);
					horizonCos0 = max(horizonCos0, mix(lowHorizonCos0, shc, weight));
				}
			}

			// negative direction
			if (all(greaterThanEqual(uv1, viewportMin)) && all(lessThan(uv1, viewportMax)))
			{
				float z1 = textureLod(u_AODepthMap, uv1, mip).r;
				if (z1 > 0.0)
				{
					vec3 delta = ViewPosition(uv1, z1) - P;
					float dist = length(delta);
					float shc = dot(delta, V) / dist;
					float falloffDist = length(vec3(delta.xy, delta.z * (1.0 + thinCompensation)));
					float weight = clamp(falloffDist * falloffMul + falloffAdd, 0.0, 1.0);
					horizonCos1 = max(horizonCos1, mix(lowHorizonCos1, shc, weight));
				}
			}
		}

		// horizon angles, clamped to the hemisphere around the projected normal
		float h0 = -FastAcos(horizonCos1);
		float h1 =  FastAcos(horizonCos0);
		h0 = n + max(h0 - n, -AO_HALF_PI);
		h1 = n + min(h1 - n,  AO_HALF_PI);

		// cosine weighted visibility of the slice (inner integral, analytic)
		float sinN = sin(n);
		float iarc0 = (cosNorm + 2.0 * h0 * sinN - cos(2.0 * h0 - n)) / 4.0;
		float iarc1 = (cosNorm + 2.0 * h1 * sinN - cos(2.0 * h1 - n)) / 4.0;
		visibility += projectedNormalVecLength * (iarc0 + iarc1);

#if defined(BENT_NORMAL)
		// cosine weighted mean direction of the unoccluded arc, in the slice
		// frame (-z towards the viewer), then rotated to view space
		float t0 = (6.0 * sin(h0 - n) - sin(3.0 * h0 - n) + 6.0 * sin(h1 - n) -
			sin(3.0 * h1 - n) + 16.0 * sinN - 3.0 * (sin(h0 + n) + sin(h1 + n))) / 12.0;
		float t1 = (-cos(3.0 * h0 - n) - cos(3.0 * h1 - n) + 8.0 * cos(n) -
			3.0 * (cos(h0 + n) + cos(h1 + n))) / 12.0;
		vec3 localBentNormal = vec3(directionVec.xy * t0, -t1);
		bentNormal += RotateFromViewAxis(V, localBentNormal) * projectedNormalVecLength;
#endif
	}

	visibility /= float(sliceCount);
	visibility = pow(clamp(visibility, 0.0, 1.0), u_AOSettings2.y);
	visibility = max(visibility, 0.03);
	visibility = mix(1.0, visibility, radiusFade);

	out_Color = vec4(visibility, N * 0.5 + 0.5);
#if defined(BENT_NORMAL)
	float bentLength = length(bentNormal);
	vec3 B = bentLength > 1e-6 ? bentNormal / bentLength : N;
	B = normalize(mix(N, B, radiusFade));
	out_BentNormal = vec4(ViewOctEncode(B), 0.0, 0.0);
#endif
}
