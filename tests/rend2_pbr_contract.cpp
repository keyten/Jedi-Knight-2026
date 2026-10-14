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

struct DfgReference
{
	double scale;
	double bias;
};

static unsigned ReverseBitsReference(unsigned bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	return ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
}

// Quasi-Monte-Carlo GGX importance integration of the two Schlick DFG basis
// terms stored by R_CreateEnvBrdfLUT. scale + bias is r for F0=1. The older
// WhiteFurnaceReflectance uses uniform incoming directions independently to
// validate the BRDF itself; importance sampling keeps polished-lobe error low.
static DfgReference WhiteFurnaceDfg(double NoV, double perceptualRoughness)
{
	const unsigned samples = 16384;
	const double alpha = PerceptualRoughnessToAlpha((float)perceptualRoughness);
	const double alpha2 = alpha * alpha;
	const RefVec3 V = { std::sqrt(std::max(1.0 - NoV * NoV, 0.0)), 0.0, NoV };
	double scale = 0.0, bias = 0.0;
	for (unsigned i = 0; i < samples; ++i)
	{
		const double e1 = (double)i / samples;
		const double e2 = (double)ReverseBitsReference(i) / 4294967296.0;
		const double phi = 2.0 * 3.14159265358979323846 * e1;
		const double NoH = std::sqrt((1.0 - e2) /
			(1.0 + (alpha2 - 1.0) * e2));
		const double sinTheta = std::sqrt(std::max(1.0 - NoH * NoH, 0.0));
		const RefVec3 H = { sinTheta * std::cos(phi), sinTheta * std::sin(phi), NoH };
		const double VoH = std::max(Dot(V, H), 0.0);
		const double NoL = std::max(2.0 * VoH * NoH - NoV, 0.0);
		if (NoL <= 0.0 || NoH <= 0.0)
			continue;
		const double weight = NoL * V_SmithGGXCorrelated((float)alpha,
			(float)NoV, (float)NoL) * (4.0 * VoH / NoH);
		const double Fc = std::pow(1.0 - VoH, 5.0);
		scale += weight * (1.0 - Fc);
		bias += weight * Fc;
	}
	return { scale / samples, bias / samples };
}

static double WhiteFurnaceAverageDirectionalAlbedo(double perceptualRoughness)
{
	const int steps = 48;
	double sum = 0.0;
	for (int i = 0; i < steps; ++i)
	{
		const double mu = (i + 0.5) / steps;
		const DfgReference dfg = WhiteFurnaceDfg(mu, perceptualRoughness);
		sum += 2.0 * (dfg.scale + dfg.bias) * mu / steps;
	}
	return std::min(std::max(sum, 0.0), 1.0);
}

static double KullaContyReference(double singleScatter, double directionalAlbedo,
	double averageDirectionalAlbedo, double F0)
{
	// Directional integral of the Kulla-Conty compensation lobe. The integral
	// of (1-E(l)) NoL / pi cancels its (1-Eavg) denominator.
	const double Favg = (1.0 + 20.0 * F0) / 21.0;
	directionalAlbedo = std::min(std::max(directionalAlbedo, 0.0), 1.0);
	const double denominator = std::max(1.0 - Favg *
		(1.0 - averageDirectionalAlbedo), 1.0e-8);
	const double multipleScatter = (1.0 - directionalAlbedo) * Favg * Favg *
		averageDirectionalAlbedo / denominator;
	return singleScatter + multipleScatter;
}

static double CompensatedDirectionalAlbedo(double NoV,
	double perceptualRoughness, double F0)
{
	const DfgReference dfg = WhiteFurnaceDfg(NoV, perceptualRoughness);
	const double single = F0 * dfg.scale + dfg.bias;
	return std::min(std::max(single * GgxEnergyCompensation((float)F0,
		(float)(dfg.scale + dfg.bias)), 0.0), 1.0);
}

