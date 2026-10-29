// S04 own-content CPU controls. Answers are not passed to Generate().
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/bake_math.h"
#include "lightrecon/hidden_candidates.h"
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
using namespace Bake::Detail;
namespace H = Rend2LightRecon::Core::Hidden;
int checks = 0, failures = 0;
#define CHECK(x)                                                                                             \
	do                                                                                                       \
	{                                                                                                        \
		++checks;                                                                                            \
		if (!(x))                                                                                            \
		{                                                                                                    \
			++failures;                                                                                      \
			std::printf("FAIL %d %s\n", __LINE__, #x);                                                       \
		}                                                                                                    \
	} while (0)
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> v)
{
	return VectorChunk<T>(v, 0, v->size());
}
Bounds Box(V a, V b)
{
	Bounds x;
	x.known = true;
	x.minWorld = a.Float();
	x.maxWorld = b.Float();
	return x;
}
class RoomDomain final : public PointClassifier
{
  public:
	bool wall = false, unknown = false, second = false, opening = false, largeSecond = false;
	Error Classify(Vec3 p, double, PointDomain &out) const override
	{
		if (second && p.x > 1024)
		{
			p.x -= 2048;
			if (largeSecond)
			{
				p.x /= 4;
				p.y /= 4;
				p.z /= 4;
			}
		}
		if (opening && std::abs(p.x) < 32 && p.y <= -127.99 && p.y >= -256 && p.z > 0 && p.z < 96)
		{
			out = PointDomain::Free;
			return {};
		}
		out = unknown ? PointDomain::Unknown
					  : (std::abs(p.x) >= 128 || std::abs(p.y) >= 128 || p.z <= 0 || p.z >= 256 ||
						 (wall && std::abs(p.x) < 4))
							? PointDomain::Solid
							: PointDomain::Free;
		return {};
	}
};
class Cancel final : public Cancellation
{
  public:
	bool IsCancelled() const noexcept override { return true; }
};
class UnknownRays final : public VisibilityProvider
{
  public:
	Error Query(const RayQuery &, const ReadContext &, RayResult &out) const override
	{
		out = RayResult();
		out.issues = OpticalUnknown;
		return {};
	}
};
class BlockedRays final : public VisibilityProvider
{
  public:
	bool assumed = false;
	Error Query(const RayQuery &, const ReadContext &, RayResult &r) const override
	{
		r = RayResult();
		r.state = VisibilityState::Blocked;
		r.reliability = RayReliability::Geometric;
		r.issues = assumed ? PolicyAssumption : OpticalUnknown;
		r.terminalHit.present = true;
		r.terminalHit.obstacle = assumed ? ObstacleKind::AlphaTested : ObstacleKind::Opaque;
		return {};
	}
};
struct Scene
{
	H::Request r;
	ReadContext read;
	std::shared_ptr<std::vector<H::Sample>> samples = std::make_shared<std::vector<H::Sample>>();
	std::shared_ptr<RoomDomain> domain = std::make_shared<RoomDomain>();
	Scene(bool wall = false, bool second = false, bool opening = false, bool largeSecond = false)
	{
		read.maxBytes = 128ull * 1024 * 1024;
		r.map.path = "maps/hidden-generated.bsp";
		r.map.sha256[0] = 31;
		r.map.bytes = 1;
		r.options.allowAssumedProfiles = true;
		r.options.directOnlyAttested = true;
		r.options.samplesPerArea = 48;
		r.options.maxNodesPerArea = 512;
		r.options.refinementRounds = 2;
		r.options.retainedPerChannelProfile = 5;
		r.options.spots = false;
		Bake::Recipe recipe;
		recipe.fast = false;
		r.profiles = {Bake::NetRadiantProfile(recipe)};
		r.domain = domain;
		r.domainKey[0] = wall ? 2 : largeSecond ? 4 : second ? 3 : opening ? 5 : 1;
		domain->wall = wall;
		domain->second = second;
		domain->opening = opening;
		domain->largeSecond = largeSecond;
		r.analysisBounds = Box(V(-127, -127, 1), V(127, 127, 255));
		H::SearchArea area;
		area.id = 1;
		area.positions = Box(V(-120, -120, 16), V(120, 120, 240));
		area.receivers = Box(V(-129, -129, -1), V(129, 129, 257));
		area.reason = "Predeclared whole synthetic room, not answer coordinates";
		r.areas = {area};
		SurfaceCapture capture;
		capture.Begin(BuildPolicy::ForceRebuild);
		CHECK(capture.SetMap(r.map));
		double offset = 0, scale = 1;
		bool skyFace = false;
		auto quad = [&](V a, V b, V c, V d) {
			BakedSurface s;
			s.ordinal = uint32_t(surfaces++);
			s.kind = BakedSurfaceKind::Face;
			s.motion = SurfaceMotion::StaticWorld;
			s.material = "fixture/white";
			s.materialFacts.available = true;
			s.materialFacts.cull = SurfaceCull::None;
			s.materialFacts.signals = skyFace ? MaterialSky : 0;
			for (V p : {a, b, c, d})
			{
				BakedVertex v;
				v.position = (p * scale + V(offset, 0, 0)).Float();
				s.vertices.push_back(v);
			}
			s.indices = {0, 1, 2, 0, 2, 3};
			CHECK(capture.Add(s));
		};
		for (int room = 0; room < (second ? 2 : 1); ++room)
		{
			offset = room * 2048;
			scale = largeSecond && room ? 4 : 1;
			quad(V(-128, -128, 0), V(128, -128, 0), V(128, 128, 0), V(-128, 128, 0));
			skyFace = largeSecond && room;
			quad(V(-128, -128, 256), V(128, -128, 256), V(128, 128, 256), V(-128, 128, 256));
			skyFace = false;
			quad(V(-128, -128, 0), V(-128, 128, 0), V(-128, 128, 256), V(-128, -128, 256));
			quad(V(128, -128, 0), V(128, 128, 0), V(128, 128, 256), V(128, -128, 256));
			if (!opening)
				quad(V(-128, -128, 0), V(128, -128, 0), V(128, -128, 256), V(-128, -128, 256));
			else
			{
				quad(V(-128, -128, 0), V(-32, -128, 0), V(-32, -128, 256), V(-128, -128, 256));
				quad(V(32, -128, 0), V(128, -128, 0), V(128, -128, 256), V(32, -128, 256));
				quad(V(-32, -128, 96), V(32, -128, 96), V(32, -128, 256), V(-32, -128, 256));
			}
			quad(V(-128, 128, 0), V(128, 128, 0), V(128, 128, 256), V(-128, 128, 256));
			if (wall)
				quad(V(0, -128, 0), V(0, 128, 0), V(0, 128, 256), V(0, -128, 256));
		}
		CHECK(capture.Freeze());
		std::shared_ptr<const StaticVisibility> rays;
		CHECK(StaticVisibility::Build(capture.Acquire(), VisibilityBuildOptions(), read, rays, domain));
		r.geometry.visibility = rays;
		r.geometry.geometryKey = rays->CertificationKey();
		for (int room = 0; room < (second ? 2 : 1); ++room)
			for (int face = 0; face < 6; ++face)
				for (int j = 0; j < 8; ++j)
				{
					if (largeSecond && room && face == 1)
						continue; // sky is not a baked diffuse receiver
					double a = (j % 4 - 1.5) * 56, b = (j / 4 ? 64. : 192.);
					if (opening && face == 4 && std::abs(a) < 32 && b < 96)
						continue; // no fabricated receiver in the opening
					H::Sample s;
					s.eligible = s.encodingAttested = true;
					s.areaWorldSquared = 256;
					s.receiver.sample = samples->size() + 1;
					s.receiver.surface = room * 6 + face + 1;
					if (opening && face == 4)
						s.receiver.surface = a < -32 ? 5 : a > 32 ? 6 : 7;
					if (opening && face == 5)
						s.receiver.surface = 8;
					s.receiver.material = 1;
					V p, n;
					if (face == 0)
					{
						p = V(a, (j / 4 ? 56 : -56), 0);
						n = V(0, 0, 1);
					}
					if (face == 1)
					{
						p = V(a, (j / 4 ? 56 : -56), 256);
						n = V(0, 0, -1);
					}
					if (face == 2)
					{
						p = V(-128, a, b);
						n = V(1, 0, 0);
					}
					if (face == 3)
					{
						p = V(128, a, b);
						n = V(-1, 0, 0);
					}
					if (face == 4)
					{
						p = V(a, -128, b);
						n = V(0, 1, 0);
					}
					if (face == 5)
					{
						p = V(a, 128, b);
						n = V(0, -1, 0);
					}
					double receiverScale = largeSecond && room ? 4 : 1;
					s.receiver.world = (p * receiverScale + V(room * 2048, 0, 0)).Float();
					s.areaWorldSquared *= receiverScale * receiverScale;
					s.receiver.geometricNormal = s.receiver.shadingNormal = n.Float();
					s.receiver.mappedPlanar = s.receiver.compilerPlacementCertified = true;
					samples->push_back(s);
				}
		if (second)
		{
			auto a = r.areas[0];
			a.id = 2;
			if (largeSecond)
			{
				a.positions.minWorld = (V(a.positions.minWorld) * 4).Float();
				a.positions.maxWorld = (V(a.positions.maxWorld) * 4).Float();
				a.receivers.minWorld = (V(a.receivers.minWorld) * 4).Float();
				a.receivers.maxWorld = (V(a.receivers.maxWorld) * 4).Float();
			}
			a.positions.minWorld.x += 2048;
			a.positions.maxWorld.x += 2048;
			a.receivers.minWorld.x += 2048;
			a.receivers.maxWorld.x += 2048;
			r.areas.push_back(a);
			r.analysisBounds.maxWorld.x += 2048;
			if (largeSecond)
			{
				r.analysisBounds.maxWorld.x = 2560;
				r.analysisBounds.minWorld.y = -512;
				r.analysisBounds.maxWorld.y = 512;
				r.analysisBounds.maxWorld.z = 1023;
			}
		}
	}
	size_t surfaces = 0;
	void Bake(std::vector<std::pair<V, V>> answers, bool spot = false)
	{
		auto emitters = std::make_shared<std::vector<Bake::SourceHypothesis>>();
		for (const auto &a : answers)
		{
			Bake::SourceHypothesis s;
			s.id = emitters->size() + 1;
			s.kind = spot ? Bake::SourceKind::Spot : Bake::SourceKind::Point;
			s.position.known = true;
			s.position.world = a.first.Float();
			s.amplitudeKnown = true;
			s.compilerRgbAmplitude = a.second.Float();
			if (spot)
			{
				s.orientation.known = true;
				s.orientation.forward = Vec3(0, 0, -1);
				s.orientation.right = Vec3(1, 0, 0);
				s.orientation.up = Vec3(0, -1, 0);
				s.spotTargetDistanceWorld = 128;
				s.spotRadiusWorld = 128;
			}
			emitters->push_back(s);
		}
		auto receivers = std::make_shared<std::vector<Bake::Receiver>>();
		for (auto &s : *samples)
			receivers->push_back(s.receiver);
		Bake::Options o;
		o.allowAssumed = true;
		Bake::Result result;
		Bake::CpuForwardEvaluator evaluator;
		CHECK(evaluator.Evaluate(r.profiles[0], Lease(emitters), Lease(receivers), Decode::Profile(),
								 r.geometry, o, read, result));
		for (size_t i = 0; i < samples->size(); ++i)
		{
			CHECK(result.predictions.data[i].available);
			for (int c = 0; c < 3; ++c)
				(*samples)[i].rgb[c] = result.predictions.data[i].packedRgb[c];
		}
	}
	void DoorTransport()
	{
		// Prescribed incoming boundary radiance RGB/pi through a real aperture, with NO physical
		// source in the receiver room. This is a transport boundary condition,
		// not point-light ground truth or exact q3map bounce replay.
		for (auto &s : *samples)
		{
			V origin = V(s.receiver.world) + V(s.receiver.geometricNormal), value;
			for (double x : {-24., 0., 24.})
				for (double z : {16., 48., 80.})
				{
					V aperture(x, -127.9, z), delta = aperture - origin;
					double d = Length(delta);
					RayQuery q;
					q.startWorld = origin.Float();
					q.endWorld = aperture.Float();
					q.originSurface = s.receiver.surface;
					RayResult hit;
					CHECK(r.geometry.visibility->Query(q, read, hit));
					if (hit.state != VisibilityState::Visible)
						continue;
					double form = std::max(0., Dot(V(s.receiver.geometricNormal), Unit(delta))) *
								  std::max(0., -delta.y / d) * (64 * 96) / (Pi * 9 * d * d);
					value = value + V(240, 150, 100) * form;
				}
			double gain = Peak(value) > 255 ? 255 / Peak(value) : 1;
			for (int c = 0; c < 3; ++c)
				s.rgb[c] = uint8_t(value[c] * gain);
		}
	}
	Error Generate(H::Result &result)
	{
		r.training = Lease(samples);
		auto error = H::Generate(r, read, result);
		if (error)
			for (const auto &candidate : result.evidence)
			{
				PointDomain truth;
				CHECK(domain->Classify(candidate.position, 0, truth));
				CHECK(truth != PointDomain::Solid); // independent analytic room/wall oracle
			}
		return error;
	}
};
struct Witness
{
	std::string name;
	size_t candidates = 0, physical = 0, ambiguous = 0, solid = 0, unknown = 0;
	std::vector<double> distances;
	uint64_t rays = 0, bytes = 0;
};
std::vector<Witness> witnesses;
void Record(const char *name, const H::Result &r, const std::vector<V> &answers = {})
{
	Witness w;
	w.name = name;
	w.candidates = r.candidates.size();
	w.rays = r.report.rays;
	w.bytes = r.report.accountedPeakBytes;
	for (const auto &h : r.hypotheses)
	{
		w.physical += h.kind == H::Kind::PhysicalEmitterCandidate;
		w.ambiguous += h.kind == H::Kind::AmbiguousSourceOrTransport;
		w.unknown += h.unknown;
		CHECK(!h.existenceConfidence.known);
	}
	for (const auto &a : r.report.areas)
		w.solid += a.rejectedSolid;
	for (V truth : answers)
	{
		double best = 1e30;
		for (const auto &e : r.evidence)
			best = std::min(best, Length(V(e.position) - truth));
		w.distances.push_back(best);
	}
	std::printf("%s candidates=%zu physical=%zu unknown-rays=%zu", name, w.candidates, w.physical, w.unknown);
	for (double d : w.distances)
		std::printf(" distance=%.4f", d);
	std::puts("");
	witnesses.push_back(w);
}
int main(int argc, char **argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	V point(32, -16, 160);
	{
		Scene s;
		s.Bake({{point, V(100, 80, 60)}});
		H::Result result;
		CHECK(s.Generate(result));
		CHECK(!result.candidates.empty());
		Record("hidden_point", result, {point});
		CHECK(witnesses.back().distances[0] < 40);
		std::set<Id> ids;
		for (const auto &e : result.evidence)
		{
			CHECK(!e.intensityKnown);
			CHECK(!e.colourKnown);
			CHECK(e.unit == EvidenceUnit::Unknown);
			CHECK(e.origin == EvidenceOrigin::SurfaceObservation);
			CHECK(ids.insert(e.id).second);
			PointDomain pointDomain;
			CHECK(s.domain->Classify(e.position, 0, pointDomain));
			CHECK(pointDomain != PointDomain::Solid);
		}
		H::Result repeat;
		std::reverse(s.samples->begin(), s.samples->end());
		CHECK(s.Generate(repeat));
		CHECK(repeat.key == result.key);
		CHECK(repeat.candidates.size() == result.candidates.size());
		for (size_t i = 0; i < std::min(repeat.candidates.size(), result.candidates.size()); ++i)
			CHECK(repeat.candidates[i].id == result.candidates[i].id);
		// A later trusted controller requests more geometry search, not held-out RGB.
		s.r.areas[0].id = 77;
		s.r.areas[0].reason = "Additional unexplained-area geometry request";
		CHECK(s.Generate(repeat));
		CHECK(repeat.key != result.key);
	}
	{
		Scene s;
		V a(-64, -32, 180), b(64, 32, 96);
		s.Bake({{a, V(110, 8, 3)}, {b, V(3, 8, 110)}});
		H::Result r;
		CHECK(s.Generate(r));
		Record("two_colours", r, {a, b});
		for (double d : witnesses.back().distances)
			CHECK(d < 64);
	}
	{
		Scene s;
		V a(-16, -8, 160), b(16, 8, 160);
		s.Bake({{a, V(70, 70, 70)}, {b, V(70, 70, 70)}});
		H::Result r;
		CHECK(s.Generate(r));
		Record("nearby_white_ambiguous_count", r, {a, b});
		CHECK(r.candidates.size() > 1);
		for (const auto &h : r.hypotheses)
			CHECK(!h.existenceConfidence.known);
	}
	{
		Scene s;
		s.r.options.spots = true;
		s.Bake({{point, V(150, 150, 150)}}, true);
		H::Result r;
		CHECK(s.Generate(r));
		Record("hidden_spot", r, {point});
		CHECK(witnesses.back().distances[0] < 80);
		bool spot = false;
		for (const auto &h : r.hypotheses)
			spot |= h.sourceKind == Bake::SourceKind::Spot;
		CHECK(spot);
	}
	{
		Scene s(true);
		s.r.options.coarseDivisions = 3;
		s.Bake({{V(-64, -16, 160), V(100, 100, 100)}});
		H::Result r;
		CHECK(s.Generate(r));
		Record("known_wall", r, {V(-64, -16, 160)});
		for (const auto &e : r.evidence)
			CHECK(std::abs(e.position.x) >= 4);
		CHECK(witnesses.back().solid > 0);
	}
	{
		Scene s(false, false, true);
		s.DoorTransport();
		s.r.options.directOnlyAttested = false;
		for (auto &v : *s.samples)
			v.transportSuspected = true;
		H::Result r;
		CHECK(s.Generate(r));
		Record("bright_doorway_transport", r);
		for (const auto &h : r.hypotheses)
			CHECK(h.kind == H::Kind::TransportEvidence);
		CHECK(witnesses.back().physical == 0);
		for (auto &v : *s.samples)
			v.transportSuspected = false;
		CHECK(s.Generate(r));
		Record("unlabelled_bright_transport_ambiguous", r);
		for (const auto &h : r.hypotheses)
			CHECK(h.kind == H::Kind::AmbiguousSourceOrTransport);
	}
	{
		Scene s;
		for (size_t i = 0; i < s.samples->size(); ++i)
		{
			auto &v = (*s.samples)[i];
			v.receiver.material = i % 2 + 1;
			for (auto &c : v.rgb)
				c = i % 2 ? 150 : 30;
		}
		H::Result r;
		CHECK(s.Generate(r).code == Code::Unavailable);
		CHECK(r.candidates.empty());
		Record("material_boundary_only", r);
	}
	{
		Scene s;
		for (auto &v : *s.samples)
			for (auto &c : v.rgb)
				c = 255;
		H::Result r;
		CHECK(s.Generate(r).code == Code::Unavailable);
		CHECK(r.candidates.empty());
		Record("saturated_plateau", r);
	}
	{
		Scene s;
		s.Bake({{point, V(100, 80, 60)}});
		s.r.options.directOnlyAttested = false;
		s.r.geometry.visibility = std::make_shared<UnknownRays>();
		s.r.geometry.geometryKey[0] = 97;
		H::Result r;
		CHECK(s.Generate(r));
		Record("unknown_optics_missing_regions", r);
		for (const auto &h : r.hypotheses)
		{
			CHECK(!h.scoreAvailable);
			CHECK(h.positionUnderconstrained);
			CHECK(h.kind != H::Kind::PhysicalEmitterCandidate);
			CHECK(h.unknown > 0);
		}
	}
	{
		Scene s;
		s.Bake({{point, V(100, 80, 60)}});
		s.samples->resize(8);
		H::Result r;
		CHECK(s.Generate(r));
		Record("one_plane_underconstrained", r, {point});
		for (const auto &h : r.hypotheses)
		{
			CHECK(h.positionUnderconstrained);
			CHECK(h.kind != H::Kind::PhysicalEmitterCandidate);
		}
	}
	{
		Scene s;
		s.Bake({{point, V(100, 80, 60)}});
		GridCell cell;
		cell.presence = GridPresence::Present;
		cell.styleStatus = GridStyle::Static;
		cell.direction = DecodeGridDirection(70, 230);
		cell.centreWorld = Vec3(-80, 0, 80);
		s.r.directions = {H::GridConstraint(cell, 901, .7)};
		H::Result r;
		CHECK(s.Generate(r));
		Record("noisy_sparse_grid_seed", r, {point});
		CHECK(witnesses.back().distances[0] < 40);
		s.samples->clear();
		CHECK(s.Generate(r).code == Code::Unavailable);
		CHECK(r.candidates.empty());
	}
	{
		Scene s;
		s.Bake({{point, V(100, 80, 60)}});
		auto wrong = Bake::Recipe();
		wrong.fast = false;
		wrong.falloff = Bake::Falloff::Linear;
		s.r.profiles.push_back(Bake::NetRadiantProfile(wrong));
		H::Result r;
		CHECK(s.Generate(r));
		Record("competing_bake_profiles", r, {point});
		std::set<std::string> p;
		for (const auto &h : r.hypotheses)
			p.insert(h.profile);
		CHECK(p.size() == 2);
	}
	{
		Scene s;
		s.Bake({{point, V(100, 80, 60)}});
		auto patch = s.samples->front();
		patch.receiver.sample = 999;
		patch.receiver.mappedPlanar = false;
		patch.receiver.geometryErrorWorld = 2;
		s.samples->push_back(patch);
		s.r.options.samplesPerArea = 64;
		H::Result result;
		CHECK(s.Generate(result));
		CHECK(result.report.areas[0].unsupportedReceivers == 1);
		CHECK(result.report.areas[0].selected == 48);
		Record("unsupported_receiver_does_not_poison_faces", result, {point});
		CHECK(witnesses.back().distances[0] < 40);
	}
	{
		Scene s;
		s.Bake({{point, V(100, 80, 60)}});
		BspSpaceInput space;
		space.identity = s.r.map;
		space.worldBounds = Box(V(-128, -128, 0), V(128, 128, 256));
		SpatialPlane split;
		split.normal = Vec3(1, 0, 0);
		space.planes.push_back(split);
		SpatialNode node;
		node.children[0] = -1;
		node.children[1] = -2;
		space.nodes.push_back(node);
		SpatialLeaf leaf;
		leaf.cluster = 0;
		leaf.area = 0;
		space.leaves = {leaf, leaf};
		std::shared_ptr<const BspRegionDomain> domain;
		CHECK(BspRegionDomain::Create(space, s.read, domain));
		RegionBuildInput input;
		input.identity = s.r.map;
		input.domain = domain;
		input.geometry = std::dynamic_pointer_cast<const StaticVisibility>(s.r.geometry.visibility);
		RegionOptions options;
		options.bounds = s.r.areas[0].positions;
		options.coarseCellWorld = 64;
		options.minimumCellWorld = 32;
		options.inspectSky = false;
		std::shared_ptr<const LightingRegionGraph> graph;
		CHECK(LightingRegionGraph::Build(input, options, s.read, graph));
		CHECK(graph && !graph->Regions().empty());
		s.r.regions = graph;
		H::Result result;
		CHECK(s.Generate(result));
		bool assigned = false;
		for (const auto &h : result.hypotheses)
			assigned |= h.region != 0 && h.context == H::Context::CertifiedRegion;
		CHECK(assigned);
		Record("certified_region_optional_context", result, {point});
		Scene changed(true);
		s.r.geometry = changed.r.geometry;
		s.r.domain = changed.r.domain;
		s.r.domainKey = changed.r.domainKey;
		CHECK(s.Generate(result).code == Code::InvalidInput);
		CHECK(result.candidates.empty());
	}
	{
		Scene s(false, true, false, true);
		s.Bake({{point, V(5, 4, 3)}, {point + V(2048, 0, 0), V(3000, 2400, 1800)}});
		H::Result r;
		CHECK(s.Generate(r));
		Record("dark_room_plus_bright_courtyard", r, {point, point + V(2048, 0, 0)});
		CHECK(r.report.areas.size() == 2);
		if (r.report.areas.size() == 2)
			CHECK(r.report.areas[0].retained > 0 && r.report.areas[1].retained > 0);
	}
	{
		Scene s;
		s.Bake({{point, V(100, 80, 60)}});
		H::Result r;
		Cancel cancel;
		s.read.cancellation = &cancel;
		CHECK(s.Generate(r).code == Code::Cancelled);
		CHECK(r.candidates.empty());
		s.read.cancellation = nullptr;
		s.r.options.maxRays = 1;
		CHECK(s.Generate(r).code == Code::WorkLimit);
		CHECK(r.candidates.empty());
		s.r.options.maxRays = 2000000;
		s.r.options.maxBytes = 1;
		CHECK(s.Generate(r).code == Code::MemoryLimit);
		s.r.options.maxBytes = 64ull * 1024 * 1024;
		s.r.profiles = {Bake::ProfileHypothesis()};
		CHECK(s.Generate(r).code == Code::Unavailable);
		CHECK(r.candidates.empty());
		s.r.profiles = {Bake::NetRadiantProfile()};
		s.samples->front().receiver.world.x = std::numeric_limits<float>::quiet_NaN();
		CHECK(s.Generate(r).code == Code::InvalidInput);
	}
	{
		Scene s;
		s.Bake({{point, V(100, 80, 60)}});
		auto rays = std::make_shared<BlockedRays>();
		s.r.geometry.visibility = rays;
		s.r.geometry.geometryKey[0] = 83;
		H::Result r;
		CHECK(s.Generate(r).code == Code::Unavailable);
		CHECK(r.candidates.empty());
		Record("opaque_wall_survives_uncertain_foreground", r);
		rays->assumed = true;
		s.r.geometry.geometryKey[0] = 84;
		CHECK(s.Generate(r));
		for (const auto &h : r.hypotheses)
		{
			CHECK(!h.scoreAvailable);
			CHECK(h.kind != H::Kind::PhysicalEmitterCandidate);
		}
		Record("assumed_alpha_block_is_unknown", r);
	}
	{
		Scene s;
		Decode::Raw raw;
		raw.bytes[0] = 255;
		raw.bytes[1] = 127;
		raw.bytes[2] = 0;
		Decode::Observation observation;
		Decode::Decoder decoder;
		CHECK(decoder.Run(raw, observation));
		H::Sample sample;
		CHECK(
			H::FromObservation(s.samples->front().receiver, observation, 256, true, true, 0, false, sample));
		CHECK(sample.rgb[0] == 255 && sample.rgb[1] == 127 && !sample.encodingAttested);
		observation.raw.directionData = true;
		CHECK(H::FromObservation(s.samples->front().receiver, observation, 256, true, true, 0, false, sample)
				  .code == Code::Unavailable);
	}
	if (argc > 1)
	{
		std::ofstream f(argv[1]);
		f << std::setprecision(12) << "{\"checks\":" << checks << ",\"failures\":" << failures
		  << ",\"scenes\":[";
		for (size_t i = 0; i < witnesses.size(); ++i)
		{
			auto &w = witnesses[i];
			if (i)
				f << ',';
			f << "{\"name\":\"" << w.name << "\",\"candidates\":" << w.candidates
			  << ",\"physical_candidates\":" << w.physical << ",\"ambiguous_candidates\":" << w.ambiguous
			  << ",\"known_solid_rejected\":" << w.solid << ",\"unknown_rays\":" << w.unknown
			  << ",\"rays\":" << w.rays << ",\"accounted_bytes\":" << w.bytes << ",\"nearest_truth_world\":[";
			for (size_t j = 0; j < w.distances.size(); ++j)
			{
				if (j)
					f << ',';
				f << w.distances[j];
			}
			f << "]}";
		}
		f << "]}\n";
	}
	std::printf("S04 %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
