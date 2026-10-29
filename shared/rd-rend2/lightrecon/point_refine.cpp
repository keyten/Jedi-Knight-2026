// Profiled RGB fitting + bounded derivative-free XYZ search. CPU only.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "point_refine.h"
#include "bake_math.h"
#include "geometry_key.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <set>
#include <tuple>

namespace Rend2LightRecon
{
namespace Core
{
namespace PointRefine
{
using namespace Bake::Detail;
namespace
{
void Need(bool v, Code code, const std::string &s)
{
	if (!v)
		throw Failure(code, s);
}
void Checked(Error e)
{
	if (!e)
		throw e;
}
bool Zero(const Disk::Digest &d) { return d == Disk::Digest{}; }
bool Free(SpaceClass s) { return s == SpaceClass::Free || s == SpaceClass::Exterior; }
Bounds Box(V a, V b)
{
	Bounds out;
	out.known = true;
	float lo[3], hi[3];
	for (size_t k = 0; k < 3; ++k)
	{
		lo[k] = float(a[k]);
		hi[k] = float(b[k]);
		if (double(lo[k]) > a[k])
			lo[k] = std::nextafter(lo[k], -std::numeric_limits<float>::infinity());
		if (double(hi[k]) < b[k])
			hi[k] = std::nextafter(hi[k], std::numeric_limits<float>::infinity());
	}
	out.minWorld = Vec3(lo[0], lo[1], lo[2]);
	out.maxWorld = Vec3(hi[0], hi[1], hi[2]);
	return out;
}
bool Overlap(const Bounds &a, const Bounds &b)
{
	for (size_t k = 0; k < 3; ++k)
		if (V(a.maxWorld)[k] < V(b.minWorld)[k] || V(a.minWorld)[k] > V(b.maxWorld)[k])
			return false;
	return true;
}
bool In(const Bounds &b, V p)
{
	if (!b.known)
		return false;
	for (size_t k = 0; k < 3; ++k)
		if (p[k] < V(b.minWorld)[k] || p[k] > V(b.maxWorld)[k])
			return false;
	return true;
}
bool Valid(const Bounds &b)
{
	if (!b.known || !Finite(b.minWorld) || !Finite(b.maxWorld))
		return false;
	for (size_t k = 0; k < 3; ++k)
		if (V(b.minWorld)[k] > V(b.maxWorld)[k])
			return false;
	return true;
}
void Text(GeometryKey &k, const std::string &s)
{
	k.U(s.size());
	for (unsigned char c : s)
		k.U(c);
}
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> p)
{
	return VectorChunk<T>(p, 0, p->size());
}
bool Add(uint64_t &v, uint64_t x, uint64_t cap)
{
	// Report performed work even when this query crosses the cumulative limit.
	bool allowed = x <= cap && v <= cap - x;
	v = x > UINT64_MAX - v ? UINT64_MAX : v + x;
	return allowed;
}
class Scene final : public PlacementProvider
{
	SceneInput input;
	Disk::Digest policy{};

