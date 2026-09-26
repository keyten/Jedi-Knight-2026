/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Full resolution screen-space lighting composite, sampled by lightall
// (u_SSAOMap) during the main pass of the same view:
//
//   r = ambient occlusion: legacy SSAO (bilinear, as lightall sampled it
//       before), GTAO (depth-aware upsampled when rendered at half
//       resolution), or a split screen of both
//   g = short range contact shadow of the primary (sun) light, multiplied
//       into the sun shadow map visibility by lightall
//   ba = octahedral world space GTAO bent normal (r_gtaoBentNormals), upsampled
//       with the AO weights; lightall uses it for indirect light only
//
// Contact shadows march a short ray from the receiver towards the sun through
// the depth buffer. A sample is a hit when the ray is behind the depth buffer
// by less than the assumed occluder thickness; farther behind means the ray
// passes behind a foreground object (discontinuity), not through an occluder.

#define DEPTH_HACK_MAX 0.3001

uniform sampler2D u_ScreenDepthMap; // hardware depth, full resolution
uniform sampler2D u_AODepthMap;     // GTAO linear depth (AO resolution)
uniform sampler2D u_AOMap;          // GTAO result: r = visibility, gba = normal
uniform sampler2D u_LegacyAOMap;    // legacy SSAO, half resolution

uniform vec4 u_AOProjection;        // P[0], P[5], P[8], P[9]
uniform vec4 u_AODepthParams;       // P[14], P[10], zFar, sky threshold
uniform vec4 u_AOViewport;          // view rectangle in texture coordinates
uniform vec4 u_AOTexelSize;         // xy = 1 / AO texture size, zw = 1 / screen size
uniform vec4 u_AOSettings;          // AO source (0 none, 1 legacy, 2 GTAO, 3 split), split x, contact shadows, contact strength
uniform vec4 u_AOSettings2;         // contact length, steps, thickness, view size of a pixel at depth 1
uniform vec4 u_AOSettings3;         // soft contact shadows (r_contactShadowSoft), bent normals, unused, unused
uniform vec3 u_AOLightDir;          // view space direction to the sun
uniform sampler2D u_AOBentMap;      // GTAO bent normals, octahedral view space (AO resolution)
uniform mat4 u_AOViewToWorld;       // view space -> world space rotation

out vec4 out_Color;

// view space bent normals are stored with z flipped (gtao.glsl)
vec3 ViewOctDecode(vec2 e)
{
	e = e * 2.0 - 1.0;
	vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
	float t = max(-n.z, 0.0);
	n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
	n.z = -n.z;
	return normalize(n);
}

// world space octahedral encoding, decoded by lightall (AODecodeBentNormal)
vec2 OctEncode(vec3 n)
{
	n /= abs(n.x) + abs(n.y) + abs(n.z);
	vec2 p = n.xy;
	if (n.z < 0.0)
		p = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
	return p * 0.5 + 0.5;
}

float LinearDepth(float d)
{
	if (d <= DEPTH_HACK_MAX)
		return -1.0;
	return u_AODepthParams.x / (2.0 * d - 1.0 + u_AODepthParams.y);
}

vec3 ViewPosition(vec2 uv, float z)
{
	vec2 ndc = (uv - u_AOViewport.xy) / u_AOViewport.zw * 2.0 - 1.0;
	return vec3((ndc + u_AOProjection.zw) * z / u_AOProjection.xy, z);
}

vec2 ProjectToUV(vec3 p)
{
	vec2 ndc = p.xy * u_AOProjection.xy / p.z - u_AOProjection.zw;
	return (ndc * 0.5 + 0.5) * u_AOViewport.zw + u_AOViewport.xy;
}

float DepthAtPixel(ivec2 p)
{
	return LinearDepth(texelFetch(u_ScreenDepthMap, p, 0).r);
}

