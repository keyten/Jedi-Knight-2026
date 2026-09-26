/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// GTAO spatial denoiser: one edge-aware 3x3 pass with a variable tap distance
// (1, 2, 4... for successive passes, a-trous style). Two passes cover the
// 4x4 noise pattern of the main pass.
//
// Weights: binomial kernel * plane distance (the neighbour's position against
// the center's tangent plane, so sloped surfaces blur fully while steps and
// silhouettes do not) * normal similarity. Sky and depth hack texels are
// never used.
//
// Input/output: r = visibility, gba = view space normal * 0.5 + 0.5
// BENT_NORMAL: also filters the octahedral bent normals with the same weights
// (decoded, averaged as vectors, renormalized)

uniform sampler2D u_AOMap;       // previous GTAO result
uniform sampler2D u_AODepthMap;  // linear view depth, same resolution

uniform vec4 u_AOProjection;     // P[0], P[5], P[8], P[9]
uniform vec4 u_AODepthParams;    // P[14], P[10], zFar, sky threshold
uniform vec4 u_AOViewport;       // view rectangle in texture coordinates
uniform vec4 u_AOTexelSize;      // 1 / AO texture size (xy)
uniform vec4 u_AOSettings;       // x = tap distance in texels
#if defined(BENT_NORMAL)
uniform sampler2D u_AOBentMap;   // previous bent normals (octahedral, view space)
#endif

out vec4 out_Color;
#if defined(BENT_NORMAL)
// output 1 (bound by name as out_Glow): octahedral view space bent normal
out vec4 out_Glow;
#define out_BentNormal out_Glow

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

vec3 ViewOctDecode(vec2 e)
{
	e = e * 2.0 - 1.0;
	vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
	float t = max(-n.z, 0.0);
	n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
	n.z = -n.z;
	return normalize(n);
}
#endif

vec3 ViewPosition(vec2 uv, float z)
{
	vec2 ndc = (uv - u_AOViewport.xy) / u_AOViewport.zw * 2.0 - 1.0;
	return vec3((ndc + u_AOProjection.zw) * z / u_AOProjection.xy, z);
}

void main()
{
	ivec2 pix = ivec2(gl_FragCoord.xy);
	vec4 center = texelFetch(u_AOMap, pix, 0);
	float z = texelFetch(u_AODepthMap, pix, 0).r;

	if (z < 0.0 || z >= u_AODepthParams.w)
	{
		out_Color = center;
#if defined(BENT_NORMAL)
		out_BentNormal = texelFetch(u_AOBentMap, pix, 0);
#endif
		return;
	}

	ivec2 maxCoord = textureSize(u_AOMap, 0) - ivec2(1);
	int stride = int(u_AOSettings.x);
	vec3 P = ViewPosition((vec2(pix) + 0.5) * u_AOTexelSize.xy, z);
	vec3 N = normalize(center.gba * 2.0 - 1.0);

	// tolerated distance from the tangent plane grows with the pixel footprint
	float planeTolerance = 0.5 + z * 0.01 * float(stride);

	float sum = 0.0;
	float sumW = 0.0;
#if defined(BENT_NORMAL)
	vec3 bentSum = vec3(0.0);
#endif
	for (int y = -1; y <= 1; y++)
	{
		for (int x = -1; x <= 1; x++)
		{
			ivec2 p = pix + ivec2(x, y) * stride;
			if (any(lessThan(p, ivec2(0))) || any(greaterThan(p, maxCoord)))
				continue;

			float zs = texelFetch(u_AODepthMap, p, 0).r;
			if (zs < 0.0 || zs >= u_AODepthParams.w)
				continue;

			vec4 s = texelFetch(u_AOMap, p, 0);
			vec3 Ps = ViewPosition((vec2(p) + 0.5) * u_AOTexelSize.xy, zs);
			vec3 Ns = normalize(s.gba * 2.0 - 1.0);

			float planeDist = abs(dot(N, Ps - P)) / planeTolerance;
			float w = (x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0);
			w *= 1.0 / (1.0 + planeDist * planeDist);
			w *= pow(clamp(dot(N, Ns), 0.0, 1.0), 8.0);

			sum += s.r * w;
			sumW += w;
#if defined(BENT_NORMAL)
			bentSum += ViewOctDecode(texelFetch(u_AOBentMap, p, 0).rg) * w;
#endif
		}
	}

	out_Color = vec4(sumW > 1e-4 ? sum / sumW : center.r, center.gba);
#if defined(BENT_NORMAL)
	if (sumW > 1e-4 && dot(bentSum, bentSum) > 1e-8)
		out_BentNormal = vec4(ViewOctEncode(normalize(bentSum)), 0.0, 0.0);
	else
		out_BentNormal = texelFetch(u_AOBentMap, pix, 0);
#endif
}
