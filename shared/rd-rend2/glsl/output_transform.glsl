/*[Fragment]*/
// Shared HDR -> display output transform.
//
// This file is not a program on its own: its fragment block is inserted into the fragment shaders of
// tonemap.glsl (main view) and refraction.glsl (refractive surfaces, which sample the HDR scene buffer
// after the main tone map pass), so both paths always apply the same exposure, tone mapping operator
// and display encoding. See GLSL_LoadGPUShader's fragmentLibrary argument.
//
// USE_LINEAR_LIGHT is defined when the HDR buffer holds scene-linear values (maps with HDR lightmaps).
// Otherwise the buffer holds display-encoded values, because non-HDR maps are lit in gamma space, and
// the scene-referred operators decode it before applying exposure.

#define TONEMAP_LEGACY 0
#define TONEMAP_ACES   1
#define TONEMAP_AGX    2

#define CAMERA_LEGACY          0
#define CAMERA_PHYSICAL_MANUAL 1
#define CAMERA_PHYSICAL_AUTO   2

#define TONEMAP_DEBUG_NONE        0
#define TONEMAP_DEBUG_SPLIT       1
#define TONEMAP_DEBUG_SPLIT3      2
#define TONEMAP_DEBUG_RAW         3
#define TONEMAP_DEBUG_EXPOSED     4
#define TONEMAP_DEBUG_FALSE_COLOR 5

// Display gamma used to convert metering done on display-encoded buffers to scene-linear exposure
#define DISPLAY_GAMMA 2.2
#define PHYSICAL_LOG_LUMINANCE_RANGE 24.0

const vec3 TONEMAP_LUMINANCE = vec3(0.2126, 0.7152, 0.0722);

vec3 LinearTosRGB( in vec3 color )
{
	vec3 lo = 12.92 * color;
	vec3 hi = 1.055 * pow(color, vec3(0.4166666)) - 0.055;
	return mix(lo, hi, greaterThanEqual(color, vec3(0.0031308)));
}

// Inverse of LinearTosRGB. Values above 1.0 continue along the power segment,
// so over-bright HDR values are decoded as well.
vec3 sRGBToLinear( in vec3 color )
{
	color = max(color, vec3(0.0));
	vec3 lo = color * (1.0 / 12.92);
	vec3 hi = pow((color + vec3(0.055)) * (1.0 / 1.055), vec3(2.4));
	return mix(lo, hi, greaterThan(color, vec3(0.04045)));
}

// Bloom is accumulated in scene-linear HDR before the shared output transform.
// Legacy gamma-space scene buffers are decoded only for this addition and encoded
// back to their original domain so the existing legacy tonemapper stays unchanged.
vec3 AddBloomToScene(vec3 scene, vec3 bloom)
{
#if defined(USE_LINEAR_LIGHT)
	return scene + bloom;
#else
	return LinearTosRGB(sRGBToLinear(scene) + bloom);
#endif
}

//
// Legacy Rend2 operator: John Hable's filmic curve, normalized so that
// toneMax - toneMin maps to white. Unchanged from the original tonemap.glsl.
//
vec3 FilmicTonemap(vec3 x)
{
	const float SS  = 0.22; // Shoulder Strength
	const float LS  = 0.30; // Linear Strength
	const float LA  = 0.10; // Linear Angle
	const float TS  = 0.20; // Toe Strength
	const float TAN = 0.01; // Toe Angle Numerator
	const float TAD = 0.30; // Toe Angle Denominator

	vec3 SSxx = SS * x * x;
	vec3 LSx = LS * x;
	vec3 LALSx = LSx * LA;

	return ((SSxx + LALSx + TS * TAN) / (SSxx + LSx + TS * TAD)) - TAN / TAD;

	//return ((x*(SS*x+LA*LS)+TS*TAN)/(x*(SS*x+LS)+TS*TAD)) - TAN/TAD;
}