// Reconstruct a full-resolution geometric normal and select the closest
// finite difference on each axis. This avoids pulling the contact-ray origin
// across depth discontinuities at silhouettes.
vec3 ContactNormal(ivec2 pix, vec3 P)
{
	ivec2 maxPixel = ivec2(1.0 / u_AOTexelSize.zw) - ivec2(1);
	ivec2 px0 = clamp(pix - ivec2(1, 0), ivec2(0), maxPixel);
	ivec2 px1 = clamp(pix + ivec2(1, 0), ivec2(0), maxPixel);
	ivec2 py0 = clamp(pix - ivec2(0, 1), ivec2(0), maxPixel);
	ivec2 py1 = clamp(pix + ivec2(0, 1), ivec2(0), maxPixel);
	float zx0 = DepthAtPixel(px0);
	float zx1 = DepthAtPixel(px1);
	float zy0 = DepthAtPixel(py0);
	float zy1 = DepthAtPixel(py1);
	vec3 dx0 = zx0 > 0.0 ? P - ViewPosition((vec2(px0) + 0.5) * u_AOTexelSize.zw, zx0) : vec3(1e10);
	vec3 dx1 = zx1 > 0.0 ? ViewPosition((vec2(px1) + 0.5) * u_AOTexelSize.zw, zx1) - P : vec3(1e10);
	vec3 dy0 = zy0 > 0.0 ? P - ViewPosition((vec2(py0) + 0.5) * u_AOTexelSize.zw, zy0) : vec3(1e10);
	vec3 dy1 = zy1 > 0.0 ? ViewPosition((vec2(py1) + 0.5) * u_AOTexelSize.zw, zy1) - P : vec3(1e10);
	float dx0Sq = dot(dx0, dx0);
	float dx1Sq = dot(dx1, dx1);
	float dy0Sq = dot(dy0, dy0);
	float dy1Sq = dot(dy1, dy1);
	vec3 dx = dx0Sq > 1e-8 && dx0Sq < dx1Sq ? dx0 : dx1;
	vec3 dy = dy0Sq > 1e-8 && dy0Sq < dy1Sq ? dy0 : dy1;
	vec3 crossNormal = cross(dx, dy);
	vec3 N = dot(crossNormal, crossNormal) > 1e-10 ? normalize(crossNormal) : normalize(-P);
	return dot(N, -P) < 0.0 ? -N : N;
}

// Depth-aware upsampling of the GTAO result: the 2x2 bilinear footprint,
// each texel weighted by how well its tangent plane (depth + normal) predicts
// this pixel's position. Falls back to the best matching texel on edges.
// The bent normal (view space) uses the same weights when requested.
float UpsampleGTAO(vec2 uv, vec3 P, float z, bool withBent, out vec3 bent)
{
	vec3 bentSum = vec3(0.0);
	vec3 bestBent = vec3(0.0, 0.0, -1.0);
	vec2 aoSize = 1.0 / u_AOTexelSize.xy;
	vec2 st = uv * aoSize - 0.5;
	ivec2 base = ivec2(floor(st));
	vec2 f = st - vec2(base);
	ivec2 maxCoord = ivec2(aoSize) - ivec2(1);

	float tolerance = 0.5 + z * 0.01;
	float sum = 0.0;
	float sumW = 0.0;
	float best = 1.0;
	float bestDist = 1e30;
	for (int i = 0; i < 4; i++)
	{
		ivec2 o = ivec2(i & 1, i >> 1);
		ivec2 p = clamp(base + o, ivec2(0), maxCoord);
		float zs = texelFetch(u_AODepthMap, p, 0).r;
		if (zs < 0.0 || zs >= u_AODepthParams.w)
			continue;

		vec4 s = texelFetch(u_AOMap, p, 0);
		vec3 Ps = ViewPosition((vec2(p) + 0.5) * u_AOTexelSize.xy, zs);
		vec3 Ns = normalize(s.gba * 2.0 - 1.0);
		float planeDist = abs(dot(Ns, P - Ps)) + 0.25 * abs(zs - z);

		vec2 bw = mix(vec2(1.0) - f, f, vec2(o));
		float d = planeDist / tolerance;
		float w = bw.x * bw.y / (1.0 + d * d * 16.0);
		sum += s.r * w;
		sumW += w;

		vec3 b = withBent ? ViewOctDecode(texelFetch(u_AOBentMap, p, 0).rg) : vec3(0.0);
		bentSum += b * w;

		if (planeDist < bestDist)
		{
			bestDist = planeDist;
			best = s.r;
			bestBent = b;
		}
	}

	bent = sumW > 1e-3 && dot(bentSum, bentSum) > 1e-8 ? normalize(bentSum) : bestBent;
	return sumW > 1e-3 ? sum / sumW : best;
}

