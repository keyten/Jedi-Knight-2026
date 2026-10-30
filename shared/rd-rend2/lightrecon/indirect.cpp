// Sparse passive first-bounce transport, bounded material fit, post-fit unresolved field.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "indirect.h"
#include "bake_math.h"
#include "geometry_key.h"
#include <algorithm>
#include <functional>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
namespace Rend2LightRecon
{
namespace Core
{
namespace Indirect
{
using namespace Bake::Detail;
namespace
{
void Need(bool b, Code c, const std::string &s)
{
	if (!b)
		throw Failure(c, s);
}
void Check(Error e)
{
	if (!e)
		throw e;
}
bool Zero(Disk::Digest d) { return d == Disk::Digest{}; }
void Cancel(const ReadContext &r) { Need(!Cancelled(r), Code::Cancelled, "Indirect stage cancelled"); }
void Text(GeometryKey &k, const std::string &s)
{
	k.U(s.size());
	for (unsigned char c : s)
		k.U(c);
}
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> v)
{
	return VectorChunk<T>(v, 0, v->size());
}
double Component(Vec3 v, int c) { return V(v)[c]; }
void Set(Vec3 &v, int c, double x)
{
	Need(std::isfinite(x) && std::abs(x) <= std::numeric_limits<float>::max(), Code::AlgorithmError,
		 "Transport overflow");
	if (c == 0)
		v.x = float(x);
	else if (c == 1)
		v.y = float(x);
	else
		v.z = float(x);
}
struct Node
{
	V p;
	double area = 0;
};
void Nodes(V a, V b, V c, unsigned depth, std::vector<Node> &out)
{
	if (depth)
	{
		V ab = (a + b) * .5, bc = (b + c) * .5, ca = (c + a) * .5;
		Nodes(a, ab, ca, depth - 1, out);
		Nodes(ab, b, bc, depth - 1, out);
		Nodes(ca, bc, c, depth - 1, out);
		Nodes(ab, bc, ca, depth - 1, out);
	}
	else
	{
		Node n;
		n.p = (a + b + c) / 3;
		n.area = Length(Cross(b - a, c - a)) * .5;
		out.push_back(n);
	}
}
std::vector<Node> Nodes(const Patch &p, unsigned depth)
{
	std::vector<Node> r;
	for (size_t t = 0; t < p.receiver.footprint.count; ++t)
	{
		const auto &f = p.receiver.footprint.data[t];
		Nodes(V(f.world[0]), V(f.world[1]), V(f.world[2]), depth, r);
	}
	return r;
}
bool SegmentBox(const Bounds &b, V a, V z)
{
	if (!b.known)
		return true;
	V d = z - a;
	double lo = 0, hi = 1;
	for (int k = 0; k < 3; ++k)
	{
		if (std::abs(d[k]) < 1e-15)
		{
			if (a[k] < V(b.minWorld)[k] || a[k] > V(b.maxWorld)[k])
				return false;
		}
		else
		{
			double x = (V(b.minWorld)[k] - a[k]) / d[k], y = (V(b.maxWorld)[k] - a[k]) / d[k];
			if (x > y)
				std::swap(x, y);
			lo = std::max(lo, x);
			hi = std::min(hi, y);
			if (lo > hi)
				return false;
		}
	}
	return true;
}
double IntervalResidual(double x, const Amplitude::Interval &i)
{
	if (!i.present)
		return 0;
	return x < i.lower ? x - i.lower : x > i.upper ? x - i.upper : 0;
}
double Huber(double r, double h)
{
	r = std::abs(r);
	return r <= h ? .5 * r * r : h * (r - .5 * h);
}
} // namespace
struct Transfer::Data
{
	BuildInput input;
	BuildOptions options;
	std::vector<Link> links;
	BuildReport report;
	Disk::Digest key{};
};
const std::vector<Patch> &Transfer::Patches() const { return data->input.patches; }
const std::vector<Link> &Transfer::Links() const { return data->links; }
const BuildReport &Transfer::Report() const { return data->report; }
Disk::Digest Transfer::Key() const { return data->key; }
Disk::Digest Transfer::MapKey() const { return data->input.mapKey; }
Disk::Digest Transfer::GeometryKey() const { return data->input.geometry->CertificationKey(); }
const std::shared_ptr<const StaticVisibility> &Transfer::Geometry() const { return data->input.geometry; }
Error Transfer::QueryPath(const RayQuery &q, const ReadContext &r, RayResult &out) const
{
	auto e = data->input.geometry->Query(q, r, out);
	if (!e)
		return e;
	if (out.state == VisibilityState::Blocked && out.reliability == RayReliability::Geometric)
		return {};
	for (const auto &m : data->input.moving)
		if (!m.sweptExtentKnown || SegmentBox(m.bounds, V(q.startWorld), V(q.endWorld)))
		{
			out.state = VisibilityState::Unknown;
			out.reliability = RayReliability::Unknown;
			out.issues |= PolicyAssumption;
		}
	return {};
}
Error Transfer::Build(BuildInput input, const BuildOptions &o, const ReadContext &read,
					  std::shared_ptr<const Transfer> &out)
{
	out.reset();
	try
	{
		Cancel(read);
		Need(input.geometry && !Zero(input.mapKey) && !Zero(input.patchInterpretationKey) &&
				 input.geometry->GeometryIdentity().sha256 == input.mapKey,
			 Code::MissingGeometry, "Transport map/geometry interpretation mismatch");
		Need(!input.patches.empty() && input.patches.size() <= o.maxPatches && o.maxPatches <= 50000 &&
				 o.maxPairs && o.maxPairs <= 1600000 && o.subdivisions <= 2,
			 Code::MemoryLimit, "Patch/link/quadrature limits");
		Need(std::isfinite(o.maximumLinkDistanceWorld) && o.maximumLinkDistanceWorld > 0 &&
				 std::isfinite(o.receiverBiasWorld) && o.receiverBiasWorld >= 0 && o.maxRays && o.maxWork,
			 Code::InvalidInput, "Transport build policy");
		uint64_t cap = read.maxBytes ? std::min(read.maxBytes, o.maxBytes) : o.maxBytes;
		Need(input.patches.size() * (sizeof(Patch) + 2048) + 65536 <= cap, Code::MemoryLimit,
			 "Patch build preflight");
		Need(input.moving.size() <= 4096, Code::MemoryLimit, "Moving geometry bounds limit");
		auto d = std::make_shared<Data>();
		d->options = o;
		std::sort(input.patches.begin(), input.patches.end(),
				  [](const Patch &a, const Patch &b) { return a.id < b.id; });
		Core::GeometryKey key;
		key.U(kVersion);
		key.Digest(input.mapKey);
		key.Digest(input.patchInterpretationKey);
		key.Digest(input.geometry->CertificationKey());
		key.F(o.maximumLinkDistanceWorld);
		key.F(o.receiverBiasWorld);
		key.U(o.subdivisions);
		key.U(input.completePatchSet);
		for (size_t i = 0; i < input.patches.size(); ++i)
		{
			Cancel(read);
			auto &p = input.patches[i];
			const auto &r = p.receiver;
			Need(p.id && (!i || p.id != input.patches[i - 1].id) && p.material && r.surface &&
					 r.surface <= input.geometry->Surfaces().size() && Finite(r.world) &&
					 Normal(r.geometricNormal) && Normal(r.shadingNormal) && !r.twoSided,
				 Code::InvalidInput, "Patch identity, side and normal");
			Need(r.footprint.owner && r.footprint.data && r.footprint.count && r.footprint.count <= 64 &&
					 std::isfinite(p.areaWorldSquared) && p.areaWorldSquared > 0 &&
					 std::isfinite(r.geometryErrorWorld) && r.geometryErrorWorld >= 0,
				 Code::InvalidInput, "Actual mapped footprint required");
			double area = 0;
			for (size_t t = 0; t < r.footprint.count; ++t)
			{
				const auto &f = r.footprint.data[t];
				for (int k = 0; k < 3; ++k)
				{
					Need(Finite(f.world[k]) && Normal(f.normal[k]) &&
							 Length(V(f.normal[k]) - V(r.shadingNormal)) < 1e-4,
						 Code::Unavailable,
						 "Varying curved normals require a different transport quadrature");
					key.V(f.world[k]);
					key.V(f.normal[k]);
				}
				double a = Length(Cross(V(f.world[1]) - V(f.world[0]), V(f.world[2]) - V(f.world[0]))) * .5;
				Need(a > 1e-14, Code::InvalidInput, "Degenerate patch footprint");
				area += a;
			}
			Need(std::abs(area - p.areaWorldSquared) <= 1e-5 * std::max(1., area), Code::InvalidInput,
				 "Patch area differs from original footprint");
			key.U(p.id);
			key.U(p.material);
			key.U(p.region);
			key.U(r.surface);
			key.U(r.style);
			key.V(r.world);
			key.V(r.geometricNormal);
			key.V(r.shadingNormal);
			key.F(r.geometryErrorWorld);
			key.F(area);
		}
		std::sort(input.moving.begin(), input.moving.end(),
				  [](const DynamicBoundary &a, const DynamicBoundary &b) { return a.id < b.id; });
		for (const auto &m : input.moving)
		{
			Need(m.id, Code::InvalidInput, "Moving bound ID");
			if (m.sweptExtentKnown)
			{
				Need(m.bounds.known && Finite(m.bounds.minWorld) && Finite(m.bounds.maxWorld),
					 Code::InvalidInput, "Invalid moving bound");
				for (int k = 0; k < 3; ++k)
					Need(V(m.bounds.minWorld)[k] <= V(m.bounds.maxWorld)[k], Code::InvalidInput,
						 "Reversed moving bound");
			}
			key.U(m.id);
			key.B(m.bounds);
			key.U(m.sweptExtentKnown);
		}
		if (input.regions)
		{
			Need(input.regions->Identity().sha256 == input.mapKey &&
					 input.regions->VisibilityKey() == input.geometry->CertificationKey(),
				 Code::MissingGeometry, "Stale optional region graph");
			key.Digest(input.regions->CertificationKey());
		}
		d->input = std::move(input);
		const auto &patches = d->input.patches;
		auto &r = d->report;
		using Cell = std::tuple<int64_t, int64_t, int64_t>;
		std::map<Cell, std::vector<size_t>> grid;
		auto cell = [&](V p) {
			double v[3] = {std::floor(p.x / o.maximumLinkDistanceWorld),
						   std::floor(p.y / o.maximumLinkDistanceWorld),
						   std::floor(p.z / o.maximumLinkDistanceWorld)};
			for (double x : v)
				Need(std::abs(x) < 1e12, Code::InvalidInput, "Transport spatial index range");
			return Cell(int64_t(v[0]), int64_t(v[1]), int64_t(v[2]));
		};
		for (size_t i = 0; i < patches.size(); ++i)
			grid[cell(V(patches[i].receiver.world))].push_back(i);
		auto step = [&]() {
			Cancel(read);
			Need(++r.work <= o.maxWork && r.rays <= o.maxRays, Code::WorkLimit,
				 "Transfer build work/ray budget");
		};
		auto integrate = [&](size_t a, size_t b, unsigned depth, double &possible, uint32_t &issues) {
			double sum = 0;
			possible = 0;
			const auto &A = patches[a];
			const auto &B = patches[b];
			auto an = Nodes(A, depth), bn = Nodes(B, depth);
			for (const auto &x : an)
				for (const auto &y : bn)
				{
					step();
					V delta = y.p - x.p;
					double distance = Length(delta);
					if (distance < 1e-8)
					{
						throw Failure(Code::Unavailable, "Coincident/overlapping transport nodes; split or "
														 "deduplicate original receiving patches");
					}
					V u = delta / distance;
					double ca = std::max(0., Dot(V(A.receiver.geometricNormal), u)),
						   cb = std::max(0., -Dot(V(B.receiver.geometricNormal), u));
					double g = x.area * y.area * ca * cb / (Pi * distance * distance);
					if (g <= 0)
						continue;
					RayQuery q;
					q.startWorld = x.p.Float();
					q.endWorld = y.p.Float();
					q.originSurface = A.receiver.surface;
					q.targetSurface = B.receiver.surface;
					q.targetToleranceWorld = std::min(distance * .01, std::max(1e-7, o.receiverBiasWorld));
					q.bias.absoluteWorld = q.bias.maximumWorld = o.receiverBiasWorld;
					q.bias.relative = 0;
					q.bias.normalKnown = true;
					q.bias.geometricNormal = A.receiver.geometricNormal;
					q.purpose = RayPurpose::IrradianceProbe;
					auto trace = [&](RayQuery query, RayResult &hit) {
						Need(r.rays < o.maxRays && r.work < o.maxWork, Code::WorkLimit,
							 "Transfer work/ray budget");
						query.maxWork = o.maxWork - r.work;
						Check(d->input.geometry->Query(query, read, hit));
						++r.rays;
						r.work += hit.nodeTests + hit.primitiveTests;
						Need(r.work <= o.maxWork, Code::WorkLimit, "Transfer work budget");
					};
					RayResult forward, reverse;
					trace(q, forward);
					q.startWorld = y.p.Float();
					q.endWorld = x.p.Float();
					q.originSurface = B.receiver.surface;
					q.targetSurface = A.receiver.surface;
					q.bias.geometricNormal = B.receiver.geometricNormal;
					trace(q, reverse);
					auto is = [](const RayResult &hit, VisibilityState state) {
						return hit.state == state && hit.reliability == RayReliability::Geometric;
					};
					// A reciprocal physical link needs agreement in both directions. Renderer
					// one-sided culling is not proof of a transparent transport path.
					if (is(forward, VisibilityState::Blocked) && is(reverse, VisibilityState::Blocked))
					{
						r.blockedRays += 2;
						continue;
					}
					bool unknown =
						!is(forward, VisibilityState::Visible) || !is(reverse, VisibilityState::Visible);
					for (const auto &m : d->input.moving)
						if (!m.sweptExtentKnown || SegmentBox(m.bounds, x.p, y.p))
						{
							unknown = true;
							issues |= MovingPath;
						}
					if (A.receiver.geometryErrorWorld > 0 || B.receiver.geometryErrorWorld > 0)
					{
						unknown = true;
						issues |= ApproximateGeometry;
					}
					possible += g;
					if (unknown)
					{
						r.unknownRays += 2;
						issues |= UnknownOptics;
					}
					else
						sum += g;
				}
			return sum;
		};
		uint64_t visited = 0;
		std::vector<double> outgoing(patches.size());
		for (size_t i = 0; i < patches.size(); ++i)
		{
			step();
			auto c = cell(V(patches[i].receiver.world));
			for (int x = -1; x <= 1; ++x)
				for (int y = -1; y <= 1; ++y)
					for (int z = -1; z <= 1; ++z)
					{
						auto g = grid.find(Cell(std::get<0>(c) + x, std::get<1>(c) + y, std::get<2>(c) + z));
						if (g == grid.end())
							continue;
						for (size_t j : g->second)
						{
							step();
							if (j <= i)
								continue;
							if (Length(V(patches[i].receiver.world) - V(patches[j].receiver.world)) >
								o.maximumLinkDistanceWorld)
								continue;
							++visited;
							Need(++r.candidatePairs <= o.maxPairs, Code::WorkLimit,
								 "Sparse pair candidate budget; no silent link truncation");
							if (patches[i].receiver.style != patches[j].receiver.style)
								continue;
							double cp = 0, fp = 0;
							uint32_t flags = QuadratureApproximation;
							double coarse = integrate(i, j, o.subdivisions, cp, flags),
								   fine = integrate(i, j, o.subdivisions + 1, fp, flags);
							double difference = std::abs(fine - coarse);
							r.maximumRuleDifference = std::max(r.maximumRuleDifference, difference);
							if (fp == 0 && fine == 0)
								continue;
							Link l;
							l.a = uint32_t(i);
							l.b = uint32_t(j);
							l.conductance = fine;
							l.possibleConductance = fp;
							l.quadratureDifference = difference;
							l.issues = flags;
							Need(fine <= std::min(patches[i].areaWorldSquared, patches[j].areaWorldSquared) +
											 1e-9,
								 Code::Unavailable,
								 "Near-field quadrature violates pair energy; subdivide supplied patches");
							outgoing[i] += fine / patches[i].areaWorldSquared;
							outgoing[j] += fine / patches[j].areaWorldSquared;
							Need(std::max(outgoing[i], outgoing[j]) <= 1 + 1e-7, Code::Unavailable,
								 "Outgoing form-factor budget exceeds one; no renormalization");
							Need((d->links.size() + 1) * sizeof(Link) +
										 patches.size() * (sizeof(Patch) + 2048) + 65536 <=
									 cap,
								 Code::MemoryLimit, "Sparse transfer allocation budget");
							d->links.push_back(l);
						}
					}
		}
		std::sort(d->links.begin(), d->links.end(),
				  [](const Link &a, const Link &b) { return std::tie(a.a, a.b) < std::tie(b.a, b.b); });
		r.completePatchSet = d->input.completePatchSet;
		r.allPairsRepresented = visited == uint64_t(patches.size()) * (patches.size() - 1) / 2;
		r.maximumOutgoingFraction = *std::max_element(outgoing.begin(), outgoing.end());
		r.bytes = d->links.capacity() * sizeof(Link) + patches.size() * (sizeof(Patch) + 2048) + 65536;
		d->key = key.Finish();
		auto result = std::make_shared<Transfer>();
		result->data = d;
		out = result;
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Transfer allocation");
	}
}

Error PredictRoot(const Transfer &t, TransportRootKind kind, const Bake::ProfileHypothesis &p,
				  const Chunk<Bake::SourceHypothesis> &sources, const Bake::EvaluationContext &geometry,
				  const Bake::Options &o, const ReadContext &read, Root &out, Bake::Work &work)
{
	out = {};
	work = {};
	try
	{
		Need(sources.count && sources.owner && sources.data, Code::InvalidInput,
			 "Explicit fixed root required");
		Need(geometry.geometryKey == t.GeometryKey(), Code::MissingGeometry, "Stale primary geometry");
		Need(o.output == Bake::Domain::CompilerLinearRelative255 && o.mode == Bake::Mode::CompilerFaithful,
			 Code::Unavailable,
			 "PredictRoot supports pre-encoding historical surface values only; physical caller supplies "
			 "explicit incident values");
		Need(kind == TransportRootKind::LocalEmitter || kind == TransportRootKind::Sun ||
				 kind == TransportRootKind::Sky,
			 Code::Unavailable, "Root kind needs a supported operator");
		Need(sources.count == 1, Code::InvalidInput,
			 "Preserve one canonical root per prediction; expanded nodes require an explicit aggregation "
			 "adapter");
		const auto &s = sources.data[0];
		Need(s.id && ((kind == TransportRootKind::Sun && s.kind == Bake::SourceKind::Directional) ||
					  (kind == TransportRootKind::Sky && s.kind == Bake::SourceKind::Sky) ||
					  (kind == TransportRootKind::LocalEmitter && s.kind != Bake::SourceKind::Sky &&
					   s.kind != Bake::SourceKind::Directional)),
			 Code::InvalidInput, "Root/source kind mismatch");
		Bake::Recipe recipe;
		Check(Bake::DescribeRecipe(p, recipe));
		Need(Peak(recipe.explicitSurfaceAmbient) == 0, Code::InvalidInput,
			 "Ambient must be a separate fixed root, not duplicated per source");
		Root r;
		r.source = s.id;
		r.kind = kind;
		r.domain = Root::Domain::CompilerRelative255;
		r.sunCoverage =
			kind == TransportRootKind::Sky ? SunCoverage::ExcludesExplicitSun : SunCoverage::Unknown;
		r.provenance =
			"Production CpuForwardEvaluator, fixed source, historical pre-encoding relative surface values";
		Core::GeometryKey key;
		key.Digest(t.Key());
		key.Digest(p.contentKey);
		key.U(s.id);
		key.V(s.compilerRgbAmplitude);
		key.V(s.position.world);
		key.U(unsigned(kind));
		Bake::CpuForwardEvaluator evaluator;
		for (size_t first = 0; first < t.Patches().size(); first += 64)
		{
			Cancel(read);
			auto receivers = std::make_shared<std::vector<Bake::Receiver>>();
			for (size_t i = first; i < std::min(first + 64, t.Patches().size()); ++i)
				receivers->push_back(t.Patches()[i].receiver);
			Bake::Result batch;
			Need(work.pairs < o.maxPairs && work.rays < o.maxRays &&
					 work.nodeTests + work.triangleTests < o.maxWork,
				 Code::WorkLimit, "Fixed primary cumulative evaluation budget");
			auto remaining = o;
			remaining.maxPairs -= work.pairs;
			remaining.maxRays -= work.rays;
			remaining.maxWork -= work.nodeTests + work.triangleTests;
			Check(evaluator.Evaluate(p, sources, Lease(receivers), {}, geometry, remaining, read, batch));
			for (size_t i = 0; i < batch.predictions.count; ++i)
			{
				const auto &v = batch.predictions.data[i];
				r.incident.push_back(v.available ? v.rgb : Vec3());
				r.available.push_back(v.available);
				key.V(v.rgb);
				key.U(v.available);
			}
			work.pairs += batch.work.pairs;
			work.rays += batch.work.rays;
			work.nodeTests += batch.work.nodeTests;
			work.triangleTests += batch.work.triangleTests;
		}
		r.generation = key.Finish();
		out = std::move(r);
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Primary root allocation");
	}
}

namespace
{
struct Engine
{
	const Request &r;
	const ReadContext &read;
	Result out;
	std::map<Id, size_t> patchIndex, materialIndex;
	std::vector<Observation> obs;
	std::vector<Vec3> rho, lowRho, highRho;
	std::vector<PatchValue> values;
	double denominator = 0;
	uint64_t work = 0;
	std::vector<size_t> fieldCell;
	std::vector<Vec3> field;
	void Step(uint64_t n = 1)
	{
		Cancel(read);
		work += n;
		Need(work <= r.options.maxWork, Code::WorkLimit, "Indirect fitting work budget");
	}
	Engine(const Request &request, const ReadContext &ctx) : r(request), read(ctx)
	{
		Need(r.transfer && r.training && r.count && r.count <= r.options.maxObservations &&
				 r.count <= 1000000 && !Zero(r.trainingKey),
			 Code::InvalidInput, "Pinned training-only stream and transfer required");
		Need(r.roots.size() <= 64 && r.materials.size() <= 256 && r.options.maxIterations &&
				 r.options.maxIterations <= 1000 && r.options.maxEvaluations && r.options.huber > 0 &&
				 std::isfinite(r.options.huber),
			 Code::InvalidInput, "Indirect parameter/work limits");
		const auto &c = r.contract;
		Need(c.operation <= ObservationOperator::Unknown && std::isfinite(c.units.worldUnitsPerMeter) &&
				 c.units.worldUnitsPerMeter > 0,
			 Code::InvalidInput, "Observation contract");
		Need(c.unit == PhotometricUnit::Relative || c.unit == PhotometricUnit::Lux, Code::InvalidInput,
			 "Incident transport units must be Relative or Lux");
		Need(c.unit != PhotometricUnit::Lux ||
				 (c.calibration.id && ((c.incidentDomain == Root::Domain::PhysicalLux &&
										c.calibration.kind == CalibrationKind::PhysicallyAuthored) ||
									   (c.incidentDomain == Root::Domain::SceneEquivalentLux &&
										c.calibration.kind == CalibrationKind::LegacySceneEquivalent &&
										!c.calibration.method.empty()))),
			 Code::InvalidInput, "Lux requires authored photometry or explicit scene-equivalent calibration");
		Need(!c.provenance.empty(), Code::InvalidInput, "Transport hypothesis provenance required");
		Need((c.unit == PhotometricUnit::Lux) == (c.incidentDomain == Root::Domain::PhysicalLux ||
												  c.incidentDomain == Root::Domain::SceneEquivalentLux),
			 Code::InvalidInput, "Incident domain and physical units conflict");
		Need(c.operation != ObservationOperator::NetRadiantEncoded ||
				 c.incidentDomain == Root::Domain::CompilerRelative255,
			 Code::InvalidInput, "Compiler encoding requires compiler-relative255 incident contributions");
		for (double x :
			 {r.options.objectiveTolerance, r.options.parameterTolerance, r.options.plausibilitySlack})
			Need(std::isfinite(x) && x >= 0, Code::InvalidInput,
				 "Invalid indirect stopping/alternative tolerance");
		Need(r.options.parameterTolerance > 0, Code::InvalidInput, "Positive parameter tolerance required");
		if (c.operation == ObservationOperator::NetRadiantEncoded)
		{
			Bake::Recipe recipe;
			Check(Bake::DescribeRecipe(c.profile, recipe));
			Need(Peak(recipe.explicitSurfaceAmbient) == 0, Code::InvalidInput,
				 "Ambient must remain a separate component");
		}
		auto cap = read.maxBytes ? std::min(read.maxBytes, r.options.maxBytes) : r.options.maxBytes;
		Need(r.transfer->Patches().size() *
						 (sizeof(PatchValue) * 4 + r.roots.size() * sizeof(Vec3) * 3 + 128) +
					 r.count * sizeof(Observation) * 2 + 1048576 <=
				 cap,
			 Code::MemoryLimit, "Indirect bounded fitting memory");
		out.work.accountedBytes = r.transfer->Patches().size() *
									  (sizeof(PatchValue) * 4 + r.roots.size() * sizeof(Vec3) * 3 + 128) +
								  r.count * sizeof(Observation) * 2 + 1048576;
		out.contract = c;
		out.trainingKey = r.trainingKey;
		out.transferKey = r.transfer->Key();
		out.materials = r.materials;
		std::sort(out.materials.begin(), out.materials.end(),
				  [](const Reflectance &a, const Reflectance &b) { return a.material < b.material; });
		for (size_t i = 0; i < out.materials.size(); ++i)
		{
			const auto &m = out.materials[i];
			Need(m.material && materialIndex.emplace(m.material, i).second &&
					 m.knowledge <= MaterialKnowledge::Unknown && Positive(m.lower) && Positive(m.upper) &&
					 Positive(m.initial) && Peak(m.upper) <= 1 && std::isfinite(m.priorWeight) &&
					 m.priorWeight >= 0,
				 Code::InvalidInput, "Reflectance bounds/identity");
			for (int k = 0; k < 3; ++k)
				Need(Component(m.lower, k) <= Component(m.initial, k) &&
						 Component(m.initial, k) <= Component(m.upper, k),
					 Code::InvalidInput, "Reflectance initial outside bounds");
			Need(m.knowledge == MaterialKnowledge::Unknown || !m.provenance.empty(), Code::InvalidInput,
				 "Material provenance required");
			if (m.knowledge == MaterialKnowledge::Known)
				Need(Length(V(m.lower) - V(m.upper)) == 0 && Length(V(m.initial) - V(m.lower)) == 0,
					 Code::InvalidInput, "Known reflectance must have identical bounds");
			if (m.knowledge == MaterialKnowledge::Unknown)
				Need(Peak(m.initial) == 0 && Peak(m.lower) == 0, Code::InvalidInput,
					 "Unknown albedo has no asserted nominal reflectance; only an upper hypothesis");
			rho.push_back(m.initial);
			lowRho.push_back(m.lower);
			highRho.push_back(m.upper);
		}
		for (size_t i = 0; i < r.transfer->Patches().size(); ++i)
		{
			const auto &p = r.transfer->Patches()[i];
			patchIndex[p.id] = i;
			Need(materialIndex.count(p.material), Code::InvalidInput,
				 "Patch material missing, provide explicit Unknown");
		}
		out.fixedRoots = r.roots;
		std::sort(out.fixedRoots.begin(), out.fixedRoots.end(),
				  [](const Root &a, const Root &b) { return a.source < b.source; });
		GeometryKey roots;
		roots.Digest(r.transfer->Key());
		for (size_t j = 0; j < out.fixedRoots.size(); ++j)
		{
			const auto &root = out.fixedRoots[j];
			Need(root.source && (!j || root.source != out.fixedRoots[j - 1].source) &&
					 root.kind <= TransportRootKind::Unknown && root.incident.size() == patchIndex.size() &&
					 root.available.size() == patchIndex.size() && !Zero(root.generation) &&
					 !root.provenance.empty(),
				 Code::InvalidInput, "Fixed-root correspondence/provenance");
			roots.U(root.source);
			Need(root.domain == c.incidentDomain && root.calibration == c.calibration.id, Code::InvalidInput,
				 "Root/observation units or calibration mismatch");
			Need(root.kind != TransportRootKind::Unknown, Code::Unavailable,
				 "Unknown incident root cannot be classified as physical direct or ambient");
			roots.U(unsigned(root.domain));
			roots.U(root.calibration);
			roots.U(unsigned(root.kind));
			Need(root.kind != TransportRootKind::Sky || root.sunCoverage == SunCoverage::ExcludesExplicitSun,
				 Code::InvalidInput, "Sky root must explicitly exclude direct sun before composition");
			roots.U(unsigned(root.sunCoverage));
			roots.Digest(root.generation);
			for (size_t i = 0; i < root.incident.size(); ++i)
			{
				Need(Positive(root.incident[i]), Code::InvalidInput, "Invalid primary energy");
				Need(root.available[i] <= 1, Code::InvalidInput, "Invalid primary availability");
				roots.V(root.incident[i]);
				roots.U(root.available[i]);
			}
		}
		out.fixedRootsKey = roots.Finish();
		std::set<Id> seen;
		for (size_t first = 0; first < r.count; first += 128)
		{
			Step();
			Chunk<Observation> batch;
			Check(r.training->Read(first, size_t(std::min<uint64_t>(128, r.count - first)), read, batch));
			Need(batch.owner && batch.data && batch.count == std::min<uint64_t>(128, r.count - first),
				 Code::ProviderError, "Short training stream");
			out.work.rowReads += batch.count;
			for (size_t i = 0; i < batch.count; ++i)
			{
				const auto &o = batch.data[i];
				Need(o.role == Role::Training && o.id && seen.insert(o.id).second &&
						 patchIndex.count(o.patch) && o.spatialBlock && std::isfinite(o.areaWorldSquared) &&
						 o.areaWorldSquared > 0 && std::isfinite(o.sigma) && o.sigma > 0,
					 Code::InvalidInput, "Training role/patch/area/noise");
				for (const auto &q : o.channels)
					Need(!q.present || (std::isfinite(q.lower) && q.lower >= 0 &&
										(std::isfinite(q.upper) || q.censored) && q.upper >= q.lower),
						 Code::InvalidInput, "Quantization/censor interval");
				obs.push_back(o);
			}
		}
		std::sort(obs.begin(), obs.end(),
				  [](const Observation &a, const Observation &b) { return a.id < b.id; });
		for (const auto &o : obs)
			if (o.eligible)
				denominator += o.areaWorldSquared;
		Need(denominator > 0, Code::Unavailable, "No eligible training area");
	}
	bool Admissible() const
	{
		return r.contract.operation != ObservationOperator::Unknown && r.contract.transferAttested &&
			   r.contract.primariesAttested;
	}
	Vec3 Encode(size_t i, Vec3 incident) const
	{
		const auto &c = r.contract;
		if (c.operation == ObservationOperator::NetRadiantEncoded)
		{
			Vec3 encoded;
			uint8_t packed[3];
			Check(Bake::Encode(c.profile, incident, encoded, packed));
			return encoded;
		}
		if (c.operation == ObservationOperator::OutgoingLambertian)
		{
			auto m = materialIndex.at(r.transfer->Patches()[i].material);
			return V(Component(rho[m], 0) * incident.x / Pi, Component(rho[m], 1) * incident.y / Pi,
					 Component(rho[m], 2) * incident.z / Pi)
				.Float();
		}
		return incident;
	}
	std::vector<Vec3> Bounce(const Root &root, const std::vector<Vec3> &reflectance, bool upper, bool &known)
	{
		const auto &patches = r.transfer->Patches();
		std::vector<V> sum(patches.size());
		known = true;
		double energy[3] = {};
		for (size_t j = 0; j < patches.size(); ++j)
		{
			Step();
			auto m = materialIndex.at(patches[j].material);
			if (!root.available[j])
				known = false;
			for (int c = 0; c < 3; ++c)
				energy[c] += patches[j].areaWorldSquared * Component(root.incident[j], c) *
							 Component(reflectance[m], c);
		}
		for (const auto &link : r.transfer->Links())
		{
			Step();
			++out.work.linkOperations;
			for (unsigned orientation = 0; orientation < 2; ++orientation)
			{
				size_t a = orientation ? link.b : link.a, b = orientation ? link.a : link.b;
				auto m = materialIndex.at(patches[a].material);
				double C = upper ? link.possibleConductance : link.conductance;
				if (!root.available[a])
				{
					known = false;
					continue;
				}
				if (C != link.possibleConductance)
					known = false;
				for (int c = 0; c < 3; ++c)
					sum[b][c] += C / patches[b].areaWorldSquared * Component(reflectance[m], c) *
								 Component(root.incident[a], c);
			}
		}
		bool complete = r.transfer->Report().completePatchSet && r.transfer->Report().allPairsRepresented;
		known &= complete;
		std::vector<Vec3> result;
		for (size_t i = 0; i < sum.size(); ++i)
		{
			if (upper)
				for (int c = 0; c < 3; ++c)
					sum[i][c] = std::min(sum[i][c], energy[c] / patches[i].areaWorldSquared);
			result.push_back(sum[i].Float());
		}
		return result;
	}
	void Predict(bool directOnly = false)
	{
		values.assign(patchIndex.size(), {});
		out.byRoot.clear();
		const auto &patches = r.transfer->Patches();
		std::vector<uint32_t> issues(patches.size());
		for (const auto &l : r.transfer->Links())
		{
			issues[l.a] |= l.issues;
			issues[l.b] |= l.issues;
		}
		for (size_t i = 0; i < values.size(); ++i)
		{
			auto &v = values[i];
			v.patch = patches[i].id;
			v.comparable = Admissible();
			v.issues = issues[i] | ((r.contract.unit == PhotometricUnit::Relative ||
									 r.contract.calibration.kind != CalibrationKind::PhysicallyAuthored)
										? RelativeScale
										: 0u);
			if (!r.transfer->Report().completePatchSet || !r.transfer->Report().allPairsRepresented)
				v.issues |= OmittedLinks;
			if (!Admissible() || r.contract.profile.family == "UnknownProfile")
				v.issues |= UnknownProfile;
			v.upperKnown = true;
			if (v.issues & (UnknownOptics | MovingPath | ApproximateGeometry))
				v.comparable = false;
		}
		for (const auto &root : out.fixedRoots)
		{
			bool certain = false, a = false, b = false;
			auto lower = Bounce(root, lowRho, false, a), upper = Bounce(root, highRho, true, b),
				 nominal = Bounce(root, rho, false, certain);
			RootBounce layer;
			layer.source = root.source;
			layer.kind = root.kind;
			layer.incident = nominal;
			layer.lower = lower;
			layer.upper = upper;
			if (!r.contract.physicalTransportAssumed)
				std::fill(layer.incident.begin(), layer.incident.end(), Vec3());
			for (size_t i = 0; i < values.size(); ++i)
			{
				auto &v = values[i];
				Vec3 *destination = root.kind == TransportRootKind::LocalEmitter
										? &v.direct
										: root.kind == TransportRootKind::Sun
											  ? &v.sun
											  : root.kind == TransportRootKind::Sky ? &v.sky : &v.ambient;
				*destination = (V(*destination) + V(root.incident[i])).Float();
				if (!root.available[i])
				{
					v.comparable = false;
					v.issues |= MissingPrimary;
				}
				v.upperKnown &= b;
				if (!directOnly && r.contract.physicalTransportAssumed)
				{
					v.indirect = (V(v.indirect) + V(nominal[i])).Float();
					v.indirectLower = (V(v.indirectLower) + V(lower[i])).Float();
					v.indirectUpper = (V(v.indirectUpper) + V(upper[i])).Float();
				}
			}
			if (r.contract.physicalTransportAssumed)
				for (const auto &link : r.transfer->Links())
					if (link.possibleConductance > 0)
						for (unsigned side = 0; side < 2; ++side)
						{
							size_t sender = side ? link.b : link.a, receiver = side ? link.a : link.b;
							if (!root.available[sender])
							{
								values[receiver].issues |= MissingPrimary;
								values[receiver].comparable = false;
							}
							if (out.materials[materialIndex.at(patches[sender].material)].knowledge ==
								MaterialKnowledge::Unknown)
							{
								values[receiver].issues |= UnknownMaterial;
								values[receiver].comparable = false;
							}
						}
			out.byRoot.push_back(std::move(layer));
		}
		for (size_t i = 0; i < values.size(); ++i)
		{
			auto &m = out.materials[materialIndex.at(patches[i].material)];
			if (m.knowledge == MaterialKnowledge::Unknown)
				values[i].issues |= UnknownMaterial;
			auto &v = values[i];
			v.directOnlyPrediction =
				Admissible() ? Encode(i, (V(v.direct) + V(v.sun) + V(v.sky) + V(v.ambient)).Float()) : Vec3();
			v.totalPreencoding = (V(v.direct) + V(v.sun) + V(v.sky) + V(v.ambient) + V(v.indirect)).Float();
			if (!field.empty())
			{
				v.unresolved = field[fieldCell[i]];
				v.totalPreencoding = (V(v.totalPreencoding) + V(v.unresolved)).Float();
				v.issues |= UnresolvedAppearance;
			}
			v.totalPrediction = Admissible() ? Encode(i, v.totalPreencoding) : Vec3();
			for (Vec3 x : {v.direct, v.sun, v.sky, v.ambient, v.indirect, v.indirectLower, v.indirectUpper,
						   v.unresolved, v.totalPreencoding, v.totalPrediction})
				Need(Positive(x), Code::AlgorithmError, "Non-finite or negative transport accumulation");
		}
	}
	Terms Objective()
	{
		Step();
		Need(++out.work.evaluations <= r.options.maxEvaluations, Code::WorkLimit,
			 "Indirect objective budget");
		Predict();
		Terms t;
		for (const auto &o : obs)
		{
			Step();
			auto i = patchIndex.at(o.patch);
			if (!o.eligible || !values[i].comparable)
				continue;
			for (int c = 0; c < 3; ++c)
				if (o.channels[c].present)
					t.data += o.areaWorldSquared / denominator *
							  Huber(IntervalResidual(Component(values[i].totalPrediction, c), o.channels[c]) /
										o.sigma,
									r.options.huber);
		}
		for (size_t m = 0; m < rho.size(); ++m)
			for (int c = 0; c < 3; ++c)
				t.materialPrior += out.materials[m].priorWeight *
								   std::pow(Component(rho[m], c) - Component(out.materials[m].initial, c), 2);
		for (const auto &v : field)
			t.unresolvedMagnitude +=
				r.options.unresolved.magnitudePenalty * Dot(V(v), V(v)) / std::max<size_t>(1, field.size());
		Need(std::isfinite(t.Total()), Code::AlgorithmError, "Non-finite indirect objective");
		return t;
	}
	void FitMaterials()
	{
		auto best = Objective();
		if (!Admissible())
		{
			out.stop = "UnknownObservationOperator";
			return;
		}
		bool movable = false;
		for (const auto &m : out.materials)
			movable |= m.knowledge == MaterialKnowledge::Bounded && r.contract.physicalTransportAssumed;
		for (size_t iteration = 0; iteration < r.options.maxIterations && movable; ++iteration)
		{
			double before = best.Total(), motion = 0;
			for (size_t m = 0; m < rho.size(); ++m)
				if (out.materials[m].knowledge == MaterialKnowledge::Bounded)
					for (int c = 0; c < 3; ++c)
					{
						double lo = Component(lowRho[m], c), hi = Component(highRho[m], c),
							   old = Component(rho[m], c), chosen = old;
						if (hi == lo)
							continue;
						double x = lo, y = hi;
						for (unsigned k = 0; k < 28 && y - x > r.options.parameterTolerance; ++k)
						{
							double a = x + (y - x) / 3, b = y - (y - x) / 3;
							Set(rho[m], c, a);
							auto va = Objective();
							Set(rho[m], c, b);
							auto vb = Objective();
							if (va.Total() < best.Total())
							{
								best = va;
								chosen = a;
							}
							if (vb.Total() < best.Total())
							{
								best = vb;
								chosen = b;
							}
							if (va.Total() <= vb.Total())
								y = b;
							else
								x = a;
						}
						for (double endpoint : {lo, hi})
						{
							Set(rho[m], c, endpoint);
							auto v = Objective();
							if (v.Total() < best.Total())
							{
								best = v;
								chosen = endpoint;
							}
						}
						Set(rho[m], c, chosen);
						motion = std::max(motion, std::abs(chosen - old));
					}
			++out.work.iterations;
			if (std::abs(before - best.Total()) <= r.options.objectiveTolerance * std::max(1., before) ||
				motion <= r.options.parameterTolerance)
			{
				out.converged = true;
				out.stop = "BoundedCoordinateStationary";
				break;
			}
		}
		if (!movable)
		{
			out.converged = true;
			out.stop = "FixedMaterialForward";
		}
		else if (out.stop.empty())
			out.stop = "IterationLimit";
		out.objective = Objective();
		// Conditional endpoint alternatives expose material uncertainty; not joint CIs.
		auto saved = rho;
		double reference = out.objective.Total();
		for (unsigned side = 0; side < 2; ++side)
		{
			rho = side ? highRho : lowRho;
			auto v = Objective();
			if (v.Total() <= reference + r.options.plausibilitySlack)
			{
				Alternative a;
				a.name = side ? "material-upper-endpoint" : "material-lower-endpoint";
				a.trainingObjective = v.Total();
				a.reflectance = rho;
				out.alternatives.push_back(a);
			}
		}
		rho = saved;
	}
	void FitUnresolved()
	{
		const auto &o = r.options.unresolved;
		if (!o.enabled)
			return;
		Need(Admissible() && r.contract.operation == ObservationOperator::IncidentLinear, Code::Unavailable,
			 "Unresolved field fit requires explicitly linear incident observation space");
		Need(std::isfinite(o.cellWorld) && o.cellWorld > 0 && std::isfinite(o.maximumValue) &&
				 o.maximumValue > 0 && std::isfinite(o.magnitudePenalty) && o.magnitudePenalty > 0 &&
				 o.smoothnessPenalty == 0 && o.maxCells && o.maxCells <= 4096,
			 Code::InvalidInput, "Explicit bounded unresolved model; smoothing not implemented");
		Predict();
		const auto &patches = r.transfer->Patches();
		Need(Finite(o.originWorld), Code::InvalidInput, "Unresolved grid origin");
		using Cell = std::tuple<Id, Id, uint8_t, int64_t, int64_t, int64_t>;
		std::map<Cell, size_t> cells;
		fieldCell.resize(patches.size());
		std::vector<Id> firstSurface;
		for (size_t i = 0; i < patches.size(); ++i)
		{
			const auto &p = patches[i];
			V pos = V(p.receiver.world) - V(o.originWorld);
			Cell cell(p.receiver.surface, p.material, p.receiver.style,
					  int64_t(std::floor(pos.x / o.cellWorld)), int64_t(std::floor(pos.y / o.cellWorld)),
					  int64_t(std::floor(pos.z / o.cellWorld)));
			auto found = cells.emplace(cell, cells.size());
			fieldCell[i] = found.first->second;
			Need(cells.size() <= o.maxCells, Code::MemoryLimit, "Coarse unresolved cell limit");
		}
		field.assign(cells.size(), Vec3()); // no per-luxel degrees of freedom
		std::vector<size_t> anchor(field.size(), SIZE_MAX);
		for (const auto &sample : obs)
			if (sample.eligible && values[patchIndex.at(sample.patch)].comparable)
			{
				size_t p = patchIndex.at(sample.patch);
				anchor[fieldCell[p]] = std::min(anchor[fieldCell[p]], p);
			}
		// Uncertified or blocked straight paths to the training anchor get a private
		// zero slot. Thus proximity/region ID never interpolates through a wall.
		size_t nullSlot = field.size();
		field.push_back(Vec3());
		auto clearPath = [&](RayQuery query) {
			for (unsigned direction = 0; direction < 2; ++direction)
			{
				Step();
				Need(work < r.options.maxWork, Code::WorkLimit, "Unresolved path work budget");
				query.maxWork = std::min(uint64_t(100000), r.options.maxWork - work);
				RayResult hit;
				Check(r.transfer->QueryPath(query, read, hit));
				++out.work.rays;
				Step(hit.nodeTests + hit.primitiveTests);
				if (hit.state != VisibilityState::Visible || hit.reliability != RayReliability::Geometric)
					return false;
				std::swap(query.startWorld, query.endWorld);
				std::swap(query.originSurface, query.targetSurface);
				query.bias.normalKnown = false;
			}
			return true;
		};
		for (size_t i = 0; i < patches.size(); ++i)
		{
			size_t c = fieldCell[i];
			if (anchor[c] == SIZE_MAX)
			{
				fieldCell[i] = nullSlot;
				continue;
			}
			size_t a = anchor[c];
			if (Dot(V(patches[a].receiver.geometricNormal), V(patches[i].receiver.geometricNormal)) < .999)
			{
				fieldCell[i] = nullSlot;
				continue;
			}
			if (i == a)
				continue;
			RayQuery q;
			Need(std::isfinite(o.normalOffsetWorld) && o.normalOffsetWorld > 0, Code::InvalidInput,
				 "Unresolved endpoint offset");
			bool guarded = true;
			for (size_t endpoint : {a, i})
			{
				RayQuery guard;
				guard.startWorld = patches[endpoint].receiver.world;
				guard.endWorld = (V(guard.startWorld) +
								  V(patches[endpoint].receiver.geometricNormal) * o.normalOffsetWorld)
									 .Float();
				guard.originSurface = patches[endpoint].receiver.surface;
				guard.bias.absoluteWorld = guard.bias.relative = 0;
				guard.bias.normalKnown = true;
				guard.bias.geometricNormal = patches[endpoint].receiver.geometricNormal;
				guard.targetToleranceWorld = o.normalOffsetWorld * 1e-6;
				guarded &= clearPath(guard);
			}
			if (!guarded)
			{
				fieldCell[i] = nullSlot;
				continue;
			}
			q.startWorld =
				(V(patches[a].receiver.world) + V(patches[a].receiver.geometricNormal) * o.normalOffsetWorld)
					.Float();
			q.endWorld =
				(V(patches[i].receiver.world) + V(patches[i].receiver.geometricNormal) * o.normalOffsetWorld)
					.Float();
			q.bias.absoluteWorld = 0;
			q.bias.relative = 0;
			q.purpose = RayPurpose::IrradianceProbe;
			if (!clearPath(q))
				fieldCell[i] = nullSlot;
		}
		auto best = Objective();
		for (size_t c = 0; c < nullSlot; ++c)
			for (int channel = 0; channel < 3; ++channel)
			{
				double lo = 0, hi = o.maximumValue, chosen = 0;
				for (unsigned k = 0; k < 28; ++k)
				{
					double a = lo + (hi - lo) / 3, b = hi - (hi - lo) / 3;
					Set(field[c], channel, a);
					auto va = Objective();
					Set(field[c], channel, b);
					auto vb = Objective();
					if (va.Total() < best.Total())
					{
						best = va;
						chosen = a;
					}
					if (vb.Total() < best.Total())
					{
						best = vb;
						chosen = b;
					}
					if (va.Total() <= vb.Total())
						hi = b;
					else
						lo = a;
				}
				Set(field[c], channel, chosen);
			}
		out.objective = Objective();
		out.limitations.push_back("Coarse nonnegative unresolved appearance fitted AFTER frozen "
								  "direct/material terms; not physical GI");
	}
	Result Run()
	{
		Predict(true);
		for (const auto &o : obs)
			if (o.eligible && values[patchIndex.at(o.patch)].comparable)
				for (int c = 0; c < 3; ++c)
					if (o.channels[c].present)
						out.directOnlyObjective +=
							o.areaWorldSquared / denominator *
							Huber(
								IntervalResidual(Component(values[patchIndex.at(o.patch)].totalPrediction, c),
												 o.channels[c]) /
									o.sigma,
								r.options.huber);
		FitMaterials();
		FitUnresolved();
		out.objective = Objective();
		out.values = values;
		for (size_t m = 0; m < rho.size(); ++m)
			out.materials[m].initial = rho[m];
		out.fitted = Admissible() && std::any_of(obs.begin(), obs.end(), [&](const Observation &o) {
						 return o.eligible && out.values[patchIndex.at(o.patch)].comparable;
					 });
		if (!out.fitted && Admissible())
			out.stop = "UnavailableComparableTraining";
		out.absoluteScaleKnown = r.contract.unit == PhotometricUnit::Lux &&
								 r.contract.calibration.kind == CalibrationKind::PhysicallyAuthored;
		out.physicalBounceVerified = false;
		out.materialAllocationIdentifiable = false;
		Core::GeometryKey key;
		key.U(kVersion);
		key.Digest(r.transfer->Key());
		key.Digest(out.fixedRootsKey);
		key.Digest(r.trainingKey);
		key.Digest(r.contract.profile.contentKey);
		key.U(unsigned(r.contract.operation));
		key.U(unsigned(r.contract.incidentDomain));
		key.U(r.contract.calibration.id);
		key.U(unsigned(r.contract.calibration.kind));
		key.U(unsigned(r.contract.unit));
		key.F(r.contract.units.worldUnitsPerMeter);
		key.V(r.options.unresolved.originWorld);
		key.F(r.options.unresolved.cellWorld);
		key.F(r.options.unresolved.maximumValue);
		key.F(r.options.unresolved.magnitudePenalty);
		key.F(r.options.unresolved.normalOffsetWorld);
		Text(key, r.contract.provenance);
		for (const auto &m : out.materials)
		{
			key.U(m.material);
			key.V(m.initial);
			key.V(m.lower);
			key.V(m.upper);
			key.U(unsigned(m.knowledge));
		}
		for (auto &v : out.values)
		{
			key.V(v.indirect);
			key.V(v.unresolved);
		}
		out.key = key.Finish();
		out.work.linkOperations = std::max<uint64_t>(1, out.work.linkOperations);
		for (int c = 0; c < 3; ++c)
		{
			double input = 0, reflected = 0;
			for (size_t i = 0; i < out.values.size(); ++i)
			{
				double area = r.transfer->Patches()[i].areaWorldSquared;
				const auto &v = out.values[i];
				input += area * Component((V(v.direct) + V(v.sun) + V(v.sky) + V(v.ambient)).Float(), c);
				reflected += area * Component(v.indirect, c);
			}
			if (input > 0)
				out.maximumReflectedPowerFraction =
					std::max(out.maximumReflectedPowerFraction, reflected / input);
		}
		out.limitations.push_back("Passive one-bounce model, quadrature convergence estimate only; not "
								  "empirical q3map bounce replay or stock physical certification");
		out.limitations.push_back("Direct/sun/sky roots held fixed; unknown profile, material, optical and "
								  "omitted paths remain separate limitations");
		return out;
	}
};
} // namespace
Error Fit(const Request &r, const ReadContext &read, Result &out)
{
	out = {};
	try
	{
		Engine engine(r, read);
		out = engine.Run();
		return {};
	}
	catch (const Error &e)
	{
		out.stop = e.message;
		return e;
	}
	catch (const std::bad_alloc &)
	{
		out.stop = "MemoryLimit";
		return Failure(Code::MemoryLimit, "Indirect fit allocation");
	}
}

Error EvaluateFrozen(const Result &r, std::shared_ptr<const ChunkSource<Observation>> source, uint64_t count,
					 Role role, const ReadContext &read, Score &out, DiagnosticSink *sink)
{
	out = {};
	try
	{
		Need(source && count && count <= 1000000, Code::InvalidInput, "Frozen observation stream");
		std::map<Id, const PatchValue *> values;
		for (const auto &v : r.values)
			values[v.patch] = &v;
		double total = 0, direct = 0, black = 0;
		std::set<Id> seen;
		for (uint64_t first = 0; first < count; first += 128)
		{
			Cancel(read);
			Chunk<Observation> batch;
			Check(source->Read(first, size_t(std::min<uint64_t>(128, count - first)), read, batch));
			Need(batch.owner && batch.data && batch.count == std::min<uint64_t>(128, count - first),
				 Code::ProviderError, "Short frozen stream");
			for (size_t i = 0; i < batch.count; ++i)
			{
				const auto &o = batch.data[i];
				++out.requested;
				Need(o.role == role && o.id && seen.insert(o.id).second && o.areaWorldSquared > 0 &&
						 std::isfinite(o.areaWorldSquared) && o.sigma > 0 && std::isfinite(o.sigma),
					 Code::InvalidInput, "Frozen role/weight/identity");
				auto found = values.find(o.patch);
				if (!o.eligible || found == values.end() || !found->second->comparable)
				{
					++out.unavailable;
					continue;
				}
				const auto &v = *found->second;
				++out.used;
				out.measure += o.areaWorldSquared;
				Vec3 residual;
				for (int c = 0; c < 3; ++c)
					if (o.channels[c].present)
					{
						double e = IntervalResidual(Component(v.totalPrediction, c), o.channels[c]);
						Set(residual, c, e);
						total += o.areaWorldSquared * e * e;
						black += o.areaWorldSquared * std::pow(IntervalResidual(0, o.channels[c]), 2);
						if (o.channels[c].censored)
							++out.censored;
						direct +=
							o.areaWorldSquared *
							std::pow(IntervalResidual(Component(v.directOnlyPrediction, c), o.channels[c]),
									 2);
						out.unresolvedPositive += o.areaWorldSquared * std::max(0., -e);
						out.overshoot += o.areaWorldSquared * std::max(0., e);
					}
				if (sink)
					Check(sink->Write(o, v, residual));
			}
		}
		if (out.measure)
		{
			out.available = true;
			out.intervalMse = total / (3 * out.measure);
			out.directOnlyMse = direct / (3 * out.measure);
			out.blackMse = black / (3 * out.measure);
		}
		return {};
	}
	catch (const Error &e)
	{
		out = {};
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Frozen indirect score allocation");
	}
}
Error DebugTsv(const Result &r, size_t max, std::string &out)
{
	out.clear();
	std::ostringstream s;
	s << std::setprecision(12)
	  << "patch\tcomparable\tissues\tdirect_r\tdirect_g\tdirect_b\tsun_r\tsun_g\tsun_b\tsky_r\tsky_g\tsky_"
		 "b\tambient_r\tambient_g\tambient_b\tindirect_r\tindirect_g\tindirect_b\tunresolved_r\tunresolved_"
		 "g\tunresolved_b\ttotal_r\ttotal_g\ttotal_b\tupper_known\n";
	for (const auto &v : r.values)
	{
		s << v.patch << '\t' << v.comparable << '\t' << v.issues;
		for (Vec3 rgb : {v.direct, v.sun, v.sky, v.ambient, v.indirect, v.unresolved, v.totalPrediction})
			s << '\t' << rgb.x << '\t' << rgb.y << '\t' << rgb.z;
		s << '\t' << v.upperKnown << '\n';
		if (size_t(s.tellp()) > max)
			return Failure(Code::MemoryLimit, "Decomposition TSV budget");
	}
	out = s.str();
	return {};
}
Error AppendFields(const Transfer &t, const Result &r, Id first, Id provenance,
				   ReconstructedLightingAsset &asset, std::vector<Disk::Chunk> &chunks,
				   const ReadContext &read)
{
	try
	{
		Cancel(read);
		Need(r.fitted && r.transferKey == t.Key() && r.values.size() == t.Patches().size() && first &&
				 provenance,
			 Code::InvalidInput, "Frozen fitting result/transfer mismatch");
		Need(asset.units.worldUnitsPerMeter == r.contract.units.worldUnitsPerMeter, Code::InvalidInput,
			 "Field/asset scale mismatch");
		for (const auto &p : t.Patches())
			Need(p.receiver.style == t.Patches()[0].receiver.style, Code::Unavailable,
				 "Canonical field export requires a single explicit style partition");
		bool haveProvenance = false;
		for (const auto &p : asset.provenanceRecords)
			haveProvenance |= p.id == provenance;
		Need(haveProvenance, Code::InvalidInput, "Field provenance missing from canonical asset");
		uint64_t n = r.values.size(), layers = r.byRoot.size() + 1;
		std::set<Id> rootIds;
		for (const auto &root : r.byRoot)
			Need(root.source && rootIds.insert(root.source).second && root.incident.size() == n &&
					 root.lower.size() == n && root.upper.size() == n,
				 Code::InvalidInput, "Malformed per-root transport sample spans");
		for (size_t i = 0; i < n; ++i)
			Need(r.values[i].patch == t.Patches()[i].id, Code::InvalidInput,
				 "Transport patch identity mismatch");
		uint64_t existing = 0;
		for (const auto &c : chunks)
		{
			Need(c.bytes.size() <= 128ull * 1024 * 1024 - existing, Code::MemoryLimit,
				 "Existing field payload limit");
			existing += c.bytes.size();
		}
		Need(2 * existing + n * layers * 256 + 65536 <=
				 (read.maxBytes ? read.maxBytes : 128ull * 1024 * 1024),
			 Code::MemoryLimit, "Field payload budget");
		std::set<Id> used;
		for (const auto &e : asset.emitters)
			used.insert(e.source.id);
		for (const auto &e : asset.environments)
			used.insert(e.source.id);
		for (const auto &f : asset.transport)
			used.insert(f.source.id);
		for (const auto &d : asset.datasets)
			used.insert(d.id);
		Need(first < std::numeric_limits<Id>::max() - layers * 2, Code::InvalidInput, "Field ID overflow");
		auto copy = asset;
		auto payload = chunks;
		Id next = first;
		auto add = [&](const std::vector<Vec3> &mean, const RootBounce *root) {
			Id field = next++, dataset = next++;
			Need(!used.count(field) && !used.count(dataset), Code::InvalidInput,
				 "Canonical field ID collision");
			used.insert(field);
			used.insert(dataset);
			TransportField f;
			f.source.id = field;
			f.source.provenance = provenance;
			f.source.state = HypothesisState::Candidate;
			f.source.evidence.approximateIrradiance = Suitability::Candidate;
			f.component =
				root ? TransportComponent::IndirectIrradiance : TransportComponent::UnresolvedIrradiance;
			f.unit = r.contract.unit;
			f.calibration = r.contract.calibration.id;
			f.dataset = dataset;
			f.quantity = FieldQuantity::Irradiance;
			f.bounds.known = true;
			f.bounds.minWorld = f.bounds.maxWorld = t.Patches()[0].receiver.world;
			for (const auto &p : t.Patches())
				for (int c = 0; c < 3; ++c)
				{
					Set(f.bounds.minWorld, c,
						std::min(Component(f.bounds.minWorld, c), Component(p.receiver.world, c)));
					Set(f.bounds.maxWorld, c,
						std::max(Component(f.bounds.maxWorld, c), Component(p.receiver.world, c)));
				}
			auto &m = f.membership;
			m.model = "org.openjk.passive-one-bounce/1; profile=" + r.contract.profile.id +
					  "; generation=" + Disk::Hex(r.key);
			m.directLocal = m.directSun = m.directSky = m.legacyAmbient = FieldMembership::Excluded;
			m.reflected = root ? FieldMembership::Included : FieldMembership::Excluded;
			m.unresolved = root ? FieldMembership::Excluded : FieldMembership::Included;
			m.reflectionOrderKnown = root != nullptr;
			m.style = t.Patches()[0].receiver.style;
			m.minimumReflectionOrder = m.maximumReflectionOrder = root ? 1 : 0;
			if (root)
			{
				TransportRoot v;
				v.source = root->source;
				v.kind = root->kind;
				m.roots.push_back(v);
			}
			Dataset d;
			d.id = dataset;
			d.purpose = DatasetPurpose::Transport;
			d.layout = DatasetLayout::Chunked;
			d.locator = "embedded://s10/" + std::to_string(dataset);
			const char *domain = r.contract.incidentDomain == Root::Domain::CompilerRelative255
									 ? "compiler-relative255"
									 : r.contract.incidentDomain == Root::Domain::PhysicalLux
										   ? "physical-lux"
										   : r.contract.incidentDomain == Root::Domain::SceneEquivalentLux
												 ? "scene-equivalent-lux"
												 : "source-linear-relative";
			d.encoding = std::string("org.openjk.passive-transport-samples/") + domain;
			d.encodingVersion = 1;
			d.elementCount = n;
			d.chunkElements = 128;
			GeometryKey hash;
			for (size_t a = 0; a < n; a += 128)
			{
				Cancel(read);
				Disk::Chunk block;
				block.dataset = dataset;
				block.firstElement = a;
				block.count = std::min<size_t>(128, n - a);
				auto u = [&](uint64_t x, unsigned bytes) {
					for (unsigned k = 0; k < bytes; ++k)
						block.bytes.push_back(uint8_t(x >> (8 * k)));
				};
				auto fl = [&](float value) {
					uint32_t bits;
					std::memcpy(&bits, &value, 4);
					u(bits, 4);
				};
				u(0x3154474a, 4);
				u(kVersion, 4);
				u(block.count, 4);
				u(unsigned(f.component), 4);
				for (size_t i = a; i < a + block.count; ++i)
				{
					u(t.Patches()[i].id, 8);
					u(r.values[i].issues, 4);
					u(r.values[i].comparable, 1);
					u(r.values[i].upperKnown, 1);
					u(t.Patches()[i].receiver.style, 1);
					u(0, 1);
					Vec3 lower = root ? root->lower[i] : mean[i], upper = root ? root->upper[i] : mean[i];
					Need(Positive(mean[i]) && Positive(lower) && Positive(upper), Code::InvalidInput,
						 "Invalid field RGB payload");
					for (Vec3 v : {t.Patches()[i].receiver.world, t.Patches()[i].receiver.geometricNormal,
								   mean[i], lower, upper})
					{
						fl(v.x);
						fl(v.y);
						fl(v.z);
					}
				}
				hash.Digest(Disk::Hash(block.bytes.data(), block.bytes.size()));
				payload.push_back(std::move(block));
			}
			d.contentHash = "sha256-chain:" + Disk::Hex(hash.Finish());
			copy.datasets.push_back(d);
			copy.transport.push_back(f);
		};
		if (r.contract.physicalTransportAssumed)
			for (const auto &root : r.byRoot)
				add(root.incident, &root);
		std::vector<Vec3> unresolved;
		for (const auto &v : r.values)
			unresolved.push_back(v.unresolved);
		if (std::any_of(unresolved.begin(), unresolved.end(), [](Vec3 v) { return Peak(v) > 0; }))
			add(unresolved, nullptr);
		std::string problem;
		Need(Validate(copy, &problem), Code::InvalidInput, "Canonical field validation: " + problem);
		asset = std::move(copy);
		chunks = std::move(payload);
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Transport field export allocation");
	}
}
} // namespace Indirect
} // namespace Core
} // namespace Rend2LightRecon
