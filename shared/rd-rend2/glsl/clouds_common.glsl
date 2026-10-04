/*[Fragment]*/
// Volumetric clouds (r_clouds, tr_clouds.cpp, docs/rend2-volumetric-clouds.md).
//
// This file is not a program on its own: its fragment block is inserted into the cloud programs (march,
// resolve, composite, shadow map), see GLSL_LoadGPUProgramClouds.
//
// Cloud space: kilometres, x / y = the world axes, z = height above the ground (r_atmosphereGroundZ), the
// planet centre at (0, 0, -R). The clouds are a shell between two altitudes above that sphere, so the layer
// bends down to the horizon. Altitudes are formed without r^2 - R^2 cancellation (as atmosphere_common.glsl).
//
// u_Cloud (tr_clouds.cpp, RB_CloudsUniforms):
//   [0]  base (km), top (km), planet radius (km), coverage (r_cloudCoverage)
//   [1]  extinction at density 1 (1/km), forward lobe g, back lobe g, forward lobe weight
//   [2]  tile size (km) of the shape noise, of the detail noise, of the weather map, detail (erosion) strength
//   [3]  shape noise offset (tiles, wind), jitter of this frame (0..1)
//   [4]  detail noise offset (tiles), max distance (km)
//   [5]  weather map offset (tiles) xy, multiple scattering octaves, light march length (km)
//   [6]  direction towards the sun (world, z up), light march steps
//   [7]  sun illuminance (buffer units) without the atmosphere, 1 = sun / ambient / aerial from the atmosphere
//   [8]  ambient sky radiance (no atmosphere), ambient scale (r_cloudAmbient)
//   [9]  ground bounce radiance (no atmosphere), ground altitude above sea level (km, atmosphere lookups)
//   [10] camera (cloud space), max march steps
//   [11] march step length (km), min march steps, km per world unit, ground z (world units)
//   [12] legacy cloud layer (r_cloudLegacy): centre xy (km), outer radius (km), inner radius (km)
//   [13] legacy: mode (0 none, 1 hint, 2 literal), tube, haze (ALT), cloud type scale
//   [14] view rectangle in render target texture coordinates (x, y, w, h)
//   [15] low resolution view rectangle in pixels (x, y, w, h)
//   [16] debug view, temporal weight (0 = no history), frame index, full resolution pixels per march pixel
//   [17] cloud shadow map: centre xy (km), extent (km), steps
//   [18] composite mode (0 composite, 1 sun ray mask), 1 = aerial perspective, wind velocity (world units / s) xy
//   [19] camera (world units), frame time (s)

#define CLOUD_UNIFORM_VEC4S 20

uniform vec4 u_Cloud[CLOUD_UNIFORM_VEC4S];

#define CLOUD_PI 3.14159265358979

// march pixels without sky (sentinel cloud distance)
#define CLOUD_NO_SKY -1.0
// cloud distance of a ray that misses the layer: far away (km)
#define CLOUD_FAR_DISTANCE 1000.0

float CloudRemap(float x, float a, float b, float c, float d)
{
	return c + (x - a) * (d - c) / max(b - a, 1e-5);
}

// altitude above the ground sphere of a cloud space point: |p - c| - R = q / (|p - c| + R), q = x^2 + y^2 + z^2 + 2 R z
float CloudAltitude(vec3 p)
{
	float R = u_Cloud[0].z;
	float q = dot(p, p) + 2.0 * R * p.z;
	return q / (sqrt(max(q + R * R, 0.0)) + R);
}

float CloudHeightFraction(float h)
{
	return (h - u_Cloud[0].x) / max(u_Cloud[0].y - u_Cloud[0].x, 1e-4);
}

// Distances along the unit direction d from o (altitude ho) to the sphere of altitude h: the near and far
// root, false when the line misses it. t^2 + 2 b t + c = 0, c = |o - centre|^2 - (R + h)^2 = (ho - h)(2R + ho + h),
// roots q and c / q (stable).
bool CloudSphere(vec3 o, float ho, vec3 d, float h, out float t0, out float t1)
{
	float R = u_Cloud[0].z;
	float b = dot(vec3(o.xy, o.z + R), d);
	float c = (ho - h) * (2.0 * R + ho + h);
	float disc = b * b - c;
	t0 = 0.0;
	t1 = 0.0;
	if (disc < 0.0)
		return false;
	float s = sqrt(disc);
	float q = b >= 0.0 ? -b - s : -b + s;
	float r0 = q;
	float r1 = abs(q) > 1e-20 ? c / q : 0.0;
	t0 = min(r0, r1);
	t1 = max(r0, r1);
	return true;
}

