#include "rd-rend2/tr_camera.h"
#include <boost/test/unit_test.hpp>

using namespace Rend2Camera;

BOOST_AUTO_TEST_CASE(rend2_camera_ev100_known_combinations)
{
	BOOST_CHECK_SMALL(EV100(100.0f, 1.0f, 1.0f), 1.0e-6f);
	BOOST_CHECK_CLOSE(EV100(100.0f, 16.0f, 1.0f / 128.0f), 15.0f, 1.0e-4f);
	BOOST_CHECK_CLOSE(EV100FromLuminance(GreyCardLuminanceForEV100(15.0f)),
		15.0f, 1.0e-4f);
}

BOOST_AUTO_TEST_CASE(rend2_camera_one_stop_controls)
{
	const float base = SceneExposure(EV100(100.0f, 4.0f, 1.0f / 125.0f), 1.0e-4f);
	const float iso = SceneExposure(EV100(200.0f, 4.0f, 1.0f / 125.0f), 1.0e-4f);
	const float apertureOpen = SceneExposure(EV100(100.0f, 2.82842712f,
		1.0f / 125.0f), 1.0e-4f);
	const float apertureClosed = SceneExposure(EV100(100.0f, 5.65685425f,
		1.0f / 125.0f), 1.0e-4f);
	const float shutter = SceneExposure(EV100(100.0f, 4.0f, 2.0f / 125.0f),
		1.0e-4f);
	BOOST_CHECK_CLOSE(iso / base, 2.0f, 1.0e-4f);
	BOOST_CHECK_CLOSE(apertureOpen / base, 2.0f, 1.0e-3f);
	BOOST_CHECK_CLOSE(apertureClosed / base, 0.5f, 1.0e-3f);
	BOOST_CHECK_CLOSE(shutter / base, 2.0f, 1.0e-4f);
}

BOOST_AUTO_TEST_CASE(rend2_camera_exposure_compensation_is_exact_stops)
{
	const float base = SceneExposure(10.0f, 1.0e-4f, 0.0f);
	BOOST_CHECK_CLOSE(SceneExposure(10.0f, 1.0e-4f, 1.0f) / base,
		2.0f, 1.0e-4f);
	BOOST_CHECK_CLOSE(SceneExposure(10.0f, 1.0e-4f, -1.0f) / base,
		0.5f, 1.0e-4f);
}

BOOST_AUTO_TEST_CASE(rend2_camera_iso_12232_grey_card_calibration)
{
	// A Lambertian 18% card at a known 100 cd/m^2 requires about 1745 lux.
	// Its reflectance identifies the test surface; the reflected-light meter
	// operates on its measured luminance.
	const float luminance = 100.0f;
	const float illuminance = luminance * 3.14159265358979323846f / 0.18f;
	BOOST_CHECK_CLOSE(0.18f * illuminance / 3.14159265358979323846f,
		luminance, 1.0e-4f);
	const float ev = EV100FromLuminance(luminance);
	const float exposed = luminance * PhysicalExposure(ev);
	BOOST_CHECK_CLOSE(exposed,
		kReflectedLightMeterK / (100.0f * kSaturationCalibration), 1.0e-4f);
	BOOST_CHECK_CLOSE(exposed, 0.1041666667f, 1.0e-4f);
}
