/*[Fragment]*/
// Shared screen-space library of the ssr_*.glsl (screen-space reflections, tr_ssr.cpp) and ssgi_*.glsl
// (screen-space diffuse GI, tr_ssgi.cpp) programs; the shared resources are owned by tr_screenspace.cpp.
//
// This file is not a program on its own: its fragment block is inserted into the fragment shaders of
// the SSR and SSGI programs, see GLSL_LoadGPUProgramScreenSpace.
//
// View space: x right, y up, z forward (positive linear depth), as in the screen-space AO passes.
//
// Attachments of renderFbo, written by the opaque lightall stages:
//   u_SSRNormalMap    RGB10_A2  rg = octahedral world normal, b = roughness, a = SSR receiver (shared:
//                               with USE_SSGI every lit opaque lightall fragment writes its normal)
//   u_SSRSpecularMap  RGB10_A2  rgb = sqrt(W), W = specular IBL weight (F0 * EnvBRDF.x + EnvBRDF.y), SSR
//   u_SSRCubemapMap   RGBA16F   rgb = cubemap reflection C that lightall added, a = view depth, SSR
//   u_SSGIAlbedoMap   RGBA8     rgb = sRGB encoded diffuse albedo (no diffuse lobe of metals), a = GI receiver
//   u_SSGIRadianceMap RGBA16F   rgb = linear GI source radiance (dynamic diffuse / emissive), a = view depth
//
// u_SSRHiZMap mip 0 holds the linear view depth of every pixel (SSR_DEPTH_VIEWMODEL for first person
// surfaces drawn with a hacked depth range, SSR_DEPTH_SKY where nothing was drawn), mips 1.. the
// closest depth of the 2x2 texels below (ignoring the view model).

uniform sampler2D u_ScreenDepthMap;    // hardware depth
uniform sampler2D u_SSGIAlbedoMap;
uniform sampler2D u_SSGIRadianceMap;
uniform sampler2D u_SSGISourceMap;     // GI source radiance at the hit points (half resolution, mips)
uniform sampler2D u_SSRNormalMap;
uniform sampler2D u_SSRSpecularMap;
uniform sampler2D u_SSRCubemapMap;
uniform sampler2D u_SSRSceneMap;       // opaque scene color pyramid
uniform sampler2D u_SSRTraceMap;       // input of the pass
uniform sampler2D u_SSRHistoryMap;
uniform sampler2D u_SSRHistoryGeomMap;
uniform sampler2D u_SSRHiZMap;
uniform sampler2D u_VelocityMap;
uniform sampler2D u_SSRHitMap;        // SSR ray hits of this frame (trace resolution)
uniform sampler2D u_SSRPrevHitMap;    // SSR ray hits of the previous frame (hit cache)

uniform vec4 u_SSRProjection;   // P[0], P[5], P[8], P[9]
uniform vec4 u_SSRDepthParams;  // P[14], P[10], zFar, view space size of one pixel at depth 1
uniform vec4 u_SSRViewport;     // view rectangle in texture coordinates
uniform vec4 u_SSRTexelSize;    // 1 / source size, 1 / destination size
uniform vec4 u_SSRSettings;     // pass specific
uniform vec4 u_SSRSettings2;    // pass specific
uniform vec4 u_SSRSettings3;    // pass specific
uniform vec4 u_SSRSettings4;    // pass specific
uniform mat4 u_SSRWorldToView;
uniform mat4 u_SSRReproject;    // view space -> previous frame clip space
uniform mat4 u_SSRPrevViewToView; // previous frame view space -> view space

// RF_DEPTHHACK surfaces (first person weapon) are drawn with glDepthRange(0, 0.3)
#define SSR_DEPTH_HACK_MAX 0.3001
#define SSR_DEPTH_VIEWMODEL -1.0
#define SSR_DEPTH_SKY 1.0e20

vec3 SSRViewPosition(vec2 uv, float z)
{
	vec2 ndc = (uv - u_SSRViewport.xy) / u_SSRViewport.zw * 2.0 - 1.0;
	return vec3((ndc + u_SSRProjection.zw) * z / u_SSRProjection.xy, z);
}

vec2 SSRProjectToUV(vec3 p)
{
	vec2 ndc = p.xy * u_SSRProjection.xy / p.z - u_SSRProjection.zw;
	return (ndc * 0.5 + 0.5) * u_SSRViewport.zw + u_SSRViewport.xy;
}

bool SSRInsideView(vec2 uv)
{
	vec2 rel = (uv - u_SSRViewport.xy) / u_SSRViewport.zw;
	return all(greaterThanEqual(rel, vec2(0.0))) && all(lessThan(rel, vec2(1.0)));
}

