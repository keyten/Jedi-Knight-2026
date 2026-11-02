// Deterministic P03 CPU mathematical controls. No renderer/stock assets.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../shared/rd-rend2/lightrecon/contributions.h"
#include "../../shared/rd-rend2/lightrecon/field_basis.h"
#include <cmath>
#include <cstdio>
#include <limits>
namespace A = Rend2LightRecon::Core::Angular;
using A::V;
int checks = 0, failures = 0;
#define CHECK(x)                                                                                   \
	do                                                                                             \
	{                                                                                              \
		++checks;                                                                                  \
		if (!(x))                                                                                  \
		{                                                                                          \
			++failures;                                                                            \
			std::printf("FAIL %d: %s\n", __LINE__, #x);                                            \
		}                                                                                          \
	} while (0)
bool Near(double a, double b, double eps = 1e-10)
{
	return std::abs(a - b) <= eps * std::max(1., std::abs(b));
}
void Composition()
{
	using namespace Rend2LightRecon;
	namespace C = Rend2LightRecon::Core::Attribution;
	ReconstructedLightingAsset asset;
	asset.lineage = "P03/owned/angular";
	asset.provenance = 1;
	Provenance provenance;
	provenance.id = 1;
	provenance.kind = ProvenanceKind::Authored;
	provenance.producer = "P03 declared roots";
	asset.provenanceRecords.push_back(provenance);
	Calibration calibration;
	calibration.id = 2;
	calibration.provenance = 1;
	calibration.kind = CalibrationKind::PhysicallyAuthored;
	asset.calibrations.push_back(calibration);
	PhysicalEmitter source;
	source.source.id = 10;
	source.source.provenance = 1;
	source.emission.calibration = 2;
	source.emission.unit = PhotometricUnit::Candela;
	asset.emitters.push_back(source);
	for (unsigned order = 0; order < 2; ++order)
	{
		Dataset dataset;
		dataset.id = 100 + order;
		dataset.purpose = DatasetPurpose::Transport;
		dataset.layout = DatasetLayout::Chunked;
		dataset.chunkElements = 1;
		dataset.encoding = "P03 membership fixture";
		dataset.contentHash = "owned";
		dataset.locator = "owned";
		asset.datasets.push_back(dataset);
		TransportField field;
		field.source.id = 20 + order;
		field.source.provenance = 1;
		field.component = TransportComponent::ApertureTransport;
		field.dataset = dataset.id;
		field.unit = PhotometricUnit::Lux;
		field.calibration = 2;
		auto &m = field.membership;
		m.version = 2;
		m.model = "P03/known-split";
		m.rootsComplete = true;
		m.reflectionOrderKnown = true;
		m.minimumReflectionOrder = m.maximumReflectionOrder = order;
		m.reflectionOrders = {order};
		TransportRoot root;
		root.source = 10;
		root.kind = TransportRootKind::LocalEmitter;
		m.roots.push_back(root);
		m.directLocal = order ? FieldMembership::Excluded : FieldMembership::Included;
		m.directSun = m.directSky = m.legacyAmbient = m.unresolved = FieldMembership::Excluded;
		m.reflected = order ? FieldMembership::Included : FieldMembership::Excluded;
		m.colour = ContributionColour::LinearRec709D65;
		m.domain = "physical-irradiance";
		m.generation = "P03/1";
		m.geometry = "owned";
		m.profile = "physical reference";
		m.addition = CompositionRule::Permitted;
		asset.transport.push_back(field);
	}
	std::string problem;
	CHECK(Validate(asset, &problem));
	if (!problem.empty())
		std::printf("membership: %s\n", problem.c_str());
	C::Component direct, indirect;
	direct.field = indirect.field = true;
	direct.dataset = 100;
	indirect.dataset = 101;
	CHECK(C::CanAdd(asset, direct, indirect)); // explicit source + its separate bounce
	CHECK(!C::CanAdd(asset, direct, direct));
	auto &m = asset.transport[1].membership;
	m.minimumReflectionOrder = 0;
	m.reflectionOrders = {0, 1};
	m.directLocal = FieldMembership::Included;
	CHECK(!C::CanAdd(asset, direct, indirect)); // total field + same direct light
	m.rootsComplete = false;
	CHECK(!C::CanAdd(asset, direct, indirect));
}
std::vector<A::Sample> Samples(unsigned n, V rgb)
{
	std::vector<A::Sample> out;
	for (auto p : A::Sphere(n))
		out.push_back(A::Sample{p, rgb});
	return out;
}
int main()
{
	Composition();
	std::vector<double> Y;
	CHECK(A::Basis(V{{1, 0, 0}}, 1, Y) == A::Code::Ok);
	CHECK(Y.size() == 4);
	CHECK(Near(Y[3], -std::sqrt(3 / (4 * A::Pi))));
	CHECK(A::Basis(V{{0, 1, 0}}, 1, Y) == A::Code::Ok);
	CHECK(Near(Y[1], -std::sqrt(3 / (4 * A::Pi))));
	CHECK(A::Basis(V{{0, 0, 1}}, 1, Y) == A::Code::Ok);
	CHECK(Near(Y[2], std::sqrt(3 / (4 * A::Pi))));
	auto nodes = A::Sphere(16);
	std::vector<std::vector<double>> basis;
	for (auto p : nodes)
	{
		A::Basis(p.direction, 4, Y);
		basis.push_back(Y);
	}
	for (unsigned i = 0; i < 25; ++i)
		for (unsigned j = 0; j < 25; ++j)
		{
			double v = 0;
			for (size_t k = 0; k < nodes.size(); ++k)
				v += nodes[k].weight * basis[k][i] * basis[k][j];
			CHECK(Near(v, i == j ? 1. : 0.));
		}
	auto f = A::CosineFactors(8);
	double expected[] = {A::Pi, 2 * A::Pi / 3, A::Pi / 4, 0, -A::Pi / 24};
	for (unsigned l = 0; l < 5; ++l)
		CHECK(Near(f[l], expected[l]));
	CHECK(std::abs(f[5]) < 1e-12 && std::abs(f[7]) < 1e-12);
	A::Field sh;
	CHECK(A::Project(Samples(16, V{{1, 2, 3}}), 4, sh) == A::Code::Ok);
	for (V d : A::Directions(37))
	{
		auto L = A::Evaluate(sh, d, A::Quantity::IncomingRadiance),
			 E = A::Evaluate(sh, d, A::Quantity::DiffuseIrradiance);
		for (unsigned c = 0; c < 3; ++c)
		{
			CHECK(Near(L.rgb[c], c + 1.));
			CHECK(Near(E.rgb[c], (c + 1) * A::Pi));
		}
	}
	auto convolved = sh;
	convolved.quantity = A::Quantity::DiffuseIrradiance;
	for (unsigned l = 0; l <= 4; ++l)
		for (unsigned k = l * l; k < (l + 1) * (l + 1); ++k)
			convolved.sh[k] = A::Mul(convolved.sh[k], f[l]);
	std::vector<uint8_t> irradianceBytes;
	CHECK(A::Encode(convolved, A::Storage::Float32, irradianceBytes) == A::Code::Ok);
	A::Field restoredE;
	CHECK(A::Decode(irradianceBytes, restoredE) == A::Code::Ok);
	CHECK(Near(A::Evaluate(restoredE, V{{0, 0, 1}}, A::Quantity::DiffuseIrradiance).rgb[0], A::Pi,
			   1e-6));
	CHECK(A::Evaluate(restoredE, V{{0, 0, 1}}, A::Quantity::IncomingRadiance).code ==
		  A::Code::Unsupported);
	// Independent full-sphere cosine quadrature vs SG one-dimensional integral.
	auto fine = A::Sphere(128);
	V axis = A::Unit(V{{.2, .3, 1.}});
	for (double width : {0., 2., 32., 256.})
	{
		double energy = 0;
		for (auto p : fine)
			energy += p.weight * A::SG(p.direction, axis, width);
		CHECK(Near(energy, 1, 1e-9));
		for (V n : {axis, A::Mul(axis, -1), A::Unit(V{{1, -.3, .1}})})
		{
			double reference = 0;
			for (auto p : fine)
				reference += p.weight * A::SG(p.direction, axis, width) *
							 std::max(0., A::Dot(p.direction, n));
			double coarse = A::SGDiffuse(n, axis, width, 32),
				   precise = A::SGDiffuse(n, axis, width, 96);
			CHECK(Near(precise, reference, 8e-5));
			CHECK(Near(coarse, precise, 3e-7));
		}
	}
	// Null space: an L3 radiance mode disappears under diffuse convolution.
	sh.sh.assign(16, V{{0, 0, 0}});
	sh.degree = 3;
	sh.sh[12] = V{{1, 0, 0}};
	CHECK(std::abs(A::Evaluate(sh, V{{0, 0, 1}}, A::Quantity::DiffuseIrradiance).rgb[0]) < 1e-12);
	CHECK(std::abs(A::Evaluate(sh, V{{0, 0, 1}}, A::Quantity::IncomingRadiance).rgb[0]) > .1);
	sh.quantity = A::Quantity::DiffuseIrradiance;
	CHECK(A::Evaluate(sh, V{{0, 0, 1}}, A::Quantity::IncomingRadiance).code ==
		  A::Code::Unsupported);
	A::Field missing;
	CHECK(A::Evaluate(missing, V{{0, 0, 1}}, A::Quantity::IncomingRadiance).code ==
		  A::Code::Unavailable);
	CHECK(A::Project(Samples(8, V{{0, 0, 0}}), 2, sh) == A::Code::Ok);
	CHECK(A::Evaluate(sh, V{{0, 0, 1}}, A::Quantity::IncomingRadiance).code == A::Code::Ok);
	CHECK(A::Evaluate(sh, V{{0, 0, 1}}, A::Quantity::IncomingRadiance).rgb == V({{0, 0, 0}}));
	// Signed SH ringing is returned, never hidden by positivity clamping.
	sh.sh[2] = V{{10, 0, 0}};
	auto negative = A::Evaluate(sh, V{{0, 0, -1}}, A::Quantity::IncomingRadiance);
	CHECK(negative.negative && negative.rgb[0] < 0);
	for (auto storage : {A::Storage::Float32, A::Storage::Float16, A::Storage::BlockInt16})
	{
		std::vector<uint8_t> bytes, again;
		CHECK(A::Encode(sh, storage, bytes) == A::Code::Ok);
		A::Field decoded;
		CHECK(A::Decode(bytes, decoded) == A::Code::Ok);
		CHECK(A::Encode(decoded, storage, again) == A::Code::Ok);
		CHECK(bytes == again);
		CHECK(Near(A::Evaluate(sh, axis, A::Quantity::IncomingRadiance).rgb[0],
				   A::Evaluate(decoded, axis, A::Quantity::IncomingRadiance).rgb[0], .001));
		bytes.back() ^= 1;
		CHECK(A::Decode(bytes, decoded) == A::Code::Corrupt);
		bytes.resize(12);
		CHECK(A::Decode(bytes, decoded) == A::Code::Corrupt);
	}
	sh.sh[0] = V{{1e8, 0, 0}};
	std::vector<uint8_t> bytes;
	CHECK(A::Encode(sh, A::Storage::Float16, bytes) == A::Code::Invalid);
	CHECK(A::Encode(sh, A::Storage::Float32, bytes) == A::Code::Ok);
	CHECK(A::Basis(V{{0, 0, 0}}, 2, Y) == A::Code::Invalid);
	CHECK(A::Basis(axis, 9, Y) == A::Code::Invalid);
	sh.sh[0][0] = std::numeric_limits<double>::infinity();
	CHECK(A::Validate(sh) == A::Code::Invalid);
	// Unknown-axis fit receives sampled RGB only. These axes are not passed to FitSG.
	auto samples = Samples(24, V{{.01, .01, .01}});
	for (auto &a : samples)
	{
		a.rgb = A::Add(a.rgb, A::Mul(V{{1, .1, .03}}, A::SG(a.node.direction, axis, 20)));
	}
	A::FitOptions options;
	options.lobes = 1;
	options.iterations = 80;
	A::FitWork work;
	A::Field fitted;
	CHECK(A::FitSG(samples, options, fitted, work) == A::Code::Ok);
	CHECK(work.finalMSE < work.initialMSE * 1e-4);
	CHECK(A::Dot(fitted.sg[1].axis, axis) > .999);
	CHECK(std::abs(fitted.sg[1].sharpness - 20) < .5);
	A::Field repeat;
	A::FitWork rw;
	CHECK(A::FitSG(samples, options, repeat, rw) == A::Code::Ok);
	CHECK(fitted.sg[1].axis == repeat.sg[1].axis && work.finalMSE == rw.finalMSE);
	CHECK(A::Encode(fitted, A::Storage::Float16, bytes) == A::Code::Ok);
	CHECK(A::Decode(bytes, repeat) == A::Code::Ok);
	CHECK(A::IsDirection(repeat.sg[1].axis));
	CHECK(A::Encode(fitted, A::Storage::BlockInt16, bytes) == A::Code::Unsupported);
	options.maxKernelEvaluations = 10;
	CHECK(A::FitSG(samples, options, repeat, rw) == A::Code::BudgetExceeded && rw.budgetLimited);
	options.maxKernelEvaluations = 100000000;
	samples[0].rgb[0] = -1;
	CHECK(A::FitSG(samples, options, repeat, rw) == A::Code::Invalid);
	// RGB projection independence; no peak normalization across channels.
	auto s = Samples(12, V{{1, 0, 0}});
	CHECK(A::Project(s, 4, sh) == A::Code::Ok);
	for (auto c : sh.sh)
		CHECK(c[1] == 0 && c[2] == 0);
	std::printf("P03 angular basis: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
