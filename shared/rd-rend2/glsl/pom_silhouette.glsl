/*[Fragment]*/
// Silhouette parallax occlusion mapping (r_pomSilhouette), tr_pom_silhouette.cpp,
// docs/rend2-silhouette-pom.md.
//
// This file is not a program on its own: its fragment block is inserted into the fragment shaders
// of the programs compiled with USE_SILHOUETTE_POM (the lightall silhouette set, the fog pass
// variant and pom_silhouette_depth). All of them trace with the functions below and the same
// uniforms, so the depth prepass, the colour pass and the fog pass find the same hit.
//
// Geometry: a world surface with the pomSilhouette keyword gets a shell (tr_pom_silhouette.cpp):
// its triangles moved to the top of the displaced volume plus side walls along the boundary edges
// of coplanar groups. The shell is only a conservative raster volume. Each shell vertex carries the
// base surface frame (flat normal / tangent of its group) and the texture coordinates of its foot
// point on the base plane; on a plane they are affine, so they interpolate exactly. attr_Position2:
// x = s0, normalized depth of the vertex in the height field volume (0 top, 1 bottom)
// y = D, world units per unit of s
// z = group header texel * 2 + 1 for side wall bottom vertices (flat, provoking vertex)
//
// Height convention (shared with GetParallaxOffset / RayIntersectDisplaceMap): the red channel of
// the normalHeightMap image is the flipped height (tr_image.cpp), i.e. the depth s in [0, 1] below
// the top of the volume. The base plane lies at s = parallaxBias, so the displaced surface is
// (parallaxBias - s) * D above it. With the aspect correction c of non square maps the ray steps
// (du, dv) / ds = -(Vt.xy * c) * parallaxDepth / Vt.z, exactly as the ordinary POM offset.
//
// u_PomGroups (RGBA32F buffer texture), per group:
//   header    : first edge texel, edge count, 0, 0
//   header + 1: lightmap = A * uv + b, A (column major: a00, a10, a01, a11)
//   header + 2: b.xy, 0, 0
//   edges     : a.xy, b.xy in texture space, oriented with the group inside on the left

#if defined(USE_SILHOUETTE_POM)
uniform samplerBuffer u_PomGroups;
uniform vec4 u_PomParams;	// linear steps at the normal, at grazing angles, binary steps, view dependence
uniform vec4 u_PomParams2;	// draw mode (1 shell, 2 crossfade base), depth mode (1 = behind a depth
							// prepass), orthographic pixel footprint (world, 0 = perspective), debug view
uniform vec4 u_PomFade;		// crossfade start distance, 1 / width, debug split x (window, < 0 off),
							// shadow caster offset (world) | contact shadows on shells (colour passes)

#define POM_MAX_LINEAR_STEPS 128
#define POM_MAX_BINARY_STEPS 16

struct PomHit
{
	bool  hit;
	bool  entryInside;	// entered through a wall below the height field (not a hit)
	vec2  uv;			// displaced texture coordinate
	vec2  lmUV;			// lightmap coordinate at the hit
	vec3  position;		// virtual world position
	float depth;		// depth s of the hit in the height field volume (POM self shadow)
	float t;			// world distance from the shell fragment
	float samples;		// height samples taken
};

bool PomIsShellDraw()
{
	return u_PomParams2.x < 1.5;
}

bool PomIsWall(in float header)
{
	return mod(header, 2.0) > 0.5;
}

int PomHeaderTexel(in float header)
{
	return int(header * 0.5);
}

// 4x4 ordered dither, the crossfade of shells and their base surfaces uses the
// same threshold in every pass so each pixel is drawn by exactly one of them
float PomDither(in vec2 fragCoord)
{
	const float bayer[16] = float[16](
		0.0, 8.0, 2.0, 10.0,
		12.0, 4.0, 14.0, 6.0,
		3.0, 11.0, 1.0, 9.0,
		15.0, 7.0, 13.0, 5.0);
	ivec2 p = ivec2(fragCoord) & ivec2(3);
	return (bayer[p.y * 4 + p.x] + 0.5) / 16.0;
}

// Crossfade band between the shell (near) and ordinary POM on the base surface
// (far): true when this draw keeps the fragment. r_pomSilhouetteDebug 9 splits
// the screen instead, ordinary POM left of u_PomFade.z.
bool PomFadeKeep(in vec3 worldPosition, in vec3 viewOrigin, in vec2 fragCoord, in bool shell)
{
	float f;
	if (u_PomFade.z >= 0.0)
		f = fragCoord.x < u_PomFade.z ? 1.0 : 0.0;
	else
		f = clamp((distance(worldPosition, viewOrigin) - u_PomFade.x) * u_PomFade.y, 0.0, 1.0);
	float r = PomDither(fragCoord);
	return shell ? (r >= f) : (r < f);
}

