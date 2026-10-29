// Explicit historical expansion OR finite-source irradiance quadrature; never runtime LTC.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "emitter_shape.h"
#include "bake_math.h"
#include "geometry_key.h"
#include <algorithm>
#include <map>
#include <set>
namespace Rend2LightRecon
{
namespace Core
{
namespace Shape
{
using namespace Bake::Detail;
namespace
{
void Need(bool ok, Code c, const std::string &s)
{
	if (!ok)
		throw Failure(c, s);
}
void Check(Error e)
{
	if (!e)
		throw e;
}
void Cancel(const ReadContext &r)
{
	Need(!Cancelled(r), Code::Cancelled, "Extended emitter operation cancelled");
}
bool Zero(const Disk::Digest &d) { return d == Disk::Digest{}; }
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> v)
{
	return VectorChunk<T>(v, 0, v->size());
}
void Text(GeometryKey &k, const std::string &s)
{
	k.U(s.size());
	for (unsigned char c : s)
		k.U(c);
}
void Add(Work &a, const Work &b)
{
	a.blocks += b.blocks;
	a.values += b.values;
	a.pairs += b.pairs;
	a.rays += b.rays;
	a.traversal += b.traversal;
	a.nodes += b.nodes;
}
void Budget(const Work &w, const Sampling &s)
{
	Need(w.rays <= s.maxRays && w.traversal <= s.maxWork, Code::WorkLimit,
		 "Extended emitter ray/traversal budget");
}
void FrameValid(const Frame &f)
{
	Need(f.known && Normal(f.right) && Normal(f.up) && Normal(f.forward) &&
			 Length(Cross(V(f.right), V(f.up)) - V(f.forward)) < 1e-4 &&
			 std::abs(Dot(V(f.right), V(f.up))) < 1e-4,
		 Code::InvalidInput, "Emitter frame must be right-handed orthonormal");
}
void Validate(const Model &m, const Emitter &e)
{
	const auto &p = e.geometry;
	auto kind = p.kind;
	Need(p.source.id && p.source.state == HypothesisState::Candidate && p.position.known &&
			 Finite(p.position.world) && Positive(e.rgbAmplitude),
		 Code::InvalidInput, "Invalid candidate identity/position/RGB");
	Need(kind == EmitterKind::Point || kind == EmitterKind::Spot || kind == EmitterKind::Rectangle ||
			 kind == EmitterKind::Tube,
		 Code::Unavailable, "Extended fit supports point, spot, rectangle and open finite cylinder only");
	if (kind != EmitterKind::Point)
		FrameValid(p.frame);
	if (kind == EmitterKind::Rectangle)
		Need(p.geometry.known && std::isfinite(p.geometry.halfWidthWorld) && p.geometry.halfWidthWorld > 0 &&
				 std::isfinite(p.geometry.halfHeightWorld) && p.geometry.halfHeightWorld > 0,
			 Code::InvalidInput, "Positive rectangle dimensions required");
	if (kind == EmitterKind::Tube)
		Need(p.geometry.known && std::isfinite(p.geometry.halfLengthWorld) &&
				 p.geometry.halfLengthWorld > 0 && std::isfinite(p.geometry.radiusWorld) &&
				 p.geometry.radiusWorld > 0 && !p.angular.twoSided,
			 Code::InvalidInput, "Positive open-cylinder length/radius required");
	Need(!p.geometry.effectiveRadiusWorld.known || p.geometry.effectiveRadiusWorld.value == 0,
		 Code::Unavailable, "Finite point/spot extent requires a separate supported source model");
	if (kind == EmitterKind::Spot)
	{
		if (m.law == Law::HistoricalBake)
			Need(std::isfinite(e.spotTargetDistanceWorld) && e.spotTargetDistanceWorld > 0 &&
					 std::isfinite(e.spotRadiusWorld) && e.spotRadiusWorld >= 0,
				 Code::InvalidInput, "Historical spot target distance/radius required");
		else
			Need(p.angular.kind == AngularKind::SmoothCone && std::isfinite(p.angular.cosInner) &&
					 std::isfinite(p.angular.cosOuter) && p.angular.cosOuter >= -1 &&
					 p.angular.cosInner <= 1 && p.angular.cosInner > p.angular.cosOuter,
				 Code::InvalidInput, "Ordered finite smooth-cone bounds required");
	}
	if (m.law == Law::DiffuseIntegral && (kind == EmitterKind::Rectangle || kind == EmitterKind::Tube))
		Need(p.angular.kind == AngularKind::Lambertian, Code::Unavailable,
			 "Finite surface requires explicit Lambertian emission");
	Need(p.source.state != HypothesisState::Resolved, Code::InvalidInput,
		 "Shape diagnostics do not publish resolved sources");
	if (m.law == Law::DiffuseIntegral && kind == EmitterKind::Point)
		Need(p.angular.kind == AngularKind::Isotropic, Code::Unavailable,
			 "Unknown point angular emission cannot default to isotropic");
}
Frame MakeFrame(V f, V preferred = V(1, 0, 0))
{
	f = Unit(f);
	V r = preferred - f * Dot(preferred, f);
	if (Length(r) < 1e-5)
		r = Cross(V(0, 1, 0), f);
	if (Length(r) < 1e-5)
		r = Cross(V(0, 0, 1), f);
	Frame out;
	out.known = true;
	out.forward = f.Float();
	out.right = Unit(r).Float();
	out.up = Cross(f, Unit(r)).Float();
	return out;
}
V Rotate(V v, V axis, double angle)
{
	axis = Unit(axis);
	return v * std::cos(angle) + Cross(axis, v) * std::sin(angle) +
		   axis * (Dot(axis, v) * (1 - std::cos(angle)));
}
void HashEmitter(GeometryKey &k, const Emitter &e)
{
	const auto &p = e.geometry;
	k.U(p.source.id);
	k.U(uint32_t(p.kind));
	k.V(p.position.world);
	k.V(p.frame.right);
	k.V(p.frame.up);
	k.V(p.frame.forward);
	k.U(p.geometry.known);
	k.F(p.geometry.halfWidthWorld);
	k.F(p.geometry.halfHeightWorld);
	k.F(p.geometry.halfLengthWorld);
	k.F(p.geometry.radiusWorld);
	k.U(uint32_t(p.angular.kind));
	k.F(p.angular.cosInner);
	k.F(p.angular.cosOuter);
	k.U(p.angular.twoSided);
	k.F(e.spotTargetDistanceWorld);
	k.F(e.spotRadiusWorld);
	k.U(e.emittingSurface);
}
void HashModel(GeometryKey &k, const Model &m)
{
	k.U(kVersion);
	k.U(uint32_t(m.law));
	k.U(uint32_t(m.output));
	k.Digest(m.profile.contentKey);
	k.F(m.units.worldUnitsPerMeter);
	k.F(m.units.physicalToScene);
	k.U(m.calibration.id);
	k.U(uint32_t(m.calibration.kind));
	k.U(m.calibration.provenance);
	Text(k, m.calibration.method);
	k.U(m.allowAssumed);
	k.U(m.allowFacetExpansion);
	k.U(m.linearRec709PrimariesAttested);
	k.U(m.rectangleDivisions);
	k.U(m.tubeSides);
	k.U(m.tubeSegments);
	k.F(m.historicalEmitterPushWorld);
	Text(k, m.provenance);
}
void HashSampling(GeometryKey &k, const Sampling &s)
{
	k.U(s.areaDivisions);
	k.U(s.tubeSides);
	k.U(s.tubeSegments);
	k.U(s.receiverSubdivisions);
	k.U(s.compareRefined);
	k.F(s.receiverOffsetWorld);
	k.F(s.endpointToleranceWorld);
	k.F(s.minimumDistanceWorld);
}
struct Node
{
	V p, n;
	double area = 1;
	bool surface = false;
};
std::vector<Node> Nodes(const Emitter &e, const Sampling &s, bool refined)
{
	const auto &p = e.geometry;
	unsigned multiplier = refined ? 2 : 1;
	std::vector<Node> v;
	if (p.kind == EmitterKind::Point || p.kind == EmitterKind::Spot)
	{
		Node n;
		n.p = V(p.position.world);
		v.push_back(n);
		return v;
	}
	if (p.kind == EmitterKind::Rectangle)
	{
		unsigned count = s.areaDivisions * multiplier;
		double area = 4. * p.geometry.halfWidthWorld * p.geometry.halfHeightWorld / (count * count);
		for (unsigned i = 0; i < count; ++i)
			for (unsigned j = 0; j < count; ++j)
			{
				Node n;
				n.surface = true;
				n.n = V(p.frame.forward);
				n.area = area;
				n.p = V(p.position.world) +
					  V(p.frame.right) * (2 * p.geometry.halfWidthWorld * ((i + .5) / count - .5)) +
					  V(p.frame.up) * (2 * p.geometry.halfHeightWorld * ((j + .5) / count - .5));
				v.push_back(n);
			}
	}
	else
	{
		unsigned around = s.tubeSides * multiplier, along = s.tubeSegments * multiplier;
		double area = 4 * Pi * p.geometry.radiusWorld * p.geometry.halfLengthWorld / (around * along);
		for (unsigned i = 0; i < around; ++i)
			for (unsigned j = 0; j < along; ++j)
			{
				double a = 2 * Pi * (i + .5) / around;
				Node n;
				n.surface = true;
				n.n = V(p.frame.up) * std::cos(a) + V(p.frame.forward) * std::sin(a);
				n.area = area;
				n.p = V(p.position.world) +
					  V(p.frame.right) * (2 * p.geometry.halfLengthWorld * ((j + .5) / along - .5)) +
					  n.n * p.geometry.radiusWorld;
				v.push_back(n);
			}
	}
	return v;
}
struct ReceiverNode
{
	V p, n;
	double area = 1;
};
void Pieces(V a, V b, V c, V na, V nb, V nc, unsigned depth, std::vector<ReceiverNode> &out)
{
	if (!depth)
	{
		ReceiverNode r;
		r.p = (a + b + c) / 3;
		r.n = Unit(na + nb + nc);
		r.area = Length(Cross(b - a, c - a)) * .5;
		Need(r.area > 1e-14 && Normal(r.n), Code::Unavailable, "Degenerate receiver footprint/normal");
		out.push_back(r);
		return;
	}
	V ab = (a + b) * .5, bc = (b + c) * .5, ca = (c + a) * .5, nab = Unit(na + nb), nbc = Unit(nb + nc),
	  nca = Unit(nc + na);
	Pieces(a, ab, ca, na, nab, nca, depth - 1, out);
	Pieces(ab, b, bc, nab, nb, nbc, depth - 1, out);
	Pieces(ca, bc, c, nca, nbc, nc, depth - 1, out);
	Pieces(ab, bc, ca, nab, nbc, nca, depth - 1, out);
}
std::vector<ReceiverNode> Receivers(const Bake::Receiver &r, unsigned subdivisions)
{
	std::vector<ReceiverNode> out;
	if (r.footprint.count)
	{
		Need(r.footprint.owner && r.footprint.data && r.footprint.count <= 64, Code::InvalidInput,
			 "Unowned/oversized receiver footprint");
		for (size_t i = 0; i < r.footprint.count; ++i)
		{
			const auto &t = r.footprint.data[i];
			for (int k = 0; k < 3; ++k)
				Need(Finite(t.world[k]) && Normal(t.normal[k]), Code::InvalidInput, "Footprint data");
			Pieces(V(t.world[0]), V(t.world[1]), V(t.world[2]), V(t.normal[0]), V(t.normal[1]),
				   V(t.normal[2]), subdivisions, out);
		}
		double area = 0;
		for (auto &p : out)
			area += p.area;
		for (auto &p : out)
			p.area /= area;
	}
	else
	{
		ReceiverNode n;
		n.p = V(r.world);
		n.n = V(r.shadingNormal);
		out.push_back(n);
	}
	return out;
}
struct Expanded
{
	std::shared_ptr<std::vector<Bake::SourceHypothesis>> sources =
		std::make_shared<std::vector<Bake::SourceHypothesis>>();
	std::vector<size_t> root;
};
Expanded Expand(const Model &model, const std::vector<Emitter> &emitters)
{
	Expanded x;
	auto add = [&](size_t root, Bake::SourceHypothesis s) {
		s.id = x.sources->size() + 1;
		s.amplitudeKnown = true;
		s.compilerRgbAmplitude = Vec3(1, 1, 1);
		x.sources->push_back(s);
		x.root.push_back(root);
	};
	for (size_t j = 0; j < emitters.size(); ++j)
	{
		const auto &e = emitters[j];
		const auto &p = e.geometry;
		Bake::SourceHypothesis base;
		base.position = p.position;
		base.orientation = p.frame;
		base.worldUnits = model.units;
		base.angular = p.angular;
		base.geometry = p.geometry;
		if (p.kind == EmitterKind::Point || p.kind == EmitterKind::Spot)
		{
			base.kind = p.kind == EmitterKind::Point ? Bake::SourceKind::Point : Bake::SourceKind::Spot;
			base.spotTargetDistanceWorld = e.spotTargetDistanceWorld;
			base.spotRadiusWorld = e.spotRadiusWorld;
			add(j, base);
			continue;
		}
		Need(model.allowFacetExpansion, Code::Unavailable,
			 "Historical finite emission requires an explicit polygon subdivision hypothesis");
		auto polygon = [&](V center, V right, V up, V normal, double hw, double hh) {
			auto vertices = std::make_shared<std::vector<Vec3>>();
			for (V v : {center - right * hw - up * hh, center + right * hw - up * hh,
						center + right * hw + up * hh, center - right * hw + up * hh})
				vertices->push_back(v.Float());
			auto s = base;
			s.kind = Bake::SourceKind::Polygon;
			s.position.world = (center + normal * model.historicalEmitterPushWorld).Float();
			s.orientation = MakeFrame(normal, right);
			s.polygon = Lease(vertices);
			add(j, s);
		};
		if (p.kind == EmitterKind::Rectangle)
		{
			unsigned n = model.rectangleDivisions;
			for (unsigned a = 0; a < n; ++a)
				for (unsigned b = 0; b < n; ++b)
				{
					V center = V(p.position.world) +
							   V(p.frame.right) * (2 * p.geometry.halfWidthWorld * ((a + .5) / n - .5)) +
							   V(p.frame.up) * (2 * p.geometry.halfHeightWorld * ((b + .5) / n - .5));
					polygon(center, V(p.frame.right), V(p.frame.up), V(p.frame.forward),
							p.geometry.halfWidthWorld / n, p.geometry.halfHeightWorld / n);
				}
		}
		else
		{
			unsigned around = model.tubeSides, along = model.tubeSegments;
			for (unsigned a = 0; a < around; ++a)
				for (unsigned b = 0; b < along; ++b)
				{
					double angle = 2 * Pi * (a + .5) / around;
					V normal = V(p.frame.up) * std::cos(angle) + V(p.frame.forward) * std::sin(angle);
					V tangent = Cross(normal, V(p.frame.right));
					V center = V(p.position.world) +
							   V(p.frame.right) * (2 * p.geometry.halfLengthWorld * ((b + .5) / along - .5)) +
							   normal * (p.geometry.radiusWorld * std::cos(Pi / around));
					polygon(center, V(p.frame.right), tangent, normal, p.geometry.halfLengthWorld / along,
							p.geometry.radiusWorld * std::sin(Pi / around));
				}
		}
	}
	return x;
}
} // namespace
struct Block::Data
{
	Model model;
	Sampling sampling;
	std::vector<Emitter> emitters;
	std::shared_ptr<std::vector<Bake::Receiver>> receivers;
	Bake::EvaluationContext context;
	Expanded expanded;
	std::shared_ptr<const Bake::PreparedFactors> factors;
	Bake::Recipe recipe;
	std::vector<double> coefficient, coarse;
	std::vector<bool> known;
	std::vector<uint32_t> issues;
	Disk::Digest key{};
	Work work;
	uint64_t bytes = 0;
};
const Disk::Digest &Block::Key() const { return data->key; }
uint64_t Block::Bytes() const { return data->bytes; }
const Work &Block::PreparationWork() const { return data->work; }
bool Block::Linear(Vec3 lo, Vec3 hi, size_t source) const
{
	if (source >= data->emitters.size() || !Positive(lo) || !Positive(hi))
		return false;
	if (data->model.law == Law::DiffuseIntegral)
		return true;
	for (size_t r = 0; r < data->receivers->size(); ++r)
		for (size_t j = 0; j < data->expanded.root.size(); ++j)
			if (data->expanded.root[j] == source)
			{
				Bake::LinearResponse v;
				auto e = Bake::ProveLinearResponse(
					data->recipe, (*data->expanded.sources)[j],
					data->factors->Records()[r * data->expanded.root.size() + j], lo, hi, v);
				if (!e || !v.proven)
					return false;
			}
	return true;
}
bool Block::AllLinear(const std::vector<Vec3> &lo, const std::vector<Vec3> &hi) const
{
	if (lo.size() != data->emitters.size() || hi.size() != lo.size())
		return false;
	for (size_t j = 0; j < lo.size(); ++j)
		if (!Linear(lo[j], hi[j], j))
			return false;
	if (data->model.output != Output::EncodedRgb8)
		return true;
	const auto &r = data->recipe;
	if (r.gamma != 1 || r.exposure != 0 || r.srgbOutput)
		return false;
	for (size_t i = 0; i < data->receivers->size(); ++i)
	{
		V upper(r.explicitSurfaceAmbient);
		for (size_t j = 0; j < hi.size(); ++j)
			upper = upper + V(hi[j]) * Coefficient(i, j);
		if (Peak(upper) > 255)
			return false;
	}
	return true;
}
double Block::Coefficient(size_t receiver, size_t source) const
{
	if (receiver >= data->receivers->size() || source >= data->emitters.size())
		return 0;
	return data->coefficient[receiver * data->emitters.size() + source];
}
Error Prepare(const Model &model, const std::vector<Emitter> &emitters, const Chunk<Bake::Receiver> &input,
			  const Bake::EvaluationContext &context, const Sampling &sampling, const ReadContext &read,
			  std::shared_ptr<const Block> &output)
{
	output.reset();
	try
	{
		Cancel(read);
		Need(!model.provenance.empty() && !Zero(context.geometryKey) && context.visibility,
			 Code::MissingGeometry, "Explicit model provenance and visibility generation required");
		Need(model.law <= Law::DiffuseIntegral && model.output <= Output::PhysicalLux &&
				 model.units.worldUnitsPerMeter > 0 && std::isfinite(model.units.worldUnitsPerMeter),
			 Code::InvalidInput, "Invalid model/geometry scale");
		Need(emitters.size() > 0 && emitters.size() <= 16 && input.count > 0 && input.count <= 4096 &&
				 input.owner && input.data,
			 Code::InvalidInput, "Emitter/receiver block bounds");
		Need(sampling.areaDivisions >= 1 && sampling.areaDivisions <= 32 && sampling.tubeSides >= 4 &&
				 sampling.tubeSides <= 64 && sampling.tubeSegments >= 1 && sampling.tubeSegments <= 32 &&
				 sampling.receiverSubdivisions <= 3 && std::isfinite(sampling.receiverOffsetWorld) &&
				 sampling.receiverOffsetWorld >= 0 && std::isfinite(sampling.endpointToleranceWorld) &&
				 sampling.endpointToleranceWorld >= 0 && std::isfinite(sampling.minimumDistanceWorld) &&
				 sampling.minimumDistanceWorld > 0,
			 Code::InvalidInput, "Invalid quadrature/trace tolerances");
		Need(model.rectangleDivisions >= 1 && model.rectangleDivisions <= 32 && model.tubeSides >= 4 &&
				 model.tubeSides <= 64 && model.tubeSegments >= 1 && model.tubeSegments <= 32 &&
				 std::isfinite(model.historicalEmitterPushWorld) && model.historicalEmitterPushWorld >= 0,
			 Code::InvalidInput, "Invalid historical expansion recipe");
		Need(model.law != Law::HistoricalBake || model.output != Output::PhysicalLux, Code::Unavailable,
			 "Historical Q has no implicit lux conversion");
		Need(model.law != Law::DiffuseIntegral || model.output != Output::EncodedRgb8, Code::Unavailable,
			 "Physical/relative integral has no implicit historical RGB8 encoding");
		if (model.output == Output::PhysicalLux)
			Need(model.calibration.id && model.calibration.kind == CalibrationKind::PhysicallyAuthored &&
					 model.linearRec709PrimariesAttested,
				 Code::Unavailable, "Physical output requires independently authored calibration");
		uint64_t cap = read.maxBytes ? std::min(read.maxBytes, sampling.maxBytes) : sampling.maxBytes;
		Need(cap >= 65536 && input.count * (sizeof(Bake::Receiver) + emitters.size() * 40ull) + 65536 <= cap,
			 Code::MemoryLimit, "Shape block allocation preflight");
		auto d = std::make_shared<Block::Data>();
		d->model = model;
		d->sampling = sampling;
		d->emitters = emitters;
		d->context = context;
		d->receivers = std::make_shared<std::vector<Bake::Receiver>>(input.data, input.data + input.count);
		std::set<Id> ids;
		GeometryKey key;
		HashModel(key, model);
		HashSampling(key, sampling);
		key.Digest(context.geometryKey);
		key.Digest(context.materialKey);
		key.Digest(context.observationPolicyKey);
		for (const auto &e : emitters)
		{
			Validate(model, e);
			Need(ids.insert(e.geometry.source.id).second, Code::InvalidInput, "Duplicate root emitter ID");
			HashEmitter(key, e);
		}
		for (const auto &r : *d->receivers)
		{
			Need(r.sample && Finite(r.world) && Normal(r.geometricNormal) && Normal(r.shadingNormal) &&
					 r.style == 0,
				 Code::InvalidInput, "Receiver geometry/style invalid");
			Need(r.geometryErrorWorld == 0, Code::Unavailable,
				 "Uncertain curved receiver is not a certified integral footprint");
			key.U(r.sample);
			key.U(r.surface);
			key.V(r.world);
			key.V(r.geometricNormal);
			key.V(r.shadingNormal);
			key.U(r.twoSided);
			key.U(r.footprint.count);
			if (r.footprint.count)
			{
				Need(r.footprint.owner && r.footprint.data && r.footprint.count <= 64, Code::InvalidInput,
					 "Invalid footprint lease");
				for (size_t f = 0; f < r.footprint.count; ++f)
					for (int k = 0; k < 3; ++k)
					{
						key.V(r.footprint.data[f].world[k]);
						key.V(r.footprint.data[f].normal[k]);
					}
			}
		}
		uint64_t expansionCount = 0;
		for (const auto &e : emitters)
			expansionCount += e.geometry.kind == EmitterKind::Rectangle
								  ? uint64_t(model.rectangleDivisions) * model.rectangleDivisions
								  : e.geometry.kind == EmitterKind::Tube
										? uint64_t(model.tubeSides) * model.tubeSegments
										: 1;
		if (model.law == Law::HistoricalBake)
			Need(expansionCount <= 4096 &&
					 expansionCount * input.count * (sizeof(Bake::Factor) + 64) + 65536 <= cap,
				 Code::MemoryLimit, "Historical expansion preflight");
		size_t pairs = emitters.size() * input.count;
		d->coefficient.assign(pairs, 0);
		d->coarse.assign(pairs, 0);
		d->known.assign(input.count, true);
		d->issues.assign(input.count, 0);
		d->work.blocks = 1;
		if (model.law == Law::HistoricalBake)
		{
			Check(Bake::DescribeRecipe(model.profile, d->recipe));
			d->expanded = Expand(model, emitters);
			Need(d->expanded.sources->size() <= 4096 &&
					 uint64_t(d->expanded.sources->size()) * input.count * (sizeof(Bake::Factor) + 64) +
							 65536 <=
						 cap,
				 Code::MemoryLimit, "Historical expanded block memory limit");
			Bake::Options o;
			o.allowAssumed = model.allowAssumed;
			o.output = Bake::Domain::CompilerLinearRelative255;
			o.maxRays = sampling.maxRays;
			o.maxWork = sampling.maxWork;
			Bake::CpuForwardEvaluator evaluator;
			Check(evaluator.Prepare(model.profile, Lease(d->expanded.sources), Lease(d->receivers), context,
									o, read, d->factors));
			const auto &w = d->factors->PreparationWork();
			d->work.rays = w.rays;
			d->work.traversal = w.nodeTests + w.triangleTests;
			for (size_t r = 0; r < input.count; ++r)
				for (size_t j = 0; j < d->expanded.root.size(); ++j)
				{
					const auto &f = d->factors->Records()[r * d->expanded.root.size() + j];
					d->known[r] = d->known[r] && f.visibility != VisibilityState::Unknown;
					d->issues[r] |= f.issues;
					// Geometry coefficient is independent of the active FAST cutoff;
					// use it only AFTER Linear() proves the caller's entire Q box.
					auto coefficientRecipe = d->recipe;
					coefficientRecipe.fast = false;
					Bake::LinearResponse v;
					Check(Bake::ProveLinearResponse(coefficientRecipe, (*d->expanded.sources)[j], f, Vec3(),
													Vec3(1, 1, 1), v));
					if (v.proven)
						d->coefficient[r * emitters.size() + d->expanded.root[j]] += v.coefficient;
				}
			d->bytes = 65536 + input.count * sizeof(Bake::Receiver) + pairs * 24 +
					   d->expanded.sources->size() * input.count * (sizeof(Bake::Factor) + 64);
		}
		else
		{
			double scale = model.units.worldUnitsPerMeter;
			double scale2 = scale * scale;
			for (size_t j = 0; j < emitters.size(); ++j)
				for (int level = 0; level < (sampling.compareRefined ? 2 : 1); ++level)
				{
					auto nodes = Nodes(emitters[j], sampling, level != 0);
					Need(nodes.size() <= 16384, Code::MemoryLimit, "Finite source node limit");
					d->work.nodes += nodes.size();
					for (size_t i = 0; i < input.count; ++i)
					{
						Cancel(read);
						const auto &r = (*d->receivers)[i];
						auto receivers = Receivers(r, sampling.receiverSubdivisions + (level ? 1 : 0));
						double factor = 0;
						for (const auto &receiver : receivers)
							for (const auto &n : nodes)
							{
								Cancel(read);
								V delta = n.p - receiver.p;
								double distance = Length(delta);
								Need(distance > sampling.minimumDistanceWorld, Code::Unavailable,
									 "Singular/near emitting surface sample");
								V incoming = delta / distance;
								double cosine = Dot(receiver.n, incoming);
								cosine = r.twoSided ? std::abs(cosine) : std::max(0., cosine);
								if (cosine <= 0)
									continue;
								double sourceCosine = n.surface ? (emitters[j].geometry.angular.twoSided
																	   ? std::abs(Dot(n.n, incoming))
																	   : std::max(0., -Dot(n.n, incoming)))
																: 1;
								if (sourceCosine <= 0)
									continue;
								double cone = 1;
								const auto &e = emitters[j].geometry;
								if (e.kind == EmitterKind::Spot)
								{
									double x =
										Clamp((-Dot(V(e.frame.forward), incoming) - e.angular.cosOuter) /
												  (e.angular.cosInner - e.angular.cosOuter),
											  0, 1);
									cone = x * x * (3 - 2 * x);
									if (cone <= 0)
										continue;
								}
								Need(d->work.rays < sampling.maxRays && d->work.traversal < sampling.maxWork,
									 Code::WorkLimit, "Finite source integration ray budget");
								RayQuery ray;
								ray.startWorld = (receiver.p + V(r.geometricNormal) *
																   (Dot(V(r.geometricNormal), incoming) >= 0
																		? sampling.receiverOffsetWorld
																		: -sampling.receiverOffsetWorld))
													 .Float();
								ray.endWorld = n.p.Float();
								ray.originSurface = r.surface;
								ray.targetSurface = emitters[j].emittingSurface;
								ray.targetToleranceWorld = sampling.endpointToleranceWorld;
								ray.bias.absoluteWorld = ray.bias.relative = 0;
								ray.maxWork = sampling.maxWork - d->work.traversal;
								RayResult hit;
								Check(context.visibility->Query(ray, read, hit));
								++d->work.rays;
								d->work.traversal += hit.nodeTests + hit.primitiveTests;
								d->issues[i] |= hit.issues;
								if (hit.state == VisibilityState::Unknown ||
									hit.reliability == RayReliability::Assumed ||
									(hit.issues & PolicyAssumption))
									d->known[i] = false;
								if (hit.state != VisibilityState::Visible)
									continue;
								factor += receiver.area * cosine * sourceCosine * cone /
										  (distance * distance) * (n.surface ? n.area : scale2);
							}
						d->coarse[i * emitters.size() + j] =
							level ? d->coarse[i * emitters.size() + j] : factor;
						d->coefficient[i * emitters.size() + j] = factor;
					}
				}
			d->bytes = 65536 + input.count * sizeof(Bake::Receiver) + pairs * 24 + 16384 * sizeof(Node);
		}
		Need(d->bytes <= cap, Code::MemoryLimit, "Shape block memory limit");
		Budget(d->work, sampling);
		d->key = key.Finish();
		auto block = std::make_shared<Block>();
		block->data = d;
		output = block;
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Shape preparation allocation");
	}
}
Error Block::Evaluate(const std::vector<Vec3> &rgb, const ReadContext &read, std::vector<Prediction> &output,
					  Work &work) const
{
	output.clear();
	work = {};
	try
	{
		Cancel(read);
		Need(rgb.size() == data->emitters.size(), Code::InvalidInput, "Wrong amplitude root count");
		for (auto q : rgb)
			Need(Positive(q), Code::InvalidInput, "Nonnegative finite RGB required");
		std::vector<Prediction> result(data->receivers->size());
		work.values = 1;
		if (data->model.law == Law::HistoricalBake)
		{
			auto sources = std::make_shared<std::vector<Bake::SourceHypothesis>>(*data->expanded.sources);
			for (size_t j = 0; j < sources->size(); ++j)
				(*sources)[j].compilerRgbAmplitude = rgb[data->expanded.root[j]];
			Bake::Options o;
			o.output = data->model.output == Output::EncodedRgb8 ? Bake::Domain::EncodedRgb8BeforeQuantization
																 : Bake::Domain::CompilerLinearRelative255;
			o.allowAssumed = data->model.allowAssumed;
			o.factors = data->factors;
			o.maxPairs = uint64_t(sources->size()) * result.size() + 1;
			o.maxRays = data->sampling.maxRays;
			o.maxWork = data->sampling.maxWork;
			// Encoding is a post-accumulation stage: factors were prepared in pre-encoding surface domain.
			o.output = Bake::Domain::CompilerLinearRelative255;
			Bake::Result prediction;
			Bake::CpuForwardEvaluator f;
			Check(f.Evaluate(data->model.profile, Lease(sources), Lease(data->receivers), Decode::Profile(),
							 data->context, o, read, prediction));
			work.pairs = prediction.work.pairs;
			work.rays = prediction.work.rays;
			work.traversal = prediction.work.nodeTests + prediction.work.triangleTests;
			for (size_t i = 0; i < result.size(); ++i)
			{
				const auto &p = prediction.predictions.data[i];
				auto &q = result[i];
				q.observation = (*data->receivers)[i].sample;
				q.available = p.available;
				q.visibilityIssues = p.visibilityIssues;
				q.value = p.compilerRelative;
				if (q.available && data->model.output == Output::EncodedRgb8)
				{
					uint8_t bytes[3];
					Check(Bake::Encode(data->recipe, q.value, q.value, bytes));
				}
			}
		}
		else
			for (size_t i = 0; i < result.size(); ++i)
			{
				Cancel(read);
				auto &p = result[i];
				p.observation = (*data->receivers)[i].sample;
				p.available = data->known[i];
				p.visibilityIssues = data->issues[i];
				p.conditional = data->model.output != Output::PhysicalLux;
				V value, coarse;
				for (size_t j = 0; j < rgb.size(); ++j)
				{
					value = value + V(rgb[j]) * data->coefficient[i * rgb.size() + j];
					coarse = coarse + V(rgb[j]) * data->coarse[i * rgb.size() + j];
					++work.pairs;
				}
				Need(Positive(value) && Peak(value) <= std::numeric_limits<float>::max(),
					 Code::AlgorithmError, "Finite-source accumulation overflow");
				p.value = value.Float();
				p.quadratureDifference = Peak(V(std::abs(value.x - coarse.x), std::abs(value.y - coarse.y),
												std::abs(value.z - coarse.z)));
			}
		output = std::move(result);
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Shape prediction allocation");
	}
}
namespace
{
double Residual(double p, const Amplitude::Interval &x)
{
	return p < x.lower ? p - x.lower : p > x.upper ? p - x.upper : 0;
}
double Huber(double e, double d)
{
	double a = std::abs(e);
	return a <= d ? .5 * e * e : d * (a - .5 * d);
}
bool Inside(const Bounds &b, V p)
{
	if (!b.known || !Finite(b.minWorld) || !Finite(b.maxWorld))
		return false;
	for (int k = 0; k < 3; ++k)
		if (p[k] < V(b.minWorld)[k] || p[k] > V(b.maxWorld)[k])
			return false;
	return true;
}
void ValidateObservation(const Observation &a, Role role)
{
	Need(a.id && a.receiver.sample == a.id && a.spatialBlock && a.role == role && a.comparable &&
			 std::isfinite(a.area) && a.area > 0 && std::isfinite(a.sigma) && a.sigma > 0,
		 Code::Unavailable, "Incomparable/missing observation or partition role mismatch");
	bool present = false;
	for (const auto &c : a.channel)
		if (c.present)
		{
			present = true;
			Need(std::isfinite(c.lower) && c.lower >= 0 && !std::isnan(c.upper) && c.upper >= c.lower &&
					 (std::isfinite(c.upper) || c.censored),
				 Code::InvalidInput, "Invalid quantization/censoring interval");
		}
	Need(present, Code::Unavailable, "Missing all colour channels");
}
void HashObservation(GeometryKey &key, const Observation &a)
{
	key.U(a.id);
	key.U(a.spatialBlock);
	key.V(a.receiver.world);
	key.V(a.receiver.geometricNormal);
	key.V(a.receiver.shadingNormal);
	key.U(a.receiver.surface);
	key.U(a.receiver.twoSided);
	key.F(a.area);
	key.F(a.sigma);
	key.U(uint32_t(a.role));
	key.U(a.receiver.footprint.count);
	for (size_t f = 0; f < a.receiver.footprint.count; ++f)
		for (int k = 0; k < 3; ++k)
		{
			key.V(a.receiver.footprint.data[f].world[k]);
			key.V(a.receiver.footprint.data[f].normal[k]);
		}
	for (const auto &c : a.channel)
	{
		key.U(c.present);
		key.F(c.lower);
		if (std::isfinite(c.upper))
			key.F(c.upper);
		else
			key.U(UINT64_MAX);
		key.U(c.censored);
	}
}
struct Param
{
	size_t source;
	int kind, axis;
	double lo, hi, step;
}; // xyz, local rotation, dimensions, cone
struct State
{
	std::vector<double> x;
	std::vector<Emitter> emitters;
	std::vector<Vec3> q;
	double data = 0, prior = 0, kkt = 0, shadow = 0;
	FitEvidence::Value shadowValue;
	bool linear = false, qConverged = false;
	double Total() const { return data + prior + shadow; }
};
struct Engine
{
	const Request &r;
	const ReadContext &read;
	FitResult out;
	std::vector<Candidate> candidates;
	std::vector<Param> params;
	std::shared_ptr<const PointRefine::PlacementProvider> placement;
	double area = 0;
	bool positive = false;
	std::map<Disk::Digest, std::shared_ptr<const Block>> cache;
	uint64_t cacheBytes = 0;
	Disk::Digest identity{};
	size_t sweep = 0;
	Engine(const Request &request, const ReadContext &context)
		: r(request), read(context), candidates(request.candidates)
	{
		const auto &o = r.options;
		Cancel(read);
		Need(std::isfinite(r.shadowWeight) && r.shadowWeight >= 0, Code::InvalidInput,
			 "Invalid shadow weight");
		if (r.shadowWeight > 0)
			Need(r.shadow && r.shadow->MapKey() == r.mapKey &&
					 r.shadow->GeometryKey() == r.geometry.geometryKey,
				 Code::MissingGeometry, "Shadow evidence has a stale map/geometry generation");
		Need(r.training && r.count && r.count <= o.maxRows && r.count <= 1000000 && o.batchRows &&
				 o.batchRows <= 512 && !Zero(r.mapKey) && !Zero(r.observationsKey) && candidates.size() > 0 &&
				 candidates.size() <= 8 && o.maxTrials && o.maxTrials <= 20000 && o.maxSweeps &&
				 o.maxSweeps <= 2000 && o.maxAmplitudeIterations && o.maxAmplitudeIterations <= 1000,
			 Code::InvalidInput, "Shape fit stream/count/work limits");
		for (double value : {o.positionStepWorld, o.rotationStepRadians, o.logSizeStep, o.coneStep,
							 o.minimumStepFraction, o.kktTolerance, o.huber})
			Need(std::isfinite(value) && value > 0, Code::InvalidInput, "Shape search/loss tolerance");
		Need(std::isfinite(o.relativeImprovement) && o.relativeImprovement >= 0 && std::isfinite(o.ridge) &&
				 o.ridge >= 0,
			 Code::InvalidInput, "Shape convergence/prior policy");
		uint64_t bytes = uint64_t(o.maxTrials) * sizeof(Iteration) + o.maxCacheBytes + 2ull * 1024 * 1024 +
						 std::max(r.searchSampling.maxBytes, r.strictSampling.maxBytes);
		Need(bytes <= o.maxBytes && (!read.maxBytes || bytes <= read.maxBytes), Code::MemoryLimit,
			 "Shape fit scratch/trace/cache preflight");
		out.accountedBytes = bytes;
		Need(r.placement.map.sha256 == r.mapKey && r.placement.geometry &&
				 r.placement.geometry->CertificationKey() == r.geometry.geometryKey,
			 Code::MissingGeometry, "Shape fitting requires matching BSP/visual placement generation");
		Check(PointRefine::MakePlacement(r.placement, read, placement));
		std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
			return a.initial.geometry.source.id < b.initial.geometry.source.id;
		});
		Id last = 0;
		auto add = [&](size_t source, int kind, int axis, double lo, double hi, double step) {
			Need(std::isfinite(lo) && std::isfinite(hi) && lo <= 0 && hi >= 0, Code::InvalidInput,
				 "Initial geometry outside parameter bounds");
			if (hi > lo)
			{
				Param p;
				p.source = source;
				p.kind = kind;
				p.axis = axis;
				p.lo = lo;
				p.hi = hi;
				p.step = step;
				params.push_back(p);
			}
		};
		for (size_t i = 0; i < candidates.size(); ++i)
		{
			auto &c = candidates[i];
			auto &e = c.initial;
			Validate(r.model, e);
			Need(e.geometry.source.id > last, Code::InvalidInput, "Duplicate candidate IDs");
			last = e.geometry.source.id;
			Need(Inside(c.centreBounds, V(e.geometry.position.world)) && Positive(c.lowerRgb) &&
					 Positive(c.upperRgb) && Positive(e.rgbAmplitude),
				 Code::InvalidInput, "Candidate bounds/seed invalid");
			for (int k = 0; k < 3; ++k)
				Need(V(c.lowerRgb)[k] <= V(e.rgbAmplitude)[k] && V(e.rgbAmplitude)[k] <= V(c.upperRgb)[k],
					 Code::InvalidInput, "RGB seed outside admissible bounds");
			for (double v :
				 {c.maximumTranslationWorld, c.maximumRotationRadians, c.minimumDimensionWorld,
				  c.maximumDimensionWorld, c.positionSigmaWorld, c.angleSigmaRadians, c.logSizeSigma})
				Need(std::isfinite(v) && v > 0, Code::InvalidInput,
					 "Positive candidate geometry bounds/scales required");
			Need(c.minimumDimensionWorld <= c.maximumDimensionWorld && c.maximumRotationRadians <= Pi &&
					 c.maximumTranslationWorld <= 1e6 && c.maximumDimensionWorld <= 1e6 &&
					 std::isfinite(c.geometryPriorWeight) && c.geometryPriorWeight >= 0 &&
					 !c.provenance.empty(),
				 Code::InvalidInput, "Candidate prior provenance/limits");
			if (c.evidence == ShapeEvidence::Authored || c.evidence == ShapeEvidence::VisibleFixture)
				Need(c.geometryPriorWeight > 0, Code::InvalidInput,
					 "Visible/authored geometry requires an explicit strong prior, not unrestricted error "
					 "fitting");
			if (c.fitPosition)
				for (int k = 0; k < 3; ++k)
					add(i, 0, k,
						std::max(double(V(c.centreBounds.minWorld)[k]) - V(e.geometry.position.world)[k],
								 -c.maximumTranslationWorld),
						std::min(double(V(c.centreBounds.maxWorld)[k]) - V(e.geometry.position.world)[k],
								 c.maximumTranslationWorld),
						o.positionStepWorld);
			if (c.fitFrame && e.geometry.kind != EmitterKind::Point)
				for (int k = 0; k < (e.geometry.kind == EmitterKind::Rectangle ? 3 : 2); ++k)
					add(i, 1, k, -c.maximumRotationRadians, c.maximumRotationRadians, o.rotationStepRadians);
			if (c.fitDimensions &&
				(e.geometry.kind == EmitterKind::Rectangle || e.geometry.kind == EmitterKind::Tube))
				for (int k = 0; k < 2; ++k)
				{
					double value =
						e.geometry.kind == EmitterKind::Rectangle
							? (k ? e.geometry.geometry.halfHeightWorld : e.geometry.geometry.halfWidthWorld)
							: (k ? e.geometry.geometry.radiusWorld : e.geometry.geometry.halfLengthWorld);
					add(i, 2, k, std::log(c.minimumDimensionWorld / value),
						std::log(c.maximumDimensionWorld / value), o.logSizeStep);
				}
			if (c.fitCone && e.geometry.kind == EmitterKind::Spot)
			{
				Need(std::isfinite(c.minimumOuterRadians) && std::isfinite(c.maximumOuterRadians) &&
						 c.minimumOuterRadians > 0 && c.minimumOuterRadians < c.maximumOuterRadians &&
						 c.maximumOuterRadians < Pi / 2,
					 Code::InvalidInput, "Spot half-angle search bounds");
				if (r.model.law == Law::HistoricalBake)
				{
					double a = std::atan((e.spotRadiusWorld + 16) / e.spotTargetDistanceWorld);
					add(i, 3, 0, c.minimumOuterRadians - a, c.maximumOuterRadians - a, o.coneStep);
				}
				else
				{
					double a = std::acos(e.geometry.angular.cosOuter),
						   inner = std::acos(e.geometry.angular.cosInner);
					add(i, 3, 0, c.minimumOuterRadians - a, c.maximumOuterRadians - a, o.coneStep);
					double fraction = inner / a;
					add(i, 3, 1, .02 - fraction, .98 - fraction, o.coneStep);
				}
			}
		}
		Need(params.size() <= 96, Code::MemoryLimit, "Geometry parameter cap");
		out.parameters = params.size() + candidates.size() * 3;
		GeometryKey key;
		key.Digest(r.mapKey);
		key.Digest(r.observationsKey);
		HashModel(key, r.model);
		Id previous = 0;
		for (uint64_t first = 0; first < r.count; first += o.batchRows)
		{
			auto rows = Read(first);
			for (size_t j = 0; j < rows.count; ++j)
			{
				const auto &a = rows.data[j];
				ValidateObservation(a, Role::Training);
				Need(a.id > previous, Code::InvalidInput, "Training observation order/duplicates");
				previous = a.id;
				HashObservation(key, a);
				area += a.area;
				for (const auto &c : a.channel)
					positive |= c.present && c.lower > 0;
			}
		}
		Need(std::isfinite(area) && area > 0, Code::InvalidInput, "Observation area overflow");
		identity = key.Finish();
		out.trainingIdentity = identity;
	}
	Chunk<Observation> Read(uint64_t first)
	{
		Cancel(read);
		Chunk<Observation> b;
		size_t n = size_t(std::min<uint64_t>(r.options.batchRows, r.count - first));
		Check(r.training->Read(first, n, read, b));
		Need(b.owner && b.data && b.count == n, Code::ProviderError, "Truncated/unowned training block");
		return b;
	}
	void Limit()
	{
		Cancel(read);
		Need(out.work.rays <= r.options.maxRays && out.work.traversal <= r.options.maxWork &&
				 out.work.values <= r.options.maxValues,
			 Code::WorkLimit, "Shape fitting cumulative work limit");
	}
	std::vector<Emitter> Decode(const std::vector<double> &x)
	{
		std::vector<Emitter> result;
		for (const auto &c : candidates)
			result.push_back(c.initial);
		std::vector<V> rotation(result.size());
		std::vector<double> outer(result.size()), fraction(result.size());
		for (size_t i = 0; i < result.size(); ++i)
			if (result[i].geometry.kind == EmitterKind::Spot)
			{
				outer[i] =
					r.model.law == Law::HistoricalBake
						? std::atan((result[i].spotRadiusWorld + 16) / result[i].spotTargetDistanceWorld)
						: std::acos(result[i].geometry.angular.cosOuter);
				fraction[i] = r.model.law == Law::HistoricalBake
								  ? 0
								  : std::acos(result[i].geometry.angular.cosInner) / outer[i];
			}
		for (size_t j = 0; j < x.size(); ++j)
		{
			const auto &p = params[j];
			auto &e = result[p.source];
			if (p.kind == 0)
			{
				V v(e.geometry.position.world);
				v[p.axis] += x[j];
				e.geometry.position.world = v.Float();
			}
			else if (p.kind == 1)
				rotation[p.source][p.axis] = x[j];
			else if (p.kind == 2)
			{
				float *value =
					e.geometry.kind == EmitterKind::Rectangle
						? (p.axis ? &e.geometry.geometry.halfHeightWorld
								  : &e.geometry.geometry.halfWidthWorld)
						: (p.axis ? &e.geometry.geometry.radiusWorld : &e.geometry.geometry.halfLengthWorld);
				*value = float(*value * std::exp(x[j]));
			}
			else if (p.axis == 0)
				outer[p.source] += x[j];
			else
				fraction[p.source] += x[j];
		}
		for (size_t i = 0; i < result.size(); ++i)
		{
			auto &e = result[i];
			V local = rotation[i];
			V axis = V(candidates[i].initial.geometry.frame.right) * local.x +
					 V(candidates[i].initial.geometry.frame.up) * local.y +
					 V(candidates[i].initial.geometry.frame.forward) * local.z;
			if (e.geometry.kind == EmitterKind::Tube)
				axis = V(candidates[i].initial.geometry.frame.up) * local.x +
					   V(candidates[i].initial.geometry.frame.forward) * local.y;
			double angle = Length(axis);
			if (angle > 1e-10)
			{
				e.geometry.frame.right = Rotate(V(e.geometry.frame.right), axis, angle).Float();
				e.geometry.frame.up = Rotate(V(e.geometry.frame.up), axis, angle).Float();
				e.geometry.frame.forward = Rotate(V(e.geometry.frame.forward), axis, angle).Float();
			}
			if (e.geometry.kind == EmitterKind::Spot)
			{
				if (r.model.law == Law::HistoricalBake)
					e.spotRadiusWorld = std::max(0., e.spotTargetDistanceWorld * std::tan(outer[i]) - 16);
				else
				{
					e.geometry.angular.cosOuter = float(std::cos(outer[i]));
					e.geometry.angular.cosInner = float(std::cos(outer[i] * fraction[i]));
				}
			}
		}
		return result;
	}
	double Prior(const std::vector<double> &x)
	{
		double v = 0;
		for (size_t j = 0; j < x.size(); ++j)
		{
			const auto &p = params[j];
			const auto &c = candidates[p.source];
			double sigma = p.kind == 0 ? c.positionSigmaWorld
									   : p.kind == 1 || p.kind == 3 ? c.angleSigmaRadians : c.logSizeSigma;
			v += .5 * c.geometryPriorWeight * x[j] * x[j] / (sigma * sigma);
		}
		return v;
	}
	bool Place(const std::vector<Emitter> &from, const std::vector<Emitter> &to)
	{
		for (size_t i = 0; i < to.size(); ++i)
		{
			const auto &e = to[i].geometry;
			const auto &c = candidates[i];
			if (!Inside(c.centreBounds, V(e.position.world)) ||
				Length(V(e.position.world) - V(c.initial.geometry.position.world)) >
					c.maximumTranslationWorld + 1e-4)
			{
				++out.rejectedGeometry;
				return false;
			}
			double cosine = (Dot(V(c.initial.geometry.frame.right), V(e.frame.right)) +
							 Dot(V(c.initial.geometry.frame.up), V(e.frame.up)) +
							 Dot(V(c.initial.geometry.frame.forward), V(e.frame.forward)) - 1) *
							.5;
			double angle = std::acos(Clamp(cosine, -1, 1));
			if (e.kind != EmitterKind::Point && angle > c.maximumRotationRadians + 1e-5)
			{
				++out.rejectedGeometry;
				return false;
			}
			const auto &before = from[i].geometry;
			auto radius = [](const PhysicalEmitter &p) {
				return p.kind == EmitterKind::Rectangle
						   ? std::hypot(p.geometry.halfWidthWorld, p.geometry.halfHeightWorld)
						   : p.kind == EmitterKind::Tube
								 ? std::hypot(p.geometry.halfLengthWorld, p.geometry.radiusWorld)
								 : 0.;
			};
			double relativeCos =
				(Dot(V(before.frame.right), V(e.frame.right)) + Dot(V(before.frame.up), V(e.frame.up)) +
				 Dot(V(before.frame.forward), V(e.frame.forward)) - 1) *
				.5;
			double relativeAngle = std::acos(Clamp(relativeCos, -1, 1));
			// Union of local support cubes covers the ENTIRE emitter. Rotation
			// arcs are conservatively inflated by 2*R*sin(theta/2), including
			// varying dimensions. This is a volume proof, not node sampling.
			unsigned nx = e.kind == EmitterKind::Rectangle ? 8 : e.kind == EmitterKind::Tube ? 32 : 1,
					 ny = e.kind == EmitterKind::Rectangle ? 8 : 1;
			double a = e.kind == EmitterKind::Rectangle
						   ? std::max(before.geometry.halfWidthWorld, e.geometry.halfWidthWorld) / nx
						   : e.kind == EmitterKind::Tube
								 ? std::max(before.geometry.halfLengthWorld, e.geometry.halfLengthWorld) / nx
								 : 0;
			double b = e.kind == EmitterKind::Rectangle
						   ? std::max(before.geometry.halfHeightWorld, e.geometry.halfHeightWorld) / ny
						   : e.kind == EmitterKind::Tube
								 ? std::max(before.geometry.radiusWorld, e.geometry.radiusWorld)
								 : 0;
			double support = std::hypot(a, b) +
							 2 * std::max(radius(before), radius(e)) * std::sin(relativeAngle * .5) + .001;
			for (unsigned x = 0; x < nx; ++x)
				for (unsigned y = 0; y < ny; ++y)
				{
					auto centre = [&](const PhysicalEmitter &p) {
						V v(p.position.world);
						if (p.kind == EmitterKind::Rectangle)
							v = v +
								V(p.frame.right) * (2 * p.geometry.halfWidthWorld * ((x + .5) / nx - .5)) +
								V(p.frame.up) * (2 * p.geometry.halfHeightWorld * ((y + .5) / ny - .5));
						if (p.kind == EmitterKind::Tube)
							v = v +
								V(p.frame.right) * (2 * p.geometry.halfLengthWorld * ((x + .5) / nx - .5));
						return v.Float();
					};
					PointRefine::PlacementResult p;
					Check(placement->Check(centre(before), centre(e), support, read, p));
					out.work.rays += p.rays;
					out.work.traversal += p.work;
					Limit();
					if (p.state != PointRefine::Constraint::Free)
					{
						++out.rejectedGeometry;
						return false;
					}
				}
		}
		return true;
	}
	std::shared_ptr<const Block> Get(const std::vector<Emitter> &e, const Chunk<Bake::Receiver> &receivers,
									 const Sampling &sampling)
	{
		GeometryKey key;
		HashSampling(key, sampling);
		HashModel(key, r.model);
		for (const auto &x : e)
			HashEmitter(key, x);
		for (size_t i = 0; i < receivers.count; ++i)
			key.U(receivers.data[i].sample);
		auto id = key.Finish();
		auto found = cache.find(id);
		if (found != cache.end())
			return found->second;
		std::shared_ptr<const Block> block;
		Check(Prepare(r.model, e, receivers, r.geometry, sampling, read, block));
		Add(out.work, block->PreparationWork());
		Limit();
		if (block->Bytes() <= r.options.maxCacheBytes)
		{
			if (cacheBytes > r.options.maxCacheBytes - block->Bytes())
			{
				cache.clear();
				cacheBytes = 0;
			}
			cache[id] = block;
			cacheBytes += block->Bytes();
		}
		return block;
	}
	double Value(const std::vector<Emitter> &e, const std::vector<Vec3> &q, const Sampling &sampling,
				 bool *linear = nullptr, size_t derivative = SIZE_MAX, double *gradient = nullptr,
				 double *curvature = nullptr, std::vector<V> *values = nullptr)
	{
		Limit();
		double f = 0;
		if (linear)
			*linear = true;
		if (gradient)
			*gradient = 0;
		if (curvature)
			*curvature = 0;
		std::vector<Vec3> lo, hi;
		for (const auto &c : candidates)
		{
			lo.push_back(c.lowerRgb);
			hi.push_back(c.upperRgb);
		}
		for (uint64_t first = 0; first < r.count; first += r.options.batchRows)
		{
			auto rows = Read(first);
			auto receivers = std::make_shared<std::vector<Bake::Receiver>>();
			for (size_t j = 0; j < rows.count; ++j)
				receivers->push_back(rows.data[j].receiver);
			auto block = Get(e, Lease(receivers), sampling);
			bool admissible = block->AllLinear(lo, hi);
			if (linear)
				*linear &= admissible;
			Need(derivative == SIZE_MAX || admissible, Code::InvalidState,
				 "Linear coordinate solve requested on nonlinear branch");
			std::vector<Prediction> predicted;
			Work w;
			Check(block->Evaluate(q, read, predicted, w));
			Add(out.work, w);
			Limit();
			for (size_t j = 0; j < rows.count; ++j)
			{
				const auto &a = rows.data[j];
				Need(predicted[j].available, Code::Unavailable,
					 "Unknown visibility cannot be discarded to improve shape fit");
				if (values)
					values->push_back(V(predicted[j].value));
				double weight = a.area / area;
				for (size_t c = 0; c < 3; ++c)
					if (a.channel[c].present)
					{
						double error = Residual(V(predicted[j].value)[c], a.channel[c]) / a.sigma;
						f += weight * Huber(error, r.options.huber);
						if (derivative != SIZE_MAX && c == derivative % 3)
						{
							double g = block->Coefficient(j, derivative / 3) / a.sigma;
							if (r.model.output == Output::EncodedRgb8)
							{
								Bake::Recipe recipe;
								Check(Bake::DescribeRecipe(r.model.profile, recipe));
								g /= recipe.compensate;
							}
							*gradient += weight * g * Clamp(error, -r.options.huber, r.options.huber);
							*curvature += weight * g * g;
						}
					}
			}
		}
		for (size_t j = 0; j < q.size(); ++j)
			for (int k = 0; k < 3; ++k)
			{
				double value = V(q[j])[k];
				f += .5 * r.options.ridge * value * value;
				if (derivative == j * 3 + size_t(k))
				{
					*gradient += r.options.ridge * value;
					*curvature += r.options.ridge;
				}
			}
		Need(std::isfinite(f), Code::AlgorithmError, "Nonfinite shape objective");
		return f;
	}
	bool Intensities(State &s)
	{
		s.data = Value(s.emitters, s.q, r.searchSampling, &s.linear);
		s.qConverged = false;
		for (size_t iteration = 0; iteration < r.options.maxAmplitudeIterations; ++iteration)
		{
			double before = s.data;
			double motion = 0, maxKkt = 0;
			for (size_t j = 0; j < s.q.size(); ++j)
				for (size_t k = 0; k < 3; ++k)
				{
					double lo = V(candidates[j].lowerRgb)[k], hi = V(candidates[j].upperRgb)[k],
						   old = V(s.q[j])[k];
					if (lo == hi)
						continue;
					double chosen = old, best = s.data;
					auto test = [&](double value) {
						auto q = s.q;
						V v = q[j];
						v[k] = value;
						q[j] = v.Float();
						double f = Value(s.emitters, q, r.searchSampling);
						if (f < best)
						{
							chosen = value;
							best = f;
						}
						return f;
					};
					if (s.linear)
					{
						double g = 0, h = 0;
						Value(s.emitters, s.q, r.searchSampling, nullptr, j * 3 + k, &g, &h);
						double pg =
							old <= lo + 1e-6 ? std::min(0., g) : old >= hi - 1e-6 ? std::max(0., g) : g;
						maxKkt = std::max(maxKkt, std::abs(pg));
						if (h > 1e-30)
							test(Clamp(old - g / h, lo, hi));
					}
					else
					{
						std::vector<double> knots = {lo, hi, old};
						for (double f : {.025, .08, .16, .32, .64})
							knots.push_back(lo + f * (hi - lo));
						std::sort(knots.begin(), knots.end());
						for (double v : knots)
							test(v);
						auto it = std::lower_bound(knots.begin(), knots.end(), chosen);
						size_t at = size_t(it - knots.begin());
						double a = knots[at ? at - 1 : 0], b = knots[std::min(at + 1, knots.size() - 1)];
						double x = b - .6180339887498948 * (b - a), y = a + .6180339887498948 * (b - a),
							   fx = test(x), fy = test(y);
						for (unsigned step = 0; step < 18; ++step)
						{
							if (fx <= fy)
							{
								b = y;
								y = x;
								fy = fx;
								x = b - .6180339887498948 * (b - a);
								fx = test(x);
							}
							else
							{
								a = x;
								x = y;
								fx = fy;
								y = a + .6180339887498948 * (b - a);
								fy = test(y);
							}
						}
					}
					V q = s.q[j];
					q[k] = chosen;
					s.q[j] = q.Float();
					s.data = best;
					motion = std::max(motion, std::abs(old - chosen));
				}
			s.kkt = maxKkt;
			if (s.linear && maxKkt < r.options.kktTolerance)
			{
				s.qConverged = true;
				break;
			}
			if (motion <= 1e-4 && before - s.data <= 1e-10 * std::max(1., before))
			{
				s.qConverged = true;
				break;
			}
		}
		// Re-evaluate at rounded canonical RGB values, not the unrounded line-search value.
		s.data = Value(s.emitters, s.q, r.searchSampling);
		for (size_t i = 0; i < s.emitters.size(); ++i)
			s.emitters[i].rgbAmplitude = s.q[i];
		return true;
	}
	bool Trial(const State *from, const std::vector<double> &x, State &s)
	{
		Cancel(read);
		Need(out.trials < r.options.maxTrials, Code::WorkLimit, "Geometry trial budget exhausted");
		++out.trials;
		s.x = x;
		s.emitters = Decode(x);
		std::vector<Emitter> start;
		for (auto &c : candidates)
			start.push_back(c.initial);
		if (!Place(from ? from->emitters : start, s.emitters))
			return false;
		s.q = from ? from->q : std::vector<Vec3>();
		if (!from)
			for (const auto &c : candidates)
				s.q.push_back(c.initial.rgbAmplitude);
		try
		{
			Intensities(s);
		}
		catch (const Error &e)
		{
			if (from && e.code == Code::Unavailable)
			{
				++out.rejectedCoverage;
				return false;
			}
			throw;
		}
		s.prior = Prior(x);
		if (r.shadowWeight > 0)
		{
			std::vector<FitEvidence::Source> sources;
			for (const auto &e : s.emitters)
			{
				FitEvidence::Source source;
				source.geometry = e.geometry;
				source.rgb = e.rgbAmplitude;
				source.legacySpotDistance = e.spotTargetDistanceWorld;
				source.legacySpotRadius = e.spotRadiusWorld;
				source.emittingSurface = e.emittingSurface;
				sources.push_back(source);
			}
			Check(r.shadow->Evaluate(sources, read, s.shadowValue));
			out.work.rays += s.shadowValue.rays;
			out.work.traversal += s.shadowValue.work;
			Limit();
			Need(s.shadowValue.available && std::isfinite(s.shadowValue.loss) && s.shadowValue.loss >= 0,
				 Code::Unavailable, "Shadow term unavailable");
			s.shadow = r.shadowWeight * s.shadowValue.loss;
		}
		Iteration t;
		t.trial = out.trials;
		t.sweep = sweep;
		t.objective = s.data;
		t.prior = s.prior;
		t.shadow = s.shadow;
		out.trace.push_back(t);
		return true;
	}
	// Small bounded joint least-squares PROPOSAL, not an advertised derivative.
	// Strict trials still refit amplitudes and may reject any visibility change.
	bool JointProposal(State &current)
	{
		size_t nx = params.size(), n = nx + 3 * current.q.size();
		if (!nx || n > 24 || r.count > 128)
			return false;
		std::vector<V> centre;
		Value(current.emitters, current.q, r.searchSampling, nullptr, SIZE_MAX, nullptr, nullptr, &centre);
		std::vector<double> jac(n * r.count * 3), scale(n, 1);
		for (size_t k = 0; k < n; ++k)
		{
			State a = current, b = current;
			double span;
			if (k < nx)
			{
				double h = std::max(1e-5, params[k].step * .02);
				a.x[k] = std::min(params[k].hi, a.x[k] + h);
				b.x[k] = std::max(params[k].lo, b.x[k] - h);
				span = a.x[k] - b.x[k];
				scale[k] = params[k].step;
				a.emitters = Decode(a.x);
				b.emitters = Decode(b.x);
				if (!Place(current.emitters, a.emitters) || !Place(current.emitters, b.emitters))
					return false;
			}
			else
			{
				size_t s = (k - nx) / 3, c = (k - nx) % 3;
				V qa = a.q[s], qb = b.q[s];
				double h = std::max(.01, std::abs(qa[c]) * .005);
				scale[k] = std::max(1., std::abs(qa[c]));
				qa[c] = std::min(double(V(candidates[s].upperRgb)[c]), qa[c] + h);
				qb[c] = std::max(double(V(candidates[s].lowerRgb)[c]), qb[c] - h);
				span = qa[c] - qb[c];
				a.q[s] = qa.Float();
				b.q[s] = qb.Float();
			}
			if (span <= 1e-12)
				continue;
			std::vector<V> pa, pb;
			try
			{
				Value(a.emitters, a.q, r.searchSampling, nullptr, SIZE_MAX, nullptr, nullptr, &pa);
				Value(b.emitters, b.q, r.searchSampling, nullptr, SIZE_MAX, nullptr, nullptr, &pb);
			}
			catch (const Error &e)
			{
				if (e.code == Code::Unavailable)
					return false;
				throw;
			}
			Need(pa.size() == centre.size() && pb.size() == centre.size(), Code::InvalidState,
				 "Changing joint-proposal observation support");
			for (size_t i = 0; i < centre.size(); ++i)
				for (size_t c = 0; c < 3; ++c)
					jac[(k * r.count + i) * 3 + c] = (pa[i][c] - pb[i][c]) / span * scale[k];
		}
		std::vector<double> gram(n * n, 0), g(n, 0);
		for (uint64_t first = 0; first < r.count; first += r.options.batchRows)
		{
			auto rows = Read(first);
			for (size_t i = 0; i < rows.count; ++i)
			{
				const auto &a = rows.data[i];
				size_t at = first + i;
				for (size_t c = 0; c < 3; ++c)
					if (a.channel[c].present)
					{
						double error = Residual(centre[at][c], a.channel[c]) / a.sigma;
						double weight =
							a.area / area *
							(std::abs(error) > r.options.huber ? r.options.huber / std::abs(error) : 1);
						for (size_t j = 0; j < n; ++j)
						{
							double dj = jac[(j * r.count + at) * 3 + c] / a.sigma;
							g[j] += weight * dj * error;
							for (size_t k = 0; k < n; ++k)
								gram[j * n + k] += weight * dj * jac[(k * r.count + at) * 3 + c] / a.sigma;
						}
					}
			}
		}
		for (size_t j = 0; j < n; ++j)
		{
			double regularization = 0, value = 0;
			if (j < nx)
			{
				const auto &p = params[j];
				const auto &c = candidates[p.source];
				double sigma =
					p.kind == 0 ? c.positionSigmaWorld : p.kind == 2 ? c.logSizeSigma : c.angleSigmaRadians;
				regularization = c.geometryPriorWeight / (sigma * sigma);
				value = current.x[j];
			}
			else
			{
				regularization = r.options.ridge;
				value = V(current.q[(j - nx) / 3])[(j - nx) % 3];
			}
			gram[j * n + j] += regularization * scale[j] * scale[j];
			g[j] += regularization * value * scale[j];
		}
		double maximum = 0;
		for (size_t k = 0; k < n; ++k)
			maximum = std::max(maximum, gram[k * n + k]);
		for (size_t k = 0; k < n; ++k)
		{
			gram[k * n + k] += std::max(1e-12, maximum * 1e-5);
			g[k] = -g[k];
		}
		for (size_t k = 0; k < n; ++k)
		{
			size_t pivot = k;
			for (size_t i = k + 1; i < n; ++i)
				if (std::abs(gram[i * n + k]) > std::abs(gram[pivot * n + k]))
					pivot = i;
			if (std::abs(gram[pivot * n + k]) < 1e-20)
				return false;
			for (size_t j = k; j < n; ++j)
				std::swap(gram[k * n + j], gram[pivot * n + j]);
			std::swap(g[k], g[pivot]);
			double d = gram[k * n + k];
			for (size_t j = k; j < n; ++j)
				gram[k * n + j] /= d;
			g[k] /= d;
			for (size_t i = 0; i < n; ++i)
				if (i != k)
				{
					double f = gram[i * n + k];
					for (size_t j = k; j < n; ++j)
						gram[i * n + j] -= f * gram[k * n + j];
					g[i] -= f * g[k];
				}
		}
		for (double alpha : {1., .5, .25, .125})
		{
			State warm = current;
			auto x = current.x;
			for (size_t k = 0; k < n; ++k)
			{
				double delta = alpha * Clamp(g[k], -2, 2) * scale[k];
				if (k < nx)
					x[k] = Clamp(x[k] + delta, params[k].lo, params[k].hi);
				else
				{
					size_t s = (k - nx) / 3, c = (k - nx) % 3;
					V q = warm.q[s];
					q[c] = Clamp(q[c] + delta, V(candidates[s].lowerRgb)[c], V(candidates[s].upperRgb)[c]);
					warm.q[s] = q.Float();
				}
			}
			State trial;
			if (Trial(&warm, x, trial) && trial.Total() < current.Total() - r.options.relativeImprovement *
																				std::max(1., current.Total()))
			{
				current = std::move(trial);
				out.trace.back().accepted = true;
				return true;
			}
		}
		return false;
	}
	FitResult Run()
	{
		State current;
		Need(Trial(nullptr, std::vector<double>(params.size(), 0), current), Code::Unavailable,
			 "Initial shape has no certified placement");
		out.initialObjective = current.Total();
		out.trace.back().accepted = true;
		// A broad cone may have an exactly flat response at every current receiver.
		// Local angle polls cannot cross that plateau: use bounded training-only
		// discrete cone starts, then refine continuously. No visibility gradient.
		if (positive && r.options.angularStarts)
			for (size_t j = 0; j < params.size(); ++j)
				if (params[j].kind == 3 && params[j].axis == 0)
				{
					const auto &e = candidates[params[j].source].initial;
					double initial = r.model.law == Law::HistoricalBake
										 ? std::atan((e.spotRadiusWorld + 16) / e.spotTargetDistanceWorld)
										 : std::acos(e.geometry.angular.cosOuter);
					for (double angle : {.2, .4, .6, .9, 1.3})
					{
						auto x = current.x;
						x[j] = Clamp(angle - initial, params[j].lo, params[j].hi);
						State trial;
						if (Trial(&current, x, trial) && trial.Total() < current.Total())
						{
							current = std::move(trial);
							out.trace.back().accepted = true;
						}
					}
				}
		if (positive)
			for (unsigned round = 0; round < 8; ++round)
				if (!JointProposal(current))
					break;
		double fraction = 1;
		auto better = [&](const State &a, const State &b) {
			return a.Total() < b.Total() - r.options.relativeImprovement * std::max(1., std::abs(b.Total()));
		};
		if (positive)
			for (sweep = 0; sweep < r.options.maxSweeps; ++sweep)
			{
				if (sweep && sweep % 12 == 0)
					JointProposal(current);
				State best = current;
				size_t accepted = SIZE_MAX;
				for (size_t j = 0; j < params.size(); ++j)
				{
					State side[2];
					bool ok[2] = {};
					double step = params[j].step * fraction;
					for (unsigned k = 0; k < 2; ++k)
					{
						auto x = current.x;
						x[j] = Clamp(x[j] + (k ? step : -step), params[j].lo, params[j].hi);
						if (x[j] == current.x[j])
							continue;
						ok[k] = Trial(&current, x, side[k]);
						if (ok[k] && better(side[k], best))
						{
							best = side[k];
							accepted = out.trace.size() - 1;
						}
					}
					if (r.options.parabolicProposals && ok[0] && ok[1] &&
						std::abs(side[0].x[j] + side[1].x[j] - 2 * current.x[j]) < 1e-8)
					{
						double curvature = side[0].Total() + side[1].Total() - 2 * current.Total();
						if (curvature > 1e-14)
						{
							auto x = current.x;
							x[j] = Clamp(
								x[j] + Clamp(.5 * step * (side[0].Total() - side[1].Total()) / curvature,
											 -step, step),
								params[j].lo, params[j].hi);
							State alternative;
							if (std::abs(x[j] - current.x[j]) > 1e-7 && Trial(&current, x, alternative) &&
								better(alternative, best))
							{
								best = alternative;
								accepted = out.trace.size() - 1;
							}
						}
					}
				}
				if (better(best, current))
				{
					// Follow an actually improving coupled direction, always
					// certifying movement and refitting RGB; no shadow derivative.
					if (r.options.parabolicProposals)
						for (double scale : {1., 2., 4.})
						{
							auto x = best.x;
							bool moved = false;
							for (size_t j = 0; j < x.size(); ++j)
							{
								x[j] = Clamp(best.x[j] + scale * (best.x[j] - current.x[j]), params[j].lo,
											 params[j].hi);
								moved |= std::abs(x[j] - best.x[j]) > 1e-8;
							}
							State alternative;
							if (moved && Trial(&best, x, alternative) && better(alternative, best))
							{
								best = std::move(alternative);
								accepted = out.trace.size() - 1;
							}
						}
					current = std::move(best);
					out.trace[accepted].accepted = true;
					continue;
				}
				if (fraction <= r.options.minimumStepFraction)
				{
					out.converged = true;
					out.stop = Stop::PatternStationary;
					break;
				}
				fraction = std::max(r.options.minimumStepFraction, fraction * .5);
			}
		if (!positive)
		{
			out.stop = Stop::Uninformative;
			out.converged = false;
		}
		else if (!out.converged)
			out.stop = Stop::SweepLimit;
		cache.clear();
		cacheBytes = 0;
		double strict = Value(current.emitters, current.q, r.strictSampling);
		out.strictDifference = std::abs(strict - current.data);
		// Only frozen geometry/RGB is evaluated with strict sampling. No target-dependent retuning.
		out.mapKey = r.mapKey;
		out.geometryKey = r.geometry.geometryKey;
		out.model = r.model;
		out.emitters = current.emitters;
		out.objective = strict + current.prior + current.shadow;
		out.shadowObjective = current.shadow;
		out.shadowWeight = r.shadowWeight;
		out.shadowDiagnostics = current.shadowValue;
		if (r.shadowWeight > 0)
		{
			std::vector<FitEvidence::Source> sources;
			for (const auto &e : current.emitters)
			{
				FitEvidence::Source s;
				s.geometry = e.geometry;
				s.rgb = e.rgbAmplitude;
				s.legacySpotDistance = e.spotTargetDistanceWorld;
				s.legacySpotRadius = e.spotRadiusWorld;
				sources.push_back(s);
			}
			Check(r.shadow->EvaluateReference(sources, read, out.shadowReference));
			out.work.rays += out.shadowReference.rays;
			out.work.traversal += out.shadowReference.work;
			Limit();
			Need(out.shadowReference.available, Code::Unavailable,
				 "Frozen reference shadow assessment unavailable");
		}
		out.projectedKkt = current.kkt;
		out.linearAmplitudes = current.linear;
		out.projectedKktAvailable = current.linear;
		for (auto &e : out.emitters)
		{
			auto &p = e.geometry.emission;
			Chromaticity colour;
			double y = Dot(V(e.rgbAmplitude), V(.2126, .7152, .0722));
			p = {};
			p.unit = r.model.output == Output::PhysicalLux
						 ? ((e.geometry.kind == EmitterKind::Point || e.geometry.kind == EmitterKind::Spot)
								? PhotometricUnit::Candela
								: PhotometricUnit::Nits)
						 : PhotometricUnit::Relative;
			p.intensity.known = r.model.linearRec709PrimariesAttested;
			p.intensity.value = r.model.linearRec709PrimariesAttested ? float(y) : 0;
			p.calibration = r.model.calibration.id;
			if (r.model.linearRec709PrimariesAttested && NormalizeChromaticity(e.rgbAmplitude, colour))
				p.color = colour;
			if (r.model.law == Law::HistoricalBake && e.geometry.kind == EmitterKind::Spot)
				e.geometry.angular.kind = AngularKind::Unknown;
		}
		out.amplitudeConverged = current.qConverged;
		out.strictReevaluated = true;
		out.absoluteScaleKnown = r.model.output == Output::PhysicalLux;
		GeometryKey key;
		key.Digest(identity);
		if (r.shadowWeight > 0)
		{
			key.Digest(r.shadow->Key());
			key.F(r.shadowWeight);
		}
		key.Digest(placement->PolicyKey());
		HashModel(key, r.model);
		HashSampling(key, r.searchSampling);
		HashSampling(key, r.strictSampling);
		for (const auto &e : out.emitters)
		{
			HashEmitter(key, e);
			key.V(e.rgbAmplitude);
		}
		for (const auto &c : candidates)
		{
			Text(key, c.provenance);
			key.U(uint32_t(c.evidence));
			key.F(c.geometryPriorWeight);
		}
		out.key = key.Finish();
		out.fitted = true;
		out.limitations.push_back("Local fixed-count diagnostic; no calibrated existence/type/geometry "
								  "confidence or stock compiler identification");
		out.limitations.push_back("Uniform RGB emission only; tube radius/radiance and broad-spot/point "
								  "ambiguities require independent shape/direction evidence. No emitter "
								  "extent is inferred from an average grid sample.");
		out.limitations.push_back(r.model.law == Law::HistoricalBake
									  ? "Historical polygon expansion is an explicit recipe hypothesis; "
										"centre trace is not a continuous-source shadow integral"
									  : "Finite quadrature discrepancy is not a rigorous "
										"visibility/total-error bound; unknown optics stay unavailable");
		return out;
	}
};
} // namespace
Error Fit(const Request &r, const ReadContext &read, FitResult &out)
{
	out = {};
	std::unique_ptr<Engine> engine;
	if (r.candidates.size() > 8 || r.options.batchRows > 512)
		return Failure(Code::InvalidInput, "Shape fit metadata hard limit");
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
			out.trials = engine->out.trials;
			out.limitations = engine->out.limitations;
		}
		out.stop = e.code == Code::Cancelled ? Stop::Cancelled
											 : e.code == Code::WorkLimit ? Stop::Budget : Stop::Unavailable;
		out.limitations.push_back(e.message);
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Shape fit allocation");
	}
}
Error EvaluateFrozen(const FitResult &fit, std::shared_ptr<const ChunkSource<Observation>> source,
					 uint64_t count, Role role, const Bake::EvaluationContext &geometry,
					 const Sampling &sampling, double complexity, const ReadContext &read, Score &out)
{
	out = {};
	try
	{
		Cancel(read);
		Need(fit.fitted && fit.strictReevaluated && source && count && count <= 1000000 &&
				 fit.geometryKey == geometry.geometryKey && role != Role::Training &&
				 std::isfinite(complexity) && complexity >= 0,
			 Code::InvalidInput, "Frozen validation/held-out evaluation request invalid");
		GeometryKey key;
		key.Digest(fit.mapKey);
		key.Digest(geometry.geometryKey);
		double sum = 0, black = 0, weight = 0;
		std::set<Id> blocks;
		Id last = 0;
		std::vector<Vec3> rgb;
		for (const auto &e : fit.emitters)
			rgb.push_back(e.rgbAmplitude);
		for (uint64_t first = 0; first < count; first += 64)
		{
			Cancel(read);
			Chunk<Observation> rows;
			size_t n = size_t(std::min<uint64_t>(64, count - first));
			Check(source->Read(first, n, read, rows));
			Need(rows.owner && rows.data && rows.count == n, Code::ProviderError,
				 "Truncated evaluation stream");
			auto receivers = std::make_shared<std::vector<Bake::Receiver>>();
			for (size_t j = 0; j < n; ++j)
			{
				const auto &a = rows.data[j];
				ValidateObservation(a, role);
				Need(a.id > last, Code::InvalidInput, "Evaluation IDs not unique/sorted");
				last = a.id;
				HashObservation(key, a);
				receivers->push_back(a.receiver);
				blocks.insert(a.spatialBlock);
			}
			std::shared_ptr<const Block> prepared;
			Check(Prepare(fit.model, fit.emitters, Lease(receivers), geometry, sampling, read, prepared));
			Add(out.work, prepared->PreparationWork());
			Need(out.work.rays <= sampling.maxRays && out.work.traversal <= sampling.maxWork, Code::WorkLimit,
				 "Frozen scoring cumulative ray budget");
			std::vector<Prediction> prediction;
			Work w;
			Check(prepared->Evaluate(rgb, read, prediction, w));
			Add(out.work, w);
			for (size_t j = 0; j < n; ++j)
			{
				const auto &a = rows.data[j];
				++out.requested;
				if (!prediction[j].available)
				{
					++out.unknown;
					continue;
				}
				++out.comparable;
				out.quadratureDifference =
					std::max(out.quadratureDifference, prediction[j].quadratureDifference / a.sigma);
				for (int c = 0; c < 3; ++c)
					if (a.channel[c].present)
					{
						double e = Residual(V(prediction[j].value)[c], a.channel[c]) / a.sigma,
							   b = Residual(0, a.channel[c]) / a.sigma;
						sum += a.area * e * e;
						black += a.area * b * b;
						weight += a.area;
						out.maximumError = std::max(out.maximumError, std::abs(e));
						out.positive |= a.channel[c].lower > 0;
					}
			}
		}
		out.identity = key.Finish();
		out.parameters = fit.parameters;
		out.role = role;
		out.spatialBlocks = blocks.size();
		out.available = out.comparable == count && weight > 0;
		if (weight)
		{
			out.intervalMse = sum / weight;
			out.blackMse = black / weight;
		}
		out.penalty = complexity * fit.parameters;
		out.penalized = out.intervalMse + out.penalty;
		return {};
	}
	catch (const Error &e)
	{
		Work w = out.work;
		out = {};
		out.work = w;
		return e;
	}
	catch (const std::bad_alloc &)
	{
		out = {};
		return Failure(Code::MemoryLimit, "Frozen score allocation");
	}
}
Error Select(const std::vector<FitResult> &fits, const std::vector<Score> &scores, double slack,
			 Selection &out)
{
	out = {};
	if (fits.empty() || fits.size() != scores.size() || !std::isfinite(slack) || slack < 0)
		return Failure(Code::InvalidInput, "Invalid alternative-model comparison");
	double best = std::numeric_limits<double>::infinity();
	Disk::Digest identity = scores[0].identity;
	for (size_t i = 0; i < fits.size(); ++i)
	{
		const auto &s = scores[i];
		if (s.role != Role::Validation || s.identity != identity || s.requested != scores[0].requested ||
			fits[i].model.output != fits[0].model.output || fits[i].model.law != fits[0].model.law ||
			fits[i].model.units.worldUnitsPerMeter != fits[0].model.units.worldUnitsPerMeter ||
			fits[i].model.calibration.id != fits[0].model.calibration.id)
			return Failure(Code::InvalidInput,
						   "Models must share exact validation observations/domain/weights");
		if (s.available && fits[i].fitted && fits[i].strictReevaluated)
		{
			if (!s.positive || s.blackMse <= s.intervalMse)
			{
				continue;
			}
			best = std::min(best, s.penalized);
		}
	}
	if (!std::isfinite(best))
	{
		out.state = Selection::State::InsufficientSignal;
		out.reason = "No model improves a genuinely positive fully covered validation set over black";
		return {};
	}
	for (size_t i = 0; i < scores.size(); ++i)
	{
		const auto &s = scores[i];
		if (!s.available || !fits[i].fitted || !s.positive || s.intervalMse >= s.blackMse)
			continue;
		// Broad cones without independent direction/fixture evidence remain alternatives.
		double discrepancy = s.quadratureDifference * s.quadratureDifference;
		if (s.penalized <= best + slack + discrepancy)
			out.retained.push_back(i);
		if (s.penalized == best && out.preferred == SIZE_MAX)
			out.preferred = i;
	}
	out.state = out.retained.size() == 1 && scores[0].spatialBlocks >= 3 ? Selection::State::Discriminated
																		 : Selection::State::Alternatives;
	out.reason = "Conditional validation plus declared complexity penalty and numerical slack; not a type "
				 "posterior or physical-source certification";
	return {};
}
const char *StopName(Stop s)
{
	const char *names[] = {"NotStarted", "PatternStationary", "SweepLimit", "Uninformative", "Cancelled",
						   "Budget",	 "Unavailable"};
	return size_t(s) < 7 ? names[size_t(s)] : "Invalid";
}
} // namespace Shape
} // namespace Core
} // namespace Rend2LightRecon
