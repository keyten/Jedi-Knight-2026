// Representation-neutral angular reference mathematics. No spatial field or renderer.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace Rend2LightRecon
{
namespace Core
{
namespace Angular
{
constexpr uint32_t kVersion = 1;
constexpr double Pi = 3.14159265358979323846;
using V = std::array<double, 3>; // double geometry / linear RGB; never encoded BSP bytes
V Add(V, V);
V Mul(V, double);
double Dot(V, V);
V Unit(V);
bool IsDirection(V);
bool Finite(V);
enum class Code
{
	Ok,
	Unavailable,
	Unsupported,
	Invalid,
	BudgetExceeded,
	Corrupt
};
enum class Quantity : uint32_t
{
	IncomingRadiance,
	DiffuseIrradiance
};
enum class Storage : uint32_t
{
	Float32,
	Float16,
	BlockInt16
};
struct Node
{
	V direction;
	double weight;
}; // solid angle, steradians
struct Sample
{
	Node node;
	V rgb;
};
struct Lobe
{
	V axis{{0, 0, 1}};
	double sharpness = 0;
	V energy{{0, 0, 0}};
	Lobe() = default;
	Lobe(V a, double s, V e) : axis(a), sharpness(s), energy(e)
	{
	}
}; // unit-integral SG
struct Field
{
	bool present = false; // explicit black is present with zero coefficients
	Quantity quantity = Quantity::IncomingRadiance;
	unsigned degree = 0;
	std::vector<V> sh;	  // signed orthonormal REAL coefficients; may be empty for SG-only
	std::vector<Lobe> sg; // only incoming radiance; no inverse irradiance convolution
};
struct Evaluation
{
	Code code = Code::Unavailable;
	V rgb{{0, 0, 0}};
	bool negative = false;
};
// World receiver -> incoming source; z polar, Condon-Shortley, index l*l+l+m.
Code Basis(V direction, unsigned degree, std::vector<double> &);
std::vector<Node> Sphere(unsigned order, double azimuthOffset = .5); // 2*order^2 nodes
std::vector<std::array<double, 2>> Interval(unsigned order);		 // [0,1] Gauss nodes/weights
std::vector<V> Directions(unsigned count); // independent Fibonacci test normals
std::vector<double> CosineFactors(unsigned degree);
Code Project(const std::vector<Sample> &, unsigned degree, Field &);
Code Validate(const Field &);
double SG(V direction, V axis, double sharpness);
// Independent 1D angular integral; no SH truncation or clamp.
double SGDiffuse(V normal, V axis, double sharpness, unsigned order = 32);
Evaluation Evaluate(const Field &, V directionOrNormal, Quantity);
struct FitOptions
{
	unsigned lobes = 3, iterations = 32, amplitudeSweeps = 8;
	uint64_t maxKernelEvaluations = 100000000;
};
struct FitWork
{
	uint64_t kernelEvaluations = 0, amplitudeSweeps = 0;
	unsigned iterations = 0, inactiveLobes = 0;
	double initialMSE = 0, finalMSE = 0, finalAngularStep = 0;
	bool budgetLimited = false, localStagnation = false, iterationLimited = false;
};
// Deterministic bounded dictionary initialization + axis/log-width pattern search
// + nonnegative RGB block descent. Receives ONLY training angular samples.
Code FitSG(const std::vector<Sample> &, const FitOptions &, Field &, FitWork &);
// Experimental portable angular block, not an installed canonical Dataset codec.
// Little endian, version/checksum, bounded sizes, no GPU formats. Carries quantity;
// unit/calibration/root/spatial metadata MUST stay on the owning canonical record.
Code Encode(const Field &, Storage, std::vector<uint8_t> &);
Code Decode(const std::vector<uint8_t> &, Field &);
} // namespace Angular
} // namespace Core
} // namespace Rend2LightRecon