// The part of the ray inside the cloud shell (one interval: the nearest), ahead of the camera, before the ground
// and before the max distance
bool CloudInterval(vec3 o, vec3 d, out float tStart, out float tEnd)
{
	float hb = u_Cloud[0].x;
	float ht = u_Cloud[0].y;
	float ho = CloudAltitude(o);
	tStart = 0.0;
	tEnd = 0.0;
	if (ht <= hb)
		return false;

	// the ground hides everything behind it
	float groundHit = 1e9;
	float g0, g1;
	if (ho > 0.0 && d.z < 0.0 && CloudSphere(o, ho, d, 0.0, g0, g1) && g0 > 0.0)
		groundHit = g0;

	float b0, b1, t0, t1;
	bool hitBase = CloudSphere(o, ho, d, hb, b0, b1);
	bool hitTop = CloudSphere(o, ho, d, ht, t0, t1);
	if (ho < hb)
	{
		// below: inside both spheres, leave the base sphere, then the top sphere
		if (!hitBase || !hitTop)
			return false;
		tStart = b1;
		tEnd = t1;
		if (groundHit < tStart)
			return false;
	}
	else if (ho <= ht)
	{
		// inside the layer
		if (!hitTop)
			return false;
		tStart = 0.0;
		tEnd = t1;
		if (hitBase && b0 > 0.0)
			tEnd = min(tEnd, b0);
		tEnd = min(tEnd, groundHit);
	}
	else
	{
		// above: enter the top sphere, leave at the base sphere (or the far side of the top one)
		if (!hitTop || t1 <= 0.0)
			return false;
		tStart = max(t0, 0.0);
		tEnd = (hitBase && b0 > 0.0) ? b0 : t1;
	}
	tEnd = min(tEnd, u_Cloud[4].w);
	return tEnd > tStart;
}

// --- density --------------------------------------------------------------------------------------------------

uniform sampler3D u_CloudShapeMap;
uniform sampler3D u_CloudDetailMap;
uniform sampler3D u_CloudWeatherMap;

// coverage (r) and cloud type (g) of the weather map at a cloud space point
vec2 CloudWeather(vec2 xy, float lod)
{
	return textureLod(u_CloudWeatherMap, vec3(xy / u_Cloud[2].z + u_Cloud[5].xy, 0.5), lod).rg;
}

// r_cloudLegacy: the fx_cloudlayer entity of the map (tr_clouds.cpp). Mode 1 only takes its style (a TUBE
// layer leaves the sky above the camera open), mode 2 its literal disc / ring.
float CloudLegacyMask(vec2 xy)
{
	int mode = int(u_Cloud[13].x);
	if (mode == 0)
		return 1.0;
	bool tube = u_Cloud[13].y > 0.5;
	if (mode == 1)
		return tube ? smoothstep(1.0, 6.0, distance(xy, u_Cloud[10].xy)) : 1.0;
	float d = distance(xy, u_Cloud[12].xy);
	float outer = max(u_Cloud[12].z, 1e-3);
	float mask = 1.0 - smoothstep(outer * 0.6, outer, d);
	if (tube)
	{
		float inner = clamp(u_Cloud[12].w, 0.0, outer);
		mask *= smoothstep(inner, inner + max((outer - inner) * 0.15, 1e-3), d);
	}
	return mask;
}

// share of the weather map value that becomes cloud: coverage 0 = clear, 0.5 = the map, 1 = overcast
float CloudCoverage(vec2 weather, vec2 xy)
{
	return clamp(weather.r + 2.0 * u_Cloud[0].w - 1.0, 0.0, 1.0) * CloudLegacyMask(xy);
}

// vertical profile by cloud type: 0 = flat stratus, 1 = tall cumulus
float CloudProfile(float hf, float type)
{
	float bottom = smoothstep(0.0, 0.08, hf);
	float topEnd = mix(0.4, 1.0, type);
	float top = 1.0 - smoothstep(topEnd * 0.55, topEnd, hf);
	return bottom * top;
}

struct CloudDensitySample
{
	float extinction;	// 1/km
	float coverage;
	float type;			// cloud type of the weather map
	float low;			// shape noise
	float high;			// detail noise (0 without erosion)
};