  public:
	explicit Scene(SceneInput in) : input(std::move(in))
	{
		Core::GeometryKey k;
		k.U(kVersion);
		k.Digest(input.map.sha256);
		k.Digest(input.domainKey);
		k.Digest(input.geometry->CertificationKey());
		k.U(input.maxWorkPerCheck);
		k.U(input.maxSegments);
		k.F(input.minimumSegmentWorld);
		std::sort(input.dynamic.begin(), input.dynamic.end(),
				  [](const DynamicBoundary &a, const DynamicBoundary &b) { return a.id < b.id; });
		for (const auto &d : input.dynamic)
		{
			k.U(d.id);
			k.U(d.model);
			k.B(d.bounds);
			k.U(d.sweptExtentKnown);
		}
		if (input.regions)
			k.Digest(input.regions->CertificationKey());
		policy = k.Finish();
	}
	Disk::Digest MapKey() const override { return input.map.sha256; }
	Disk::Digest GeometryKey() const override { return input.geometry->CertificationKey(); }
	Disk::Digest PolicyKey() const override { return policy; }
	Error Check(Vec3 from, Vec3 to, double radius, const ReadContext &read,
				PlacementResult &out) const override
	{
		out = {};
		if (!Finite(from) || !Finite(to) || !std::isfinite(radius) || radius < 0)
			return Failure(Code::InvalidInput, "Invalid placement segment/radius");
		try
		{
			auto cancelled = [&]() {
				Need(!(read.cancellation && read.cancellation->IsCancelled()), Code::Cancelled,
					 "Placement cancelled");
				Need(out.work < input.maxWorkPerCheck, Code::WorkLimit, "Placement query work budget");
			};
			auto inspect = [&](V a, V b, bool endpoint) {
				cancelled();
				V lo, hi;
				for (size_t k = 0; k < 3; ++k)
				{
					lo[k] = std::min(a[k], b[k]) - radius;
					hi[k] = std::max(a[k], b[k]) + radius;
				}
				Bounds box = Box(lo, hi);
				for (const auto &d : input.dynamic)
				{
					Need(Add(out.work, 1, input.maxWorkPerCheck), Code::WorkLimit,
						 "Moving-bound query budget");
					if (!d.sweptExtentKnown || !d.bounds.known || Overlap(box, d.bounds))
					{
						out.state = Constraint::Dynamic;
						return false;
					}
				}
				SpaceClass space;
				uint64_t work = 0;
				Checked(input.domain->ClassifyBox(box, space, work));
				Need(Add(out.work, work, input.maxWorkPerCheck), Code::WorkLimit, "Placement domain budget");
				if (space == SpaceClass::Solid)
				{
					out.state = Constraint::Solid;
					return false;
				}
				if (space == SpaceClass::Outside || space == SpaceClass::Sky)
				{
					out.state = Constraint::Outside;
					return false;
				}
				GeometryBoxResult visual;
				Checked(input.geometry->OverlapBox(box, read, visual,
												   std::max<uint64_t>(1, input.maxWorkPerCheck - out.work)));
				Need(Add(out.work, visual.work, input.maxWorkPerCheck), Code::WorkLimit,
					 "Placement visual budget");
				if (!visual.coverageComplete)
				{
					out.state = Constraint::UnknownGeometry;
					return false;
				}
				if (visual.intersects)
				{
					out.state = (visual.issues & (OpticalUnknown | UnsupportedGeometry | PatchApproximation))
									? Constraint::UnknownGeometry
									: Constraint::Clearance;
					return false;
				}
				if (!Free(space))
				{
					out.state = Constraint::UnknownGeometry;
					return false;
				}
				if (endpoint)
				{
					SpatialLocation centre;
					Checked(input.domain->Locate(b.Float(), centre));
					Need(Add(out.work, centre.work, input.maxWorkPerCheck), Code::WorkLimit,
						 "Placement point budget");
					if (!Free(centre.space))
					{
						out.state = centre.space == SpaceClass::Solid ? Constraint::Solid
																	  : Constraint::UnknownGeometry;
						return false;
					}
				}
				out.state = Constraint::Free;
				return true;
			};
			if (!inspect(V(from), V(from), true) || !inspect(V(to), V(to), true))
				return {};
			if (Length(V(to) - V(from)) > 1e-9)
			{
				cancelled();
				RayQuery ray;
				ray.startWorld = from;
				ray.endWorld = to;
				ray.originKnownFree = true;
				ray.bias.absoluteWorld = ray.bias.relative = 0;
				ray.maxWork = std::max<uint64_t>(1, input.maxWorkPerCheck - out.work);
				RayResult hit;
				Checked(input.geometry->Query(ray, read, hit));
				++out.rays;
				Need(Add(out.work, hit.nodeTests + hit.primitiveTests, input.maxWorkPerCheck),
					 Code::WorkLimit, "Placement ray budget");
				if (hit.state == VisibilityState::Blocked && hit.reliability != RayReliability::Assumed &&
					!(hit.issues & PolicyAssumption))
				{
					out.state = Constraint::OpaquePath;
					return {};
				}
				if (hit.state != VisibilityState::Visible || hit.reliability != RayReliability::Geometric ||
					(hit.issues & PolicyAssumption))
				{
					out.state = Constraint::UnknownPath;
					return {};
				}
				std::vector<std::pair<V, V>> pending;
				pending.emplace_back(V(from), V(to));
				size_t tested = 0;
				while (!pending.empty())
				{
					cancelled();
					if (++tested > input.maxSegments)
					{
						out.state = Constraint::UnknownPath;
						return {};
					}
					auto segment = pending.back();
					pending.pop_back();
					if (inspect(segment.first, segment.second, false))
						continue;
					if (out.state == Constraint::Dynamic || out.state == Constraint::Solid ||
						out.state == Constraint::Outside)
						return {};
					if (Length(segment.second - segment.first) <= input.minimumSegmentWorld)
					{
						out.state = Constraint::UnknownPath;
						return {};
					}
					V mid = (segment.first + segment.second) * .5;
					pending.emplace_back(mid, segment.second);
					pending.emplace_back(segment.first, mid);
				}
			}
			out.state = Constraint::Free;
			if (input.regions)
			{
				RegionAssignment a;
				Checked(input.regions->Lookup(to, read, a));
				out.regionKnown = a.state == RegionAssignmentState::Assigned;
				out.region = out.regionKnown ? a.region : 0;
			}
			return {};
		}
		catch (const Error &e)
		{
			return e;
		}
		catch (const std::bad_alloc &)
		{
			return Failure(Code::MemoryLimit, "Placement scratch allocation");
		}
	}
};
typedef std::tuple<Id, int64_t, int64_t, int64_t, size_t> WeightKey;
WeightKey Group(const Amplitude::Observation &a)
{
	if (a.region)
		return {a.region, 0, 0, 0, size_t(a.type)};
	V p(a.receiver.world);
	Need(Finite(p) && std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)}) < 1e12, Code::InvalidInput,
		 "Invalid spatial weighting position");
	return {0, int64_t(std::floor(p.x / 128)), int64_t(std::floor(p.y / 128)), int64_t(std::floor(p.z / 128)),
			size_t(a.type)};
}
class Balanced final : public ChunkSource<Amplitude::Observation>
{
  public:
	std::shared_ptr<const ChunkSource<Amplitude::Observation>> source;
	std::map<WeightKey, double> measure;
	Error Read(uint64_t first, size_t count, const ReadContext &read,
			   Chunk<Amplitude::Observation> &out) const override
	{
		Chunk<Amplitude::Observation> input;
		auto e = source->Read(first, count, read, input);
		if (!e)
			return e;
		if (input.count != count || !input.owner || !input.data)
			return Failure(Code::ProviderError, "Truncated balancing stream");
		try
		{
			auto v = std::make_shared<std::vector<Amplitude::Observation>>(input.data, input.data + count);
			for (auto &a : *v)
			{
				auto found = measure.find(Group(a));
				if (found != measure.end() && found->second > 0)
					a.measure /= found->second;
			}
			out = Lease(v);
			return {};
		}
		catch (const Error &e)
		{
			return e;
		}
		catch (const std::bad_alloc &)
		{
			return Failure(Code::MemoryLimit, "Weighting scratch allocation");
		}
	}
};
class Stored final : public ChunkSource<Amplitude::Observation>
{
  public:
	std::shared_ptr<std::vector<Amplitude::Observation>> data =
		std::make_shared<std::vector<Amplitude::Observation>>();
	Error Read(uint64_t first, size_t n, const ReadContext &,
			   Chunk<Amplitude::Observation> &out) const override
	{
		if (first > data->size() || n > data->size() - first)
			return Failure(Code::InvalidInput, "Diagnostic stream range");
		out = VectorChunk<Amplitude::Observation>(data, size_t(first), n);
		return {};
	}
};
struct State
{
	std::vector<Vec3> positions;
	Amplitude::Result amplitude;
	double prior = 0, directional = 0, shadow = 0;
	FitEvidence::Value shadowValue;
	double Total() const { return amplitude.objective.Total() + prior + directional + shadow; }
};
struct SampleValues final : public Amplitude::PredictionSink
{
	std::vector<Amplitude::Observation> rows;
	std::vector<V> values;
	std::vector<double> weight;
	Error Write(const Amplitude::Observation &a, Vec3 p, double w) override
	{
		rows.push_back(a);
		values.emplace_back(p);
		weight.push_back(w);
		return {};
	}
};
struct Eigen
{
	std::vector<double> values, vectors;
};
Eigen Diagonalize(std::vector<double> a, size_t n)
{
	Eigen e;
	e.vectors.assign(n * n, 0);
	for (size_t i = 0; i < n; ++i)
		e.vectors[i * n + i] = 1;
	for (size_t iteration = 0; iteration < 80 * n * n; ++iteration)
	{
		size_t p = 0, q = 0;
		double maximum = 0, diag = 0;
		for (size_t i = 0; i < n; ++i)
		{
			diag = std::max(diag, std::abs(a[i * n + i]));
			for (size_t j = i + 1; j < n; ++j)
				if (std::abs(a[i * n + j]) > maximum)
				{
					maximum = std::abs(a[i * n + j]);
					p = i;
					q = j;
				}
		}
		if (maximum <= std::max(1e-28, diag * 1e-13))
			break;
		double angle = .5 * std::atan2(2 * a[p * n + q], a[q * n + q] - a[p * n + p]), c = std::cos(angle),
			   s = std::sin(angle), app = a[p * n + p], aqq = a[q * n + q], apq = a[p * n + q];
		for (size_t k = 0; k < n; ++k)
			if (k != p && k != q)
			{
				double x = a[k * n + p], y = a[k * n + q];
				a[k * n + p] = a[p * n + k] = c * x - s * y;
				a[k * n + q] = a[q * n + k] = s * x + c * y;
			}
		a[p * n + p] = c * c * app - 2 * c * s * apq + s * s * aqq;
		a[q * n + q] = s * s * app + 2 * c * s * apq + c * c * aqq;
		a[p * n + q] = a[q * n + p] = 0;
		for (size_t k = 0; k < n; ++k)
		{
			double x = e.vectors[k * n + p], y = e.vectors[k * n + q];
			e.vectors[k * n + p] = c * x - s * y;
			e.vectors[k * n + q] = s * x + c * y;
		}
	}
	for (size_t i = 0; i < n; ++i)
		e.values.push_back(std::max(0.0, a[i * n + i]));
	return e;
}
double Halton(uint64_t n, unsigned base)
{
	double result = 0, f = 1;
	while (n)
	{
		f /= base;
		result += f * (n % base);
		n /= base;
	}
	return result;
}
struct Engine
{
	const Request &r;
	const Options &o;
	ReadContext read;
	Result out;
	Amplitude::Request lighting;
	std::vector<Point> points;
	std::vector<Vec3> original;
	std::vector<double> radius;
	std::vector<bool> boundary;
	std::map<Id, size_t> sourceIndex, pointIndex;
	std::shared_ptr<Amplitude::Cache> cache = std::make_shared<Amplitude::Cache>();
	std::vector<DirectionEvidence> directions;
	Disk::Digest admission{};
	GeometryKey key;
	size_t sweep = 0;
	uint64_t diagnosticFits = 0;
	Engine(const Request &request, const ReadContext &context)
		: r(request), o(r.options), read(context), lighting(r.lighting), points(r.points)
	{
		Need(std::isfinite(r.shadowWeight) && r.shadowWeight >= 0, Code::InvalidInput,
			 "Invalid shadow weight");
		if (r.shadowWeight > 0)
			Need(r.shadow && r.shadow->MapKey() == lighting.mapKey &&
					 r.shadow->GeometryKey() == lighting.geometry.geometryKey,
				 Code::MissingGeometry, "Shadow objective map/geometry generation mismatch");
		Need(r.placement && r.placement->MapKey() == lighting.mapKey &&
				 r.placement->GeometryKey() == lighting.geometry.geometryKey &&
				 !Zero(r.placement->PolicyKey()),
			 Code::MissingGeometry, "Missing/stale placement certificate provider");
		Need(!points.empty() && points.size() <= o.maxPoints && o.maxPoints <= 16 && o.maxFits &&
				 o.maxFits <= 100000 && o.maxSweeps && o.maxSweeps <= 10000 && o.multiStarts &&
				 o.multiStarts <= 16 && o.diagnosticRows <= 512 && lighting.options.batchRows &&
				 lighting.count && lighting.count <= lighting.options.maxRows,
			 Code::InvalidInput, "Invalid point-refinement limits");
		Need(lighting.options.baselineUpper.x == 0 && lighting.options.baselineUpper.y == 0 &&
				 lighting.options.baselineUpper.z == 0,
			 Code::Unavailable, "XYZ refinement does not fit an ambient/GI nuisance baseline");
		Need(r.directions.size() <= 1024, Code::MemoryLimit, "Directional evidence limit");
		Need(std::isfinite(o.initialStepWorld) && o.initialStepWorld > 0 &&
				 std::isfinite(o.minimumStepWorld) && o.minimumStepWorld > 0 &&
				 o.minimumStepWorld <= o.initialStepWorld && std::isfinite(o.relativeImprovement) &&
				 o.relativeImprovement >= 0 && std::isfinite(o.diagnosticStepWorld) &&
				 o.diagnosticStepWorld > 0 && std::isfinite(o.feasibleObjectiveSlack) &&
				 o.feasibleObjectiveSlack >= 0,
			 Code::InvalidInput, "Invalid position tolerances");
		uint64_t cap = read.maxBytes ? std::min(read.maxBytes, o.maxBytes) : o.maxBytes;
		uint64_t reserve = 2 * 1024 * 1024 +
						   (uint64_t(o.maxFits) + uint64_t(o.maxSweeps) * o.multiStarts) * sizeof(Iteration) +
						   uint64_t(o.diagnosticRows) * sizeof(Amplitude::Observation) * 4;
		Need(reserve < cap && lighting.options.maxBytes <= cap - reserve, Code::MemoryLimit,
			 "Point refinement/amplitude scratch preflight");
		out.work.accountedBytes = reserve + lighting.options.maxBytes;
		read.maxBytes = cap;
		std::sort(points.begin(), points.end(),
				  [](const Point &a, const Point &b) { return a.source < b.source; });
		for (size_t i = 0; i < lighting.candidates.size(); ++i)
			Need(sourceIndex.emplace(lighting.candidates[i].source.id, i).second, Code::InvalidInput,
				 "Duplicate candidate ID");
		key.U(kVersion);
		if (r.shadowWeight > 0)
		{
			key.Digest(r.shadow->Key());
			key.F(r.shadowWeight);
		}
		key.Digest(lighting.mapKey);
		key.Digest(lighting.observationKey);
		key.Digest(lighting.profile.contentKey);
		key.Digest(r.placement->PolicyKey());
		key.F(o.initialStepWorld);
		key.F(o.minimumStepWorld);
		key.F(o.relativeImprovement);
		key.F(o.diagnosticStepWorld);
		key.F(o.feasibleObjectiveSlack);
		key.U(o.maxSweeps);
		key.U(o.maxFits);
		key.U(o.multiStarts);
		key.U(o.seed);
		key.U(o.expand);
		key.U(o.balanceRegions);
		key.U(o.parabolicAcceleration);
		key.U(o.diagnostics);
		key.U(o.diagnosticRows);
		key.U(o.maxDiagnosticFits);
		key.U(o.maxPairs);
		key.U(o.maxRays);
		key.U(o.maxWork);
		key.U(o.maxAmplitudeEvaluations);
		for (size_t i = 0; i < points.size(); ++i)
		{
			const auto &p = points[i];
			Need(p.source && pointIndex.emplace(p.source, i).second && sourceIndex.count(p.source) &&
					 Valid(p.bounds),
				 Code::InvalidInput, "Invalid point identity/search bounds");
			const auto &s = lighting.candidates[sourceIndex.at(p.source)].source;
			Need(s.kind == Bake::SourceKind::Point && s.position.known && Finite(s.position.world),
				 Code::Unavailable, "XYZ refinement supports known point geometry only");
			Need(std::isfinite(p.initialRadiusWorld) && p.initialRadiusWorld > 0 &&
					 std::isfinite(p.maximumRadiusWorld) && p.maximumRadiusWorld >= p.initialRadiusWorld &&
					 p.maximumRadiusWorld <= 1e9 && std::isfinite(p.sourceSupportRadiusWorld) &&
					 p.sourceSupportRadiusWorld >= 0 && p.sourceSupportRadiusWorld <= 1e6 &&
					 std::isfinite(p.clearanceWorld) && p.clearanceWorld > 0 && p.clearanceWorld <= 1e6,
				 Code::InvalidInput, "Invalid source clearance/search radius");
			Need(In(p.bounds, V(s.position.world)), Code::InvalidInput, "Initial point outside hard bounds");
			if (p.prior.known)
				Need(Finite(p.prior.meanWorld) && Positive(V(p.prior.sigmaWorld)) &&
						 p.prior.sigmaWorld.x > 0 && p.prior.sigmaWorld.y > 0 && p.prior.sigmaWorld.z > 0 &&
						 std::isfinite(p.prior.weight) && p.prior.weight >= 0 && !p.prior.provenance.empty(),
					 Code::InvalidInput, "Position prior requires finite positive scales and provenance");
			if (p.mount.known)
				Need(Finite(p.mount.anchorWorld) && Normal(V(p.mount.normalWorld)) &&
						 std::isfinite(p.mount.minimumOffsetWorld) &&
						 std::isfinite(p.mount.maximumOffsetWorld) &&
						 p.mount.minimumOffsetWorld >= p.sourceSupportRadiusWorld + p.clearanceWorld &&
						 p.mount.maximumOffsetWorld >= p.mount.minimumOffsetWorld,
					 Code::InvalidInput, "Invalid mount offset policy");
			original.push_back(s.position.world);
			radius.push_back(p.initialRadiusWorld);
			boundary.push_back(false);
			key.U(p.source);
			key.V(s.position.world);
			key.B(p.bounds);
			key.F(p.initialRadiusWorld);
			key.F(p.maximumRadiusWorld);
			key.F(p.sourceSupportRadiusWorld);
			key.F(p.clearanceWorld);
			key.U(p.prior.known);
			key.V(p.prior.meanWorld);
			key.V(p.prior.sigmaWorld);
			key.F(p.prior.weight);
			key.U(uint32_t(p.prior.kind));
			Text(key, p.prior.provenance);
			key.U(p.mount.known);
			key.V(p.mount.anchorWorld);
			key.V(p.mount.normalWorld);
			key.F(p.mount.minimumOffsetWorld);
			key.F(p.mount.maximumOffsetWorld);
		}
		for (const auto &d : r.directions)
		{
			Need(d.id && pointIndex.count(d.source) && Finite(d.originWorld) && Normal(V(d.toSource)) &&
					 std::isfinite(d.halfAngleRadians) && d.halfAngleRadians >= 0 &&
					 d.halfAngleRadians < Pi / 2 && std::isfinite(d.weight) && d.weight >= 0 &&
					 !d.provenance.empty(),
				 Code::InvalidInput, "Invalid directional constraint");
			if (d.isolatedSourceAttested && d.profileValidated && d.weight > 0)
				directions.push_back(d);
			else
				out.diagnostics.push_back("Excluded unvalidated/mixed directional hint " +
										  std::to_string(d.id));
		}
		Need(directions.size() <= 1024, Code::MemoryLimit, "Directional evidence limit");
		std::sort(directions.begin(), directions.end(),
				  [](const DirectionEvidence &a, const DirectionEvidence &b) { return a.id < b.id; });
		for (size_t i = 0; i < directions.size(); ++i)
		{
			const auto &d = directions[i];
			Need(!i || d.id != directions[i - 1].id, Code::InvalidInput, "Duplicate directional ID");
			key.U(d.id);
			key.U(d.source);
			key.U(d.surface);
			key.V(d.originWorld);
			key.V(d.toSource);
			key.F(d.halfAngleRadians);
			key.F(d.weight);
			key.U(d.originKnownFree);
			Text(key, d.provenance);
		}
		lighting.cache = cache;
		lighting.options.diagnostics = false;
		if (o.balanceRegions)
		{
			auto balanced = std::make_shared<Balanced>();
			balanced->source = lighting.observations;
			Need(bool(balanced->source), Code::InvalidInput, "Missing training stream");
			for (uint64_t first = 0; first < lighting.count; first += lighting.options.batchRows)
			{
				Cancel();
				Chunk<Amplitude::Observation> rows;
				size_t n = size_t(std::min<uint64_t>(lighting.options.batchRows, lighting.count - first));
				Need(n > 0, Code::InvalidInput, "Zero amplitude batch");
				Checked(balanced->source->Read(first, n, read, rows));
				Need(rows.count == n && rows.owner && rows.data, Code::ProviderError,
					 "Truncated weight stream");
				for (size_t i = 0; i < n; ++i)
				{
					const auto &a = rows.data[i];
					Need(std::isfinite(a.measure) && a.measure >= 0 && std::isfinite(a.reliability) &&
							 a.reliability >= 0,
						 Code::InvalidInput, "Invalid balancing measure");
					if (a.eligible && a.measure > 0 && a.reliability > 0)
					{
						auto group = Group(a);
						Need(balanced->measure.size() < 4096 || balanced->measure.count(group),
							 Code::MemoryLimit, "Region weight group limit");
						double &total = balanced->measure[group];
						total += a.measure * a.reliability;
						Need(std::isfinite(total), Code::InvalidInput, "Balancing measure overflow");
					}
				}
			}
			lighting.observations = balanced;
		}
	}
	void Cancel()
	{
		Need(!(read.cancellation && read.cancellation->IsCancelled()), Code::Cancelled,
			 "Point refinement cancelled");
	}
	void AmplitudeWork(const Amplitude::Work &w)
	{
		Need(Add(out.work.amplitudeEvaluations, w.objectiveEvaluations, o.maxAmplitudeEvaluations) &&
				 Add(out.work.pairs, w.pairs, o.maxPairs) && Add(out.work.rays, w.rays, o.maxRays) &&
				 Add(out.work.traversal, w.traversal, o.maxWork),
			 Code::WorkLimit, "Point refinement cumulative numerical budget");
		out.work.cacheHits += w.cacheHits;
	}
	bool Placement(size_t i, Vec3 from, Vec3 to, bool local = true)
	{
		Cancel();
		const auto &p = points[i];
		if (!In(p.bounds, V(to)) || (local && (std::abs(double(to.x) - original[i].x) > radius[i] + 1e-5 ||
											   std::abs(double(to.y) - original[i].y) > radius[i] + 1e-5 ||
											   std::abs(double(to.z) - original[i].z) > radius[i] + 1e-5)))
		{
			++out.work.rejected[size_t(Constraint::SearchBounds)];
			boundary[i] = true;
			return false;
		}
		if (p.mount.known)
		{
			double offset = Dot(V(to) - V(p.mount.anchorWorld), V(p.mount.normalWorld));
			if (offset < p.mount.minimumOffsetWorld || offset > p.mount.maximumOffsetWorld)
			{
				++out.work.rejected[size_t(Constraint::Mount)];
				boundary[i] = true;
				return false;
			}
		}
		PlacementResult result;
		++out.work.placementChecks;
		Checked(r.placement->Check(from, to, p.sourceSupportRadiusWorld + p.clearanceWorld, read, result));
		Need(Add(out.work.rays, result.rays, o.maxRays) && Add(out.work.traversal, result.work, o.maxWork),
			 Code::WorkLimit, "Placement cumulative budget");
		if (result.state != Constraint::Free)
		{
			++out.work.rejected[size_t(result.state)];
			boundary[i] = true;
			return false;
		}
		return true;
	}
	double Prior(const std::vector<Vec3> &positions)
	{
		double score = 0;
		for (size_t i = 0; i < points.size(); ++i)
			if (points[i].prior.known)
			{
				V delta = V(positions[i]) - V(points[i].prior.meanWorld);
				for (size_t k = 0; k < 3; ++k)
				{
					double z = delta[k] / V(points[i].prior.sigmaWorld)[k];
					score += .5 * points[i].prior.weight * z * z;
				}
			}
		return score;
	}
	bool Directional(const std::vector<Vec3> &positions, double &value)
	{
		value = 0;
		for (const auto &d : directions)
		{
			V delta = V(positions[pointIndex.at(d.source)]) - V(d.originWorld);
			if (Length(delta) <= 1e-8)
				return false;
			RayQuery ray;
			ray.startWorld = d.originWorld;
			ray.endWorld = positions[pointIndex.at(d.source)];
			ray.originSurface = d.surface;
			ray.originKnownFree = d.originKnownFree;
			ray.maxWork = std::max<uint64_t>(1, std::min<uint64_t>(1048576, o.maxWork - out.work.traversal));
			RayResult hit;
			Need(bool(lighting.geometry.visibility), Code::MissingGeometry,
				 "Directional constraints require static visibility");
			Checked(lighting.geometry.visibility->Query(ray, read, hit));
			Need(Add(out.work.rays, 1, o.maxRays) &&
					 Add(out.work.traversal, hit.nodeTests + hit.primitiveTests, o.maxWork),
				 Code::WorkLimit, "Directional ray budget");
			if (hit.state == VisibilityState::Unknown || hit.reliability == RayReliability::Assumed)
				return false;
			double error = hit.state == VisibilityState::Blocked
							   ? Pi
							   : std::max(0., std::acos(Clamp(Dot(Unit(delta), V(d.toSource)), -1, 1)) -
												  d.halfAngleRadians);
			value += .5 * d.weight * error * error;
		}
		return std::isfinite(value);
	}
	Amplitude::Request Setup(const std::vector<Vec3> &positions, const Amplitude::Result *warm = nullptr)
	{
		auto q = lighting;
		for (size_t i = 0; i < points.size(); ++i)
			q.candidates[sourceIndex.at(points[i].source)].source.position.world = positions[i];
		if (warm)
			for (const auto &s : warm->sources)
			{
				auto &c = q.candidates[sourceIndex.at(s.source)];
				V v(s.compilerQ);
				for (size_t k = 0; k < 3; ++k)
					v[k] = Clamp(v[k], V(c.lowerQ)[k], V(c.upperQ)[k]);
				c.initialQ = v.Float();
			}
		q.options.maxEvaluations = std::min<uint64_t>(
			q.options.maxEvaluations, o.maxAmplitudeEvaluations - out.work.amplitudeEvaluations);
		q.options.maxPairs = std::min(q.options.maxPairs, o.maxPairs - out.work.pairs);
		q.options.maxRays = std::min(q.options.maxRays, o.maxRays - out.work.rays);
		q.options.maxTraversal = std::min(q.options.maxTraversal, o.maxWork - out.work.traversal);
		return q;
	}
	bool FitAt(const std::vector<Vec3> &positions, const Amplitude::Result *warm, State &state,
			   bool record = true)
	{
		Cancel();
		Need(out.work.fits < o.maxFits, Code::WorkLimit, "Position trial/amplitude fit budget");
		++out.work.fits;
		auto q = Setup(positions, warm);
		Amplitude::Result a;
		auto e = Amplitude::Fit(q, read, a);
		AmplitudeWork(a.work);
		if (!e)
		{
			if (e.code == Code::Unavailable)
			{
				++out.work.coverageRejected;
				return false;
			}
			throw e;
		}
		Need(a.fitted && a.strictReevaluated, Code::AlgorithmError, "Unfrozen amplitude result");
		if (!Zero(admission) && a.admissionKey != admission)
		{
			++out.work.coverageRejected;
			return false;
		}
		if (Zero(admission))
			admission = a.admissionKey;
		state.positions = positions;
		state.amplitude = std::move(a);
		state.prior = Prior(positions);
		if (!Directional(positions, state.directional))
		{
			++out.work.coverageRejected;
			return false;
		}
		if (r.shadowWeight > 0)
		{
			std::vector<FitEvidence::Source> sources;
			for (const auto &c : q.candidates)
			{
				FitEvidence::Source source;
				source.geometry.source.id = c.source.id;
				source.geometry.kind = c.source.kind == Bake::SourceKind::Point
										   ? EmitterKind::Point
										   : c.source.kind == Bake::SourceKind::Spot
												 ? EmitterKind::Spot
												 : c.source.kind == Bake::SourceKind::Polygon
													   ? EmitterKind::Rectangle
													   : c.source.kind == Bake::SourceKind::Tube
															 ? EmitterKind::Tube
															 : EmitterKind::Extension;
				source.geometry.position = c.source.position;
				source.geometry.frame = c.source.orientation;
				source.geometry.geometry = c.source.geometry;
				source.geometry.angular = c.source.angular;
				source.legacySpotDistance = c.source.spotTargetDistanceWorld;
				source.legacySpotRadius = c.source.spotRadiusWorld;
				for (const auto &value : state.amplitude.sources)
					if (value.source == c.source.id)
						source.rgb = value.compilerQ;
				sources.push_back(source);
			}
			Checked(r.shadow->Evaluate(sources, read, state.shadowValue));
			Need(Add(out.work.rays, state.shadowValue.rays, o.maxRays) &&
					 Add(out.work.traversal, state.shadowValue.work, o.maxWork),
				 Code::WorkLimit, "Shadow evidence cumulative budget");
			Need(state.shadowValue.available && std::isfinite(state.shadowValue.loss) &&
					 state.shadowValue.loss >= 0,
				 Code::Unavailable, "Shadow objective unavailable");
			state.shadow = r.shadowWeight * state.shadowValue.loss;
		}
		Need(std::isfinite(state.Total()), Code::AlgorithmError, "Nonfinite position objective");
		++out.work.cacheInvalidations;
		if (record)
		{
			Iteration it;
			it.evaluation = out.work.fits;
			it.sweep = sweep;
			it.data = state.amplitude.objective.Total();
			it.prior = state.prior;
			it.directional = state.directional;
			it.shadow = state.shadow;
			it.total = state.Total();
			out.trace.push_back(it);
		}
		return true;
	}
	Vec3 Clip(size_t i, V value)
	{
		for (size_t k = 0; k < 3; ++k)
			value[k] =
				Clamp(value[k], std::max(V(points[i].bounds.minWorld)[k], V(original[i])[k] - radius[i]),
					  std::min(V(points[i].bounds.maxWorld)[k], V(original[i])[k] + radius[i]));
		return value.Float();
	}
	bool Trial(const State &current, size_t point, Vec3 position, double step, State &outState,
			   bool record = true)
	{
		if (Length(V(position) - V(current.positions[point])) < 1e-7)
			return false;
		if (!Placement(point, current.positions[point], position))
			return false;
		auto all = current.positions;
		all[point] = position;
		if (!FitAt(all, &current.amplitude, outState, record))
			return false;
		bool changed = outState.amplitude.visibilityKey != current.amplitude.visibilityKey;
		out.work.visibilityChanges += changed;
		if (record)
		{
			auto &t = out.trace.back();
			t.source = points[point].source;
			t.position = position;
			t.stepWorld = step;
			t.visibilityChanged = changed;
		}
		return true;
	}
	bool Better(const State &a, const State &b) const
	{
		return a.Total() < b.Total() - o.relativeImprovement * std::max(1.0, std::abs(b.Total()));
	}
	State Search(State current)
	{
		double step = o.initialStepWorld;
		for (sweep = 0; sweep < o.maxSweeps; ++sweep)
		{
			Cancel();
			State best = current;
			size_t chosen = SIZE_MAX;
			std::vector<bool> neighborhoodHit(points.size(), false);
			for (size_t i = 0; i < points.size(); ++i)
				for (size_t axis = 0; axis < 3; ++axis)
				{
					State sides[2];
					bool valid[2] = {};
					for (unsigned sign = 0; sign < 2; ++sign)
					{
						V trial(current.positions[i]);
						trial[axis] += (sign ? 1 : -1) * step;
						Vec3 clipped = Clip(i, trial);
						bool clippedToBounds = Length(trial - V(clipped)) > 1e-6;
						neighborhoodHit[i] = neighborhoodHit[i] ||
											 std::abs(trial[axis] - V(original[i])[axis]) > radius[i] + 1e-6;
						if (clippedToBounds)
						{
							boundary[i] = true;
							++out.work.rejected[size_t(Constraint::SearchBounds)];
						}
						valid[sign] = Trial(current, i, clipped, step, sides[sign]);
						if (valid[sign] && Better(sides[sign], best))
						{
							best = sides[sign];
							chosen = out.trace.size() - 1;
						}
					}
					if (o.parabolicAcceleration && valid[0] && valid[1] && current.amplitude.linear &&
						sides[0].amplitude.linear && sides[1].amplitude.linear &&
						sides[0].amplitude.visibilityKey == current.amplitude.visibilityKey &&
						sides[1].amplitude.visibilityKey == current.amplitude.visibilityKey)
					{
						double left = sides[0].Total(), right = sides[1].Total(),
							   curvature = left + right - 2 * current.Total();
						if (curvature > 1e-15)
						{
							double delta = Clamp(.5 * step * (left - right) / curvature, -step, step);
							V proposed(current.positions[i]);
							proposed[axis] += delta;
							State refined;
							if (Trial(current, i, Clip(i, proposed), step, refined) && Better(refined, best))
							{
								best = refined;
								chosen = out.trace.size() - 1;
							}
						}
					}
				}
			if (Better(best, current))
			{
				if (chosen != SIZE_MAX)
					out.trace[chosen].accepted = true;
				++out.work.accepted;
				current = std::move(best);
				continue;
			}
			bool expanded = false;
			if (o.expand && current.amplitude.objective.data > o.feasibleObjectiveSlack)
			{
				for (size_t i = 0; i < points.size(); ++i)
					if (neighborhoodHit[i] && radius[i] < points[i].maximumRadiusWorld)
					{
						radius[i] = std::min(points[i].maximumRadiusWorld, radius[i] * 2);
						expanded = true;
					}
				if (expanded)
				{
					++out.work.expansions;
					step = std::max(step, o.initialStepWorld);
					Iteration it;
					it.evaluation = out.work.fits;
					it.sweep = sweep;
					it.expansion = true;
					it.total = current.Total();
					it.data = current.amplitude.objective.Total();
					it.prior = current.prior;
					it.directional = current.directional;
					out.trace.push_back(it);
					continue;
				}
			}
			if (step <= o.minimumStepWorld)
			{
				out.stop = Stop::PatternStationary;
				out.converged = true;
				return current;
			}
			step = std::max(o.minimumStepWorld, step * .5);
		}
		out.stop = Stop::IterationLimit;
		out.converged = false;
		return current;
	}
	Amplitude::Result AssessAt(const State &state,
							   std::shared_ptr<const ChunkSource<Amplitude::Observation>> observations,
							   uint64_t count, SampleValues &sink)
	{
		auto q = Setup(state.positions, &state.amplitude);
		q.observations = observations;
		q.count = count;
		q.options.diagnostics = false;
		Amplitude::Result a;
		++out.work.assessments;
		auto e = Amplitude::Assess(q, read, a, &sink);
		AmplitudeWork(a.work);
		Checked(e);
		return a;
	}
	void InformationAt(const State &state)
	{
		auto &info = out.information;
		info.stepWorld = o.diagnosticStepWorld;
		size_t nx = points.size() * 3, nq = lighting.candidates.size() * 3, n = nx + nq;
		info.parameters = nx;
		info.scope = "Local finite differences of strict surface/grid prediction, RGB amplitudes projected "
					 "out; no priors, calibrated covariance or visibility-boundary gradient";
		if (n > 48 || !o.diagnosticRows)
		{
			info.scope += "; diagnostic parameter budget unavailable";
			return;
		}
		auto subset = std::make_shared<Stored>();
		std::map<uint64_t, Amplitude::Observation> selected;
		for (uint64_t first = 0; first < lighting.count; first += lighting.options.batchRows)
		{
			Cancel();
			Chunk<Amplitude::Observation> rows;
			size_t count = size_t(std::min<uint64_t>(lighting.options.batchRows, lighting.count - first));
			Checked(lighting.observations->Read(first, count, read, rows));
			Need(rows.count == count && rows.data && rows.owner, Code::ProviderError,
				 "Truncated diagnostic stream");
			for (size_t i = 0; i < count; ++i)
			{
				const auto &a = rows.data[i];
				if (!a.eligible)
					continue;
				uint64_t id = a.id;
				uint64_t h = id ^ (o.seed + 0x9e3779b97f4a7c15ull);
				h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ull;
				h = (h ^ (h >> 27)) * 0x94d049bb133111ebull;
				h ^= h >> 31;
				selected.emplace(h, a);
				if (selected.size() > o.diagnosticRows)
					selected.erase(std::prev(selected.end()));
			}
		}
		for (const auto &s : selected)
			subset->data->push_back(s.second);
		std::sort(
			subset->data->begin(), subset->data->end(),
			[](const Amplitude::Observation &a, const Amplitude::Observation &b) { return a.id < b.id; });
		if (subset->data->empty())
			return;
		SampleValues centre;
		auto center = AssessAt(state, subset, subset->data->size(), centre);
		info.rows = centre.rows.size();
		if (!info.rows)
			return;
		size_t m = centre.rows.size() * 3;
		std::vector<double> jac(n * m, 0);
		bool stable = true;
		for (size_t p = 0; p < n; ++p)
		{
			State plus = state, minus = state;
			double span = 0;
			if (p < nx)
			{
				size_t i = p / 3, axis = p % 3;
				V a(state.positions[i]), b = a;
				a[axis] += o.diagnosticStepWorld;
				b[axis] -= o.diagnosticStepWorld;
				if (!Placement(i, state.positions[i], a.Float(), false) ||
					!Placement(i, state.positions[i], b.Float(), false))
				{
					stable = false;
					break;
				}
				plus.positions[i] = a.Float();
				minus.positions[i] = b.Float();
				span = 2 * o.diagnosticStepWorld;
			}
			else
			{
				size_t j = (p - nx) / 3, c = (p - nx) % 3;
				V a(plus.amplitude.sources[j].compilerQ), b = a;
				const auto &bound = lighting.candidates[sourceIndex.at(plus.amplitude.sources[j].source)];
				a[c] = std::min(double(V(bound.upperQ)[c]), a[c] + 1);
				b[c] = std::max(double(V(bound.lowerQ)[c]), b[c] - 1);
				span = a[c] - b[c];
				plus.amplitude.sources[j].compilerQ = a.Float();
				minus.amplitude.sources[j].compilerQ = b.Float();
				if (span <= 0)
					continue;
			}
			SampleValues a, b;
			auto pa = AssessAt(plus, subset, subset->data->size(), a),
				 pb = AssessAt(minus, subset, subset->data->size(), b);
			if (pa.admissionKey != center.admissionKey || pb.admissionKey != center.admissionKey ||
				pa.visibilityKey != center.visibilityKey || pb.visibilityKey != center.visibilityKey ||
				a.values.size() != centre.values.size() || b.values.size() != centre.values.size())
			{
				stable = false;
				break;
			}
			for (size_t i = 0; i < centre.rows.size(); ++i)
				for (size_t c = 0; c < 3; ++c)
					if (centre.rows[i].channels[c].present && !centre.rows[i].channels[c].censored)
						jac[p * m + i * 3 + c] = (a.values[i][c] - b.values[i][c]) / span *
												 std::sqrt(centre.weight[i]) / centre.rows[i].sigma;
			if (p < nx)
			{
				size_t i = p / 3;
				plus.positions[i] = ((V(plus.positions[i]) + V(state.positions[i])) * .5).Float();
				minus.positions[i] = ((V(minus.positions[i]) + V(state.positions[i])) * .5).Float();
				SampleValues halfA, halfB;
				auto ha = AssessAt(plus, subset, subset->data->size(), halfA),
					 hb = AssessAt(minus, subset, subset->data->size(), halfB);
				if (ha.admissionKey != center.admissionKey || hb.admissionKey != center.admissionKey ||
					ha.visibilityKey != center.visibilityKey || hb.visibilityKey != center.visibilityKey ||
					halfA.values.size() != centre.values.size() ||
					halfB.values.size() != centre.values.size())
				{
					stable = false;
					break;
				}
				double difference = 0, magnitude = 0;
				for (size_t j = 0; j < centre.rows.size(); ++j)
					for (size_t c = 0; c < 3; ++c)
						if (centre.rows[j].channels[c].present && !centre.rows[j].channels[c].censored)
						{
							double h = (halfA.values[j][c] - halfB.values[j][c]) / (span * .5) *
									   std::sqrt(centre.weight[j]) / centre.rows[j].sigma;
							double full = jac[p * m + j * 3 + c];
							difference += (full - h) * (full - h);
							magnitude += h * h;
						}
				info.maximumDerivativeRelativeChange =
					std::max(info.maximumDerivativeRelativeChange,
							 std::sqrt(difference) / std::max(1e-7, std::sqrt(magnitude)));
			}
		}
		info.stableVisibility = stable;
		info.stepStable = stable && info.maximumDerivativeRelativeChange <= .2;
		if (!stable)
		{
			info.scope += "; boundary/visibility changes prevent smooth local assessment";
			return;
		}
		std::vector<double> gram(n * n, 0);
		for (size_t i = 0; i < n; ++i)
			for (size_t j = 0; j < n; ++j)
				for (size_t k = 0; k < m; ++k)
					gram[i * n + j] += jac[i * m + k] * jac[j * m + k];
		std::vector<double> nuisance(nq * nq, 0);
		for (size_t i = 0; i < nq; ++i)
			for (size_t j = 0; j < nq; ++j)
				nuisance[i * nq + j] = gram[(nx + i) * n + nx + j];
		auto eigen = Diagonalize(nuisance, nq);
		double maximum = 0;
		for (double v : eigen.values)
			maximum = std::max(maximum, v);
		info.amplitudeParameters = nq;
		for (double v : eigen.values)
		{
			info.amplitudeSingular.push_back(std::sqrt(std::max(0., v)));
			info.amplitudeRank += v > std::max(1e-24, maximum * 1e-12);
		}
		std::sort(info.amplitudeSingular.rbegin(), info.amplitudeSingular.rend());
		std::vector<double> schur(nx * nx, 0);
		for (size_t i = 0; i < nx; ++i)
			for (size_t j = 0; j < nx; ++j)
			{
				double value = gram[i * n + j];
				for (size_t k = 0; k < nq; ++k)
					if (eigen.values[k] > std::max(1e-24, maximum * 1e-12))
					{
						double left = 0, right = 0;
						for (size_t q = 0; q < nq; ++q)
						{
							left += gram[i * n + nx + q] * eigen.vectors[q * nq + k];
							right += gram[j * n + nx + q] * eigen.vectors[q * nq + k];
						}
						value -= left * right / eigen.values[k];
					}
				schur[i * nx + j] = value;
			}
		auto spatial = Diagonalize(schur, nx);
		for (double v : spatial.values)
			info.singular.push_back(std::sqrt(std::max(0., v)));
		std::sort(info.singular.rbegin(), info.singular.rend());
		double cutoff = std::max(1e-7, info.singular.front() * info.relativeRankThreshold);
		for (double s : info.singular)
			info.rank += s > cutoff;
		info.available = true;
		info.conditionKnown = info.rank == nx;
		if (info.conditionKnown)
			info.condition = info.singular.front() / info.singular.back();
	}
	void Ranges(const State &best)
	{
		for (size_t i = 0; i < points.size(); ++i)
			for (size_t axis = 0; axis < 3; ++axis)
			{
				auto &range = out.points[i].sampledRange[axis];
				range.minimum = range.maximum = V(best.positions[i])[axis];
				for (double scale : {1., 2., 4.})
					for (int sign : {-1, 1})
					{
						if (diagnosticFits >= o.maxDiagnosticFits)
							return;
						++diagnosticFits;
						V p(best.positions[i]);
						p[axis] += sign * o.diagnosticStepWorld * scale;
						if (!In(points[i].bounds, p) || std::abs(p[axis] - V(original[i])[axis]) > radius[i])
						{
							range.limitedBySearch = true;
							continue;
						}
						if (!Placement(i, best.positions[i], p.Float()))
						{
							range.limitedByGeometry = true;
							continue;
						}
						auto all = best.positions;
						all[i] = p.Float();
						State alternative;
						if (!FitAt(all, &best.amplitude, alternative, false))
							continue;
						range.sampled = true;
						if (alternative.Total() <= best.Total() + o.feasibleObjectiveSlack)
						{
							range.minimum = std::min(range.minimum, p[axis]);
							range.maximum = std::max(range.maximum, p[axis]);
						}
					}
			}
	}
	Result Run()
	{
		for (size_t i = 0; i < points.size(); ++i)
			Need(Placement(i, original[i], original[i]), Code::Unavailable,
				 "Initial point has no certified admissible placement");
		State initial;
		Need(FitAt(original, nullptr, initial), Code::Unavailable,
			 "Initial comparable lighting/visibility unavailable");
		out.initialObjective = initial.Total();
		out.trace.back().accepted = true;
		State best = initial.amplitude.coverage.positive ? Search(initial) : initial;
		Stop bestStop = out.stop;
		bool converged = out.converged;
		for (size_t start = 1; initial.amplitude.coverage.positive && start < o.multiStarts; ++start)
		{
			auto p = original;
			bool allowed = true;
			for (size_t i = 0; i < points.size(); ++i)
			{
				V proposal(original[i]);
				for (size_t k = 0; k < 3; ++k)
					proposal[k] +=
						(2 * Halton(start + o.seed % 1024 + 1, unsigned(k == 0 ? 2 : k == 1 ? 3 : 5)) - 1) *
						points[i].initialRadiusWorld * .5;
				p[i] = Clip(i, proposal);
				allowed &= Placement(i, original[i], p[i]);
			}
			if (!allowed)
				continue;
			State seed;
			if (!FitAt(p, nullptr, seed))
				continue;
			State alternative = Search(seed);
			if (Better(alternative, best))
			{
				best = alternative;
				bestStop = out.stop;
				converged = out.converged;
			}
		}
		out.stop = bestStop;
		out.converged = converged;
		for (size_t i = 0; i < points.size(); ++i)
		{
			PointResult point;
			point.source = points[i].source;
			point.initialWorld = original[i];
			point.world = best.positions[i];
			point.boundaryConstrained = boundary[i];
			PlacementResult location;
			Checked(r.placement->Check(point.world, point.world,
									   points[i].sourceSupportRadiusWorld + points[i].clearanceWorld, read,
									   location));
			++out.work.placementChecks;
			Need(Add(out.work.rays, location.rays, o.maxRays) &&
					 Add(out.work.traversal, location.work, o.maxWork),
				 Code::WorkLimit, "Final placement cumulative budget");
			Need(location.state == Constraint::Free, Code::InvalidState, "Final source certificate changed");
			point.regionKnown = location.regionKnown;
			point.region = location.region;
			point.uncertainty = "Conditional on supplied profile/material/geometry; no calibrated "
								"source-existence or XYZ confidence";
			out.points.push_back(point);
		}
		if (o.diagnostics)
		{
			try
			{
				InformationAt(best);
			}
			catch (const Error &e)
			{
				if (e.code != Code::Unavailable)
					throw;
				out.information.available = false;
				out.information.scope += "; perturbation prediction unavailable: " + e.message;
			}
			Ranges(best);
		}
		out.objective = best.Total();
		out.positionPrior = best.prior;
		out.directionalObjective = best.directional;
		out.shadowObjective = best.shadow;
		out.shadowWeight = r.shadowWeight;
		out.shadowDiagnostics = best.shadowValue;
		auto final = Setup(best.positions, &best.amplitude);
		final.cache->Clear();
		Amplitude::Result assessment;
		++out.work.assessments;
		Checked(Amplitude::Assess(final, read, assessment));
		AmplitudeWork(assessment.work);
		Need(assessment.admissionKey == admission && assessment.strictReevaluated, Code::InvalidState,
			 "Final strict replay coverage changed");
		Need(std::abs(assessment.objective.Total() - best.amplitude.objective.Total()) <=
				 r.lighting.options.strictTolerance * std::max(1., best.amplitude.objective.Total()),
			 Code::AlgorithmError, "Frozen RGB/XYZ strict replay disagrees");
		out.amplitude = best.amplitude;
		out.amplitude.objective = assessment.objective;
		out.amplitude.groups = assessment.groups;
		out.amplitude.maximumIntervalError = assessment.maximumIntervalError;
		out.objective = assessment.objective.Total() + best.prior + best.directional + best.shadow;
		if (r.shadowWeight > 0)
		{
			std::vector<FitEvidence::Source> sources;
			for (const auto &c : final.candidates)
			{
				FitEvidence::Source s;
				s.geometry.source.id = c.source.id;
				s.geometry.kind = c.source.kind == Bake::SourceKind::Point
									  ? EmitterKind::Point
									  : c.source.kind == Bake::SourceKind::Spot
											? EmitterKind::Spot
											: c.source.kind == Bake::SourceKind::Polygon
												  ? EmitterKind::Rectangle
												  : c.source.kind == Bake::SourceKind::Tube
														? EmitterKind::Tube
														: EmitterKind::Extension;
				s.geometry.position = c.source.position;
				s.geometry.frame = c.source.orientation;
				s.geometry.geometry = c.source.geometry;
				s.geometry.angular = c.source.angular;
				s.legacySpotDistance = c.source.spotTargetDistanceWorld;
				s.legacySpotRadius = c.source.spotRadiusWorld;
				for (const auto &q : best.amplitude.sources)
					if (q.source == c.source.id)
						s.rgb = q.compilerQ;
				sources.push_back(s);
			}
			Checked(r.shadow->EvaluateReference(sources, read, out.shadowReference));
			Need(Add(out.work.rays, out.shadowReference.rays, o.maxRays) &&
					 Add(out.work.traversal, out.shadowReference.work, o.maxWork),
				 Code::WorkLimit, "Frozen reference shadow budget");
			Need(out.shadowReference.available, Code::Unavailable, "Frozen reference shadow unavailable");
		}
		out.strictReevaluated = true;
		out.admissionKey = admission;
		key.Digest(out.amplitude.key);
		for (const auto &p : out.points)
			key.V(p.world);
		out.key = key.Finish();
		for (auto &p : out.points)
			p.locallyIdentifiable = out.information.available && out.information.stepStable &&
									out.information.rank == out.information.parameters &&
									out.amplitude.coverage.positive > 0 && !out.amplitude.coverage.censored;
		if (out.amplitude.coverage.positive == 0)
		{
			out.stop = Stop::Uninformative;
			out.converged = false;
		}
		out.diagnostics.push_back("Point XYZ diagnostic refinement only; IDs/count/types preserved. No "
								  "recovered runtime lights or absolute photometric calibration.");
		Cancel();
		out.refined = true;
		return out;
	}
};
} // namespace
Error MakePlacement(SceneInput input, const ReadContext &read, std::shared_ptr<const PlacementProvider> &out)
{
	out.reset();
	if (read.cancellation && read.cancellation->IsCancelled())
		return Failure(Code::Cancelled, "Placement setup cancelled");
	if (!input.domain || !input.geometry || Zero(input.map.sha256) || Zero(input.domainKey) ||
		input.map.sha256 != input.geometry->GeometryIdentity().sha256 || !input.maxWorkPerCheck ||
		!input.maxSegments || input.maxSegments > 65536 || !std::isfinite(input.minimumSegmentWorld) ||
		input.minimumSegmentWorld <= 0)
		return Failure(Code::InvalidInput, "Missing/incompatible placement sources or query bounds");
	if (input.domain->SourceIdentity() && input.domain->SourceIdentity()->sha256 != input.map.sha256)
		return Failure(Code::InvalidInput, "Placement BSP map mismatch");
	if (input.regions && (input.regions->Identity().sha256 != input.map.sha256 ||
						  input.regions->VisibilityKey() != input.geometry->CertificationKey()))
		return Failure(Code::InvalidInput, "Stale placement regions");
	if (input.dynamic.size() > 4096)
		return Failure(Code::MemoryLimit, "Dynamic-bound limit");
	try
	{
		std::set<Id> dynamicIds;
		for (const auto &d : input.dynamic)
			if (!d.id || !dynamicIds.insert(d.id).second || (d.bounds.known && !Valid(d.bounds)))
				return Failure(Code::InvalidInput, "Invalid/duplicate moving-bound identity or bounds");
		out = std::make_shared<Scene>(std::move(input));
		return {};
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Placement metadata allocation");
	}
}
Error Refine(const Request &r, const ReadContext &read, Result &out)
{
	out = {};
	// Reject oversized policies before Engine copies metadata or balances/reads rows.
	if (r.points.size() > 16 || r.directions.size() > 1024 || r.lighting.candidates.size() > 128 ||
		!r.lighting.options.batchRows || r.lighting.options.batchRows > 4096)
		return Failure(Code::InvalidInput, "Point refinement metadata/batch hard limit");
	std::unique_ptr<Engine> engine;
	try
	{
		engine.reset(new Engine(r, read));
		out = engine->Run();
		return {};
	}
	catch (const Error &e)
	{
		if (engine)
		{
			out.work = engine->out.work;
			out.trace = engine->out.trace;
			out.diagnostics = engine->out.diagnostics;
		}
		out.stop = e.code == Code::Cancelled
					   ? Stop::Cancelled
					   : e.code == Code::WorkLimit
							 ? Stop::WorkLimit
							 : e.code == Code::AlgorithmError ? Stop::NumericalFailure : Stop::Unavailable;
		out.diagnostics.push_back(e.message);
		return e;
	}
	catch (const std::bad_alloc &)
	{
		out = {};
		return Failure(Code::MemoryLimit, "Point refinement allocation");
	}
}
const char *StopName(Stop s)
{
	const char *n[] = {"NotStarted",  "PatternStationary", "IterationLimit", "Uninformative",
					   "Unavailable", "Cancelled",		   "WorkLimit",		 "NumericalFailure"};
	return size_t(s) < 8 ? n[size_t(s)] : "Invalid";
}
const char *ConstraintName(Constraint c)
{
	const char *n[] = {"Free",		 "Solid",		"Outside", "UnknownGeometry", "Clearance",
					   "OpaquePath", "UnknownPath", "Dynamic", "Mount",			  "SearchBounds"};
	return size_t(c) < size_t(Constraint::Count) ? n[size_t(c)] : "Invalid";
}
} // namespace PointRefine
} // namespace Core
} // namespace Rend2LightRecon