vec2 SSREncodeNormal(vec3 n)
{
	n /= abs(n.x) + abs(n.y) + abs(n.z);
	vec2 e = n.xy;
	if (n.z < 0.0)
		e = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
	return e * 0.5 + 0.5;
}

vec3 SSRDecodeNormal(vec2 e)
{
	e = e * 2.0 - 1.0;
	vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
	float t = max(-n.z, 0.0);
	n.x += n.x >= 0.0 ? -t : t;
	n.y += n.y >= 0.0 ? -t : t;
	return normalize(n);
}

vec3 SSRViewNormal(vec2 encoded)
{
	return normalize(mat3(u_SSRWorldToView) * SSRDecodeNormal(encoded));
}

bool SSRIsSurface(float z)
{
	return z > 0.0 && z < SSR_DEPTH_SKY * 0.5;
}

// Pixel that gets screen-space reflections: an opaque PBR surface whose
// material data belongs to the visible surface (the stored view depth
// matches the depth buffer: not stale, not a hacked depth range)
bool SSRIsReceiver(ivec2 pix, float z, out vec4 normalRoughness)
{
	normalRoughness = texelFetch(u_SSRNormalMap, pix, 0);
	if (normalRoughness.a < 0.5 || !SSRIsSurface(z))
		return false;

	float storedDepth = texelFetch(u_SSRCubemapMap, pix, 0).a;
	return abs(storedDepth - z) <= 0.02 * z + 1.0;
}

// Pixel that gets screen-space GI: an opaque lightall surface (any material)
// whose data belongs to the visible surface (as SSRIsReceiver). N = world normal.
bool SSGIIsReceiver(ivec2 pix, float z, out vec3 N)
{
	N = vec3(0.0, 0.0, 1.0);
	if (!SSRIsSurface(z) || texelFetch(u_SSGIAlbedoMap, pix, 0).a < 0.5)
		return false;

	float storedDepth = texelFetch(u_SSGIRadianceMap, pix, 0).a;
	if (abs(storedDepth - z) > 0.02 * z + 1.0)
		return false;

	N = SSRDecodeNormal(texelFetch(u_SSRNormalMap, pix, 0).rg);
	return true;
}

vec3 SSGISRGBToLinear(vec3 color)
{
	color = max(color, vec3(0.0));
	vec3 lo = color * (1.0 / 12.92);
	vec3 hi = pow((color + vec3(0.055)) * (1.0 / 1.055), vec3(2.4));
	return mix(lo, hi, greaterThan(color, vec3(0.04045)));
}

vec3 SSGILinearToSRGB(vec3 color)
{
	color = max(color, vec3(0.0));
	vec3 lo = 12.92 * color;
	vec3 hi = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
	return mix(lo, hi, greaterThanEqual(color, vec3(0.0031308)));
}

vec3 SSRSpecularWeight(ivec2 pix)
{
	vec3 w = texelFetch(u_SSRSpecularMap, pix, 0).rgb;
	return w * w;
}

/*
SSR trace texels (RGBA16, tr_ssr.cpp): xy = hit uv, z = SSREncodeHitDepth(hit view depth), w = confidence
(0 = miss). The lowest bit of w flags a hit reused from the previous frame (hit cache, r_ssrDebug 12).
The hit depth is log encoded over [1, zFar]: 16 bits keep it within ~0.02% everywhere.
*/
float SSREncodeHitDepth(float z)
{
	return clamp(log2(max(z, 1.0)) / log2(max(u_SSRDepthParams.z, 2.0)), 0.0, 1.0);
}

float SSRDecodeHitDepth(float e)
{
	return exp2(e * log2(max(u_SSRDepthParams.z, 2.0)));
}

// view space hit point of a trace texel
vec3 SSRHitPosition(vec4 hit)
{
	return SSRViewPosition(hit.xy, SSRDecodeHitDepth(hit.z));
}

float SSREncodeConfidence(float confidence, bool reused)
{
	float q = floor(clamp(confidence, 0.0, 1.0) * 32767.0) * 2.0 + (reused ? 1.0 : 0.0);
	return q / 65535.0;
}

bool SSRHitReused(float w)
{
	return (int(w * 65535.0 + 0.5) & 1) != 0;
}

// full resolution pixel that half resolution trace texel q traces: 2q + phase, the phase rotates over
// the frames with the temporal accumulation (u_SSR... phase uniform of the pass, 0 or 1 per axis)
ivec2 SSRTracePixel(ivec2 q, float gridScale, vec2 phase, ivec2 fullSize)
{
	if (gridScale < 1.5)
		return q;
	return min(q * 2 + ivec2(phase), fullSize - ivec2(1));
}