// ordinary POM aspect correction of non square height maps
vec2 PomAspect(in vec2 heightMapSize)
{
	float aspect = heightMapSize.y / heightMapSize.x;
	return vec2(max(1.0, aspect), max(1.0, 1.0 / aspect));
}

// Texture gradients for the height samples and the material maps. Top cap
// fragments use the screen derivatives of their foot point coordinates, as
// texturing the base surface would. Walls project to a line on the base plane
// (rank 1 derivatives): they use an isotropic footprint from the distance.
void PomGradients(in bool wall, in vec2 uvDx, in vec2 uvDy, in float pixelFootprint,
	in float D, in float parallaxDepth, in vec2 aspect, out vec2 gradX, out vec2 gradY)
{
	if (!wall)
	{
		gradX = uvDx;
		gradY = uvDy;
		return;
	}
	// texture units per world unit along the tangents: c * parallaxDepth / D
	vec2 perWorld = aspect * (parallaxDepth / max(D, 1e-6));
	gradX = vec2(pixelFootprint * perWorld.x, 0.0);
	gradY = vec2(0.0, pixelFootprint * perWorld.y);
}

float PomSampleDepth(in sampler2D heightMap, in vec2 uv, in vec2 gradX, in vec2 gradY)
{
	// height is flipped before uploaded to the gpu (same reading as
	// lightall SampleMaterialDepth: 0 = top, 1 = deepest)
	return textureGrad(heightMap, uv, gradX, gradY).r;
}

float PomCross(in vec2 a, in vec2 b)
{
	return a.x * b.y - a.y * b.x;
}

// First exit of the texture space ray p0 + t * d from the group footprint:
// the nearest boundary edge crossed from inside (left) to outside (right).
// The wall a fragment entered through is crossed inwards and never counts.
float PomFootprintExit(in int header, in vec2 p0, in vec2 d)
{
	vec4 h = texelFetch(u_PomGroups, header);
	int first = int(h.x);
	int count = int(h.y);
	float tExit = 1e30;
	for (int i = 0; i < count; i++)
	{
		vec4 e = texelFetch(u_PomGroups, first + i);
		vec2 ed = e.zw - e.xy;
		float k = PomCross(d, ed);
		if (k <= 0.0)
			continue; // parallel or entering
		vec2 w = e.xy - p0;
		float t = PomCross(w, ed) / k;
		float sigma = PomCross(w, d) / k;
		if (sigma >= -1e-4 && sigma <= 1.0001 && t > 0.0)
			tExit = min(tExit, t);
	}
	return tExit;
}

vec2 PomLightmapCoords(in int header, in vec2 uv)
{
	vec4 a = texelFetch(u_PomGroups, header + 1);
	vec4 b = texelFetch(u_PomGroups, header + 2);
	return mat2(a.xy, a.zw) * uv + b.xy;
}

// Linear steps for a ray with tangent space direction Rt: more towards grazing
// angles (r_pomSilhouetteSteps .. r_pomSilhouetteMaxSteps)
float PomLinearSteps(in vec3 Rt)
{
	float grazing = clamp((1.0 - abs(Rt.z)) * u_PomParams.w, 0.0, 1.0);
	return floor(mix(u_PomParams.x, u_PomParams.y, grazing) + 0.5);
}