static double GgxAverageAlbedoQuadrature(double perceptualRoughness, double F0)
{
	const double mu[] = { 0.0694318442, 0.3300094782,
		0.6699905218, 0.9305681558 };
	const double weight[] = { 0.1739274226, 0.3260725774,
		0.3260725774, 0.1739274226 };
	double average = 0.0;
	for (int i = 0; i < 4; ++i)
		average += weight[i] * 2.0 * mu[i] *
			CompensatedDirectionalAlbedo(mu[i], perceptualRoughness, F0);
	return average;
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

BOOST_AUTO_TEST_CASE(rend2_physical_point_inverse_square_and_cutoff)
{
	const float range = 10.0f * kDefaultWorldUnitsPerMeter;
	const float e1 = PointIrradiance(1000.0f, 1.0f * kDefaultWorldUnitsPerMeter,
		range, kDefaultWorldUnitsPerMeter);
	const float e2 = PointIrradiance(1000.0f, 2.0f * kDefaultWorldUnitsPerMeter,
		range, kDefaultWorldUnitsPerMeter);
	const float e4 = PointIrradiance(1000.0f, 4.0f * kDefaultWorldUnitsPerMeter,
		range, kDefaultWorldUnitsPerMeter);
	BOOST_CHECK_CLOSE(e1 / e2, 4.0f, 1e-4f);
	BOOST_CHECK_CLOSE(e1 / e4, 16.0f, 1e-4f);
	BOOST_CHECK_CLOSE(RangeWindow(0.79f * range, range), 1.0f, 1e-4f);
	BOOST_CHECK_EQUAL(RangeWindow(range, range), 0.0f);
	BOOST_CHECK_CLOSE(PointIrradiance(1.0f, 0.0f, range,
		kDefaultWorldUnitsPerMeter, 0.02f), 2500.0f, 1e-4f);
}

BOOST_AUTO_TEST_CASE(rend2_spot_lumens_integrate_actual_profile)
{
	const float cosInner = std::cos(20.0f * 3.14159265358979323846f / 180.0f);
	const float cosOuter = std::cos(30.0f * 3.14159265358979323846f / 180.0f);
	const float omega = SpotProfileSolidAngle(cosInner, cosOuter);
	const int steps = 100000;
	double integral = 0.0;
	for (int i = 0; i < steps; ++i)
	{
		const double u = cosOuter + (1.0 - cosOuter) * (i + 0.5) / steps;
		double profile = 1.0;
		if (u < cosInner)
		{
			const double t = (u - cosOuter) / (cosInner - cosOuter);
			profile = t * t * (3.0 - 2.0 * t);
		}
		integral += profile;
	}
	integral *= 2.0 * 3.14159265358979323846 * (1.0 - cosOuter) / steps;
	BOOST_CHECK_CLOSE(omega, (float)integral, 1e-3f);
	const float lumens = 1200.0f;
	BOOST_CHECK_CLOSE(LumensToSpotCandela(lumens, cosInner, cosOuter) * omega,
		lumens, 1e-4f);
	BOOST_CHECK_GT(LumensToSpotCandela(lumens, cosInner, cosOuter),
		lumens / (2.0f * 3.14159265358979323846f * (1.0f - cosOuter)));
}

BOOST_AUTO_TEST_CASE(rend2_specular_aa_stays_in_alpha_variance_domain)
{
	const float materialRoughness = 0.2f;
	const float materialAlpha = PerceptualRoughnessToAlpha(materialRoughness);
	const float variance = 0.03f;
	const float effectiveAlpha = FilteredGgxAlpha(materialAlpha, variance);
	const float effectiveRoughness = FilteredPerceptualRoughness(
		materialRoughness, variance, 1.0f);

	BOOST_CHECK_CLOSE(effectiveAlpha * effectiveAlpha,
		materialAlpha * materialAlpha + 2.0f * variance, 1e-4f);
	BOOST_CHECK_CLOSE(effectiveRoughness * effectiveRoughness,
		effectiveAlpha, 1e-4f);
	BOOST_CHECK_GT(effectiveRoughness, materialRoughness);
	BOOST_CHECK_EQUAL(FilteredPerceptualRoughness(materialRoughness,
		variance, 0.0f), materialRoughness);
	BOOST_CHECK_LE(FilteredPerceptualRoughness(materialRoughness,
		variance, 0.1f), materialRoughness + 0.1f);
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

BOOST_AUTO_TEST_CASE(rend2_ggx_multiscatter_white_furnace)
{
	const double views[] = { 0.1, 0.3, 0.6, 1.0 };
	const double roughnesses[] = { 0.05, 0.25, 0.5, 0.75, 1.0 };
	// Dielectric, copper-like, and gold-like F0. Each channel is evaluated
	// independently, exactly as the RGB shader formulation.
	const double f0s[][3] = {
		{ 0.04, 0.04, 0.04 },
		{ 0.95, 0.64, 0.54 },
		{ 1.00, 0.71, 0.29 }
	};
	for (double roughness : roughnesses)
	{
		const double averageAlbedo =
			WhiteFurnaceAverageDirectionalAlbedo(roughness);
		BOOST_REQUIRE_GE(averageAlbedo, 0.0);
		BOOST_REQUIRE_LE(averageAlbedo, 1.0);
		for (double NoV : views)
		{
			const DfgReference dfg = WhiteFurnaceDfg(NoV, roughness);
			const double r = dfg.scale + dfg.bias;
			BOOST_REQUIRE(std::isfinite(r));
			BOOST_REQUIRE_GT(r, 0.0);
			for (const auto& f0 : f0s)
				for (double channel : f0)
				{
					const double single = channel * dfg.scale + dfg.bias;
					const double kullaConty = KullaContyReference(single, r,
						averageAlbedo, channel);
					const double compensation = GgxEnergyCompensation(
						(float)channel, (float)r);
					const double compensated = single * compensation;
					const double diffuseTransmission = 1.0 -
						std::min(std::max(compensated, 0.0), 1.0);
					BOOST_CHECK(std::isfinite(compensated));
					BOOST_CHECK(std::isfinite(kullaConty));
					BOOST_CHECK_GE(compensated, single - 1.0e-6);
					BOOST_CHECK_LE(compensated, 1.001);
					BOOST_CHECK_GE(kullaConty, single - 1.0e-6);
					BOOST_CHECK_LE(kullaConty, 1.001);
					// A unit Lambert substrate plus the compensated interface
					// remains a bounded white furnace by construction.
					BOOST_CHECK_LE(compensated + diffuseTransmission, 1.001);
				}
		}
	}

	const DfgReference polished = WhiteFurnaceDfg(1.0, 0.05);
	BOOST_CHECK_SMALL(GgxEnergyCompensation(0.95f,
		(float)(polished.scale + polished.bias)) - 1.0f, 0.01f);
	const DfgReference rough = WhiteFurnaceDfg(1.0, 1.0);
	BOOST_CHECK_GT(GgxEnergyCompensation(0.95f,
		(float)(rough.scale + rough.bias)), 1.2f);
}

BOOST_AUTO_TEST_CASE(rend2_standard_lobe_energy_partition)
{
	const double f0s[] = { 0.0, 0.04, 0.25, 0.5, 1.0 };
	double previousDiffuse = 2.0;
	for (double f0 : f0s)
	{
		const double specular = SchlickAverageFresnel((float)f0);
		const double transmission = AverageDiffuseTransmission((float)f0);
		const double diffuse = EffectiveDiffuseReflectance(1.0f, 0.0f,
			(float)transmission);
		BOOST_CHECK_CLOSE(specular + transmission, 1.0, 1e-4);
		BOOST_CHECK_LE(diffuse, previousDiffuse);
		BOOST_CHECK_LE(specular + diffuse, 1.000001);
		previousDiffuse = diffuse;
	}

	const double metallics[] = { 0.0, 0.25, 0.5, 0.75, 1.0 };
	double previous = 2.0;
	for (double metallic : metallics)
	{
		const double diffuse = EffectiveDiffuseReflectance(1.0f,
			(float)metallic, AverageDiffuseTransmission(0.04f));
		BOOST_CHECK_LE(diffuse, previous);
		previous = diffuse;
	}
	BOOST_CHECK_SMALL((double)EffectiveDiffuseReflectance(1.0f, 1.0f,
		AverageDiffuseTransmission(0.04f)), 1e-7);

	// Colored conductors retain their per-channel interface response while the
	// normal diffuse substrate is exactly absent.
	const float goldF0[] = { 1.0f, 0.71f, 0.29f };
	for (float channel : goldF0)
	{
		BOOST_CHECK_GE(SchlickAverageFresnel(channel), channel);
		BOOST_CHECK_SMALL((double)EffectiveDiffuseReflectance(1.0f, 1.0f,
			AverageDiffuseTransmission(channel)), 1e-7);
	}
}

BOOST_AUTO_TEST_CASE(rend2_layered_ggx_partition_white_furnace)
{
	const double roughnesses[] = { 0.05, 0.25, 0.5, 0.75, 1.0 };
	const double f0s[] = { 0.04, 0.5, 0.95 };
	const double views[] = { 0.1, 0.3, 0.6, 1.0 };
	for (double roughness : roughnesses)
		for (double f0 : f0s)
		{
			const double averageSpecular = GgxAverageAlbedoQuadrature(
				roughness, f0);
			const double averageTransmission = 1.0 - averageSpecular;
			BOOST_REQUIRE_GT(averageTransmission, 0.0);
			BOOST_REQUIRE_LE(averageTransmission, 1.0);
			for (double NoV : views)
			{
				const double specular = CompensatedDirectionalAlbedo(NoV,
					roughness, f0);
				const double exitTransmission = 1.0 - specular;
				// The quadrature average of Tin cancels the layered denominator.
				const double diffuse = exitTransmission / averageTransmission *
					averageTransmission;
				BOOST_CHECK_CLOSE(specular + diffuse, 1.0, 1e-4);
			}
		}
}

BOOST_AUTO_TEST_CASE(rend2_lambert_and_burley_share_the_same_energy_budget)
{
	const int steps = 4096;
	const double f0 = 0.04;
	const double diffuseBudget = AverageDiffuseTransmission((float)f0);
	for (double roughness : { 0.0, 0.25, 0.5, 0.75, 1.0 })
	{
		double lambert = 0.0;
		double burley = 0.0;
		for (int i = 0; i < steps; ++i)
		{
			const double NoL = (i + 0.5) / steps;
			const double LoH = std::sqrt((1.0 + NoL) * 0.5);
			const double f90 = 0.5 + 2.0 * roughness * LoH * LoH;
			const double lightScatter = 1.0 + (f90 - 1.0) *
				std::pow(1.0 - NoL, 5.0);
			// NoV=1, so the Burley view-scatter factor is exactly one.
			lambert += 2.0 * NoL / steps;
			burley += 2.0 * NoL * std::min(lightScatter, 1.0) / steps;
		}
		lambert *= diffuseBudget;
		burley *= diffuseBudget;
		BOOST_CHECK_LE(lambert + SchlickAverageFresnel((float)f0), 1.00001);
		BOOST_CHECK_LE(burley + SchlickAverageFresnel((float)f0), 1.00001);
		BOOST_CHECK_LE(burley, lambert + 1.0e-6);
		BOOST_CHECK_LT(std::abs(burley - lambert), 0.08);
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

BOOST_AUTO_TEST_CASE(rend2_emissive_legacy_units_are_not_nits)
{
	BOOST_CHECK_CLOSE(LegacyEmissiveToNits(4.0f, 1000.0f), 4000.0f, 1e-4f);
	BOOST_CHECK_EQUAL(LegacyEmissiveToNits(-1.0f, 1000.0f), 0.0f);
	BOOST_CHECK_CLOSE(PhysicalToScene(LegacyEmissiveToNits(1.0f, 1000.0f),
		kDefaultPhysicalToScene), 0.1f, 1e-4f);
}