float ContactShadow(ivec2 pix, vec2 uv, vec3 P, float z)
{
	vec3 L = u_AOLightDir;
	float rayLength = u_AOSettings2.x;
	int steps = int(u_AOSettings2.y);
	float pixelSize = z * u_AOSettings2.w;
	vec3 N = ContactNormal(pix, P);

	// Start along the receiver normal and a little towards the light. The
	// normal term retains contacts while rejecting the receiver itself.
	vec3 origin = P + N * (pixelSize * 1.5 + 0.05) + L * 0.05;
	float stepLength = rayLength / float(steps);

	vec2 screenSize = 1.0 / u_AOTexelSize.zw;
	vec2 viewportMin = u_AOViewport.xy;
	vec2 viewportMax = u_AOViewport.xy + u_AOViewport.zw;

	float occlusion = 0.0;
	if (u_AOSettings3.x > 0.5)
	{
		// r_contactShadowSoft: the steps are packed quadratically towards the
		// receiver, where small occluders (nose, chin, armour overlaps, a hand
		// on the torso) are. Every step occludes by how far it lies inside the
		// assumed occluder thickness and the strongest one wins; without the
		// first hit exit the result changes continuously while characters
		// animate instead of flipping per pixel.
		for (int i = 0; i < steps; i++)
		{
			float u = (float(i) + 0.5) / float(steps);
			float t = u * u * rayLength;
			vec3 Q = origin + L * t;
			if (Q.z <= 1.0)
				break;

			vec2 quv = ProjectToUV(Q);
			if (any(lessThan(quv, viewportMin)) || any(greaterThanEqual(quv, viewportMax)))
				break;

			float sceneZ = LinearDepth(texelFetch(u_ScreenDepthMap, ivec2(quv * screenSize), 0).r);
			if (sceneZ < 0.0)
				continue;

			float behind = Q.z - sceneZ;
			float samplePixelSize = Q.z * u_AOSettings2.w;
			float bias = samplePixelSize * 1.5 + 0.05;
			float thickness = u_AOSettings2.z + samplePixelSize * 2.0;
			float inside = smoothstep(bias, bias + samplePixelSize * 2.0 + 0.1, behind) *
				(1.0 - smoothstep(0.5 * thickness, thickness, behind));
			occlusion = max(occlusion, inside * (1.0 - smoothstep(0.5, 1.0, t / rayLength)));
		}
	}
	else for (int i = 0; i < steps; i++)
	{
		// A fixed midpoint is temporally stable. A screen-locked Bayer offset
		// visibly crawled across receivers while the camera moved.
		float t = (float(i) + 0.5) * stepLength;
		vec3 Q = origin + L * t;
		if (Q.z <= 1.0)
			break; // behind the camera

		vec2 quv = ProjectToUV(Q);
		if (any(lessThan(quv, viewportMin)) || any(greaterThanEqual(quv, viewportMax)))
			break; // occluders off screen are unknown

		float sceneZ = LinearDepth(texelFetch(u_ScreenDepthMap, ivec2(quv * screenSize), 0).r);
		if (sceneZ < 0.0)
			continue;

		float behind = Q.z - sceneZ;
		float samplePixelSize = Q.z * u_AOSettings2.w;
		float bias = samplePixelSize * 1.5 + 0.05;
		float thickness = u_AOSettings2.z + samplePixelSize * 2.0;
		if (behind > bias && behind < thickness)
		{
			// fade out towards the end of the ray, the first hit is the strongest
			occlusion = 1.0 - smoothstep(0.5, 1.0, t / rayLength);
			break;
		}
	}

	// fade near the screen edges, where occluders start to be missing
	vec2 edge = min(uv - viewportMin, viewportMax - uv) / u_AOViewport.zw;
	occlusion *= clamp(min(edge.x, edge.y) * 20.0, 0.0, 1.0);

	return 1.0 - occlusion * u_AOSettings.w;
}

void main()
{
	ivec2 pix = ivec2(gl_FragCoord.xy);
	vec2 uv = gl_FragCoord.xy * u_AOTexelSize.zw;
	float z = LinearDepth(texelFetch(u_ScreenDepthMap, pix, 0).r);

	if (z < 0.0 || z >= u_AODepthParams.w)
	{
		// sky, far plane, depth hack: legacy AO kept as lightall saw it before
		float legacy = u_AOSettings.x == 1.0 ? texture(u_LegacyAOMap, uv).r : 1.0;
		out_Color = vec4(legacy, 1.0, 0.5, 0.5);
		return;
	}

	vec3 P = ViewPosition(uv, z);

	float ao = 1.0;
	bool withBent = u_AOSettings3.y > 0.5;
	vec3 bentView = vec3(0.0, 0.0, -1.0);
	int source = int(u_AOSettings.x);
	if (source == 3)
		source = uv.x < u_AOSettings.y ? 1 : 2;
	if (source == 1)
		ao = texture(u_LegacyAOMap, uv).r;
	else if (source == 2)
		ao = UpsampleGTAO(uv, P, z, withBent, bentView);

	// neutral encoding when off: lightall weights the bent normal by 1 - AO
	// and the scene level strength, so it is not used then anyway
	vec2 bentEncoded = vec2(0.5);
	if (withBent && source == 2)
		bentEncoded = OctEncode(normalize(mat3(u_AOViewToWorld) * bentView));

	float contact = 1.0;
	if (u_AOSettings.z > 0.5)
		contact = ContactShadow(pix, uv, P, z);

	out_Color = vec4(ao, contact, bentEncoded);
}