// Trace the ray position0 + t * rayDir (world, normalized) through the height
// field volume of the group, from the shell fragment (texture coordinate uv0,
// depth s0) to where it leaves the volume: the slab 0 <= s <= 1 or the group
// footprint. T, B, N: world frame of the base surface (B = cross(N, T) * sign).
PomHit PomSilhouetteTrace(in sampler2D heightMap, in vec2 aspect, in float parallaxDepth,
	in vec3 position0, in vec3 rayDir, in vec2 uv0, in float s0, in float D, in int header,
	in vec3 T, in vec3 B, in vec3 N, in vec2 gradX, in vec2 gradY)
{
	PomHit result;
	result.hit = false;
	result.entryInside = false;
	result.uv = uv0;
	result.lmUV = vec2(0.0);
	result.position = position0;
	result.depth = s0;
	result.t = 0.0;
	result.samples = 0.0;

	vec3 Rt = vec3(dot(rayDir, T), dot(rayDir, B), dot(rayDir, N));
	D = max(D, 1e-6);
	// (u, v, s) per world unit along the ray
	vec3 dir = vec3(Rt.xy * aspect * (parallaxDepth / D), -Rt.z / D);

	float tMax = 1e30;
	if (dir.z > 1e-7)
		tMax = (1.0 - s0) / dir.z;
	else if (dir.z < -1e-7)
		tMax = -s0 / dir.z;
	float tFootprint = PomFootprintExit(header, uv0, dir.xy);
	// leaving through the floor: the last sample lies exactly on s = 1, a
	// height field at full depth (groove floors) is still hit
	bool bottomExit = dir.z > 1e-7 && tMax <= tFootprint;
	tMax = min(tMax, tFootprint);
	if (tMax >= 1e29)
		return result;
	tMax = max(tMax, 0.0);

	// The hit is the first point where the ray goes from above the height
	// field to below it. The top cap touching the height field is a hit at
	// the entry. A wall entry below the height field is inside the solid
	// (its side is not a displaced surface, neighbouring geometry owns it):
	// the search starts where the ray leaves the solid again.
	float dPrev = s0 - PomSampleDepth(heightMap, uv0, gradX, gradY);
	result.samples = 1.0;
	bool inside = false;
	if (dPrev >= 0.0)
	{
		if (s0 < 0.002)
		{
			result.hit = true;
			result.lmUV = PomLightmapCoords(header, uv0);
			return result;
		}
		inside = true;
		result.entryInside = true;
	}

	float steps = PomLinearSteps(Rt);
	float dt = tMax / steps;
	float tPrev = 0.0;
	float tCur = 0.0;
	float dCur = dPrev;
	bool found = false;
	for (int i = 1; i <= POM_MAX_LINEAR_STEPS; i++)
	{
		if (float(i) > steps)
			break;
		tCur = dt * float(i);
		vec3 p = vec3(uv0, s0) + dir * tCur;
		if (bottomExit && float(i) >= steps)
			p.z = 1.0;
		dCur = p.z - PomSampleDepth(heightMap, p.xy, gradX, gradY);
		result.samples += 1.0;
		if (inside)
		{
			inside = dCur >= 0.0;
		}
		else if (dCur >= 0.0)
		{
			found = true;
			break;
		}
		tPrev = tCur;
		dPrev = dCur;
	}
	if (!found)
		return result;

	// binary refinement between the last point above and the first below
	int binarySteps = int(u_PomParams.z);
	for (int i = 0; i < POM_MAX_BINARY_STEPS; i++)
	{
		if (i >= binarySteps)
			break;
		float tMid = 0.5 * (tPrev + tCur);
		vec3 p = vec3(uv0, s0) + dir * tMid;
		float dMid = p.z - PomSampleDepth(heightMap, p.xy, gradX, gradY);
		result.samples += 1.0;
		if (dMid >= 0.0)
		{
			tCur = tMid;
			dCur = dMid;
		}
		else
		{
			tPrev = tMid;
			dPrev = dMid;
		}
	}

	// secant between the bracketing samples
	float t = tCur;
	float denom = dCur - dPrev;
	if (denom > 1e-6)
		t = tPrev + (tCur - tPrev) * (-dPrev / denom);

	result.hit = true;
	result.t = t;
	result.uv = uv0 + dir.xy * t;
	result.depth = s0 + dir.z * t;
	result.position = position0 + rayDir * t;
	result.lmUV = PomLightmapCoords(header, result.uv);
	return result;
}

// window depth of a world position, as the rasterizer computes it
float PomWindowDepth(in mat4 viewProjection, in vec3 worldPosition)
{
	vec4 clip = viewProjection * vec4(worldPosition, 1.0);
	float ndc = clip.z / clip.w;
	return 0.5 * (gl_DepthRange.diff * ndc + gl_DepthRange.near + gl_DepthRange.far);
}

// Depth written by a shell fragment. Behind a depth prepass (depth mode 1) the
// colour and fog passes test LEQUAL against the prepass result without writing
// depth: the hit is pulled towards the camera by a small distance so the
// differences of separately compiled programs cannot fail the test.
float PomShellDepth(in mat4 viewProjection, in vec3 hitPosition, in vec3 rayDir, in float viewDistance)
{
	vec3 p = hitPosition;
	if (u_PomParams2.y > 0.5)
		p -= rayDir * max(0.02, 1e-4 * viewDistance);
	return PomWindowDepth(viewProjection, p);
}

// Sun shadow map lookup position of a shell hit: where the ray from the hit
// towards the sun leaves the top of the volume. Seen from the sun, every caster
// of the own shell lies at or behind that plane, so the lookup never finds the
// displaced surface itself (no acne; PCSS measures the real occluders and keeps
// its penumbra). Relief shadowing its own surface is left to GetPomSelfShadow.
vec3 PomShadowLookupPosition(in vec3 hitPosition, in float depth, in float D, in vec3 N, in vec3 L)
{
	return hitPosition + L * (depth * D / max(dot(L, N), 0.2));
}

vec3 PomHeatColor(in float x)
{
	x = clamp(x, 0.0, 1.0);
	return clamp(vec3(1.5 * x - 0.25, 1.5 - abs(2.0 * x - 1.0) * 1.5, 1.25 - 1.5 * x), 0.0, 1.0);
}
#endif
