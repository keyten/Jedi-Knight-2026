/*
Standalone GGX/white-furnace reference for Rend2.

Build, for example:
  c++ -std=c++11 -O2 tools/rend2_ggx_reference.cpp -o rend2_ggx_reference

No renderer or test-framework dependency is required. A nonzero exit code
means normalization, energy, sampling/PDF or finite-value validation failed.
*/
#include "../shared/rd-rend2/tr_pbr_contract.h"

#include <cmath>
#include <cstdio>

using namespace Rend2PBR;

struct Vec3
{
	double x, y, z;
};

static double Dot(const Vec3& a, const Vec3& b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vec3 Normalize(const Vec3& v)
{
	const double invLength = 1.0 / std::sqrt(Dot(v, v));
	return { v.x * invLength, v.y * invLength, v.z * invLength };
}

static double IntegrateNdf(double perceptualRoughness)
{
	const int steps = 20000;
	const float alpha = PerceptualRoughnessToAlpha((float)perceptualRoughness);
	double sum = 0.0;
	for (int i = 0; i < steps; ++i)
	{
		const double x = (i + 0.5) / steps;
		const float NoH = (float)(1.0 - x * x * x * x);
		const double jacobian = 4.0 * x * x * x;
		sum += 2.0 * 3.14159265358979323846 * D_GGX(NoH, alpha) *
			NoH * jacobian / steps;
	}
	return sum;
}

static double WhiteFurnace(double NoV, double perceptualRoughness, double F0)
{
	const int cosSteps = 96;
	const int phiSteps = 192;
	const double pi = 3.14159265358979323846;
	const float alpha = PerceptualRoughnessToAlpha((float)perceptualRoughness);
	const Vec3 V = { std::sqrt(std::max(1.0 - NoV * NoV, 0.0)), 0.0, NoV };
	double sum = 0.0;
	for (int y = 0; y < cosSteps; ++y)
	{
		const double NoL = (y + 0.5) / cosSteps;
		const double sinTheta = std::sqrt(1.0 - NoL * NoL);
		for (int x = 0; x < phiSteps; ++x)
		{
			const double phi = 2.0 * pi * (x + 0.5) / phiSteps;
			const Vec3 L = { sinTheta * std::cos(phi), sinTheta * std::sin(phi), NoL };
			const Vec3 H = Normalize({ V.x + L.x, V.y + L.y, V.z + L.z });
			const double VoH = std::max(Dot(V, H), 0.0);
			const double brdf = D_GGX((float)H.z, alpha) *
				V_SmithGGXCorrelated(alpha, (float)NoV, (float)NoL) *
				FresnelSchlick((float)F0, 1.0f, (float)VoH);
			sum += brdf * NoL;
		}
	}
	return sum * 2.0 * pi / (cosSteps * phiSteps);
}

static double SamplingPdfMaxRelativeError(double perceptualRoughness)
{
	const double pi = 3.14159265358979323846;
	const double alpha = PerceptualRoughnessToAlpha((float)perceptualRoughness);
	const double alpha2 = alpha * alpha;
	double maxError = 0.0;
	for (int i = 1; i < 999; i += 13)
	{
		const double u = i / 1000.0;
		const auto sampleCos = [alpha2](double xi) {
			return std::sqrt((1.0 - xi) / (1.0 + (alpha2 - 1.0) * xi));
		};
		const double epsilon = 1.0e-6;
		const double NoH = sampleCos(u);
		const double derivative = (sampleCos(u + epsilon) - sampleCos(u - epsilon)) /
			(2.0 * epsilon);
		const double sampledPdf = 1.0 / std::abs(derivative);
		const double evaluatedPdf = 2.0 * pi * D_GGX((float)NoH, (float)alpha) * NoH;
		maxError = std::max(maxError, std::abs(sampledPdf - evaluatedPdf) / evaluatedPdf);
	}
	return maxError;
}

int main()
{
	const double roughnesses[] = { 0.0, 0.05, 0.1, 0.25, 0.5, 1.0 };
	const double views[] = { 0.1, 0.3, 0.6, 1.0 };
	const double f0s[] = { 0.0, 0.04, 0.5, 1.0 };
	int failures = 0;
	const float aaMaterialRoughness = 0.2f;
	const float aaVariance = 0.03f;
	const float aaMaterialAlpha = PerceptualRoughnessToAlpha(aaMaterialRoughness);
	const float aaEffectiveAlpha = FilteredGgxAlpha(aaMaterialAlpha, aaVariance);
	const float aaEffectiveRoughness = FilteredPerceptualRoughness(
		aaMaterialRoughness, aaVariance, 1.0f);
	const float aaDomainError = std::abs(aaEffectiveAlpha * aaEffectiveAlpha -
		(aaMaterialAlpha * aaMaterialAlpha + 2.0f * aaVariance));
	if (aaDomainError > 1.0e-6f ||
		std::abs(aaEffectiveRoughness * aaEffectiveRoughness - aaEffectiveAlpha) > 1.0e-6f ||
		aaEffectiveRoughness < aaMaterialRoughness)
		++failures;
	std::printf("specular AA r %.3f alpha %.6f variance %.6f -> alpha %.6f r %.6f\n",
		aaMaterialRoughness, aaMaterialAlpha, aaVariance, aaEffectiveAlpha,
		aaEffectiveRoughness);
	float previousPeak = INFINITY;
	for (double roughness : roughnesses)
	{
		const float alpha = PerceptualRoughnessToAlpha((float)roughness);
		const double ndf = IntegrateNdf(roughness);
		const double pdfError = SamplingPdfMaxRelativeError(roughness);
		const float peak = D_GGX(1.0f, alpha);
		std::printf("r %.3f alpha %.6f NDF %.9f peak %.4f PDF error %.6g\n",
			roughness, alpha, ndf, peak, pdfError);
		if (!std::isfinite(ndf) || std::abs(ndf - 1.0) > 0.005 ||
			!std::isfinite(peak) || peak > previousPeak * 1.00001f || pdfError > 0.02)
			++failures;
		previousPeak = peak;
	}

	double maximumReflectance = 0.0;
	for (double NoV : views)
		for (double roughness : roughnesses)
			for (double F0 : f0s)
			{
				const double value = WhiteFurnace(NoV, roughness, F0);
				maximumReflectance = std::max(maximumReflectance, value);
				if (!std::isfinite(value) || value < 0.0 || value > 1.02)
					++failures;
			}
	std::printf("white-furnace maximum reflectance %.9f; failures %d\n",
		maximumReflectance, failures);
	return failures ? 1 : 0;
}

