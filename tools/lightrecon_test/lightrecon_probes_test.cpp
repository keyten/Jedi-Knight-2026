// Geometry correctness and lighting prediction are tested separately.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "probe_fixtures.h"
#include <chrono>
#include <fstream>
#include <iostream>
using namespace ProbeFixture;
int checks = 0, failures = 0;
#define CHECK(x)                                                                                   \
	do                                                                                             \
	{                                                                                              \
		++checks;                                                                                  \
		if (!(x))                                                                                  \
		{                                                                                          \
			++failures;                                                                            \
			std::cerr << "FAIL " << __LINE__ << ": " << #x << '\n';                                \
		}                                                                                          \
	} while (0)
struct Cancel : Cancellation
{
	bool IsCancelled() const noexcept override
	{
		return true;
	}
};
int main(int argc, char **argv)
{
	try
	{
		Scene empty;
		auto input = empty.Input();
		auto t = empty.Build(input);
		CHECK(t->Diagnostics().valid > 0);
		CHECK(t->Diagnostics().supportedStaticVolume == 1024);
		CHECK(t->Diagnostics().unknownVolume == 0);
		auto a = Asset(*t);
		auto v = Values(*t);
		auto prepared = Prepared(t, v, a);
		LightingSample sample;
		IrradianceQuery q;
		q.positionWorld = Vec3(1, 1, 3);
		q.normalWorld = Vec3(0, 0, 1);
		CHECK(prepared->Indirect(a, q, sample) == Status::Ok);
		CHECK(std::abs(sample.rgb.x - 1) < 1e-6 && std::abs(sample.rgb.y - 2) < 1e-6);
		P::Values missing;
		missing.field = 90;
		missing.topology = t->Key();
		auto without = Prepared(t, missing, a);
		CHECK(without->Indirect(a, q, sample) == Status::Unavailable);
		CHECK(without->GeometrySupport(q.positionWorld).status == Status::Ok);
		auto zero = v;
		for (auto &x : zero.records)
			x.angular.sh[0] = {{0, 0, 0}};
		CHECK(Prepared(t, zero, a)->Indirect(a, q, sample) == Status::Ok && sample.rgb.x == 0);
		auto sparse = v;
		sparse.records.resize(1);
		CHECK(Prepared(t, sparse, a)->Indirect(a, q, sample) == Status::Unavailable);
		auto stale = v;
		stale.records[0].evaluatedPositionWorld.x += .1f;
		std::shared_ptr<const P::Prepared> fail;
		CHECK(!P::Prepare(t, stale, a, ReadContext(), fail));
		auto negative = v;
		for (auto &x : negative.records)
			x.angular.sh[0][0] = -1;
		CHECK(Prepared(t, negative, a)->Indirect(a, q, sample) == Status::Unsupported);
		Manager manager;
		CHECK(manager.Publish(a, Completeness::Partial, prepared) == Status::Ok);
		CHECK(manager.Acquire().SampleIndirect(q).status==Status::Unsupported); // P01: Relative is not physical Lux
		RadianceQuery rq;
		rq.positionWorld = q.positionWorld;
		rq.directionToSource = q.normalWorld;
		CHECK(manager.Acquire().SampleIncomingRadiance(rq).status == Status::Unavailable);
		double gradientSE = 0;
		auto gradient = Prepared(t, Values(*t, 2), a);
		unsigned gradientN = 0;
		for (unsigned j = 0; j < 200; ++j)
		{
			q.positionWorld =
				Vec3(-7.5f + 15.f * (j % 20) / 19.f, -3.5f + 7.f * ((j / 20) % 10) / 9.f, 3);
			if (gradient->Indirect(a, q, sample) == Status::Ok)
			{
				gradientSE += std::pow(sample.rgb.x - (2 + .1 * q.positionWorld.x), 2);
				++gradientN;
			}
		}
		double gradientRMSE = std::sqrt(gradientSE / gradientN);
		CHECK(gradientN == 200 && gradientRMSE < .2);
		Scene wall;
		wall.Solid(Box(Vec3(-.05f, -4, 0), Vec3(.05f, 4, 8)));
		auto wi = wall.Input(true);
		auto wt = wall.Build(wi);
		auto wa = Asset(*wt);
		auto wp = Prepared(wt, Values(*wt, 1), wa);
		q.positionWorld = Vec3(-1, 0, 3);
		CHECK(wp->Indirect(wa, q, sample) == Status::Ok && sample.rgb.x == 0);
		q.positionWorld = Vec3(1, 0, 3);
		CHECK(wp->Indirect(wa, q, sample) == Status::Ok && std::abs(sample.rgb.x - 10) < 1e-6);
		q.positionWorld = Vec3(0, 0, 3);
		CHECK(wt->Lookup(q.positionWorld).status == Status::Unavailable);
		unsigned leaks = 0, blockedChecks = 0;
		for (const auto &c : wt->Cells())
			for (unsigned j = 0; j < c.linkCount; ++j)
			{
				auto &p = wt->Probes()[wt->Links()[c.firstLink + j].probe];
				if (c.probe != UINT32_MAX)
				{
					auto &own = wt->Probes()[c.probe];
					++blockedChecks;
					leaks += (own.positionWorld.x < 0) != (p.positionWorld.x < 0);
				}
			}
		CHECK(leaks == 0 && blockedChecks > 0);
		// All nearby positive values lie behind the wall: missing admissible dark-room
		// values are unknown, never normalized from a blocked positive neighbour.
		auto behind = Values(*wt, 1);
		behind.records.erase(
			std::remove_if(behind.records.begin(), behind.records.end(),
						   [](const P::Value &x) { return x.evaluatedPositionWorld.x < 0; }),
			behind.records.end());
		q.positionWorld = Vec3(-1, 0, 3);
		CHECK(Prepared(wt, behind, wa)->Indirect(wa, q, sample) == Status::Unavailable);
		Scene doorway;
		doorway.Solid(Box(Vec3(-.05f, -4, 0), Vec3(.05f, -1, 8)));
		doorway.Solid(Box(Vec3(-.05f, 1, 0), Vec3(.05f, 4, 8)));
		auto dt = doorway.Build(doorway.Input(true));
		bool crosses = false;
		for (const auto &c : dt->Cells())
			if (c.probe != UINT32_MAX)
				for (unsigned j = 0; j < c.linkCount; ++j)
					crosses |=
						(dt->Probes()[c.probe].positionWorld.x < 0) !=
						(dt->Probes()[dt->Links()[c.firstLink + j].probe].positionWorld.x < 0);
		CHECK(crosses);
		CHECK(dt->Lookup(Vec3(0, 0, 3)).status == Status::Ok);
		Scene window;
		window.Solid(Box(Vec3(-.1f, -4, 0), Vec3(.1f, 4, 2)));
		window.Solid(Box(Vec3(-.1f, -4, 6), Vec3(.1f, 4, 8)));
		auto windowT = window.Build(window.Input());
		CHECK(windowT->Lookup(Vec3(0, 0, 4)).status == Status::Ok);
		CHECK(windowT->Lookup(Vec3(0, 0, 1)).status == Status::Unavailable);
		Scene floors;
		floors.Solid(Box(Vec3(-8, -4, 3.9f), Vec3(8, 4, 4.1f)));
		auto ft = floors.Build(floors.Input());
		for (const auto &c : ft->Cells())
			if (c.probe != UINT32_MAX)
				for (unsigned j = 0; j < c.linkCount; ++j)
					CHECK((ft->Probes()[c.probe].positionWorld.z < 4) ==
						  (ft->Probes()[ft->Links()[c.firstLink + j].probe].positionWorld.z < 4));
		Scene tall;
		tall.domain->world = tall.options.bounds = Box(Vec3(-4, -4, 0), Vec3(4, 4, 32));
		auto tt = tall.Build(tall.Input());
		CHECK(tt->Lookup(Vec3(0, 0, 29)).status == Status::Ok);
		Scene courtyard;
		courtyard.domain->exterior = true;
		auto ot = courtyard.Build(courtyard.Input());
		CHECK(ot->Diagnostics().valid > 0);
		Scene curve;
		for (int k = -3; k < 4; ++k)
		{
			float x = float(k) * 2;
			curve.Solid(Box(Vec3(x - 1, 2 + float(k * k) * .06f, 0), Vec3(x + 1, 4, 8)));
		}
		auto ct = curve.Build(curve.Input());
		CHECK(ct->Lookup(Vec3(0, 0, 4)).status == Status::Ok);
		Scene alpha;
		alpha.Quad(Vec3(0, -4, 0), Vec3(0, 4, 0), Vec3(0, 4, 8), Vec3(0, -4, 8), MaterialCutout);
		auto at = alpha.Build(alpha.Input());
		CHECK(at->Lookup(Vec3(0, 0, 4)).status == Status::Unavailable);
		CHECK(at->Diagnostics().unknownVolume > 0);
		auto dynamic = input;
		DynamicBoundary door;
		door.id = 1;
		door.bounds = Box(Vec3(100, 100, 100), Vec3(101, 101, 101));
		dynamic.dynamic.push_back(door);
		auto dyn = empty.Build(dynamic);
		CHECK(dyn->Lookup(Vec3(1, 1, 3)).status == Status::Unavailable);
		P::QueryPolicy conditional;
		conditional.allowConditionalStatic = true;
		auto ds = dyn->Lookup(Vec3(1, 1, 3), conditional);
		CHECK(ds.status == Status::Ok && ds.conditional);
		dynamic.dynamic[0].sweptExtentKnown = true;
		CHECK(empty.Build(dynamic)->Lookup(Vec3(1, 1, 3)).status == Status::Ok);
		Scene unknown;
		unknown.domain->unknown = true;
		auto ut = unknown.Build(unknown.Input());
		CHECK(ut->Probes().empty());
		CHECK(ut->Lookup(Vec3(0, 0, 3)).status == Status::Unavailable);
		Scene trapped;
		trapped.Solid(trapped.options.bounds);
		auto tr = trapped.Build(trapped.Input());
		CHECK(tr->Probes().empty() && tr->Diagnostics().solid > 0);
		Scene relocate;
		relocate.Solid(Box(Vec3(.55f, -4, 0), Vec3(.56f, 4, 8)));
		auto moved = relocate.Build(relocate.Input());
		CHECK(moved->Diagnostics().relocated > 0);
		auto movedValues = Values(*moved);
		auto movedAsset = Asset(*moved);
		CHECK(P::Prepare(moved, movedValues, movedAsset, ReadContext(), fail));
		for (auto &x : movedValues.records)
			for (auto &p : moved->Probes())
				if (p.id == x.probe && p.displacementWorld > 0)
					x.evaluatedPositionWorld = p.originalWorld;
		CHECK(!P::Prepare(moved, movedValues, movedAsset, ReadContext(), fail));
		P::Moment m;
		m.present = true;
		m.samples = 32;
		m.maximumDistance = 10;
		m.mean = 2;
		m.second = 4;
		double weight = 1;
		CHECK(P::MomentWeight(m, Vec3(0, 0, 1), 5, weight) && weight == 0);
		m.second = 3;
		CHECK(!P::MomentWeight(m, Vec3(0, 0, 1), 5, weight));
		m.second = 4 - 1e-15;
		CHECK(P::MomentWeight(m, Vec3(0, 0, 1), 5, weight) && weight == 0);
		m.second = 5;
		CHECK(P::MomentWeight(m, Vec3(0, 0, 1), 3, weight) && std::abs(weight - .5) < 1e-12);
		// A nonzero statistical upper bound, normalized alone, would create full
		// brightness behind a known wall. It must never override geometric support.
		CHECK(10 * weight / weight == 10);
		P::QueryPolicy moments;
		moments.useMoments = true;
		CHECK(t->Lookup(Vec3(.3f, .2f, 3), moments).status == Status::Unavailable);
		std::vector<Disk::Chunk> chunks;
		CHECK(P::Encode(*wt, 201, 202, ReadContext(), chunks));
		std::shared_ptr<const P::Topology> decoded;
		CHECK(P::Decode(chunks, wt->GeometryKey(), ReadContext(), decoded));
		CHECK(decoded->Key() == wt->Key());
		CHECK(decoded->Lookup(Vec3(-1, 0, 3)).count == wt->Lookup(Vec3(-1, 0, 3)).count);
		auto incomplete = chunks;
		incomplete.pop_back();
		CHECK(!P::Decode(incomplete, wt->GeometryKey(), ReadContext(), decoded));
		CHECK(P::Decode(chunks, wt->GeometryKey(), ReadContext(), decoded));
		std::vector<Disk::Chunk> solidChunks;
		CHECK(P::Encode(*ft, 301, 302, ReadContext(), solidChunks));
		CHECK(P::Decode(solidChunks, ft->GeometryKey(), ReadContext(), decoded));
		CHECK(decoded->Key() == ft->Key());
		chunks[0].bytes[40] ^= 1;
		CHECK(!P::Decode(chunks, wt->GeometryKey(), ReadContext(), decoded));
		auto repeat = wall.Build(wi);
		CHECK(repeat->Key() == wt->Key());
		CHECK(repeat->Probes()[0].id == wt->Probes()[0].id);
		auto dense = empty.options;
		P::Refinement refine;
		refine.evidence = 1;
		refine.bounds = Box(Vec3(-4, -2, 2), Vec3(0, 2, 6));
		refine.spacingWorld = 1;
		refine.reason = P::RefinementReason::AngularError;
		auto adapted = input;
		adapted.refinement.push_back(refine);
		auto fine = empty.Build(adapted);
		CHECK(fine->Probes().size() > t->Probes().size());
		CHECK(empty.Build(input)->Key() ==
			  t->Key()); // removing error hints coarsens deterministically
		// Scalar fixed-normal direct irradiance tests the spatial interpolator;
		// it is NOT exported or labelled as an indirect canonical lighting field.
		auto coarseValues = Values(*t, 3), fineValues = Values(*fine, 3);
		auto scalar = [](const P::Topology &topology, const P::Values &values, Vec3 point) {
			auto s = topology.Lookup(point);
			double result = 0;
			if (s.status != Status::Ok)
				throw std::runtime_error("Missing scalar support");
			for (unsigned k = 0; k < s.count; ++k)
			{
				auto v = std::find_if(values.records.begin(), values.records.end(),
									  [&](const P::Value &x) { return x.probe == s.probes[k]; });
				result += s.weights[k] * v->angular.sh[0][0] / std::sqrt(4 * 3.141592653589793);
			}
			return result;
		};
		double coarseError = 0, fineError = 0;
		unsigned physicalCount = 0;
		for (unsigned j = 0; j < 200; ++j)
		{
			q.positionWorld =
				Vec3(-3.8f + 3.6f * (j % 20) / 19.f, -1.8f + 3.6f * ((j / 20) % 10) / 9.f, 4.2f);
			double dz = 7 - q.positionWorld.z, d = std::pow(q.positionWorld.x + 2, 2) +
												   q.positionWorld.y * q.positionWorld.y + dz * dz;
			double truth = 100 * dz / std::pow(d, 1.5);
			coarseError += std::pow(scalar(*t, coarseValues, q.positionWorld) - truth, 2);
			fineError += std::pow(scalar(*fine, fineValues, q.positionWorld) - truth, 2);
			++physicalCount;
		}
		double coarsePhysicalRMSE = std::sqrt(coarseError / physicalCount),
			   finePhysicalRMSE = std::sqrt(fineError / physicalCount);
		CHECK(finePhysicalRMSE < coarsePhysicalRMSE);
		std::shared_ptr<const P::Topology> limited;
		Cancel cancel;
		ReadContext cancelled;
		cancelled.cancellation = &cancel;
		CHECK(P::Build(input, empty.options, cancelled, limited).code == Code::Cancelled);
		dense.maxNodes = 1;
		CHECK(P::Build(input, dense, ReadContext(), limited).code == Code::WorkLimit);
		dense = empty.options;
		dense.maxBytes = 128;
		CHECK(P::Build(input, dense, ReadContext(), limited).code == Code::MemoryLimit);
		auto wrong = input;
		wrong.geometry[0] ^= 1;
		CHECK(P::Build(wrong, empty.options, ReadContext(), limited).code == Code::InvalidState);
		dense = empty.options;
		dense.maxProbes = 1;
		CHECK(P::Build(input, dense, ReadContext(), limited).code == Code::WorkLimit);
		// Directional world basis and independent RGB: compare adapter to P03 at one exact probe.
		auto ra = Asset(*t, true);
		auto rv = Values(*t, 0, true);
		for (auto &x : rv.records)
		{
			x.angular.degree = 1;
			x.angular.sh.resize(4);
			x.angular.sh[1] = {{.2 * (1 + .05 * x.evaluatedPositionWorld.x), 0, 0}};
			x.angular.sh[3] = {{0, 0, .2 * (1 + .05 * x.evaluatedPositionWorld.y)}};
		}
		auto rp = Prepared(t, rv, ra);
		rq.positionWorld = t->Probes()[0].positionWorld;
		rq.directionToSource = Vec3(1, 0, 0);
		CHECK(rp->IncomingRadiance(ra, rq, sample) == Status::Ok);
		auto ref = Angular::Evaluate(rv.records[0].angular, Angular::V{{1, 0, 0}},
									 Angular::Quantity::IncomingRadiance);
		CHECK(std::abs(sample.rgb.z - ref.rgb[2]) < 1e-6);
		double angularSE = 0;
		for (unsigned j = 0; j < 200; ++j)
		{
			rq.positionWorld =
				Vec3(-7.5f + 15.f * (j % 20) / 19.f, -3.5f + 7.f * ((j / 20) % 10) / 9.f, 3);
			rq.directionToSource = Vec3(.577350269f, .577350269f, .577350269f);
			CHECK(rp->IncomingRadiance(ra, rq, sample) == Status::Ok);
			auto exact = rv.records[0].angular;
			exact.sh[1] = {{.2 * (1 + .05 * rq.positionWorld.x), 0, 0}};
			exact.sh[3] = {{0, 0, .2 * (1 + .05 * rq.positionWorld.y)}};
			auto y = Angular::Evaluate(
				exact,
				Angular::Unit(Angular::V{
					{rq.directionToSource.x, rq.directionToSource.y, rq.directionToSource.z}}),
				Angular::Quantity::IncomingRadiance);
			angularSE +=
				std::pow(sample.rgb.x - y.rgb[0], 2) + std::pow(sample.rgb.z - y.rgb[2], 2);
		}
		double angularRMSE = std::sqrt(angularSE / 400);
		CHECK(angularRMSE < .01);
		// Ordinary prepared queries do not trace. Timing is a separate nonsemantic witness.
		auto start = std::chrono::steady_clock::now();
		uint64_t ok = 0;
		for (unsigned j = 0; j < 100000; ++j)
		{
			q.positionWorld = Vec3(1 + float(j % 10) * .01f, 1, 3);
			ok += prepared->Indirect(a, q, sample) == Status::Ok;
		}
		double ns =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() * 1e4;
		CHECK(ok == 100000);
		if (argc > 1)
		{
			std::ofstream o(argv[1]);
			o << "{\"schema\":\"org.openjk.probe-tests.v1\",\"checks\":" << checks
			  << ",\"failures\":" << failures
			  << ",\"constant_error\":0,\"linear_rmse\":" << gradientRMSE
			  << ",\"directional_colour_spatial_rmse\":" << angularRMSE
			  << ",\"point_irradiance_coarse_rmse\":" << coarsePhysicalRMSE
			  << ",\"point_irradiance_refined_rmse\":" << finePhysicalRMSE
			  << ",\"wall_leak_links\":" << leaks << ",\"wall_links_checked\":" << blockedChecks
			  << ",\"doorway_cross_links\":" << (crosses ? "true" : "false")
			  << ",\"coarse_probes\":" << t->Probes().size()
			  << ",\"adaptive_probes\":" << fine->Probes().size()
			  << ",\"relocated_probes\":" << moved->Diagnostics().relocated
			  << ",\"maximum_relocation\":" << moved->Diagnostics().maximumDisplacement
			  << ",\"topology_bytes\":" << t->Diagnostics().bytes << ",\"query_nanoseconds\":" << ns
			  << ",\"query_rays\":0}";
		}
		std::cout << checks << " checks, " << failures << " failures\n";
		return failures ? 1 : 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 2;
	}
}