vec3 LegacyToneMap(vec3 color, float avgLum, vec3 toneMinAvgMaxLinear)
{
	color *= toneMinAvgMaxLinear.y / avgLum;
	color = max(vec3(0.0), color - vec3(toneMinAvgMaxLinear.x));

	vec3 fWhite = 1.0 / FilmicTonemap(vec3(toneMinAvgMaxLinear.z - toneMinAvgMaxLinear.x));
	color = FilmicTonemap(color) * fWhite;

#if defined(USE_LINEAR_LIGHT)
	color = LinearTosRGB(color);
#endif

	return color;
}

// The legacy filmic curve without its historical average-luminance exposure.
// Physical modes feed it already-exposed scene-linear light, so this variant
// prevents a second, arbitrary camera gain from being stacked on top.
vec3 LegacyToneMapExposed(vec3 exposed, vec3 toneMinAvgMaxLinear)
{
	vec3 color = max(vec3(0.0), exposed - vec3(toneMinAvgMaxLinear.x));
	vec3 fWhite = 1.0 / FilmicTonemap(vec3(
		max(toneMinAvgMaxLinear.z - toneMinAvgMaxLinear.x, 1e-6)));
	return LinearTosRGB(FilmicTonemap(color) * fWhite);
}

//
// ACES fitted. This is Stephen Hill's fit of the ACES RRT + sRGB/Rec.709 100 nit ODT, an approximation
// of the reference ACES output transform rather than the full ACES pipeline. Input is scene-linear with
// Rec.709/sRGB primaries (D65), output is display-linear sRGB clamped to [0, 1].
//
// The ACES code in this file was originally written by Stephen Hill (@self_shadow), who deserves all
// credit for coming up with this fit and implementing it. Buy him a beer next time you see him. :)

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
const mat3 ACESInputMat = mat3
(
    vec3(0.59719, 0.35458, 0.04823),
    vec3(0.07600, 0.90834, 0.01566),
    vec3(0.02840, 0.13383, 0.83777)
);

// ODT_SAT => XYZ => D60_2_D65 => sRGB
const mat3 ACESOutputMat = mat3
(
    vec3( 1.60475, -0.53108, -0.07367),
    vec3(-0.10208,  1.10813, -0.00605),
    vec3(-0.00327, -0.07276,  1.07602)
);

