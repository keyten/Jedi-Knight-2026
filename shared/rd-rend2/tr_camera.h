/*
===========================================================================
Rend2 photographic camera math -- dependency-free so the renderer and unit
tests use exactly the same convention.  See docs/rend2-physical-camera.md.
===========================================================================
*/
#pragma once

#include <algorithm>
#include <cmath>

namespace Rend2Camera {

enum CameraMode
{
	CAMERA_LEGACY = 0,
	CAMERA_PHYSICAL_MANUAL = 1,
	CAMERA_PHYSICAL_AUTO = 2
};

// ISO 12232 saturation-based speed calibration.  With a typical lens
// transmittance q=0.65, the saturation luminance is
// Lmax = 78 / (q * S * t / N^2), hence exposure = 1/(1.2 * 2^EV100).
static constexpr float kSaturationCalibration = 1.2f;

// ISO 2720 reflected-light meter calibration.  K=12.5 makes a metered
// luminance L (cd/m^2) correspond to EV100 = log2(L * 100 / K).
static constexpr float kReflectedLightMeterK = 12.5f;

inline float PositiveFinite(float value, float fallback)
{
	return std::isfinite(value) && value > 0.0f ? value : fallback;
}

inline float EV100(float iso, float aperture, float shutterSeconds)
{
	iso = PositiveFinite(iso, 100.0f);
	aperture = PositiveFinite(aperture, 16.0f);
	shutterSeconds = PositiveFinite(shutterSeconds, 1.0f / 125.0f);
	return std::log2(aperture * aperture / shutterSeconds * 100.0f / iso);
}

inline float EV100FromLuminance(float luminanceCdM2,
	float meterK = kReflectedLightMeterK)
{
	luminanceCdM2 = PositiveFinite(luminanceCdM2, 1.0e-6f);
	meterK = PositiveFinite(meterK, kReflectedLightMeterK);
	return std::log2(luminanceCdM2 * 100.0f / meterK);
}

inline float PhysicalExposure(float ev100,
	float saturationCalibration = kSaturationCalibration)
{
	saturationCalibration = PositiveFinite(saturationCalibration,
		kSaturationCalibration);
	return 1.0f / (saturationCalibration * std::exp2(ev100));
}

inline float SceneExposure(float ev100, float physicalToScene,
	float exposureCompensationEV = 0.0f)
{
	physicalToScene = PositiveFinite(physicalToScene, 1.0e-4f);
	return PhysicalExposure(ev100) * std::exp2(exposureCompensationEV) /
		physicalToScene;
}

inline float AutoEV100(float luminanceCdM2, float minEV100, float maxEV100)
{
	if (minEV100 > maxEV100)
		std::swap(minEV100, maxEV100);
	return std::max(minEV100, std::min(EV100FromLuminance(luminanceCdM2),
		maxEV100));
}

inline float GreyCardLuminanceForEV100(float ev100,
	float meterK = kReflectedLightMeterK)
{
	meterK = PositiveFinite(meterK, kReflectedLightMeterK);
	return meterK * std::exp2(ev100) / 100.0f;
}

} // namespace Rend2Camera
