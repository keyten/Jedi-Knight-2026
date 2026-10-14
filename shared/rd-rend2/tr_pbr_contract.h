/*
===========================================================================
Rend2 scene-referred PBR contract -- dependency-free math shared by the
renderer and unit tests.  See docs/rend2-physical-pbr.md.
===========================================================================
*/
#pragma once

#include <algorithm>
#include <cmath>

namespace Rend2PBR {

// 64 JA units is the standing player collision height.  At the default scale
// it is 1.92 m; the 36-unit eye height above the origin is 1.08 m and the eye
// is 1.80 m above the feet.  Geometry remains in JA units.
static constexpr float kDefaultWorldUnitsPerMeter = 33.3333333333f;

// Physical luminance/illuminance/intensity is scaled before RGBA16F storage.
// 100,000 lux -> 10 scene units and 2,000 cd/m^2 -> 0.2 scene units.
static constexpr float kDefaultPhysicalToScene = 1.0e-4f;
static constexpr float kDielectricF0 = 0.04f;
static constexpr float kMinGgxAlpha = 0.0025f;
static constexpr float kLumaR = 0.2126f;
static constexpr float kLumaG = 0.7152f;
static constexpr float kLumaB = 0.0722f;
static constexpr float kFourPi = 12.566370614359172f;

inline float FinitePositive(float value, float fallback)
{
	return std::isfinite(value) && value > 0.0f ? value : fallback;
}

inline float MetersPerWorldUnit(float worldUnitsPerMeter)
{
	return 1.0f / FinitePositive(worldUnitsPerMeter, kDefaultWorldUnitsPerMeter);
}

inline float WorldDistanceToMeters(float distance, float worldUnitsPerMeter)
{
	return std::max(std::isfinite(distance) ? distance : 0.0f, 0.0f) *
		MetersPerWorldUnit(worldUnitsPerMeter);
}

inline float PhysicalToScene(float value, float scale)
{
	if (!std::isfinite(value) || value <= 0.0f)
		return 0.0f;
	return value * FinitePositive(scale, kDefaultPhysicalToScene);
}

inline float SceneToPhysical(float value, float scale)
{
	if (!std::isfinite(value) || value <= 0.0f)
		return 0.0f;
	return value / FinitePositive(scale, kDefaultPhysicalToScene);
}

inline float Luminance(const float rgb[3])
{
	if (!rgb)
		return 0.0f;
	const float y = kLumaR * std::max(rgb[0], 0.0f) +
		kLumaG * std::max(rgb[1], 0.0f) +
		kLumaB * std::max(rgb[2], 0.0f);
	return std::isfinite(y) ? std::max(y, 0.0f) : 0.0f;
}

// RGB is chromaticity, not three independent intensities.  The returned RGB
// has Rec.709 luminance Y=1, so scalar photometric Y remains unambiguous.
inline bool NormalizeChromaticity(const float rgb[3], float out[3])
{
	const float y = Luminance(rgb);
	if (!(y > 1.0e-6f))
	{
		out[0] = out[1] = out[2] = 0.0f;
		return false;
	}
	out[0] = std::max(rgb[0], 0.0f) / y;
	out[1] = std::max(rgb[1], 0.0f) / y;
	out[2] = std::max(rgb[2], 0.0f) / y;
	return true;
}

inline float LumensToOmniCandela(float lumens)
{
	return std::isfinite(lumens) ? std::max(lumens, 0.0f) / kFourPi : 0.0f;
}

inline float PerceptualRoughnessToAlpha(float roughness)
{
	if (!std::isfinite(roughness))
		return 1.0f;
	roughness = std::max(0.0f, std::min(1.0f, roughness));
	return std::max(roughness * roughness, kMinGgxAlpha);
}

inline float LegacyAlphaToPerceptualRoughness(float legacyAlpha)
{
	if (!std::isfinite(legacyAlpha))
		return 1.0f;
	return std::sqrt(std::max(0.0f, std::min(1.0f, legacyAlpha)));
}

inline float D_GGX(float NoH, float alpha)
{
	NoH = std::max(0.0f, std::min(1.0f, NoH));
	alpha = std::max(FinitePositive(alpha, kMinGgxAlpha), kMinGgxAlpha);
	const float alpha2 = alpha * alpha;
	const float d = NoH * NoH * (alpha2 - 1.0f) + 1.0f;
	return alpha2 / std::max(3.14159265358979323846f * d * d, 1.0e-12f);
}

inline float V_SmithGGXCorrelated(float alpha, float NoV, float NoL)
{
	alpha = std::max(FinitePositive(alpha, kMinGgxAlpha), kMinGgxAlpha);
	NoV = std::max(0.0f, std::min(1.0f, NoV));
	NoL = std::max(0.0f, std::min(1.0f, NoL));
	const float alpha2 = alpha * alpha;
	const float lambdaV = NoL * std::sqrt(NoV * NoV * (1.0f - alpha2) + alpha2);
	const float lambdaL = NoV * std::sqrt(NoL * NoL * (1.0f - alpha2) + alpha2);
	return 0.5f / std::max(lambdaV + lambdaL, 1.0e-6f);
}

inline float FresnelSchlick(float F0, float F90, float VoH)
{
	VoH = std::max(0.0f, std::min(1.0f, VoH));
	const float x = 1.0f - VoH;
	const float x2 = x * x;
	return F0 + (F90 - F0) * x2 * x2 * x;
}

// Converts the old radius-driven point light once at ingestion.  At half the
// old radius its unoccluded irradiance equals legacyReferenceScene.
inline float LegacyRadiusToSceneCandela(float radiusWorld, float worldUnitsPerMeter,
	float legacyReferenceScene)
{
	const float d = 0.5f * WorldDistanceToMeters(radiusWorld, worldUnitsPerMeter);
	const float reference = FinitePositive(legacyReferenceScene, 1.0f);
	// The canonical point evaluator also applies (1 - x^4)^2. Compensating
	// its x=0.5 value makes the migration anchor exactly equal to reference.
	const float halfRangeWindow = 0.87890625f;
	return d * d * reference / halfRangeWindow;
}

inline float PointIrradiance(float sceneCandela, float distanceWorld,
	float rangeWorld, float worldUnitsPerMeter)
{
	const float d = WorldDistanceToMeters(distanceWorld, worldUnitsPerMeter);
	const float range = std::max(std::isfinite(rangeWorld) ? rangeWorld : 0.0f, 1.0e-3f);
	const float x = std::max(distanceWorld, 0.0f) / range;
	const float windowBase = std::max(1.0f - x * x * x * x, 0.0f);
	const float window = windowBase * windowBase;
	return std::max(std::isfinite(sceneCandela) ? sceneCandela : 0.0f, 0.0f) *
		window / std::max(d * d, 1.0e-4f);
}

// A stable, bounded map-load calibration.  Relative baked lighting is never
// changed within a map.  p95=1 (ordinary normalized stock data) maps to 1.
inline float LegacyMapCalibration(float p50, float p95)
{
	(void)p50; // retained in the interface for versioned future estimators
	if (!std::isfinite(p95) || p95 <= 1.0e-4f)
		return 1.0f;
	return std::max(0.25f, std::min(4.0f, 1.0f / p95));
}

} // namespace Rend2PBR