vec3 RRTAndODTFit(vec3 v)
{
    vec3 a = v * (v + 0.0245786f) - 0.000090537f;
    vec3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

vec3 ACESFitted(vec3 color)
{
    color = color * ACESInputMat;

    // Apply RRT and ODT
    color = RRTAndODTFit(color);

    color = color * ACESOutputMat;

    // Clamp to [0, 1]
    color = clamp(color, 0.0, 1.0);

    return color;
}

//
// AgX-like. A compact approximation of Troy Sobotka's AgX base transform: inset matrix, log2 encoding of
// middle grey (0.18) -10 / +6.5 stops, the default contrast sigmoid approximated by a 6th order polynomial
// (Benjamin Wrensch, "Minimal AgX Implementation", MIT license) and the inverse inset matrix as outset.
// It is not the exact Blender/OCIO AgX: no LUT, no looks and a simplified outset. Input is scene-linear
// Rec.709/sRGB, output is display-linear sRGB in [0, 1].
//
const mat3 AgXInsetMat = mat3(
	0.842479062253094,  0.0423282422610123, 0.0423756549057051,
	0.0784335999999992, 0.878468636469772,  0.0784336,
	0.0792237451477643, 0.0791661274605434, 0.879142973793104);

const mat3 AgXOutsetMat = mat3(
	 1.19687900512017,   -0.0528968517574562, -0.0529716355144438,
	-0.0980208811401368,  1.15190312990417,   -0.0980434501171241,
	-0.0990297440797205, -0.0989611768448433,  1.15107367264116);

const float AgXMinEv = -12.47393; // log2(0.18) - 10
const float AgXMaxEv = 4.026069;  // log2(0.18) + 6.5

vec3 AgXContrastApprox(vec3 x)
{
	vec3 x2 = x * x;
	vec3 x4 = x2 * x2;

	return 15.5 * x4 * x2
		- 40.14 * x4 * x
		+ 31.96 * x4
		- 6.868 * x2 * x
		+ 0.4298 * x2
		+ 0.1191 * x
		- 0.00232;
}

vec3 AgXToneMap(vec3 color)
{
	color = AgXInsetMat * max(color, vec3(0.0));
	color = clamp(log2(max(color, vec3(1e-10))), AgXMinEv, AgXMaxEv);
	color = (color - AgXMinEv) / (AgXMaxEv - AgXMinEv);
	color = AgXContrastApprox(color);
	color = clamp(AgXOutsetMat * color, 0.0, 1.0);

	// The sigmoid produces display-encoded values. Decode them with the exact inverse of the final
	// encoder, so LinearTosRGB reproduces the AgX code values.
	return sRGBToLinear(color);
}

//
// Exposure
//

// Log2 of the metered average luminance, limited by the auto exposure range of the map.
// levels holds the encoded min/avg/max log luminance from the levels map.
float GetLogAverageLuminance(vec3 levels, vec2 autoExposureMinMax)
{
	vec3 logMinAvgMaxLum = clamp(levels * 20.0 - 10.0, -autoExposureMinMax.y, -autoExposureMinMax.x);
	return logMinAvgMaxLum.y;
}

// HDR buffer (already multiplied by the buffer gain) to scene-linear light
vec3 GetSceneLinear(vec3 color)
{
#if defined(USE_LINEAR_LIGHT)
	return max(color, vec3(0.0));
#else
	return sRGBToLinear(color);
#endif
}

// Scene-linear value the metered average luminance is mapped to
float GetExposureTarget(float toneAvgLinear)
{
#if defined(USE_LINEAR_LIGHT)
	return toneAvgLinear;
#else
	return pow(toneAvgLinear, DISPLAY_GAMMA);
#endif
}

// Exposure applied to scene-linear light: maps the metered average luminance to the exposure target
// and applies exposure compensation.
float GetSceneExposure(float logAvgLum, float toneAvgLinear, float compensationGain)
{
#if defined(USE_LINEAR_LIGHT)
	float exposure = toneAvgLinear / exp2(logAvgLum);
#else
	// Metering is done on display-encoded values, so convert the ratio with the display gamma
	float exposure = exp2(DISPLAY_GAMMA * (log2(toneAvgLinear) - logAvgLum));
#endif
	return exposure * compensationGain;
}

// The meter texture stores log2 luminance of the un-decoded HDR buffer.  The
// final component of cameraParams is the buffer-encoding correction expressed
// in scene-linear stops.  It is deliberately separate from camera exposure.
float GetPhysicalLogSceneLuminance(vec3 levels, vec4 cameraParams)
{
	float logBufferLum = levels.y * (2.0 * PHYSICAL_LOG_LUMINANCE_RANGE) -
		PHYSICAL_LOG_LUMINANCE_RANGE;
#if defined(USE_LINEAR_LIGHT)
	return logBufferLum + cameraParams.w;
#else
	return DISPLAY_GAMMA * logBufferLum + cameraParams.w;
#endif
}

float GetPhysicalEV100(float logSceneLum, vec4 cameraParams, vec4 cameraAutoParams)
{
	if (int(cameraParams.x + 0.5) == CAMERA_PHYSICAL_MANUAL)
		return cameraParams.y;

	float meterK = max(cameraAutoParams.w, 1e-6);
	float ev100 = logSceneLum - log2(max(cameraParams.z, 1e-12)) +
		log2(100.0 / meterK);
	return clamp(ev100, min(cameraAutoParams.x, cameraAutoParams.y),
		max(cameraAutoParams.x, cameraAutoParams.y));
}

float GetPhysicalSceneExposure(float ev100, vec4 cameraParams,
	vec4 cameraAutoParams, float compensationGain)
{
	float sceneScale = max(cameraParams.z, 1e-12);
	float calibration = max(cameraAutoParams.z, 1e-6);
	return compensationGain / (sceneScale * calibration * exp2(ev100));
}

// Exposure zones in stops relative to the exposure target. Magenta marks pixels the operator
// maps to (or clips at) display white.
vec3 ExposureFalseColor(vec3 exposed, float exposureTarget, vec3 display)
{
	float stops = log2(max(dot(exposed, TONEMAP_LUMINANCE), 1e-6) / exposureTarget);
	vec3 zone;

	if (stops < -6.0)
		zone = vec3(0.0);
	else if (stops < -4.0)
		zone = vec3(0.25, 0.0, 0.5);
	else if (stops < -2.0)
		zone = vec3(0.0, 0.2, 1.0);
	else if (stops < -0.5)
		zone = vec3(0.0, 0.65, 0.65);
	else if (stops < 0.5)
		zone = vec3(0.5);
	else if (stops < 2.0)
		zone = vec3(0.15, 0.8, 0.15);
	else if (stops < 4.0)
		zone = vec3(1.0, 0.9, 0.0);
	else if (stops < 6.0)
		zone = vec3(1.0, 0.45, 0.0);
	else
		zone = vec3(1.0, 0.0, 0.0);

	if (any(greaterThanEqual(display, vec3(0.995))))
		zone = vec3(1.0, 0.0, 1.0);

	return zone;
}

//
// Color grading with a 3D LUT. The LUT maps display-encoded sRGB values to display-encoded sRGB
// values (domain [0, 1], red along the width of the texture), like common .cube files.
// params: x = mode (0 off, 1 on, 2 split with the original on the left), y = intensity, z = LUT size
//
vec3 ApplyColorGrading(vec3 display, sampler3D lut, vec4 params, float fragX)
{
	if (params.x < 0.5)
		return display;

	if (params.x > 1.5)
	{
		if (abs(fragX - 0.5 * r_FBufScale.x) < 1.0)
			return vec3(0.5);

		if (fragX < 0.5 * r_FBufScale.x)
			return display;
	}

	// Sample at texel centers, so 0 and 1 hit the first and last entries
	vec3 coord = clamp(display, 0.0, 1.0) * ((params.z - 1.0) / params.z) + 0.5 / params.z;
	vec3 graded = texture(lut, coord).rgb;

	return mix(display, graded, params.y);
}

//
// Output transform entry point.
//
// color:               HDR buffer multiplied by the buffer gain (u_Color)
// levels:              sample of the levels map (auto exposure or fixed)
// toneMinAvgMaxLinear: map tone parameters (u_ToneMinAvgMaxLinear)
// params:              u_ToneMapParams: x = operator (TONEMAP_*), y = debug view (TONEMAP_DEBUG_*),
//                      z = exposure compensation gain for the legacy operator (buffer domain),
//                      w = exposure compensation gain in scene-linear light
// cameraParams:        x = camera mode, y = manual EV100, z = physical-to-scene scale,
//                      w = buffer decoding compensation in scene-linear stops
// cameraAutoParams:    x/y = auto EV100 bounds, z = ISO 12232 saturation calibration,
//                      w = ISO 2720 reflected-light meter constant K
// colorGradingLut:     3D LUT, applied after the display encoding (see ApplyColorGrading)
// colorGradingParams:  u_ColorGradingParams
// fragX:               gl_FragCoord.x, used by the split screen comparisons
//
// Returns display-encoded values, not clamped yet.
//
vec3 OutputTransform(vec3 color, vec3 levels, vec2 autoExposureMinMax, vec3 toneMinAvgMaxLinear, vec4 params,
	vec4 cameraParams, vec4 cameraAutoParams, sampler3D colorGradingLut,
	vec4 colorGradingParams, float fragX)
{
	int toneMapMode = int(params.x + 0.5);
	int debugView = int(params.y + 0.5);

	if (debugView == TONEMAP_DEBUG_SPLIT)
	{
		// Legacy on the left, the selected operator on the right
		if (abs(fragX - 0.5 * r_FBufScale.x) < 1.0)
			return vec3(0.5);

		if (fragX < 0.5 * r_FBufScale.x)
			toneMapMode = TONEMAP_LEGACY;
	}
	else if (debugView == TONEMAP_DEBUG_SPLIT3)
	{
		// Legacy | ACES | AgX
		if (abs(fragX - r_FBufScale.x / 3.0) < 1.0 || abs(fragX - r_FBufScale.x * 2.0 / 3.0) < 1.0)
			return vec3(0.5);

		toneMapMode = int(clamp(floor(3.0 * fragX / r_FBufScale.x), 0.0, 2.0));
	}

	float logAvgLum = GetLogAverageLuminance(levels, autoExposureMinMax);
	int cameraMode = int(cameraParams.x + 0.5);

	if (cameraMode != CAMERA_LEGACY)
	{
		vec3 sceneLinear = GetSceneLinear(color);
		if (debugView == TONEMAP_DEBUG_RAW)
			return LinearTosRGB(clamp(sceneLinear, 0.0, 1.0));

		float logSceneLum = GetPhysicalLogSceneLuminance(levels, cameraParams);
		float ev100 = GetPhysicalEV100(logSceneLum, cameraParams, cameraAutoParams);
		float exposure = GetPhysicalSceneExposure(ev100, cameraParams,
			cameraAutoParams, params.w);
		vec3 exposed = sceneLinear * exposure;
		if (debugView == TONEMAP_DEBUG_EXPOSED)
			return LinearTosRGB(clamp(exposed, 0.0, 1.0));

		vec3 display;
		if (toneMapMode == TONEMAP_ACES)
			display = LinearTosRGB(ACESFitted(exposed));
		else if (toneMapMode == TONEMAP_AGX)
			display = LinearTosRGB(AgXToneMap(exposed));
		else
			display = LegacyToneMapExposed(exposed, toneMinAvgMaxLinear);

		if (debugView == TONEMAP_DEBUG_FALSE_COLOR)
		{
			float greyTarget = cameraAutoParams.w /
				(100.0 * cameraAutoParams.z);
			return ExposureFalseColor(exposed, greyTarget, display);
		}

		return ApplyColorGrading(display, colorGradingLut,
			colorGradingParams, fragX);
	}

	if (toneMapMode != TONEMAP_ACES && toneMapMode != TONEMAP_AGX && debugView < TONEMAP_DEBUG_RAW)
	{
		vec3 legacy = LegacyToneMap(color * params.z, exp2(logAvgLum), toneMinAvgMaxLinear);
		return ApplyColorGrading(legacy, colorGradingLut, colorGradingParams, fragX);
	}

	vec3 sceneLinear = GetSceneLinear(color);
	if (debugView == TONEMAP_DEBUG_RAW)
		return LinearTosRGB(clamp(sceneLinear, 0.0, 1.0));

	vec3 exposed = sceneLinear * GetSceneExposure(logAvgLum, toneMinAvgMaxLinear.y, params.w);
	if (debugView == TONEMAP_DEBUG_EXPOSED)
		return LinearTosRGB(clamp(exposed, 0.0, 1.0));

	vec3 display;
	if (toneMapMode == TONEMAP_ACES)
		display = LinearTosRGB(ACESFitted(exposed));
	else if (toneMapMode == TONEMAP_AGX)
		display = LinearTosRGB(AgXToneMap(exposed));
	else
		display = LegacyToneMap(color * params.z, exp2(logAvgLum), toneMinAvgMaxLinear);

	if (debugView == TONEMAP_DEBUG_FALSE_COLOR)
		return ExposureFalseColor(exposed, GetExposureTarget(toneMinAvgMaxLinear.y), display);

	return ApplyColorGrading(display, colorGradingLut, colorGradingParams, fragX);
}
