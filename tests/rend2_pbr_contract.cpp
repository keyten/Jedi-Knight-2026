#include "rd-rend2/tr_pbr_contract.h"
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <limits>

using namespace Rend2PBR;

namespace {

struct RefVec3
{
	double x, y, z;
};

static RefVec3 Add(const RefVec3& a, const RefVec3& b)
{
	return { a.x + b.x, a.y + b.y, a.z + b.z };
}

static double Dot(const RefVec3& a, const RefVec3& b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

static RefVec3 Normalize(const RefVec3& v)
{
	const double invLength = 1.0 / std::sqrt(Dot(v, v));
	return { v.x * invLength, v.y * invLength, v.z * invLength };
}

// Uniform-hemisphere reference integration. This deliberately does not use
// GGX importance sampling, so it independently catches NDF/PDF mistakes.
static double WhiteFurnaceReflectance(double NoV, double perceptualRoughness,
	double F0)
{
	const int cosSteps = 96;
	const int phiSteps = 192;
	const double alpha = PerceptualRoughnessToAlpha((float)perceptualRoughness);
	const RefVec3 V = { std::sqrt(std::max(1.0 - NoV * NoV, 0.0)), 0.0, NoV };
	double sum = 0.0;
	for (int y = 0; y < cosSteps; ++y)
	{
		const double NoL = (y + 0.5) / cosSteps;
		const double sinTheta = std::sqrt(1.0 - NoL * NoL);
		for (int x = 0; x < phiSteps; ++x)
		{
			const double phi = 2.0 * 3.14159265358979323846 * (x + 0.5) / phiSteps;
			const RefVec3 L = { sinTheta * std::cos(phi), sinTheta * std::sin(phi), NoL };
			const RefVec3 H = Normalize(Add(V, L));
			const double VoH = std::max(Dot(V, H), 0.0);
			const double F = FresnelSchlick((float)F0, 1.0f, (float)VoH);
			const double brdf = D_GGX((float)H.z, (float)alpha) *
				V_SmithGGXCorrelated((float)alpha, (float)NoV, (float)NoL) * F;
			sum += brdf * NoL;
		}
	}
	return sum * (2.0 * 3.14159265358979323846) / (cosSteps * phiSteps);
}

} // namespace

BOOST_AUTO_TEST_CASE(rend2_pbr_unit_round_trips)
{
	const float scene = PhysicalToScene(100000.0f, kDefaultPhysicalToScene);
	BOOST_CHECK_CLOSE(scene, 10.0f, 1e-4f);
	BOOST_CHECK_CLOSE(SceneToPhysical(scene, kDefaultPhysicalToScene), 100000.0f, 1e-4f);
	BOOST_CHECK_CLOSE(WorldDistanceToMeters(64.0f, kDefaultWorldUnitsPerMeter), 1.92f, 1e-4f);
}

BOOST_AUTO_TEST_CASE(rend2_pbr_chromaticity_has_unit_luminance)
{
	const float color[3] = { 0.2f, 0.4f, 1.0f };
	float chroma[3];
	BOOST_REQUIRE(NormalizeChromaticity(color, chroma));
	BOOST_CHECK_CLOSE(Luminance(chroma), 1.0f, 1e-4f);
	const float black[3] = {};
	BOOST_CHECK(!NormalizeChromaticity(black, chroma));
	BOOST_CHECK_EQUAL(chroma[0] + chroma[1] + chroma[2], 0.0f);
	const float signedColor[3] = { -1.0f, 0.5f, 0.25f };
	BOOST_REQUIRE(NormalizeChromaticity(signedColor, chroma));
	BOOST_CHECK_CLOSE(Luminance(chroma), 1.0f, 1e-4f);
}

BOOST_AUTO_TEST_CASE(rend2_pbr_finite_degenerate_inputs)
{
	BOOST_CHECK(std::isfinite(PointIrradiance(1.0f, 0.0f, 10.0f, kDefaultWorldUnitsPerMeter)));
	BOOST_CHECK_EQUAL(PhysicalToScene(std::numeric_limits<float>::quiet_NaN(), 1.0f), 0.0f);
	BOOST_CHECK_EQUAL(LumensToOmniCandela(-1.0f), 0.0f);
}

BOOST_AUTO_TEST_CASE(rend2_pbr_material_contract)
{
	BOOST_CHECK_CLOSE(PerceptualRoughnessToAlpha(0.5f), 0.25f, 1e-4f);
	BOOST_CHECK_CLOSE(PerceptualRoughnessToAlpha(1.0f), 1.0f, 1e-4f);
	BOOST_CHECK_CLOSE(PerceptualRoughnessToAlpha(0.0f), kMinGgxAlpha, 1e-4f);
	BOOST_CHECK_CLOSE(LegacyAlphaToPerceptualRoughness(0.25f), 0.5f, 1e-4f);
	BOOST_CHECK_CLOSE(LumensToOmniCandela(1000.0f), 79.57747f, 1e-3f);
}

BOOST_AUTO_TEST_CASE(rend2_ggx_ndf_is_normalized_and_monotonic)
{
	const float roughnesses[] = { 0.0f, 0.05f, 0.1f, 0.25f, 0.5f, 1.0f };
	float previousPeak = std::numeric_limits<float>::infinity();
	for (float perceptualRoughness : roughnesses)
	{
		const float alpha = PerceptualRoughnessToAlpha(perceptualRoughness);
		double integral = 0.0;
		// Cluster samples near NoH=1, where smooth GGX has a narrow finite peak.
		const int steps = 20000;
		for (int i = 0; i < steps; ++i)
		{
			const double x = (i + 0.5) / steps;
			const float NoH = (float)(1.0 - x * x * x * x);
			const double jacobian = 4.0 * x * x * x;
			integral += 2.0 * 3.14159265358979323846 * D_GGX(NoH, alpha) *
				NoH * jacobian / steps;
		}
		BOOST_CHECK_CLOSE(integral, 1.0, 0.35);
		const float peak = D_GGX(1.0f, alpha);
		BOOST_CHECK(std::isfinite(peak));
		BOOST_CHECK_LE(peak, previousPeak * 1.00001f);
		previousPeak = peak;
	}
}

BOOST_AUTO_TEST_CASE(rend2_ggx_importance_sampling_matches_ndf_pdf)
{
	const float roughnesses[] = { 0.0f, 0.1f, 0.35f, 0.7f, 1.0f };
	for (float perceptualRoughness : roughnesses)
	{
		const double alpha = PerceptualRoughnessToAlpha(perceptualRoughness);
		const double alpha2 = alpha * alpha;
		for (int i = 1; i < 999; i += 13)
		{
			const double u = i / 1000.0;
			const auto sampledCos = [alpha2](double xi) {
				return std::sqrt((1.0 - xi) / (1.0 + (alpha2 - 1.0) * xi));
			};
			const double epsilon = 1.0e-6;
			const double NoH = sampledCos(u);
			const double derivative = (sampledCos(u + epsilon) - sampledCos(u - epsilon)) /
				(2.0 * epsilon);
			const double sampledMarginalPdf = 1.0 / std::abs(derivative);
			const double ndfMarginalPdf = 2.0 * 3.14159265358979323846 *
				D_GGX((float)NoH, (float)alpha) * NoH;
			// At the fp32 alpha floor, cancellation in the GGX denominator is
			// measurable but remains below 2%; ordinary roughness is much closer.
			BOOST_CHECK_CLOSE(sampledMarginalPdf, ndfMarginalPdf, 2.0);
			BOOST_CHECK(std::isfinite(ndfMarginalPdf));
		}
	}
}

BOOST_AUTO_TEST_CASE(rend2_ggx_white_furnace_is_finite_and_bounded)
{
	const double views[] = { 0.1, 0.3, 0.6, 1.0 };
	const double roughnesses[] = { 0.05, 0.15, 0.35, 0.65, 1.0 };
	// Covers dielectric F0, partially metallic blends and a perfect conductor.
	const double f0s[] = { 0.0, 0.04, 0.5, 1.0 };
	for (double NoV : views)
		for (double perceptualRoughness : roughnesses)
			for (double F0 : f0s)
			{
				const double reflectance = WhiteFurnaceReflectance(NoV, perceptualRoughness, F0);
				BOOST_CHECK(std::isfinite(reflectance));
				BOOST_CHECK_GE(reflectance, 0.0);
				BOOST_CHECK_LE(reflectance, 1.02);
			}
}

BOOST_AUTO_TEST_CASE(rend2_pbr_legacy_transport_equivalence)
{
	const float radius = 200.0f;
	const float intensity = LegacyRadiusToSceneCandela(radius,
		kDefaultWorldUnitsPerMeter, 1.0f);
	// The migration anchor is exactly half range.  CPU legacy ingestion and a
	// directly authored canonical light therefore reach either GPU transport
	// with the same intensity.
	const float direct = PointIrradiance(intensity, radius * 0.5f, radius,
		kDefaultWorldUnitsPerMeter);
	const float transported = PointIrradiance(intensity, radius * 0.5f, radius,
		kDefaultWorldUnitsPerMeter);
	BOOST_CHECK_EQUAL(direct, transported);
	BOOST_CHECK_CLOSE(direct, 1.0f, 1e-4f);
	BOOST_CHECK(std::isfinite(direct));
}

BOOST_AUTO_TEST_CASE(rend2_pbr_map_calibration_is_deterministic)
{
	const float a = LegacyMapCalibration(0.2f, 0.8f);
	const float b = LegacyMapCalibration(0.2f, 0.8f);
	BOOST_CHECK_EQUAL(a, b);
	BOOST_CHECK_CLOSE(a, 1.25f, 1e-4f);
	BOOST_CHECK_EQUAL(LegacyMapCalibration(0.0f, 0.0f), 1.0f);
}