// Extinction at a cloud space point p of altitude h with the weather map's coverage and cloud type at p already
// known (the light march reuses those of its sample: its few km are nothing against the weather tile). lod:
// shape, detail mip levels. Exactly 0 where there is no cloud, so a clear sky stays bit exact.
CloudDensitySample CloudDensityCovered(vec3 p, float h, vec3 lod, bool erosion, float coverage, float type)
{
	CloudDensitySample s;
	s.extinction = 0.0;
	s.coverage = coverage;
	s.type = type;
	s.low = 0.0;
	s.high = 0.0;

	float hf = CloudHeightFraction(h);
#if defined(CLOUD_TEST_HOMOGENEOUS)
	// tools/rend2/test_clouds_gl.py: a homogeneous shell of extinction u_Cloud[1].x (never defined in the game)
	if (hf > -0.01 && hf < 1.01)
	{
		s.extinction = u_Cloud[1].x;
		s.coverage = 1.0;
		s.low = 1.0;
	}
	return s;
#endif
	if (hf <= 0.0 || hf >= 1.0 || coverage <= 0.0)
		return s;
	float profile = CloudProfile(hf, type * u_Cloud[13].w);
	if (profile <= 0.0)
		return s;

	vec3 q = vec3(p.xy, h);
	s.low = textureLod(u_CloudShapeMap, q / u_Cloud[2].x + u_Cloud[3].xyz, lod.x).r;
	float cloud = clamp(CloudRemap(s.low * profile, 1.0 - s.coverage, 1.0, 0.0, 1.0), 0.0, 1.0) * s.coverage;
	if (cloud <= 0.0)
		return s;

	if (erosion && u_Cloud[2].w > 0.0)
	{
		s.high = textureLod(u_CloudDetailMap, q / u_Cloud[2].y + u_Cloud[4].xyz, lod.y).r;
		// wispy at the base, billowy at the top
		float erode = mix(s.high, 1.0 - s.high, clamp(hf * 5.0, 0.0, 1.0)) * 0.35 * u_Cloud[2].w;
		cloud = clamp(CloudRemap(cloud, erode, 1.0, 0.0, 1.0), 0.0, 1.0);
	}
	s.extinction = cloud * u_Cloud[1].x;
	return s;
}

// Extinction at a cloud space point p of altitude h. lod: shape, detail, weather mip levels.
CloudDensitySample CloudDensity(vec3 p, float h, vec3 lod, bool erosion)
{
	vec2 weather = vec2(0.0);
	float coverage = 0.0;
	float hf = CloudHeightFraction(h);
	if (hf > 0.0 && hf < 1.0)
	{
		weather = CloudWeather(p.xy, lod.z);
		coverage = CloudCoverage(weather, p.xy);
	}
	return CloudDensityCovered(p, h, lod, erosion, coverage, weather.g);
}

// mip levels of the shape, detail and weather textures for samples spaced `step` km apart
vec3 CloudLods(float step)
{
	vec3 texel = vec3(u_Cloud[2].x / 128.0, u_Cloud[2].y / 32.0, u_Cloud[2].z / 512.0);
	return max(log2(max(vec3(step) / texel, vec3(1e-6))), vec3(0.0));
}

// --- phase ----------------------------------------------------------------------------------------------------

float CloudHG(float g, float mu)
{
	float g2 = g * g;
	return (1.0 - g2) / (4.0 * CLOUD_PI * pow(max(1.0 + g2 - 2.0 * g * mu, 1e-5), 1.5));
}

// dual lobe Henyey-Greenstein, the lobes' g scaled for the multiple scattering octaves
float CloudPhase(float mu, float gScale)
{
	return mix(CloudHG(u_Cloud[1].z * gScale, mu), CloudHG(u_Cloud[1].y * gScale, mu), u_Cloud[1].w);
}

// --- reduced resolution helpers -------------------------------------------------------------------------------

// render target texture coordinates of a position in the low resolution view (pixels)
vec2 CloudLowToTc(vec2 lowPixel)
{
	vec2 uv = (lowPixel - u_Cloud[15].xy) / u_Cloud[15].zw;
	return u_Cloud[14].xy + uv * u_Cloud[14].zw;
}

// Bilinear fetch of a low resolution image over the texels that saw sky (distance >= 0) only. pos in low
// resolution pixels (texel centres at +0.5). false when no neighbour saw sky.
bool CloudFetchBilinear(sampler2D colorMap, sampler2D distanceMap, vec2 pos, out vec4 color, out float dist)
{
	vec2 f = pos - 0.5;
	vec2 base = floor(f);
	vec2 w = f - base;
	ivec2 lo = ivec2(u_Cloud[15].xy);
	ivec2 hi = lo + ivec2(u_Cloud[15].zw) - 1;
	color = vec4(0.0);
	dist = 0.0;
	float sum = 0.0;
	for (int i = 0; i < 4; i++)
	{
		ivec2 o = ivec2(i & 1, i >> 1);
		ivec2 c = clamp(ivec2(base) + o, lo, hi);
		float d = texelFetch(distanceMap, c, 0).r;
		if (d < 0.0)
			continue;
		float wt = (o.x == 1 ? w.x : 1.0 - w.x) * (o.y == 1 ? w.y : 1.0 - w.y);
		color += wt * texelFetch(colorMap, c, 0);
		dist += wt * d;
		sum += wt;
	}
	if (sum < 1e-5)
		return false;
	color /= sum;
	dist /= sum;
	return true;
}

vec3 CloudHeat(float x)
{
	x = clamp(x, 0.0, 1.0);
	return clamp(vec3(1.5 - abs(4.0 * x - 3.0), 1.5 - abs(4.0 * x - 2.0), 1.5 - abs(4.0 * x - 1.0)), 0.0, 1.0);
}