float SSRInterleavedGradientNoise(vec2 pixel)
{
	return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

float SSRLuma(vec3 color)
{
	return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// tangent of the half angle of the reflection lobe: GGX alpha (roughness^2,
// as in the cubemap prefilter) as a Phong lobe (power 2 / alpha^2 - 2),
// angle holding most of its energy
float SSRConeTangent(float roughness)
{
	float a = max(roughness * roughness, 1.0e-3);
	float power = 2.0 / (a * a) - 2.0;
	float cosAngle = pow(0.244, 1.0 / (power + 1.0));
	return sqrt(max(1.0 - cosAngle * cosAngle, 0.0)) / cosAngle;
}

/*
Shared ray march (SSR reflection rays, SSGI diffuse rays). The caller sets up the ray with
SSRSetupRay and marches it with SSRMarchRay; the pass specific parameters (steps, thickness, step
distribution, Hi-Z budget) are arguments, so SSR and SSGI keep separate settings.

The view space ray O -> E is projected to the screen: its pixel position and 1 / depth are linear in
screen space (McGuire and Mara 2014), g_S0 + g_D * s and mix(g_k0, g_k1, s), s in [0, 1].
USE_HIZ walks the closest depth mips instead of stepping evenly: cells the ray passes entirely in front
of are skipped at once, the walk only descends to single pixels near surfaces.
*/

#define SSR_MAX_LINEAR_STEPS 256
#define SSR_MAX_HIZ_ITERATIONS 1024
#define SSR_MAX_REFINE_STEPS 16

vec2 g_S0;
vec2 g_D;
float g_k0;
float g_k1;

vec2 SSRRayPixel(float s)
{
	return g_S0 + g_D * s;
}

float SSRRayDepth(float s)
{
	return 1.0 / mix(g_k0, g_k1, s);
}

float SSRSceneDepth(vec2 pixel)
{
	return texelFetch(u_SSRHiZMap, ivec2(pixel), 0).r;
}

// the depth buffer only has the front of the surfaces, assume this much
// behind them is solid (grows with the distance, where depth is less precise)
float SSRThickness(float z, float thickness)
{
	return thickness * (1.0 + z * (1.0 / 512.0));
}

// Projects the view space segment O -> E (both in front of the near plane) and clips it to the view
// rectangle (pixel centers). u_SSRTexelSize.xy must be 1 / full resolution size.
// sMin skips the pixels of the surface itself. False: the ray covers (almost) no pixels.
bool SSRSetupRay(vec3 O, vec3 E, out float sMin, out float sMax, out float screenLength)
{
	vec2 invTexel = 1.0 / u_SSRTexelSize.xy;
	g_S0 = SSRProjectToUV(O) * invTexel;
	vec2 S1 = SSRProjectToUV(E) * invTexel;
	g_D = S1 - g_S0;
	g_k0 = 1.0 / O.z;
	g_k1 = 1.0 / E.z;

	vec2 viewMin = u_SSRViewport.xy * invTexel + 0.5;
	vec2 viewMax = (u_SSRViewport.xy + u_SSRViewport.zw) * invTexel - 0.5;
	sMax = 1.0;
	if (S1.x > viewMax.x) sMax = min(sMax, (viewMax.x - g_S0.x) / g_D.x);
	if (S1.x < viewMin.x) sMax = min(sMax, (viewMin.x - g_S0.x) / g_D.x);
	if (S1.y > viewMax.y) sMax = min(sMax, (viewMax.y - g_S0.y) / g_D.y);
	if (S1.y < viewMin.y) sMax = min(sMax, (viewMin.y - g_S0.y) / g_D.y);

	screenLength = length(g_D);
	sMin = min(1.5 / max(screenLength, 1.0e-6), sMax);
	return screenLength * sMax >= 2.0;
}

// First crossing of a depth buffer surface (within its assumed thickness), refined by a binary search.
//   steps:       linear march steps (ignored by USE_HIZ)
//   stepPower:   linear step distribution, 1 = even, > 1 = denser near the origin
//   thickness:   assumed surface thickness at depth 0 (world units), see SSRThickness
//   refineSteps: binary search steps
//   hizLevel:    coarsest Hi-Z level, hizIterations: Hi-Z iteration budget (USE_HIZ)
// Returns the ray parameter of the hit in sHit.
bool SSRMarchRay(
	float sMin, float sMax, float screenLength, float jitter,
	float steps, float stepPower, float thickness, float refineSteps,
	float hizLevel, float hizIterations, out float sHit)
{
	bool hit = false;
	float lo = 0.0;
	float hi = 0.0;
	sHit = 0.0;

#if !defined(USE_HIZ)
	steps = min(steps, screenLength * sMax);
	float sPrev = sMin;
	float zPrev = SSRRayDepth(sPrev);
	for (int i = 1; i <= SSR_MAX_LINEAR_STEPS; i++)
	{
		if (float(i) > steps)
			break;

		float s;
		if (stepPower == 1.0)
			s = sMin + (sMax - sMin) * (float(i) - 1.0 + jitter) / steps;
		else
			s = sMin + (sMax - sMin) * pow((float(i) - 1.0 + jitter) / steps, stepPower);
		float zRay = SSRRayDepth(s);
		float zScene = SSRSceneDepth(SSRRayPixel(s));
		if (SSRIsSurface(zScene))
		{
			float zNear = min(zPrev, zRay);
			float zFar = max(zPrev, zRay);
			if (zFar >= zScene && zNear <= zScene + SSRThickness(zScene, thickness))
			{
				hit = true;
				lo = sPrev;
				hi = s;
				break;
			}
		}

		sPrev = s;
		zPrev = zRay;
	}
#else
	int maxLevel = int(hizLevel);
	int level = 0;
	float sEps = 0.02 / screenLength;
	float s = sMin + jitter / screenLength;
	vec2 dirStep = vec2(g_D.x >= 0.0 ? 1.0 : 0.0, g_D.y >= 0.0 ? 1.0 : 0.0);
	for (int i = 0; i < SSR_MAX_HIZ_ITERATIONS; i++)
	{
		if (float(i) >= hizIterations || s >= sMax)
			break;

		float cellSize = exp2(float(level));
		vec2 cell = floor(SSRRayPixel(s) / cellSize);

		// where the ray leaves the cell
		vec2 boundary = (cell + dirStep) * cellSize;
		float sExitX = g_D.x != 0.0 ? (boundary.x - g_S0.x) / g_D.x : 1.0e30;
		float sExitY = g_D.y != 0.0 ? (boundary.y - g_S0.y) / g_D.y : 1.0e30;
		float sExit = min(min(sExitX, sExitY), sMax);

		ivec2 levelMax = textureSize(u_SSRHiZMap, level) - ivec2(1);
		float cellZ = texelFetch(u_SSRHiZMap, clamp(ivec2(cell), ivec2(0), levelMax), level).r;
		float zA = SSRRayDepth(s);
		float zB = SSRRayDepth(sExit);
		float zNear = min(zA, zB);
		float zFar = max(zA, zB);

		if (level > 0)
		{
			if (zFar < cellZ)
			{
				// entirely in front of everything in the cell
				s = sExit + sEps;
				level = min(level + 1, maxLevel);
			}
			else
			{
				level--;
			}
		}
		else
		{
			if (SSRIsSurface(cellZ) && zFar >= cellZ && zNear <= cellZ + SSRThickness(cellZ, thickness))
			{
				hit = true;
				lo = s;
				hi = sExit;
				break;
			}

			// in front: coarser again. Behind a surface: keep walking pixels
			if (zFar < cellZ || !SSRIsSurface(cellZ))
				level = min(level + 1, maxLevel);
			s = sExit + sEps;
		}
	}
#endif

	if (!hit)
		return false;

	// binary search of the crossing
	for (int i = 0; i < SSR_MAX_REFINE_STEPS; i++)
	{
		if (float(i) >= refineSteps)
			break;

		float mid = 0.5 * (lo + hi);
		float zScene = SSRSceneDepth(SSRRayPixel(mid));
		if (SSRIsSurface(zScene) && SSRRayDepth(mid) >= zScene)
			hi = mid;
		else
			lo = mid;
	}

	sHit = hi;
	return true;
}

// Shared cone-footprint radiance resolve. Mip 0 has unit coverage; coarser
// mips contain premultiplied coverage (view model / outside-view rejection).
vec4 SSRHitRadiance(vec4 hit, vec3 P, float coneTangent, float maxMip)
{
	vec3 Q = SSRHitPosition(hit);
	float footprint = 2.0 * length(Q - P) * coneTangent /
		(max(Q.z, 1.0) * u_SSRDepthParams.w);
	float mip = clamp(log2(max(footprint, 1.0)), 0.0, maxMip);
	vec4 c = textureLod(u_SSRSceneMap, hit.xy, max(mip, 1.0));
	if (mip < 1.0)
		c = mix(vec4(textureLod(u_SSRSceneMap, hit.xy, 0.0).rgb, 1.0), c, mip);
	float confidence = hit.w * smoothstep(0.2, 0.6, c.a);
	return vec4(c.rgb / max(c.a, 1e-3) * confidence, confidence);
}
