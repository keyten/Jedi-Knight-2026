// Generated topology fixtures. No renderer, GPU, runtime or commercial assets.
#include "lightrecon/regions.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace
{
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
ReadContext Context()
{
	ReadContext c;
	c.maxBytes = 256ull * 1024 * 1024;
	return c;
}
Bounds Box(Vec3 a, Vec3 b)
{
	Bounds bounds;
	bounds.known = true;
	bounds.minWorld = a;
	bounds.maxWorld = b;
	return bounds;
}
double Axis(Vec3 p, unsigned k)
{
	return k == 0 ? p.x : k == 1 ? p.y : p.z;
}
bool In(const Bounds &b, Vec3 p, double inset = 0)
{
	for (unsigned k = 0; k < 3; ++k)
		if (Axis(p, k) < Axis(b.minWorld, k) + inset || Axis(p, k) > Axis(b.maxWorld, k) - inset)
			return false;
	return true;
}
bool Overlap(const Bounds &a, const Bounds &b)
{
	for (unsigned k = 0; k < 3; ++k)
		if (Axis(a.maxWorld, k) < Axis(b.minWorld, k) || Axis(a.minWorld, k) > Axis(b.maxWorld, k))
			return false;
	return true;
}
class Domain : public RegionDomain
{
  public:
	Bounds world;
	std::vector<Bounds> solids;
	bool exterior = false;
	Error Locate(Vec3 p, SpatialLocation &out, double tolerance = 1e-5) const override
	{
		out = SpatialLocation();
		out.work = 1;
		out.leaf = int32_t(std::floor((double(p.x) - world.minWorld.x) / 2));
		out.cluster = out.leaf;
		out.area = int32_t(std::floor(p.z / 8));
		if (!In(world, p))
		{
			out.space = SpaceClass::Outside;
			return Error();
		}
		for (const auto &b : solids)
		{
			++out.work;
			if (In(b, p, -tolerance))
			{
				out.boundary = !In(b, p, tolerance);
				out.space = out.boundary ? SpaceClass::Unknown : SpaceClass::Solid;
				return Error();
			}
		}
		out.space = exterior ? SpaceClass::Exterior : SpaceClass::Free;
		return Error();
	}
	Error ClassifyBox(const Bounds &box, SpaceClass &out, uint64_t &work) const override
	{
		work = 1;
		out = SpaceClass::Unknown;
		if (!Overlap(world, box))
		{
			out = SpaceClass::Outside;
			return Error();
		}
		if (!In(world, box.minWorld) || !In(world, box.maxWorld))
			return Error();
		for (const auto &b : solids)
		{
			++work;
			if (In(b, box.minWorld, 1e-5) && In(b, box.maxWorld, 1e-5))
			{
				out = SpaceClass::Solid;
				return Error();
			}
			if (Overlap(b, box))
				return Error();
		}
		out = exterior ? SpaceClass::Exterior : SpaceClass::Free;
		return Error();
	}
};
struct Fixture
{
	std::shared_ptr<Domain> domain = std::make_shared<Domain>();
	std::vector<BakedSurface> surfaces;
	RegionOptions options;
	Fixture()
	{
		domain->world = Box(Vec3(-12, -6, 0), Vec3(12, 6, 8));
		options.bounds = domain->world;
		options.coarseCellWorld = 4;
		options.minimumCellWorld = .5f;
		options.constrictionWorld = 2;
		options.maxBytes = 256ull * 1024 * 1024;
	}
	void Quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, uint32_t signals = 0)
	{
		BakedSurface s;
		s.ordinal = uint32_t(surfaces.size());
		s.kind = BakedSurfaceKind::Face;
		s.motion = SurfaceMotion::StaticWorld;
		s.materialFacts.available = true;
		s.materialFacts.cull = SurfaceCull::None;
		s.materialFacts.signals = signals;
		s.materialIndex = s.ordinal;
		for (auto p : {a, b, c, d})
		{
			BakedVertex v;
			v.position = p;
			s.vertices.push_back(v);
		}
		s.indices = {0, 1, 2, 0, 2, 3};
		surfaces.push_back(s);
	}
	void Solid(Bounds b, bool draw = true)
	{
		domain->solids.push_back(b);
		if (!draw)
			return;
		auto l = b.minWorld, h = b.maxWorld;
		Quad(Vec3(l.x, l.y, l.z), Vec3(l.x, h.y, l.z), Vec3(l.x, h.y, h.z), Vec3(l.x, l.y, h.z));
		Quad(Vec3(h.x, l.y, l.z), Vec3(h.x, h.y, l.z), Vec3(h.x, h.y, h.z), Vec3(h.x, l.y, h.z));
		Quad(Vec3(l.x, l.y, l.z), Vec3(h.x, l.y, l.z), Vec3(h.x, l.y, h.z), Vec3(l.x, l.y, h.z));
		Quad(Vec3(l.x, h.y, l.z), Vec3(h.x, h.y, l.z), Vec3(h.x, h.y, h.z), Vec3(l.x, h.y, h.z));
		Quad(Vec3(l.x, l.y, l.z), Vec3(h.x, l.y, l.z), Vec3(h.x, h.y, l.z), Vec3(l.x, h.y, l.z));
		Quad(Vec3(l.x, l.y, h.z), Vec3(h.x, l.y, h.z), Vec3(h.x, h.y, h.z), Vec3(l.x, h.y, h.z));
	}
	void Shell(bool sky = false)
	{
		auto l = domain->world.minWorld, h = domain->world.maxWorld;
		Quad(Vec3(l.x, l.y, l.z), Vec3(l.x, h.y, l.z), Vec3(l.x, h.y, h.z), Vec3(l.x, l.y, h.z));
		Quad(Vec3(h.x, l.y, l.z), Vec3(h.x, h.y, l.z), Vec3(h.x, h.y, h.z), Vec3(h.x, l.y, h.z));
		Quad(Vec3(l.x, l.y, l.z), Vec3(h.x, l.y, l.z), Vec3(h.x, l.y, h.z), Vec3(l.x, l.y, h.z));
		Quad(Vec3(l.x, h.y, l.z), Vec3(h.x, h.y, l.z), Vec3(h.x, h.y, h.z), Vec3(l.x, h.y, h.z));
		Quad(Vec3(l.x, l.y, l.z), Vec3(h.x, l.y, l.z), Vec3(h.x, h.y, l.z), Vec3(l.x, h.y, l.z));
		Quad(Vec3(l.x, l.y, h.z), Vec3(h.x, l.y, h.z), Vec3(h.x, h.y, h.z), Vec3(l.x, h.y, h.z),
			 sky ? uint32_t(MaterialSky) : 0u);
	}
	RegionBuildInput Input() const
	{
		SurfaceCapture capture;
		capture.Begin(BuildPolicy::ForceRebuild);
		LightmapMapIdentity id;
		id.path = "maps/regions.bsp";
		id.bytes = 64;
		id.sha256[0] = 33;
		CHECK(capture.SetMap(id));
		for (auto s : surfaces)
			CHECK(capture.Add(std::move(s)));
		CHECK(capture.Freeze());
		RegionBuildInput input;
		input.identity = id;
		input.domain = domain;
		VisibilityBuildOptions o;
		CHECK(StaticVisibility::Build(capture.Acquire(), o, Context(), input.geometry, domain));
		return input;
	}
	std::shared_ptr<const LightingRegionGraph>
	Build(std::vector<DynamicBoundary> dynamic = {}) const
	{
		auto input = Input();
		input.dynamic = std::move(dynamic);
		std::shared_ptr<const LightingRegionGraph> graph;
		auto e = LightingRegionGraph::Build(input, options, Context(), graph);
		if (!e)
			std::printf("BUILD ERROR %d %s\n", int(e.code), e.message.c_str());
		CHECK(e);
		CHECK(graph);
		return graph;
	}
};
RegionAssignment Lookup(const std::shared_ptr<const LightingRegionGraph> &g, Vec3 p)
{
	RegionAssignment a;
	CHECK(g->Lookup(p, Context(), a));
	return a;
}
void Invariants(const std::shared_ptr<const LightingRegionGraph> &graph)
{
	std::set<Id> ids;
	size_t members = 0;
	for (const auto &r : graph->Regions())
	{
		CHECK(r.id != 0 && ids.insert(r.id).second);
		CHECK(r.memberCount > 0);
		CHECK(graph->Find(r.id) == &r);
		members += r.memberCount;
		CHECK(Lookup(graph, r.representativeWorld).region == r.id);
		for (uint32_t k = 0; k < r.neighbourCount; ++k)
		{
			auto e = graph->Neighbours()[r.firstNeighbour + k];
			CHECK(e < graph->Connections().size());
			CHECK(graph->Connections()[e].a == r.id || graph->Connections()[e].b == r.id);
		}
	}
	CHECK(members == graph->Report().freeCells);
	for (const auto &c : graph->Cells())
		CHECK((c.space == SpaceClass::Free || c.space == SpaceClass::Exterior) == (c.region != 0));
	for (const auto &e : graph->Connections())
	{
		CHECK(e.a < e.b);
		CHECK(graph->Find(e.a) && graph->Find(e.b));
		CHECK(e.sampledAreaWorldSquared > 0);
		CHECK(e.apertureCandidate);
	}
}
void Rooms()
{
	Fixture sealed;
	sealed.Shell();
	sealed.Solid(Box(Vec3(-.25f, -6, 0), Vec3(.25f, 6, 8)));
	auto graph = sealed.Build();
	auto left = Lookup(graph, Vec3(-5, 0, 4)), right = Lookup(graph, Vec3(5, 0, 4));
	CHECK(left.state == RegionAssignmentState::Assigned &&
		  right.state == RegionAssignmentState::Assigned);
	CHECK(left.region != right.region);
	CHECK(left.staticComponent != right.staticComponent);
	CHECK(graph->Report().staticComponents == 2);
	CHECK(graph->Connections().empty());
	GridCell grid;
	grid.presence = GridPresence::Present;
	grid.centreWorld = Vec3(0, 0, 4);
	grid.styles[0] = 0;
	grid.ambient[0][0] = 127;
	RegionAssignment a;
	CHECK(graph->AssignGrid(grid, Context(), a));
	CHECK(a.state == RegionAssignmentState::Solid && a.region == 0);
	LightgridCapture rawGrid;
	rawGrid.Begin(BuildPolicy::ForceRebuild);
	uint8_t record[kGridRecordBytes] = {}, index[2] = {};
	record[0] = 127;
	record[24] = 0;
	GridLayout layout;
	layout.originWorld = Vec3(0, 0, 4);
	layout.spacingWorld = Vec3(1, 1, 1);
	layout.bounds[0] = layout.bounds[1] = layout.bounds[2] = 1;
	CHECK(
		rawGrid.SetSource(graph->Identity(), record, sizeof(record), 0, index, sizeof(index), 30));
	CHECK(rawGrid.SetLayout(layout));
	CHECK(rawGrid.Freeze());
	CHECK(graph->AssignGrid(*rawGrid.Acquire(), 0, Context(), a));
	CHECK(a.state == RegionAssignmentState::Solid);
	grid.centreWorld = Vec3(-5, 0, 4);
	CHECK(graph->AssignGrid(grid, Context(), a));
	CHECK(a.region == left.region);
	grid.presence = GridPresence::Absent;
	CHECK(graph->AssignGrid(grid, Context(), a));
	CHECK(a.state == RegionAssignmentState::MissingObservation);
	RegionPairFilter pair;
	CHECK(graph->FilterPair(Vec3(-5, 0, 4), Vec3(5, 0, 4), Context(), pair));
	CHECK(pair.relation == SampleGraphRelation::DisconnectedSampleGraph);
	CHECK(pair.direct.state == VisibilityState::Blocked);
	Invariants(graph);

	Fixture door;
	door.Shell();
	door.Solid(Box(Vec3(-.25f, -6, 0), Vec3(.25f, -2, 8)));
	door.Solid(Box(Vec3(-.25f, 2, 0), Vec3(.25f, 6, 8)));
	door.Solid(Box(Vec3(-.25f, -2, 6), Vec3(.25f, 2, 8)));
	auto open = door.Build();
	left = Lookup(open, Vec3(-5, 0, 4));
	right = Lookup(open, Vec3(5, 0, 4));
	CHECK(left.staticComponent == right.staticComponent);
	CHECK(left.region != right.region);
	CHECK(open->Report().staticComponents == 1);
	CHECK(!open->Connections().empty());
	bool aperture = false;
	for (const auto &e : open->Connections())
		if (e.transport == TransportConnection::OpenStaticSample && e.clearSamples)
			aperture = true;
	CHECK(aperture);
	Invariants(open);
	CHECK(open->FilterPair(Vec3(-5, 0, 4), Vec3(5, 0, 4), Context(), pair));
	CHECK(pair.direct.state == VisibilityState::Visible);
	CHECK(pair.relation == SampleGraphRelation::SameStaticComponent);
	// A fixed closed door is geometry, while a movable placeholder is conditional.
	DynamicBoundary moving;
	moving.id = 1;
	moving.model = 1;
	moving.bounds = Box(Vec3(-.5f, -2, 0), Vec3(.5f, 2, 6));
	auto dynamic = door.Build({moving});
	CHECK(dynamic->Report().dynamicCells > 0);
	CHECK(dynamic->Report().dynamicExtentUnknown);
	bool conditional = false;
	for (const auto &e : dynamic->Connections())
		conditional = conditional || e.transport == TransportConnection::ConditionalDynamic;
	CHECK(conditional);
	CHECK(dynamic->FilterPair(Vec3(-5, 0, 4), Vec3(0, 0, 4), Context(), pair));
	CHECK(pair.relation == SampleGraphRelation::Unknown);
	CHECK(pair.direct.state == VisibilityState::Visible);
	Invariants(dynamic);
	CHECK(pair.dynamicPossible && pair.dynamicExtentUnknown);
	door.Solid(Box(Vec3(-.25f, -2, 0), Vec3(.25f, 2, 6)));
	auto closed = door.Build();
	CHECK(closed->Report().staticComponents == 2);
	CHECK(closed->Connections().empty());
	std::string obj1, obj2;
	CHECK(open->DebugObj(100000, 16 * 1024 * 1024, obj1));
	auto input = sealed.Input();
	std::shared_ptr<const LightingRegionGraph> same;
	CHECK(LightingRegionGraph::Build(input, sealed.options, Context(), same));
	std::string base;
	CHECK(graph->DebugObj(100000, 16 * 1024 * 1024, base));
	CHECK(same->DebugObj(100000, 16 * 1024 * 1024, obj2));
	CHECK(base == obj2);
	CHECK(base.find("lighting region graph") != std::string::npos);
	CHECK(same->DebugObj(1000, 1, obj2).code == Code::MemoryLimit);
	CHECK(obj2.empty());
	std::printf("region rooms: sealed=%llu open=%llu dynamic=%llu aperture-edges=%llu free=%llu "
				"unknown=%llu bytes=%llu\n",
				static_cast<unsigned long long>(graph->Report().regions),
				static_cast<unsigned long long>(open->Report().regions),
				static_cast<unsigned long long>(dynamic->Report().regions),
				static_cast<unsigned long long>(open->Report().apertures),
				static_cast<unsigned long long>(open->Report().freeCells),
				static_cast<unsigned long long>(open->Report().unknownCells),
				static_cast<unsigned long long>(open->Report().ownedBytes));
}
void CorridorFloorsAndExterior()
{
	Fixture corridor;
	corridor.domain->world = Box(Vec3(-32, -2, 0), Vec3(32, 2, 4));
	corridor.options.bounds = corridor.domain->world;
	corridor.Shell();
	auto hall = corridor.Build();
	CHECK(hall->Report().staticComponents == 1);
	CHECK(hall->Regions().size() == 1);
	std::set<int32_t> clusters;
	for (const auto &b : hall->BspLinks())
		clusters.insert(b.cluster);
	CHECK(clusters.size() > 8);
	Invariants(hall);
	Fixture floors;
	floors.domain->world = Box(Vec3(-12, -6, 0), Vec3(12, 6, 16));
	floors.options.bounds = floors.domain->world;
	floors.Shell();
	floors.Solid(Box(Vec3(-12, -6, 7.75f), Vec3(12, 6, 8.25f)));
	auto g = floors.Build();
	auto low = Lookup(g, Vec3(2, 0, 4)), high = Lookup(g, Vec3(2, 0, 12));
	CHECK(low.region != high.region);
	CHECK(low.staticComponent != high.staticComponent);
	CHECK(g->Report().staticComponents == 2);
	Invariants(g);
	Fixture thin;
	thin.Shell();
	thin.Quad(Vec3(0, -6, 0), Vec3(0, 6, 0), Vec3(0, 6, 8), Vec3(0, -6, 8));
	auto partition = thin.Build();
	CHECK(partition->Report().staticComponents == 2);
	SurfaceRegionSides sides;
	CHECK(partition->AssignSurfaceSides(Vec3(0, 0, 4), Vec3(1, 0, 0), 7, Context(), sides));
	CHECK(sides.positive.state == RegionAssignmentState::Assigned);
	CHECK(sides.negative.state == RegionAssignmentState::Assigned);
	CHECK(sides.positive.region != sides.negative.region);
	CHECK(sides.positiveOffsetWorld > 0 && sides.negativeOffsetWorld > 0);
	SurfaceCapture source;
	source.Begin(BuildPolicy::ForceRebuild);
	CHECK(source.SetMap(partition->Identity()));
	for (auto s : thin.surfaces)
		CHECK(source.Add(s));
	CHECK(source.Freeze());
	LuxelTile tile;
	tile.geometry = source.Acquire();
	SurfaceLightingObservation luxel;
	luxel.coverage = LuxelCoverage::Mapped;
	luxel.positionKnown = true;
	luxel.positionWorld = Vec3(0, 0, 4);
	luxel.geometricNormal = Vec3(1, 0, 0);
	luxel.surface = 6;
	tile.samples.push_back(luxel);
	CHECK(partition->AssignSurface(tile, 0, Context(), sides));
	CHECK(sides.positive.region != sides.negative.region);
	tile.samples[0].coverage = LuxelCoverage::Uncovered;
	CHECK(partition->AssignSurface(tile, 0, Context(), sides).code == Code::Unavailable);
	CHECK(!partition->Boundaries().empty());
	Invariants(partition);
	Fixture courtyard;
	courtyard.domain->exterior = true;
	courtyard.Shell(true);
	auto outside = courtyard.Build();
	CHECK(outside->Regions().size() == 1);
	CHECK(outside->Report().skyRegions == 1);
	CHECK(outside->Regions()[0].issues & RegionExteriorKnown);
	CHECK(outside->Regions()[0].issues & RegionSkyObserved);
	CHECK(Lookup(outside, Vec3(100, 100, 100)).state == RegionAssignmentState::Outside);
	Invariants(outside);
	// Skylight: separate sky sheet, opaque roof around a hole.
	Fixture skylight;
	skylight.Shell(true);
	skylight.Quad(Vec3(-12, -6, 6), Vec3(-2, -6, 6), Vec3(-2, 6, 6), Vec3(-12, 6, 6));
	skylight.Quad(Vec3(2, -6, 6), Vec3(12, -6, 6), Vec3(12, 6, 6), Vec3(2, 6, 6));
	auto sky = skylight.Build();
	bool observed = false;
	for (const auto &r : sky->Regions())
		observed = observed || (r.issues & RegionSkyObserved);
	CHECK(observed);
	Invariants(sky);
}
BspSpaceInput Bsp()
{
	BspSpaceInput b;
	b.identity.path = "maps/spatial.bsp";
	b.identity.bytes = 1;
	b.worldBounds = Box(Vec3(-4, -4, -4), Vec3(4, 4, 4));
	SpatialPlane split;
	split.normal = Vec3(1, 0, 0);
	b.planes.push_back(split);
	SpatialNode node;
	node.children[0] = -1;
	node.children[1] = -2;
	b.nodes.push_back(node);
	for (unsigned i = 0; i < 2; ++i)
	{
		SpatialLeaf l;
		l.cluster = 7;
		l.area = 2;
		l.firstBrush = i;
		l.brushCount = 1;
		b.leaves.push_back(l);
		b.leafBrushes.push_back(0);
	}
	for (unsigned k = 0; k < 3; ++k)
		for (float sign : {-1.f, 1.f})
		{
			SpatialPlane p;
			p.distance = 1;
			if (k == 0)
				p.normal.x = sign;
			else if (k == 1)
				p.normal.y = sign;
			else
				p.normal.z = sign;
			b.sides.push_back(uint32_t(b.planes.size()));
			b.planes.push_back(p);
		}
	SpatialBrush brush;
	brush.sideCount = 6;
	brush.solid = true;
	b.brushes.push_back(brush);
	return b;
}
class Stop : public Cancellation
{
  public:
	bool IsCancelled() const noexcept override
	{
		return true;
	}
};
class FailedDomain : public Domain
{
  public:
	Error ClassifyBox(const Bounds &, SpaceClass &, uint64_t &) const override
	{
		throw std::runtime_error("fixture");
	}
};
void DomainAndLimits()
{
	auto data = Bsp();
	std::shared_ptr<const BspRegionDomain> domain;
	CHECK(BspRegionDomain::Create(data, Context(), domain));
	SpatialLocation loc;
	CHECK(domain->Locate(Vec3(0, 0, 0), loc));
	CHECK(loc.space == SpaceClass::Solid);
	CHECK(domain->Locate(Vec3(2, 0, 0), loc));
	CHECK(loc.space == SpaceClass::Free && loc.cluster == 7 && loc.leaf == 0);
	CHECK(domain->Locate(Vec3(-2, 0, 0), loc));
	CHECK(loc.space == SpaceClass::Free && loc.cluster == 7 && loc.leaf == 1);
	PointDomain p;
	CHECK(domain->Classify(Vec3(1, 0, 0), 1e-4, p));
	CHECK(p == PointDomain::Boundary);
	SpaceClass space;
	uint64_t work;
	CHECK(domain->ClassifyBox(Box(Vec3(-.5f, -.5f, -.5f), Vec3(.5f, .5f, .5f)), space, work));
	CHECK(space == SpaceClass::Solid);
	CHECK(domain->ClassifyBox(Box(Vec3(2, 2, 2), Vec3(3, 3, 3)), space, work));
	CHECK(space == SpaceClass::Free);
	CHECK(domain->ClassifyBox(Box(Vec3(-2, -2, -2), Vec3(2, 2, 2)), space, work));
	CHECK(space == SpaceClass::Unknown);
	auto bad = data;
	bad.nodes[0].children[0] = 0;
	CHECK(BspRegionDomain::Create(bad, Context(), domain).code == Code::InvalidInput);
	CHECK(!domain);
	bad = data;
	bad.sides[0] = 999;
	CHECK(BspRegionDomain::Create(bad, Context(), domain).code == Code::InvalidInput);
	bad = data;
	bad.planes[0].normal.x = std::numeric_limits<float>::quiet_NaN();
	CHECK(BspRegionDomain::Create(bad, Context(), domain).code == Code::InvalidInput);
	ReadContext tiny = Context();
	tiny.maxBytes = 1;
	CHECK(BspRegionDomain::Create(data, tiny, domain).code == Code::MemoryLimit);
	Fixture f;
	f.Shell();
	auto input = f.Input();
	std::shared_ptr<const LightingRegionGraph> graph;
	RegionOptions o = f.options;
	o.maxBytes = 1;
	CHECK(LightingRegionGraph::Build(input, o, Context(), graph).code == Code::MemoryLimit);
	CHECK(!graph);
	o = f.options;
	o.maxNodes = 1;
	CHECK(LightingRegionGraph::Build(input, o, Context(), graph).code == Code::MemoryLimit);
	o = f.options;
	o.minimumCellWorld = .3f;
	CHECK(LightingRegionGraph::Build(input, o, Context(), graph).code == Code::InvalidInput);
	o = f.options;
	o.maxWork = 1;
	CHECK(LightingRegionGraph::Build(input, o, Context(), graph).code == Code::WorkLimit);
	Stop stop;
	ReadContext cancel = Context();
	cancel.cancellation = &stop;
	CHECK(LightingRegionGraph::Build(input, f.options, cancel, graph).code == Code::Cancelled);
	auto noGeometry = input;
	noGeometry.geometry.reset();
	CHECK(LightingRegionGraph::Build(noGeometry, f.options, Context(), graph).code ==
		  Code::MissingGeometry);
	auto failed = std::make_shared<FailedDomain>();
	failed->world = f.domain->world;
	input.domain = failed;
	CHECK(LightingRegionGraph::Build(input, f.options, Context(), graph).code ==
		  Code::ProviderError);
	auto good = f.Build();
	RegionAssignment out;
	CHECK(good->Lookup(Vec3(std::numeric_limits<float>::infinity(), 0, 0), Context(), out).code ==
		  Code::InvalidInput);
	CHECK(good->Lookup(Vec3(0, 0, 4), cancel, out).code == Code::Cancelled);
	SurfaceRegionSides sides;
	CHECK(good->AssignSurfaceSides(Vec3(), Vec3(), 1, Context(), sides).code == Code::InvalidInput);
	static_assert(
		std::is_same<decltype(good->Regions()), const std::vector<LightingRegion> &>::value,
		"read-only graph");
	InputDataset dataset;
	dataset.regionGraph = good;
	InputReader reader(dataset);
	CHECK(reader.RegionGraph() == good);
	Fixture empty;
	auto emptyGraph = empty.Build();
	CHECK(emptyGraph->Report().regions == 1);
	CHECK(emptyGraph->Report().staticComponents == 1);
	Fixture allSolid;
	allSolid.Solid(allSolid.domain->world);
	CHECK(allSolid.Build()->Report().freeCells == 0);
	GeometryBoxResult overlap;
	auto geometry = f.Input().geometry;
	CHECK(geometry->OverlapBox(Box(Vec3(-1, -1, 0), Vec3(1, 1, 1)), Context(), overlap));
	CHECK(overlap.intersects && overlap.surfaceCount > 0);
	CHECK(geometry->OverlapBox(Box(Vec3(-1, -1, 2), Vec3(1, 1, 3)), Context(), overlap));
	CHECK(!overlap.intersects);
	CHECK(geometry->OverlapBox(Box(Vec3(-1, -1, 0), Vec3(1, 1, 1)), cancel, overlap).code ==
		  Code::Cancelled);
	CHECK(!overlap.intersects);
}
} // namespace
void UncertaintyAndOptionalRegions()
{
	Fixture f;
	f.Shell();
	BakedSurface s;
	s.ordinal = uint32_t(f.surfaces.size());
	s.kind = BakedSurfaceKind::Flare;
	f.surfaces.push_back(s);
	auto flare = f.Build();
	CHECK(flare->Report().freeCells > 0);
	f.surfaces.back().kind = BakedSurfaceKind::Unsupported;
	auto unknown = f.Build();
	CHECK(unknown->Report().freeCells == 0);
	CHECK(unknown->Report().globalOnlyInvalidated > 0);
	CHECK(unknown->Report().minimumLimited < unknown->Report().unknownCells);
	auto a = Lookup(unknown, Vec3(-4, 0, 4));
	CHECK(a.region == 0 && (a.issues & CellUnboundedGeometry));
	RegionPairFilter pair;
	CHECK(unknown->FilterPair(Vec3(-4, 0, 4), Vec3(-3, 0, 4), Context(), pair));
	CHECK(pair.direct.state == VisibilityState::Unknown);
	for (auto p : {Vec3(1000, 1000, 1000), Vec3(1001, 1001, 1001)})
	{
		BakedVertex vertex;
		vertex.position = p;
		f.surfaces.back().vertices.push_back(vertex);
	}
	auto bounded = f.Build();
	CHECK(bounded->Report().freeCells == flare->Report().freeCells);
	// A conservative boundary cell may lack a region although this particular ray is clear.
	f.surfaces.pop_back();
	f.Quad(Vec3(0, -6, 0), Vec3(0, 6, 0), Vec3(0, 6, 8), Vec3(0, -6, 8), MaterialCutout);
	auto boundary = f.Build();
	CHECK(boundary->Report().opticalUncertain > 0);
	CHECK(boundary->FilterPair(Vec3(-.1f, 0, 4), Vec3(-.1f, 1, 4), Context(), pair));
	CHECK(pair.source.state == RegionAssignmentState::Unknown && pair.source.region == 0);
	CHECK(pair.direct.state == VisibilityState::Visible);
	CHECK(boundary->FilterPair(Vec3(-1, 0, 4), Vec3(1, 0, 4), Context(), pair));
	CHECK(pair.direct.state == VisibilityState::Unknown && (pair.direct.issues & OpticalUnknown));
	CHECK(boundary->CertificationKey() == f.Build()->CertificationKey());
	f.surfaces.back().vertices[0].position.x = .125f;
	CHECK(boundary->CertificationKey() != f.Build()->CertificationKey());
	f.options.bounds = Box(Vec3(-2, -2, 1), Vec3(2, 2, 7));
	auto small = f.Build();
	a = Lookup(small, Vec3(-4, 0, 4));
	CHECK(a.state == RegionAssignmentState::Unknown && a.outsideAnalysisBounds &&
		  a.bsp.space == SpaceClass::Free);
	f.Solid(Box(Vec3(-5,-1,3),Vec3(-3,1,5)));
	a=Lookup(f.Build(),Vec3(-4,0,4));
	CHECK(a.state==RegionAssignmentState::Solid && a.outsideAnalysisBounds);
}
void ObliqueBrushCertificates()
{
	auto input = Bsp();
	for (float x : {-.70710677f, .70710677f})
	{
		SpatialPlane plane;
		plane.normal = Vec3(x, .70710677f, 0);
		input.sides.push_back(uint32_t(input.planes.size()));
		input.planes.push_back(plane);
	}
	input.brushes[0].sideCount = 8;
	std::shared_ptr<const BspRegionDomain> domain;
	CHECK(BspRegionDomain::Create(input, Context(), domain));
	SpaceClass space;
	uint64_t work;
	uint32_t reasons;
	CHECK(
		domain->InspectBox(Box(Vec3(-.8f, .1f, -.5f), Vec3(.8f, .2f, .5f)), space, work, reasons));
	CHECK(space == SpaceClass::Free && (reasons & BoxBrushDisjointRefined));
	CHECK(
		domain->InspectBox(Box(Vec3(-.8f, -.2f, -.5f), Vec3(.8f, .2f, .5f)), space, work, reasons));
	CHECK(space == SpaceClass::Unknown);
	// Independent sampled half-space reference: every certified box must contain no solid points.
	for (unsigned y = 0; y < 20; ++y)
		for (unsigned x = 0; x < 20; ++x)
		{
			float px = -1.1f + x * .11f, py = -1.1f + y * .11f;
			Bounds box = Box(Vec3(px, py, -.05f), Vec3(px + .1f, py + .1f, .05f));
			CHECK(domain->ClassifyBox(box, space, work));
			if (space == SpaceClass::Free)
				for (unsigned j = 0; j <= 4; ++j)
					for (unsigned i = 0; i <= 4; ++i)
					{
						float a = px + i * .025f, b = py + j * .025f;
						bool solid = std::abs(a) < 1 && std::abs(b) < 1 && b < -std::abs(a) - 1e-5f;
						CHECK(!solid);
					}
		}
}
int main()
{
	auto start = std::chrono::steady_clock::now();
	Rooms();
	CorridorFloorsAndExterior();
	DomainAndLimits();
	UncertaintyAndOptionalRegions();
	ObliqueBrushCertificates();
	std::printf("regions: %d checks, %d failures; seconds=%.6f\n", checks, failures,
				std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
	return failures ? 1 : 0;
}
