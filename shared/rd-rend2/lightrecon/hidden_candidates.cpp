// Surface-response search. This ranks hypotheses; it does not recover lights.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hidden_candidates.h"
#include "bake_math.h"
#include "geometry_key.h"
#include <map>
#include <set>
#include <tuple>

namespace Rend2LightRecon
{
namespace Core
{
namespace Hidden
{
namespace
{
using namespace Bake::Detail;
void Need(bool b, Code c, const char *message)
{
	if (!b)
		throw Failure(c, message);
}
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> p)
{
	return VectorChunk<T>(p, 0, p->size());
}
bool Inside(const Bounds &b, V p)
{
	return p.x >= b.minWorld.x && p.y >= b.minWorld.y && p.z >= b.minWorld.z && p.x <= b.maxWorld.x &&
		   p.y <= b.maxWorld.y && p.z <= b.maxWorld.z;
}
bool Valid(const Bounds &b)
{
	return b.known && Finite(b.minWorld) && Finite(b.maxWorld) && b.minWorld.x <= b.maxWorld.x &&
		   b.minWorld.y <= b.maxWorld.y && b.minWorld.z <= b.maxWorld.z;
}
Bounds Box(V a, V b)
{
	Bounds x;
	x.known = true;
	x.minWorld = a.Float();
	x.maxWorld = b.Float();
	return x;
}
void Text(GeometryKey &k, const std::string &s)
{
	k.U(s.size());
	for (unsigned char c : s)
		k.U(c);
}
Id Number(const Disk::Digest &key)
{
	Id n = 0;
	for (int i = 0; i < 8; ++i)
		n |= Id(key[i]) << (8 * i);
	return n ? n : 1;
}
Frame FrameFor(V forward)
{
	Frame f;
	f.known = true;
	V x = Unit(Cross(std::abs(forward.z) < .9 ? V(0, 0, 1) : V(0, 1, 0), forward));
	f.forward = forward.Float();
	f.right = x.Float();
	f.up = Cross(forward, x).Float();
	return f;
}
struct Engine;
class CountedVisibility final : public VisibilityProvider
{
  public:
	Engine *engine = nullptr;
	Error Query(const RayQuery &, const ReadContext &, RayResult &) const override;
};
struct Engine
{
	const Request &input;
	const ReadContext &read;
	Result &out;
	Bake::EvaluationContext context;
	Engine(const Request &i, const ReadContext &r, Result &o) : input(i), read(r), out(o), context(i.geometry)
	{
		auto counted = std::make_shared<CountedVisibility>();
		counted->engine = this;
		context.visibility = counted;
	}
	void Check()
	{
		Need(!Cancelled(read), Code::Cancelled, "Hidden candidate search cancelled");
		Need(out.report.rays <= input.options.maxRays && out.report.traversalWork <= input.options.maxWork &&
				 out.report.forwardCalls <= input.options.maxForwardCalls,
			 Code::WorkLimit, "Hidden candidate work budget");
	}
};
Error CountedVisibility::Query(const RayQuery &q, const ReadContext &r, RayResult &hit) const
{
	engine->Check();
	++engine->out.report.rays;
	engine->Check();
	RayQuery bounded = q;
	bounded.maxWork = std::min(q.maxWork, engine->input.options.maxWork - engine->out.report.traversalWork);
	auto e = engine->input.geometry.visibility->Query(bounded, r, hit);
	engine->out.report.traversalWork += hit.nodeTests + hit.primitiveTests;
	engine->Check();
	// An optical-policy assumption is not either a certified clear or blocked ray.
	// Unknown foreground cannot undo a definite opaque wall farther along the
	// segment. Conversely, an assumed alpha blockage is not a geometric proof.
	if (e &&
		((hit.state == VisibilityState::Visible &&
		  (hit.reliability != RayReliability::Geometric ||
		   (hit.issues & (PolicyAssumption | OpticalUnknown | UnsupportedGeometry | IncompleteGeometry)))) ||
		 (hit.state == VisibilityState::Blocked &&
		  (hit.reliability != RayReliability::Geometric || (hit.issues & PolicyAssumption)))))
		hit.state = VisibilityState::Unknown;
	return e;
}
Disk::Digest Key(const Request &r, const std::vector<Sample> &samples)
{
	GeometryKey k;
	k.U(kVersion);
	k.Digest(r.map.sha256);
	k.Digest(r.geometry.geometryKey);
	k.Digest(r.geometry.materialKey);
	k.Digest(r.geometry.observationPolicyKey);
	k.Digest(r.domainKey);
	if (r.regions)
		k.Digest(r.regions->CertificationKey());
	k.V(r.analysisBounds.minWorld);
	k.V(r.analysisBounds.maxWorld);
	k.F(r.units.worldUnitsPerMeter);
	k.F(r.units.physicalToScene);
	const auto &o = r.options;
	for (double v : {o.localCellWorld, o.reachWorld, o.minimumContrast, o.alternativeScoreDelta,
					 o.probeAmplitudes[0], o.probeAmplitudes[1]})
		k.F(v);
	for (uint64_t v : std::initializer_list<uint64_t>{
			 o.coarseDivisions, o.refinementRounds, o.refineSeeds, uint64_t(o.maxSamples),
			 uint64_t(o.samplesPerArea), uint64_t(o.maxAreas), uint64_t(o.maxProfiles),
			 uint64_t(o.maxNodesPerArea), uint64_t(o.retainedPerChannelProfile), o.maxRays, o.maxWork,
			 o.maxForwardCalls, o.maxBytes, uint64_t(o.spots), uint64_t(o.allowAssumedProfiles),
			 uint64_t(o.allowAssumedEncoding), uint64_t(o.directOnlyAttested)})
		k.U(v);
	for (const auto &s : samples)
	{
		k.U(s.receiver.sample);
		k.U(s.receiver.surface);
		k.U(s.receiver.material);
		k.V(s.receiver.world);
		k.V(s.receiver.geometricNormal);
		k.V(s.receiver.shadingNormal);
		k.U(s.receiver.style);
		k.U(uint64_t(s.receiver.domain));
		k.U(s.receiver.mappedPlanar);
		k.U(s.receiver.compilerPlacementCertified);
		k.U(s.receiver.twoSided);
		k.F(s.receiver.geometryErrorWorld);
		k.F(s.receiver.mappingErrorWorld);
		k.F(s.areaWorldSquared);
		for (auto b : s.rgb)
			k.U(b);
		k.U(s.encodingAttested);
		k.U(s.eligible);
		k.U(s.transportSuspected);
		k.U(s.materialUncertainty);
	}
	for (const auto &p : r.profiles)
	{
		Text(k, p.id);
		Text(k, p.family);
		Text(k, p.compilerSourceRevision);
		Text(k, p.compilerBinarySha256);
		k.Digest(p.sourceLockKey);
		k.Digest(p.catalogueKey);
		k.U(p.schemaVersion);
		k.U(p.formulaVersion);
		k.U(uint64_t(p.compiler));
		k.Digest(p.contentKey);
		k.U(uint64_t(p.applicability));
		for (const auto &f : p.rules)
		{
			Text(k, f.selector);
			Text(k, f.provenance);
			Text(k, f.verificationScope);
			k.U(uint64_t(f.knowledge));
			k.U(f.formulaVersion);
			for (const auto &v : f.parameters)
			{
				Text(k, v.name);
				k.U(v.value.known);
				k.F(v.value.value);
			}
		}
	}
	for (const auto &a : r.areas)
	{
		k.U(a.id);
		k.V(a.positions.minWorld);
		k.V(a.positions.maxWorld);
		k.V(a.receivers.minWorld);
		k.V(a.receivers.maxWorld);
		Text(k, a.reason);
	}
	for (const auto &d : r.directions)
	{
		k.U(d.id);
		k.V(d.originWorld);
		k.V(d.toSource);
		k.F(d.halfAngleRadians);
		k.U(d.usable);
		Text(k, d.provenance);
	}
	return k.Finish();
}
struct Pose
{
	V p, half;
	Method method = Method::SurfaceResponseSearch;
};
struct Trial
{
	Pose pose;
	size_t profile = 0;
	Bake::SourceKind kind = Bake::SourceKind::Point;
	Frame orientation;
	double radius = 0, score[3] = {1, 1, 1}, directionPenalty = 0;
	bool available[3] = {false, false, false};
	Feasibility feasibility = Feasibility::RegionVisibilityUnknown;
	Context context = Context::Neighbourhood;
	SpatialLocation bsp;
	Id region = 0;
	bool dynamicPossible = false;
	std::vector<SupportingObservation> relations;
};
bool Feasible(Engine &e, V p, Trial &t, AreaReport &report)
{
	const auto &r = e.input;
	bool regionFree = false;
	if (!Inside(r.analysisBounds, p))
	{
		++report.rejectedOutside;
		return false;
	}
	if (r.regions)
	{
		RegionAssignment a;
		auto error = r.regions->Lookup(p.Float(), e.read, a);
		if (!error)
			throw error;
		t.dynamicPossible = (a.issues & CellDynamicPossible) != 0;
		if (a.state == RegionAssignmentState::Solid)
		{
			++report.rejectedSolid;
			return false;
		}
		if (a.state == RegionAssignmentState::Outside && !a.outsideAnalysisBounds)
		{
			++report.rejectedOutside;
			return false;
		}
		if (a.state == RegionAssignmentState::Assigned)
		{
			regionFree = true;
			t.region = a.region;
			t.feasibility = Feasibility::ProvenFree;
			t.context = Context::CertifiedRegion;
			t.bsp = a.bsp;
		}
	}
	if (!regionFree)
		++report.unknownRegion;
	PointDomain point = PointDomain::Unknown;
	if (r.domain)
	{
		auto error = r.domain->Classify(p.Float(), 1e-5, point);
		if (!error && error.code != Code::Unavailable)
			throw error;
		if (point == PointDomain::Solid)
		{
			++report.rejectedSolid;
			return false;
		}
		if (!regionFree)
			t.context = Context::BspContext;
		if (auto domain = std::dynamic_pointer_cast<const RegionDomain>(r.domain))
		{
			SpatialLocation a;
			auto error = domain->Locate(p.Float(), a);
			if (!error)
				throw error;
			if (a.space == SpaceClass::Outside)
			{
				++report.rejectedOutside;
				return false;
			}
			t.bsp = a;
		}
	}
	t.feasibility = point == PointDomain::Boundary
						? Feasibility::Boundary
						: regionFree ? Feasibility::ProvenFree
									 : point == PointDomain::Unknown ? Feasibility::RegionVisibilityUnknown
																	 : Feasibility::PlausibleUnverified;
	if (t.dynamicPossible && t.feasibility == Feasibility::ProvenFree)
		t.feasibility = Feasibility::PlausibleUnverified;
	if (point == PointDomain::Free)
		if (auto geometry = std::dynamic_pointer_cast<const StaticVisibility>(r.geometry.visibility))
		{
			GeometryBoxResult local;
			auto error =
				geometry->OverlapBox(Box(p - V(.001, .001, .001), p + V(.001, .001, .001)), e.read, local,
									 std::max(uint64_t(1), r.options.maxWork - e.out.report.traversalWork));
			if (!error)
				throw error;
			e.out.report.traversalWork += local.work;
			e.Check();
			if (!local.intersects && local.coverageComplete && !t.dynamicPossible)
				t.feasibility = Feasibility::ProvenFree;
		}
	return true;
}
// Cosine distance between local response shapes, stratified by MATERIAL and
// surface-balanced area. No global brightness threshold or published amplitude.
// Different material groups have independent normalization; material boundaries
// cannot create a cross-material illumination gradient by themselves.
double ShapeScore(const std::vector<Sample> &s, const std::vector<double> &weights,
				  const Bake::Result &prediction, int channel, bool &available, double minimumContrast)
{
	struct Group
	{
		double yy = 0, pp = 0, yp = 0, lo = 1e300, hi = 0;
		size_t count = 0, positive = 0;
	};
	std::map<Id, Group> groups;
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (!prediction.predictions.data[i].available)
			continue;
		auto &g = groups[s[i].receiver.material];
		double y = s[i].rgb[channel], p = V(prediction.predictions.data[i].rgb)[channel], w = weights[i];
		g.yy += w * y * y;
		g.pp += w * p * p;
		g.yp += w * y * p;
		g.lo = std::min(g.lo, y);
		g.hi = std::max(g.hi, y);
		++g.count;
		g.positive += y > 0;
	}
	double score = 0;
	size_t used = 0;
	for (const auto &pair : groups)
	{
		const auto &g = pair.second;
		if (g.count < 4 || g.positive < 4 || g.yy <= 0 || (g.hi - g.lo) / (g.hi + 1) < minimumContrast)
			continue;
		// All blocked/backfacing/zero-envelope response is a poor model, not an
		// invalid zero denominator or a fabricated successful black prediction.
		score += g.pp > 0 ? 1 - Clamp(g.yp / std::sqrt(g.yy * g.pp), 0, 1) : 1;
		++used;
	}
	available = used > 0;
	return used ? score / used : 1;
}
std::vector<double> Weights(const std::vector<Sample> &samples)
{
	std::map<Id, double> area;
	for (const auto &s : samples)
		area[s.receiver.surface] += s.areaWorldSquared;
	std::vector<double> w;
	for (const auto &s : samples)
		w.push_back(s.areaWorldSquared / area[s.receiver.surface] / area.size());
	return w;
}
void Evaluate(Engine &e, const std::vector<Sample> &samples, const std::vector<double> &weights,
			  const Pose &pose, size_t profile, Bake::SourceKind kind, Trial base, double radius,
			  std::vector<Trial> &trials)
{
	e.Check();
	const auto &r = e.input;
	Trial t = base;
	t.pose = pose;
	t.profile = profile;
	t.kind = kind;
	t.radius = radius;
	auto sources = std::make_shared<std::vector<Bake::SourceHypothesis>>(1);
	auto &source = (*sources)[0];
	source.id = 1;
	source.kind = kind;
	source.position.known = true;
	source.position.world = pose.p.Float();
	source.worldUnits = r.units;
	source.amplitudeKnown = true;
	V centroid;
	double centreWeight = 0;
	for (size_t i = 0; i < samples.size(); ++i)
	{
		double w = weights[i] * (samples[i].rgb[0] + samples[i].rgb[1] + samples[i].rgb[2]);
		centroid = centroid + V(samples[i].receiver.world) * w;
		centreWeight += w;
	}
	if (centreWeight <= 0)
		return;
	centroid = centroid / centreWeight;
	if (kind == Bake::SourceKind::Spot)
	{
		V direction = centroid - pose.p;
		if (Length(direction) < 1e-6)
			return;
		source.orientation = FrameFor(Unit(direction));
		source.spotTargetDistanceWorld = 128;
		source.spotRadiusWorld = radius;
		t.orientation = source.orientation;
	}
	auto receivers = std::make_shared<std::vector<Bake::Receiver>>();
	for (const auto &s : samples)
		receivers->push_back(s.receiver);
	Bake::Options options;
	options.output = Bake::Domain::EncodedRgb8BeforeQuantization;
	options.allowAssumed = r.options.allowAssumedProfiles;
	options.maxPairs = samples.size();
	options.maxRays = std::max(uint64_t(1), r.options.maxRays - e.out.report.rays);
	options.maxWork = std::max(uint64_t(1), r.options.maxWork - e.out.report.traversalWork);
	Bake::CpuForwardEvaluator evaluator;
	std::shared_ptr<const Bake::PreparedFactors> factors;
	++e.out.report.forwardCalls;
	e.Check();
	auto error = evaluator.Prepare(r.profiles[profile], Lease(sources), Lease(receivers), e.context, options,
								   e.read, factors);
	if (!error)
	{
		if (error.code == Code::Unavailable ||
			(error.code == Code::InvalidInput && error.message == "Coincident source/receiver direction"))
			return;
		throw error;
	}
	options.factors = factors;
	t.relations.reserve(samples.size());
	for (size_t i = 0; i < samples.size(); ++i)
	{
		const auto &f = factors->Records()[i];
		SupportingObservation relation;
		relation.observation = samples[i].receiver.sample;
		relation.surface = samples[i].receiver.surface;
		relation.issues = f.issues;
		relation.relation = f.cosine <= 0
								? Relation::BackFacing
								: (f.issues & PolicyAssumption)
									  ? Relation::Assumed
									  : f.visibility == VisibilityState::Visible
											? Relation::Visible
											: f.visibility == VisibilityState::Blocked ? Relation::Blocked
																					   : Relation::Unknown;
		t.relations.push_back(relation);
	}
	bool visibleSupport = false, unknownSupport = false;
	for (size_t i = 0; i < samples.size(); ++i)
		if (samples[i].rgb[0] || samples[i].rgb[1] || samples[i].rgb[2])
		{
			visibleSupport |= t.relations[i].relation == Relation::Visible && factors->Records()[i].cone > 0;
			unknownSupport |=
				t.relations[i].relation == Relation::Unknown || t.relations[i].relation == Relation::Assumed;
		}
	if (!visibleSupport && !unknownSupport)
		return; // known geometry gives this proposal no possible positive receiver
	for (double amplitude : r.options.probeAmplitudes)
	{
		source.compilerRgbAmplitude = Vec3(float(amplitude), float(amplitude), float(amplitude));
		Bake::Result result;
		++e.out.report.forwardCalls;
		e.Check();
		e.out.report.sourceReceiverPairs += samples.size();
		error = evaluator.Evaluate(r.profiles[profile], Lease(sources), Lease(receivers), Decode::Profile(),
								   e.context, options, e.read, result);
		if (!error)
			throw error;
		for (int c = 0; c < 3; ++c)
		{
			bool available = false;
			double score = ShapeScore(samples, weights, result, c, available, r.options.minimumContrast);
			if (available && (!t.available[c] || score < t.score[c]))
			{
				t.available[c] = true;
				t.score[c] = score;
			}
		}
	}
	double sum = 0;
	size_t directions = 0;
	for (const auto &d : r.directions)
		if (d.usable && Length(pose.p - V(d.originWorld)) > 1e-6)
		{
			sum += std::max(0., std::acos(Clamp(Dot(Unit(pose.p - V(d.originWorld)), V(d.toSource)), -1, 1)) -
									d.halfAngleRadians);
			++directions;
		}
	t.directionPenalty = directions ? sum / directions : 0;
	trials.push_back(std::move(t));
}
std::vector<Pose> DirectionSeeds(const Request &r, const SearchArea &a, V half)
{
	std::vector<Pose> out;
	for (size_t i = 0; i < r.directions.size(); ++i)
		for (size_t j = i + 1; j < r.directions.size(); ++j)
		{
			const auto &a0 = r.directions[i], &a1 = r.directions[j];
			if (!a0.usable || !a1.usable || !Inside(a.receivers, V(a0.originWorld)) ||
				!Inside(a.receivers, V(a1.originWorld)))
				continue;
			V u(a0.toSource), v(a1.toSource), w = V(a0.originWorld) - V(a1.originWorld);
			double uv = Dot(u, v), den = 1 - uv * uv;
			if (den < .04)
				continue; // nearly parallel rays have no stable finite intersection
			double t = (uv * Dot(v, w) - Dot(u, w)) / den, s = (Dot(v, w) - uv * Dot(u, w)) / den;
			if (t < 0 || s < 0)
				continue;
			V p = (V(a0.originWorld) + u * t + V(a1.originWorld) + v * s) * .5;
			if (!Inside(a.positions, p))
				continue;
			Pose pose;
			pose.p = p;
			pose.half = half;
			pose.method = Method::DirectionIntersection;
			out.push_back(pose);
		}
	return out;
}
bool Better(const Trial &a, const Trial &b, int c)
{
	if (a.available[c] != b.available[c])
		return a.available[c];
	// Mixture direction is ONLY a tie breaker of equivalent surface scores.
	if (std::abs(a.score[c] - b.score[c]) > 1e-9)
		return a.score[c] < b.score[c];
	if (a.directionPenalty != b.directionPenalty)
		return a.directionPenalty < b.directionPenalty;
	return std::tie(a.pose.p.x, a.pose.p.y, a.pose.p.z, a.kind, a.radius) <
		   std::tie(b.pose.p.x, b.pose.p.y, b.pose.p.z, b.kind, b.radius);
}
std::vector<SearchArea> Areas(const Request &r, const std::vector<Sample> &samples)
{
	if (!r.areas.empty())
	{
		auto a = r.areas;
		std::sort(a.begin(), a.end(), [](const SearchArea &x, const SearchArea &y) { return x.id < y.id; });
		return a;
	}
	using Cell = std::tuple<int64_t, int64_t, int64_t>;
	std::set<Cell> bins;
	for (const auto &s : samples)
		if (s.eligible)
			bins.emplace(int64_t(std::floor(s.receiver.world.x / r.options.localCellWorld)),
						 int64_t(std::floor(s.receiver.world.y / r.options.localCellWorld)),
						 int64_t(std::floor(s.receiver.world.z / r.options.localCellWorld)));
	std::vector<SearchArea> out;
	for (const auto &b : bins)
	{
		V lo = V(double(std::get<0>(b)), double(std::get<1>(b)), double(std::get<2>(b))) *
			   r.options.localCellWorld,
		  hi = lo + V(1, 1, 1) * r.options.localCellWorld;
		SearchArea a;
		a.receivers = Box(lo, hi);
		V p = lo - V(1, 1, 1) * r.options.reachWorld, q = hi + V(1, 1, 1) * r.options.reachWorld;
		for (int axis = 0; axis < 3; ++axis)
		{
			p[axis] = std::max(p[axis], V(r.analysisBounds.minWorld)[axis]);
			q[axis] = std::min(q[axis], V(r.analysisBounds.maxWorld)[axis]);
		}
		a.positions = Box(p, q);
		GeometryKey k;
		k.Digest(r.map.sha256);
		k.V(lo.Float());
		k.F(r.options.localCellWorld);
		a.id = Number(k.Finish());
		a.reason = "Local surface neighbourhood; no invented semantic room";
		out.push_back(a);
	}
	return out;
}
std::vector<Sample> Select(const std::vector<Sample> &all, const SearchArea &a, const Options &o,
						   AreaReport &report)
{
	std::map<Id, std::vector<Sample>> surfaces;
	for (const auto &s : all)
		if (Inside(a.receivers, V(s.receiver.world)))
		{
			++report.requested;
			if (!s.eligible || (!s.encodingAttested && !o.allowAssumedEncoding))
			{
				++report.excluded;
				continue;
			}
			bool clipped = false;
			for (auto c : s.rgb)
				clipped |= c == 255;
			if (clipped)
			{
				++report.saturated;
				continue;
			}
			surfaces[s.receiver.surface].push_back(s);
		}
	std::vector<Sample> selected;
	size_t round = 0;
	bool more = true;
	while (more && selected.size() < o.samplesPerArea)
	{
		more = false;
		for (const auto &p : surfaces)
			if (round < p.second.size())
			{
				if (selected.size() == o.samplesPerArea)
					break;
				selected.push_back(p.second[round]);
				more = true;
			}
		++round;
	}
	report.selected = selected.size();
	report.sampleLimited = report.selected + report.excluded + report.saturated < report.requested;
	return selected;
}
void Publish(Engine &engine, const Trial &t, int channel, const SearchArea &area,
			 const std::vector<Sample> &samples, AreaReport &report)
{
	const auto &r = engine.input;
	auto &out = engine.out;
	Hypothesis h;
	h.area = area.id;
	h.region = t.region;
	h.feasibility = t.feasibility;
	h.context = t.context;
	h.bsp = t.bsp;
	h.method = t.pose.method;
	h.profile = r.profiles[t.profile].id;
	h.profileScope = "Conditional local shape comparison; not source existence, intensity or stock compiler "
					 "identification";
	h.channel = channel;
	h.sourceKind = t.kind;
	h.score = t.score[channel];
	h.scoreAvailable = t.available[channel];
	h.directionalDisagreementRadians = t.directionPenalty;
	h.observations = t.relations;
	h.searchedCell = Box(t.pose.p - t.pose.half, t.pose.p + t.pose.half);
	std::set<Id> surfaces, materials;
	V lo = samples.front().receiver.world, hi = lo;
	double diversity = 0;
	bool transport = false;
	for (size_t i = 0; i < samples.size(); ++i)
	{
		const auto &s = samples[i];
		surfaces.insert(s.receiver.surface);
		materials.insert(s.receiver.material);
		transport |= s.transportSuspected;
		for (int axis = 0; axis < 3; ++axis)
		{
			lo[axis] = std::min(lo[axis], V(s.receiver.world)[axis]);
			hi[axis] = std::max(hi[axis], V(s.receiver.world)[axis]);
		}
		for (const auto &v : samples)
			diversity =
				std::max(diversity, 1 - Dot(V(s.receiver.geometricNormal), V(v.receiver.geometricNormal)));
		h.visible += t.relations[i].relation == Relation::Visible;
		h.blocked += t.relations[i].relation == Relation::Blocked;
		h.unknown +=
			t.relations[i].relation == Relation::Unknown || t.relations[i].relation == Relation::Assumed;
	}
	h.distinctSurfaces = surfaces.size();
	if (h.context == Context::Neighbourhood && h.visible)
		h.context = Context::LocalVisibility;
	h.distinctMaterials = materials.size();
	h.receiverBaselineWorld = Length(hi - lo);
	h.normalDiversity = diversity;
	h.positionUnderconstrained = t.feasibility != Feasibility::ProvenFree || surfaces.size() < 3 ||
								 diversity < .1 || !h.scoreAvailable || h.unknown > 0 || h.visible < 4 ||
								 t.dynamicPossible;
	h.kind = r.options.directOnlyAttested && !h.positionUnderconstrained ? Kind::PhysicalEmitterCandidate
																		 : Kind::AmbiguousSourceOrTransport;
	if (transport)
		h.kind = Kind::TransportEvidence;
	if (r.regions)
		for (const auto &c : r.regions->Connections())
			if (c.apertureCandidate && Valid(c.sampledOpeningBounds) &&
				Inside(c.sampledOpeningBounds, t.pose.p))
				h.kind = Kind::TransportEvidence;
	if (!h.region)
		h.reasons.push_back("Region membership unavailable; direct geometry queries remain admissible");
	if (!r.options.directOnlyAttested)
		h.reasons.push_back(
			"Unknown direct/sky/bounce/material mixture: physical source versus transport unresolved");
	if (h.unknown)
		h.reasons.push_back(
			"Unknown rays were excluded from numerical shape comparison, not made transparent or black");
	if (t.dynamicPossible)
		h.reasons.push_back(
			"Moving boundary near source; static topology does not certify all runtime/bake states");
	if (h.positionUnderconstrained)
		h.reasons.push_back(
			"Receiver/visibility support underconstrained; searched cell is not a calibrated XYZ interval");
	h.reasons.push_back(
		"Point/spot geometry and colour channel are proposals; source existence/count/photometry unknown");
	GeometryKey key;
	key.Digest(out.key);
	key.U(area.id);
	Text(key, h.profile);
	key.U(uint64_t(t.kind));
	key.V(t.pose.p.Float());
	key.V(t.orientation.forward);
	key.F(t.radius);
	h.candidate = Number(key.Finish());
	for (auto &old : out.hypotheses)
		if (old.candidate == h.candidate)
		{
			old.evidenceChannels |= 1u << channel;
			old.channelScores[channel].known = h.scoreAvailable;
			old.channelScores[channel].value = float(h.score);
			return;
		}
	h.evidenceChannels = 1u << channel;
	h.channelScores[channel].known = h.scoreAvailable;
	h.channelScores[channel].value = float(h.score);
	EmitterEvidence evidence;
	evidence.id = h.candidate;
	evidence.origin = EvidenceOrigin::SurfaceObservation;
	evidence.source = "hidden-v1:" + Disk::Hex(r.map.sha256);
	evidence.mode = "surface-response-hypothesis";
	evidence.raw = "producer=hidden-v1; profile=" + h.profile +
				   "; local shape rank only; no intensity/physical-existence estimate";
	evidence.positionKnown = true;
	evidence.position = t.pose.p.Float();
	evidence.shapes = t.kind == Bake::SourceKind::Spot ? CandidateSpot : CandidatePoint;
	evidence.issues =
		ShapeHypothesis | UncalibratedEmission | ColourTransferUnknown | AmbiguousCorrespondence;
	evidence.orientationKnown = t.orientation.known;
	evidence.outward = t.orientation.forward;
	evidence.right = t.orientation.right;
	evidence.up = t.orientation.up;
	if (t.kind == Bake::SourceKind::Spot)
	{
		evidence.coneDegrees = float(std::atan(t.radius / 128) * 360 / Pi);
		evidence.coneKnown = true;
	}
	for (Id id : surfaces)
		if (id)
			evidence.surfaces.push_back(uint32_t(id - 1));
	evidence.staticEligible = true; // static-source proposal; feasibility is a separate statement
	EmitterCandidate candidate;
	candidate.id = h.candidate;
	candidate.shapes = evidence.shapes;
	candidate.issues = evidence.issues;
	candidate.staticEligible = evidence.staticEligible;
	candidate.primary = out.evidence.size();
	candidate.evidence = {candidate.primary};
	candidate.sourceIds = {evidence.id};
	out.evidence.push_back(std::move(evidence));
	out.candidates.push_back(std::move(candidate));
	out.hypotheses.push_back(std::move(h));
	++report.retained;
}
} // namespace

Error FromObservation(const Bake::Receiver &r, const Decode::Observation &o, double area, bool eligible,
					  bool attested, uint32_t materialUncertainty, bool transport, Sample &out)
{
	out = Sample();
	if (o.raw.missing || o.raw.directionData || o.raw.encoding != Decode::Encoding::Rgb8 ||
		o.raw.component != Decode::Component::Lightmap)
		return Failure(Code::Unavailable, "Hidden candidate input requires original colour lightmap RGB8");
	if (!Finite(r.world) || !Normal(r.geometricNormal) || !std::isfinite(area) || area <= 0)
		return Failure(Code::InvalidInput, "Invalid hidden candidate surface geometry");
	out.receiver = r;
	out.areaWorldSquared = area;
	out.eligible = eligible;
	out.materialUncertainty = materialUncertainty;
	out.transportSuspected = transport;
	out.encodingAttested = attested && o.resolvedTransfer == Decode::Transfer::Linear &&
						   o.resolvedPrimaries == Decode::Primaries::Rec709D65 &&
						   !(o.flags & (Decode::TransferAssumed | Decode::PrimariesAssumed));
	for (int c = 0; c < 3; ++c)
		out.rgb[c] = o.raw.bytes[c];
	return {};
}
DirectionConstraint GridConstraint(const GridCell &c, Id id, double uncertainty)
{
	DirectionConstraint d;
	d.id = id;
	d.originWorld = c.centreWorld;
	d.toSource = c.direction.toSource;
	d.halfAngleRadians = uncertainty + c.direction.errorBoundRadians;
	d.usable = c.presence == GridPresence::Present && c.styleStatus == GridStyle::Static &&
			   c.direction.quality != GridDirectionQuality::Unavailable && uncertainty > 0;
	d.provenance = "Original quantized grid mixture, weak surface-search seed only";
	return d;
}
DirectionConstraint DeluxeConstraint(const Sample &s, const DeluxeDirection &c, Id id, double uncertainty)
{
	DirectionConstraint d;
	d.id = id;
	d.originWorld = s.receiver.world;
	d.toSource = c.toSource;
	d.halfAngleRadians = uncertainty + c.angularBoundRadians;
	d.usable = c.state == DeluxeState::Available && uncertainty > 0;
	d.provenance = "Original deluxe mixture, weak surface-search seed only";
	return d;
}
Error Generate(const Request &r, const ReadContext &read, Result &out)
{
	out = Result();
	Engine engine(r, read, out);
	try
	{
		engine.Check();
		const auto &o = r.options;
		float metres = 0;
		Need(r.map.sha256 != Disk::Digest{} && r.geometry.geometryKey != Disk::Digest{}, Code::InvalidInput,
			 "Content-addressed map/geometry required");
		Need(WorldToMeters(1, r.units, metres), Code::InvalidInput, "Invalid geometry unit policy");
		Need(r.geometry.visibility != nullptr, Code::MissingGeometry,
			 "Hidden candidates require static visibility");
		Need(!r.domain || r.domainKey != Disk::Digest{}, Code::InvalidInput,
			 "Classifier generation key required");
		Need(Valid(r.analysisBounds), Code::InvalidInput, "Finite analysis bounds required");
		Need(o.coarseDivisions >= 2 && o.coarseDivisions <= 12 && o.refinementRounds <= 4 &&
				 o.refineSeeds >= 1 && o.refineSeeds <= 8 && o.retainedPerChannelProfile >= 1 &&
				 o.retainedPerChannelProfile <= 16 && o.maxNodesPerArea >= 8 && o.maxNodesPerArea <= 8192,
			 Code::InvalidInput, "Invalid local search limits");
		Need(o.maxSamples <= 4096 && o.samplesPerArea >= 6 && o.samplesPerArea <= 256 && o.maxAreas > 0 &&
				 o.maxAreas <= 128 && o.maxProfiles > 0 && o.maxProfiles <= 8,
			 Code::InvalidInput, "Invalid candidate input limits");
		for (double x : {o.localCellWorld, o.reachWorld, o.minimumContrast, o.alternativeScoreDelta,
						 o.probeAmplitudes[0], o.probeAmplitudes[1]})
			Need(std::isfinite(x) && x > 0 && x <= 1e9, Code::InvalidInput, "Invalid finite candidate scale");
		Need(r.training.count <= o.maxSamples && (!r.training.count || (r.training.owner && r.training.data)),
			 Code::InvalidInput, "Invalid training sample lease");
		Need(r.training.count * sizeof(Sample) * 8 <= o.maxBytes &&
				 (!read.maxBytes || r.training.count * sizeof(Sample) * 8 <= read.maxBytes),
			 Code::MemoryLimit, "Input sample scratch budget before allocation");
		Need(r.profiles.size() <= o.maxProfiles && r.directions.size() <= 32, Code::InvalidInput,
			 "Profile/direction budget");
		Need(r.areas.size() <= o.maxAreas, Code::WorkLimit, "Explicit area count budget before hashing");
		for (const auto &a : r.areas)
			Need(a.reason.size() <= 4096, Code::MemoryLimit, "Area provenance text budget");
		std::set<std::string> profileIds;
		for (const auto &p : r.profiles)
		{
			Need(!p.id.empty() && profileIds.insert(p.id).second, Code::InvalidInput,
				 "Duplicate profile identity");
			Need(p.id.size() <= 256 && p.family.size() <= 256, Code::MemoryLimit,
				 "Profile identity string budget");
			uint64_t bytes = p.id.size() + p.family.size() + p.compilerSourceRevision.size() +
							 p.compilerBinarySha256.size();
			for (const auto &f : p.rules)
			{
				bytes += f.selector.size() + f.provenance.size() + f.verificationScope.size() +
						 f.parameters.size() * sizeof(Bake::Parameter);
				for (const auto &v : f.parameters)
					bytes += v.name.size() + v.unit.size();
			}
			Need(bytes <= 65536, Code::MemoryLimit, "Profile metadata budget");
		}
		if (r.regions)
		{
			Need(r.regions->Identity().sha256 == r.map.sha256, Code::InvalidInput,
				 "Region graph belongs to a different map");
			Need(r.regions->VisibilityKey() == r.geometry.geometryKey, Code::InvalidInput,
				 "Region graph was certified against a different visual scene");
		}
		if (auto g = std::dynamic_pointer_cast<const StaticVisibility>(r.geometry.visibility))
			Need(g->GeometryIdentity().sha256 == r.map.sha256 &&
					 g->CertificationKey() == r.geometry.geometryKey,
				 Code::InvalidInput, "Static geometry/map certification mismatch");
		if (auto d = std::dynamic_pointer_cast<const RegionDomain>(r.domain))
			if (d->SourceIdentity())
				Need(d->SourceIdentity()->sha256 == r.map.sha256, Code::InvalidInput,
					 "BSP domain map mismatch");
		std::vector<Sample> samples;
		if (r.training.count)
			samples.assign(r.training.data, r.training.data + r.training.count);
		std::sort(samples.begin(), samples.end(),
				  [](const Sample &a, const Sample &b) { return a.receiver.sample < b.receiver.sample; });
		Id last = 0;
		for (const auto &s : samples)
		{
			Need(s.receiver.sample > last && s.receiver.surface && Finite(s.receiver.world) &&
					 Normal(s.receiver.geometricNormal) && Normal(s.receiver.shadingNormal) &&
					 std::isfinite(s.areaWorldSquared) && s.areaWorldSquared > 0,
				 Code::InvalidInput, "Invalid/duplicate receiver");
			last = s.receiver.sample;
			Need(Length(V(s.receiver.world)) / o.localCellWorld < 1e12, Code::InvalidInput,
				 "Spatial coordinate overflow");
		}
		for (const auto &d : r.directions)
			Need(d.id && !d.provenance.empty() && d.provenance.size() <= 4096 && Finite(d.originWorld) &&
					 (!d.usable || (Normal(d.toSource) && std::isfinite(d.halfAngleRadians) &&
									d.halfAngleRadians > 0 && d.halfAngleRadians <= Pi)),
				 Code::InvalidInput, "Invalid directional mixture constraint");
		out.key = Key(r, samples);
		auto areas = Areas(r, samples);
		Need(areas.size() <= o.maxAreas, Code::WorkLimit,
			 "Too many local areas; issue explicit deterministic area shards");
		uint64_t estimate =
			uint64_t(o.maxNodesPerArea) * r.profiles.size() * (o.spots ? 3 : 1) *
				(sizeof(Trial) + o.samplesPerArea * sizeof(SupportingObservation)) +
			uint64_t(o.maxAreas) * r.profiles.size() * 3 * (o.spots ? 2 : 1) * o.retainedPerChannelProfile *
				(sizeof(Hypothesis) + sizeof(EmitterEvidence) + sizeof(EmitterCandidate) + 2048 +
				 o.samplesPerArea * (sizeof(SupportingObservation) + sizeof(uint32_t))) +
			samples.size() * sizeof(Sample) * 8;
		Need(estimate <= o.maxBytes && (!read.maxBytes || estimate <= read.maxBytes), Code::MemoryLimit,
			 "Candidate accounted memory budget");
		out.report.accountedPeakBytes = estimate;
		size_t maximumOutput =
			areas.size() * r.profiles.size() * 3 * (o.spots ? 2 : 1) * o.retainedPerChannelProfile;
		out.evidence.reserve(maximumOutput);
		out.candidates.reserve(maximumOutput);
		out.hypotheses.reserve(maximumOutput);
		std::set<Id> areaIds;
		for (const auto &area : areas)
		{
			Need(area.id && areaIds.insert(area.id).second && Valid(area.positions) &&
					 Valid(area.receivers) && !area.reason.empty(),
				 Code::InvalidInput, "Invalid/duplicate search area");
			V size = V(area.positions.maxWorld) - V(area.positions.minWorld);
			Need(size.x > 0 && size.y > 0 && size.z > 0, Code::InvalidInput,
				 "Degenerate source search bounds");
			AreaReport report;
			report.id = area.id;
			auto selected = Select(samples, area, o, report);
			// A single patch/styled/unsupported receiver must not poison all the
			// otherwise usable faces. This is receiver capability, separate from
			// unknown occluding geometry, which must still affect relevant rays.
			Bake::Options supportOptions;
			supportOptions.allowAssumed = o.allowAssumedProfiles;
			selected.erase(std::remove_if(selected.begin(), selected.end(),
										  [&](const Sample &sample) {
											  bool supported = false;
											  for (const auto &profile : r.profiles)
												  supported |=
													  Bake::Capability(profile, Bake::SourceKind::Point,
																	   sample.receiver, supportOptions)
														  .implemented;
											  if (!supported)
											  {
												  ++report.unsupportedReceivers;
												  ++report.excluded;
												  out.report.diagnostics.push_back(
													  "Receiver " + std::to_string(sample.receiver.sample) +
													  " unsupported by supplied point/profile "
													  "implementation; other receivers remain usable");
											  }
											  return !supported;
										  }),
						   selected.end());
			report.selected = selected.size();
			if (selected.size() < 6)
			{
				out.report.areas.push_back(report);
				continue;
			}
			bool contrast = false;
			for (int c = 0; c < 3; ++c)
			{
				std::map<Id, std::pair<double, double>> range;
				std::map<Id, size_t> count;
				for (const auto &s : selected)
				{
					auto inserted = range.emplace(s.receiver.material,
												  std::make_pair(double(s.rgb[c]), double(s.rgb[c])));
					auto &v = inserted.first->second;
					v.first = std::min(v.first, double(s.rgb[c]));
					v.second = std::max(v.second, double(s.rgb[c]));
					count[s.receiver.material] += s.rgb[c] > 0;
				}
				for (const auto &v : range)
					contrast |=
						count[v.first] >= 4 &&
						(v.second.second - v.second.first) / (v.second.second + 1) >= o.minimumContrast;
			}
			if (!contrast)
			{
				out.report.diagnostics.push_back(
					"Area " + std::to_string(area.id) +
					": no within-material positive surface contrast; no hidden source proposed");
				out.report.areas.push_back(report);
				continue;
			}
			auto weights = Weights(selected);
			std::vector<Trial> trials;
			trials.reserve(o.maxNodesPerArea * r.profiles.size() * (o.spots ? 3 : 1));
			std::vector<Pose> queue;
			V half = size / (2 * o.coarseDivisions);
			for (uint32_t z = 0; z < o.coarseDivisions; ++z)
				for (uint32_t y = 0; y < o.coarseDivisions; ++y)
					for (uint32_t x = 0; x < o.coarseDivisions; ++x)
					{
						Pose p;
						p.p = V(area.positions.minWorld) + V(2 * x + 1, 2 * y + 1, 2 * z + 1) * 1;
						for (int a = 0; a < 3; ++a)
							p.p[a] = V(area.positions.minWorld)[a] +
									 (a == 0 ? 2 * x + 1 : a == 1 ? 2 * y + 1 : 2 * z + 1) * half[a];
						p.half = half;
						queue.push_back(p);
					}
			auto seeds = DirectionSeeds(r, area, half);
			queue.insert(queue.end(), seeds.begin(), seeds.end());
			std::set<std::tuple<float, float, float>> tested;
			for (uint32_t round = 0; round <= o.refinementRounds; ++round)
			{
				for (const auto &pose : queue)
				{
					if (!Inside(area.positions, pose.p))
						continue;
					Vec3 p = pose.p.Float();
					if (!tested.emplace(p.x, p.y, p.z).second)
						continue;
					if (report.nodes >= o.maxNodesPerArea)
					{
						report.searchLimited = true;
						break;
					}
					++report.nodes;
					Trial base;
					if (!Feasible(engine, pose.p, base, report))
						continue;
					for (size_t profile = 0; profile < r.profiles.size(); ++profile)
					{
						Bake::Options support;
						support.allowAssumed = o.allowAssumedProfiles;
						bool allowed = true;
						for (const auto &s : selected)
							allowed &= Bake::Capability(r.profiles[profile], Bake::SourceKind::Point,
														s.receiver, support)
										   .implemented;
						if (!allowed)
						{
							++report.profileUnsupported;
							continue;
						}
						bool coincident = false;
						for (const auto &s : selected)
							coincident |= Length(pose.p - V(s.receiver.world)) < 2e-3;
						if (coincident)
							continue;
						Evaluate(engine, selected, weights, pose, profile, Bake::SourceKind::Point, base, 0,
								 trials);
						if (o.spots)
							for (double radius : {64., 128.})
								Evaluate(engine, selected, weights, pose, profile, Bake::SourceKind::Spot,
										 base, radius, trials);
					}
				}
				if (round == o.refinementRounds || report.searchLimited)
					break;
				queue.clear();
				for (size_t profile = 0; profile < r.profiles.size(); ++profile)
					for (int c = 0; c < 3; ++c)
					{
						std::vector<const Trial *> order;
						for (const auto &t : trials)
							if (t.profile == profile && t.available[c])
								order.push_back(&t);
						std::sort(order.begin(), order.end(),
								  [c](const Trial *a, const Trial *b) { return Better(*a, *b, c); });
						std::set<std::tuple<float, float, float>> seedPositions;
						size_t count = 0;
						for (auto t : order)
						{
							Vec3 p = t->pose.p.Float();
							if (!seedPositions.emplace(p.x, p.y, p.z).second)
								continue;
							if (count++ == o.refineSeeds)
								break;
							for (int z : {-1, 1})
								for (int y : {-1, 1})
									for (int x : {-1, 1})
									{
										Pose next;
										next.half = t->pose.half * .5;
										next.p =
											t->pose.p + V(next.half.x * x, next.half.y * y, next.half.z * z);
										next.method = Method::Refinement;
										queue.push_back(next);
									}
						}
					}
			}
			for (size_t profile = 0; profile < r.profiles.size(); ++profile)
				for (int c = 0; c < 3; ++c)
					for (int type = 0; type < (o.spots ? 2 : 1); ++type)
					{
						std::vector<const Trial *> order;
						for (const auto &t : trials)
							if (t.profile == profile &&
								t.kind == (type ? Bake::SourceKind::Spot : Bake::SourceKind::Point))
								order.push_back(&t);
						std::sort(order.begin(), order.end(),
								  [c](const Trial *a, const Trial *b) { return Better(*a, *b, c); });
						size_t count = 0;
						std::vector<V> retained;
						for (auto t : order)
						{
							if (count == o.retainedPerChannelProfile)
							{
								report.searchLimited = true;
								break;
							}
							if (t->available[c] &&
								t->score[c] > order.front()->score[c] + o.alternativeScoreDelta)
								continue;
							bool duplicate = false;
							for (V p : retained)
								duplicate |=
									Length(t->pose.p - p) <
									std::min(t->pose.half.x, std::min(t->pose.half.y, t->pose.half.z));
							if (duplicate)
								continue;
							// No contrast or no known rays => a few unresolved spatial cells,
							// never numerical localization success or a false physical source.
							if (!t->available[c] && count >= 1)
								break;
							Publish(engine, *t, c, area, selected, report);
							retained.push_back(t->pose.p);
							++count;
						}
					}
			for (const auto &t : trials)
				for (const auto &v : t.relations)
					report.unknownVisibility +=
						v.relation == Relation::Unknown || v.relation == Relation::Assumed;
			out.report.complete &= !report.searchLimited && !report.sampleLimited;
			out.report.areas.push_back(report);
		}
		Need(!out.candidates.empty(), Code::Unavailable,
			 "No supported multi-observation surface search; directions alone cannot create a lamp");
		out.report.diagnostics.push_back("Candidate geometry only; no final amplitudes, source promotion, "
										 "physical confidence or runtime capability");
		return {};
	}
	catch (const Error &e)
	{
		auto report = std::move(out.report);
		out = Result();
		out.report = std::move(report);
		out.report.complete = false;
		out.report.diagnostics.push_back(e.message);
		return e;
	}
	catch (const std::bad_alloc &)
	{
		out = Result();
		out.report.complete = false;
		return Failure(Code::MemoryLimit, "Hidden candidate allocation failed");
	}
}
} // namespace Hidden
} // namespace Core
} // namespace Rend2LightRecon
