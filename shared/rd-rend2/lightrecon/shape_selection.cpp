// Geometry-led representation discovery; the historical solver is unchanged.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "shape_selection.h"
#include "bake_math.h"
#include "emission_geometry.h"
#include "geometry_key.h"
#include <algorithm>
#include <set>
namespace Rend2LightRecon
{
namespace Core
{
namespace ShapeSelection
{
using namespace Bake::Detail;
namespace
{
void Need(bool b, Code c, const std::string &s)
{
	if (!b)
		throw Failure(c, s);
}
void Must(Error e)
{
	if (!e)
		throw e;
}
bool Usable(const Rival &r)
{
	return r.status == Status::Supported || r.status == Status::Approximation;
}
void Text(GeometryKey &k, const std::string &s)
{
	k.U(s.size());
	for (auto c : s)
		k.U(uint8_t(c));
}
Frame FrameFor(V n, V right = V(1, 0, 0))
{
	Frame f;
	f.known = true;
	n = Unit(n);
	right = right - n * Dot(n, right);
	if (Length(right) < 1e-6)
		right = Cross(V(0, 1, 0), n);
	f.right = Unit(right).Float();
	f.forward = n.Float();
	f.up = Cross(n, V(f.right)).Float();
	return f;
}
void AddPoint(Bounds &b, Vec3 p)
{
	if (!b.known)
	{
		b.known = true;
		b.minWorld = b.maxWorld = p;
		return;
	}
	b.minWorld =
		Vec3(std::min(b.minWorld.x, p.x), std::min(b.minWorld.y, p.y), std::min(b.minWorld.z, p.z));
	b.maxWorld =
		Vec3(std::max(b.maxWorld.x, p.x), std::max(b.maxWorld.y, p.y), std::max(b.maxWorld.z, p.z));
}
bool Hood(const Request &r, const Shape::Emitter &point, double &angle, const ReadContext &read,
		  uint64_t &work)
{
	if (r.hoodSurfaces.empty())
		return false;
	Need(r.hoodSurfaces.size() <= 64, Code::InvalidInput, "Too many hood surfaces");
	std::set<Id> ids;
	V n(point.support.outward), p(point.geometry.position.world);
	V right(point.geometry.frame.right), up(point.geometry.frame.up);
	double depth = 0, radial = 0;
	for (auto id : r.hoodSurfaces)
	{
		Need(id < r.scene.originalSurfaces->Surfaces().size(), Code::InvalidInput,
			 "Hood surface ordinal");
		const auto &s = r.scene.originalSurfaces->Surfaces()[id];
		if (s.motion != SurfaceMotion::StaticWorld ||
			(s.kind != BakedSurfaceKind::Face && s.kind != BakedSurfaceKind::Triangles) ||
			!s.materialFacts.available ||
			(s.materialFacts.signals & (MaterialCutout | MaterialTranslucent | MaterialDeformed)))
			return false;
		ids.insert(Id(id) + 1);
		for (const auto &v : s.vertices)
		{
			Need(++work <= r.policy.maxWork, Code::WorkLimit, "Hood geometry budget");
			V d = V(v.position) - p;
			depth = std::max(depth, Dot(n, d));
			radial = std::max(radial, std::max(std::abs(Dot(right, d)), std::abs(Dot(up, d))));
		}
	}
	if (depth <= 0 || radial <= 0)
		return false;
	angle = std::atan2(radial, depth);
	if (angle < .03 || angle > 1.3)
		return false;
	// Sparse geometric direction evidence, not a measured angular distribution.
	// Unknown rays cannot certify an opening or an opaque reflector.
	for (unsigned i = 0; i < 5; ++i)
	{
		V tangent = i == 1 ? right : i == 2 ? right * -1 : i == 3 ? up : up * -1;
		V end = p + n * (depth * 1.1);
		if (i)
			end = end + tangent * (radial * 1.5);
		RayQuery q;
		q.startWorld = (p + n * (depth * .01)).Float();
		q.endWorld = end.Float();
		q.maxWork = std::max<uint64_t>(1, r.policy.maxWork - work);
		RayResult ray;
		Must(r.scene.geometry->Query(q, read, ray));
		work += ray.nodeTests + ray.primitiveTests;
		Need(work <= r.policy.maxWork, Code::WorkLimit, "Hood ray budget");
		if (!i)
		{
			if (ray.state != VisibilityState::Visible)
				return false;
		}
		else if (ray.state != VisibilityState::Blocked || !ray.firstHit.present ||
				 ray.firstHit.obstacle != ObstacleKind::Opaque || !ids.count(ray.firstHit.surface))
			return false;
	}
	return true;
}
void OrderAndKey(const Request &r, Result &out)
{
	std::stable_sort(out.ranked.begin(), out.ranked.end(), [](const Rival &a, const Rival &b) {
		bool aok = Usable(a), bok = Usable(b);
		if (aok != bok)
			return aok;
		if (a.preference != b.preference)
			return a.preference < b.preference;
		return unsigned(a.kind) < unsigned(b.kind);
	});
	out.primitiveKnown =
		r.fullSurfaceEmissionAttested && !out.ranked.empty() && Usable(out.ranked.front());
	if (out.primitiveKnown &&
		std::find(out.ranked.front().reasons.begin(), out.ranked.front().reasons.end(),
				  Reason::PointCollapsesExtent) != out.ranked.front().reasons.end())
	{
		out.primitiveKnown = false;
		out.limitation = "Only a point proxy passed placement; it collapses the visible emitting "
						 "extent, so the modern representation remains Unknown";
	}
	GeometryKey k;
	k.U(kVersion);
	k.Digest(out.map);
	k.Digest(out.geometry);
	k.Digest(out.placementPolicy);
	k.U(out.fixture);
	k.F(r.policy.pointExtentWorld);
	k.F(r.policy.stripAspect);
	k.F(r.policy.rectangleCoverage);
	k.U(r.fullSurfaceEmissionAttested);
	k.U(r.coneCueKnown);
	k.F(r.coneHalfAngleRadians);
	k.U(r.tubeBodyKnown);
	Text(k, r.cueProvenance);
	Text(k, r.tubeBodyProvenance);
	Text(k, r.emissionProvenance);
	k.U(out.requestedSurfaces.size());
	for (auto id : out.requestedSurfaces)
		k.U(id);
	auto hood = r.hoodSurfaces;
	std::sort(hood.begin(), hood.end());
	k.U(hood.size());
	for (auto id : hood)
		k.U(id);
	k.Digest(SourceSupport::Key(out.original));
	for (const auto &v : out.ranked)
	{
		k.U(unsigned(v.kind));
		k.U(unsigned(v.status));
		k.U(v.preference);
		const auto &g = v.emitter.geometry;
		k.V(g.position.world);
		k.V(g.frame.right);
		k.V(g.frame.up);
		k.V(g.frame.forward);
		k.F(g.geometry.halfWidthWorld);
		k.F(g.geometry.halfHeightWorld);
		k.F(g.geometry.halfLengthWorld);
		k.F(g.geometry.radiusWorld);
		k.F(g.angular.cosInner);
		k.F(g.angular.cosOuter);
		k.U(g.angular.twoSided);
		k.Digest(SourceSupport::Key(v.emitter.support));
		k.U(v.reasons.size());
		for (auto reason : v.reasons)
			k.U(unsigned(reason));
	}
	out.key = k.Finish();
}
class FrozenRows final : public ChunkSource<Shape::Observation>
{
  public:
	std::shared_ptr<std::vector<Shape::Observation>> data =
		std::make_shared<std::vector<Shape::Observation>>();
	Error Read(uint64_t first, size_t count, const ReadContext &read,
			   Chunk<Shape::Observation> &out) const override
	{
		out = {};
		if (Cancelled(read))
			return Failure(Code::Cancelled, "Frozen shape rows cancelled");
		if (first > data->size() || count > data->size() - first)
			return Failure(Code::InvalidInput, "Frozen shape row range");
		out = VectorChunk<Shape::Observation>(data, size_t(first), count);
		return {};
	}
};
struct Partition
{
	std::set<Id> ids, blocks;
	std::shared_ptr<FrozenRows> rows = std::make_shared<FrozenRows>();
};
Partition Inspect(std::shared_ptr<const ChunkSource<Shape::Observation>> source, uint64_t count,
				  Shape::Role role, Id material, const ReadContext &read)
{
	Need(source && count && count <= 4096, Code::InvalidInput,
		 "Bounded nonempty shape comparison partition required");
	Need(!read.maxBytes || count * (sizeof(Shape::Observation) + 64) <= read.maxBytes / 4,
		 Code::MemoryLimit, "Frozen shape partition budget");
	Partition p;
	for (uint64_t i = 0; i < count; i += 64)
	{
		Need(!Cancelled(read), Code::Cancelled, "Shape comparison partition cancelled");
		Chunk<Shape::Observation> rows;
		size_t n = size_t(std::min<uint64_t>(64, count - i));
		Must(source->Read(i, n, read, rows));
		Need(rows.owner && rows.data && rows.count == n, Code::InvalidInput,
			 "Comparison partition lease");
		for (size_t j = 0; j < n; ++j)
		{
			const auto &a = rows.data[j];
			Need(a.id && a.receiver.sample == a.id && a.spatialBlock && a.role == role &&
					 a.comparable && a.receiver.material == material && p.ids.insert(a.id).second,
				 Code::InvalidInput, "Invalid same-material comparison partition");
			p.blocks.insert(a.spatialBlock);
			p.rows->data->push_back(a);
		}
	}
	return p;
}
void Disjoint(const Partition &a, const Partition &b)
{
	for (auto id : a.ids)
		Need(!b.ids.count(id), Code::InvalidInput, "Shape comparison sample leakage");
	for (auto id : a.blocks)
		Need(!b.blocks.count(id), Code::InvalidInput, "Shape comparison spatial-block leakage");
}
void Domain(const ComparisonRequest &r)
{
	Need(r.sameMaterialResponseAttested && r.material && std::isfinite(r.equivalentMseSlack) &&
			 r.equivalentMseSlack >= 0,
		 Code::InvalidInput, "Explicit same-material response and score tolerance required");
	Need(r.targets == TargetDomain::RelativeLinearAttested
			 ? r.fitting.model.output == Shape::Output::RelativeIncident
			 : r.targets == TargetDomain::HistoricalEncodedRgb8 &&
				   r.fitting.model.law == Shape::Law::HistoricalBake &&
				   r.fitting.model.output == Shape::Output::EncodedRgb8,
		 Code::Unavailable, "Encoded RGB8 cannot be compared as linear incident irradiance");
	Need(r.prior.map == r.fitting.mapKey && r.prior.geometry == r.fitting.geometry.geometryKey &&
			 r.prior.ranked.size() == 4,
		 Code::InvalidInput, "Shape prior/comparison geometry identity mismatch");
}
} // namespace
Error Rank(const Request &r, const ReadContext &read, Result &out)
{
	out = {};
	try
	{
		Need(!Cancelled(read), Code::Cancelled, "Shape selection cancelled");
		Need(r.fixture && r.scene.originalSurfaces && r.scene.geometry && r.scene.domain &&
				 r.scene.geometry->BuiltFrom(r.scene.originalSurfaces),
			 Code::InvalidInput, "Shape selection requires pinned source and world geometry");
		Need(r.scene.map.sha256 == r.scene.originalSurfaces->Map().sha256 &&
				 std::isfinite(r.policy.pointExtentWorld) && r.policy.pointExtentWorld > 0 &&
				 std::isfinite(r.policy.stripAspect) && r.policy.stripAspect > 1 &&
				 std::isfinite(r.policy.rectangleCoverage) && r.policy.rectangleCoverage > 0 &&
				 r.policy.rectangleCoverage <= 1 && r.policy.maxWork &&
				 r.cueProvenance.size() <= 4096 && r.tubeBodyProvenance.size() <= 4096 &&
				 r.emissionProvenance.size() <= 4096 &&
				 (!r.fullSurfaceEmissionAttested || !r.emissionProvenance.empty()),
			 Code::InvalidInput, "Invalid shape prior policy/identity");
		Need(!r.coneCueKnown ||
				 (std::isfinite(r.coneHalfAngleRadians) && r.coneHalfAngleRadians > .03 &&
				  r.coneHalfAngleRadians < 1.5 && !r.cueProvenance.empty()),
			 Code::InvalidInput, "Invalid cone cue");
		out.fixture = r.fixture;
		out.map = r.scene.map.sha256;
		out.geometry = r.scene.geometry->CertificationKey();
		std::shared_ptr<const PointRefine::PlacementProvider> placement;
		Must(PointRefine::MakePlacement(r.scene, read, placement));
		out.placementPolicy = placement->PolicyKey();
		out.requestedSurfaces = r.emittingSurfaces;
		std::sort(out.requestedSurfaces.begin(), out.requestedSurfaces.end());
		for (auto kind :
			 {EmitterKind::Point, EmitterKind::Spot, EmitterKind::Rectangle, EmitterKind::Tube})
		{
			Rival v;
			v.kind = kind;
			v.emitter.geometry.kind = kind;
			v.emitter.geometry.source.id = r.fixture;
			v.emitter.geometry.source.state = HypothesisState::Candidate;
			v.emitter.rgbAmplitude = Vec3(1, 1, 1);
			v.reasons.push_back(Reason::NotHistoricalTruth);
			out.ranked.push_back(v);
		}
		Shape::Emitter base = out.ranked[0].emitter;
		bool rectangle = false, small = false;
		double cone = 0;
		if (r.tubeBodyKnown)
		{
			Need(!r.tubeBodyProvenance.empty() && r.tubeBody.geometry.kind == EmitterKind::Tube &&
					 r.tubeBody.support.kind == SourceSupport::Kind::Cylinder &&
					 r.tubeBody.support.fixture == r.fixture && r.tubeBody.support.mountKnown,
				 Code::InvalidInput,
				 "Measured cylinder requires an explicit named mount and provenance");
			Must(SourceSupport::Validate(r.tubeBody.support));
			const auto &body = r.tubeBody.geometry;
			Need(body.position.known && Finite(body.position.world) && body.frame.known &&
					 Normal(body.frame.right) && Normal(body.frame.up) &&
					 Normal(body.frame.forward) &&
					 Length(Cross(V(body.frame.right), V(body.frame.up)) - V(body.frame.forward)) <
						 1e-4 &&
					 body.geometry.known && std::isfinite(body.geometry.radiusWorld) &&
					 body.geometry.radiusWorld > 0 &&
					 std::isfinite(body.geometry.halfLengthWorld) &&
					 body.geometry.halfLengthWorld > 0,
				 Code::InvalidInput, "Finite measured cylindrical body dimensions/frame required");
			base = r.tubeBody;
			base.geometry.source.id = r.fixture;
			base.geometry.source.state = HypothesisState::Candidate;
			base.rgbAmplitude = Vec3(1, 1, 1);
			out.original = base.support;
			out.widthWorld = 2 * base.geometry.geometry.halfLengthWorld;
			out.heightWorld = 2 * base.geometry.geometry.radiusWorld;
			out.emittingAreaWorldSquared = 4 * Pi * base.geometry.geometry.radiusWorld *
										   base.geometry.geometry.halfLengthWorld;
			out.aspect = out.widthWorld / out.heightWorld;
			small = std::max(out.widthWorld, out.heightWorld) <= r.policy.pointExtentWorld;
			for (unsigned bits = 0; bits < 8; ++bits)
			{
				V p(base.geometry.position.world);
				p = p +
					V(base.geometry.frame.right) * (bits & 1
														? base.geometry.geometry.halfLengthWorld
														: -base.geometry.geometry.halfLengthWorld) +
					V(base.geometry.frame.up) * (bits & 2 ? base.geometry.geometry.radiusWorld
														  : -base.geometry.geometry.radiusWorld) +
					V(base.geometry.frame.forward) * (bits & 4
														  ? base.geometry.geometry.radiusWorld
														  : -base.geometry.geometry.radiusWorld);
				AddPoint(out.actualBounds, p.Float());
			}
		}
		else
		{
			auto e = SourceSupport::FromSurfaces(r.scene.originalSurfaces, *r.scene.geometry,
												 r.emittingSurfaces, r.fixture, read, out.original);
			if (!e)
			{
				if (e.code != Code::Unavailable)
					Must(e);
				for (auto id : out.requestedSurfaces)
				{
					const auto &s = r.scene.originalSurfaces->Surfaces().at(id);
					Need(s.vertices.size() <= 4096, Code::WorkLimit,
						 "Unsupported source bounds budget");
					for (const auto &v : s.vertices)
					{
						Need(++out.work <= r.policy.maxWork, Code::WorkLimit,
							 "Unsupported geometry budget");
						AddPoint(out.actualBounds, v.position);
					}
				}
				out.limitation = e.message;
				for (auto &v : out.ranked)
				{
					v.reasons.push_back(Reason::GeometryUnavailable);
					v.uncertainties.push_back(e.message);
				}
				OrderAndKey(r, out);
				return {};
			}
			std::vector<Vec3> points;
			for (size_t i = 0; i < out.original.original.count; ++i)
			{
				const auto &t = out.original.original.data[i];
				out.emittingAreaWorldSquared += .5 * Length(Cross(V(t.world[1]) - V(t.world[0]),
																  V(t.world[2]) - V(t.world[0])));
				for (auto p : t.world)
				{
					points.push_back(p);
					AddPoint(out.actualBounds, p);
				}
			}
			std::sort(points.begin(), points.end(), [](Vec3 a, Vec3 b) {
				return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z;
			});
			points.erase(std::unique(points.begin(), points.end(),
									 [](Vec3 a, Vec3 b) { return Length(V(a) - V(b)) == 0; }),
						 points.end());
			std::vector<const float *> raw;
			for (const auto &p : points)
				raw.push_back(&p.x);
			Emission::Rectangle box;
			float n[] = {out.original.outward.x, out.original.outward.y, out.original.outward.z};
			Emission::FitRectangle(raw, n, 0, box);
			base.geometry.position.known = true;
			base.geometry.position.world = Vec3(box.center[0], box.center[1], box.center[2]);
			base.geometry.frame =
				FrameFor(V(out.original.outward), V(box.right[0], box.right[1], box.right[2]));
			base.geometry.geometry.known = true;
			base.geometry.geometry.halfWidthWorld = box.halfWidth;
			base.geometry.geometry.halfHeightWorld = box.halfHeight;
			base.support = out.original;
			base.emittingSurface = out.original.surfaces.size() == 1 ? out.original.surfaces[0] : 0;
			out.widthWorld = 2 * box.halfWidth;
			out.heightWorld = 2 * box.halfHeight;
			Need(out.widthWorld > 0 && out.heightWorld > 0, Code::Unavailable,
				 "Degenerate source extent");
			out.aspect = std::max(out.widthWorld, out.heightWorld) /
						 std::min(out.widthWorld, out.heightWorld);
			out.coverage = out.emittingAreaWorldSquared / (out.widthWorld * out.heightWorld);
			rectangle = std::abs(1 - out.coverage) <= 1 - r.policy.rectangleCoverage;
			small = std::max(out.widthWorld, out.heightWorld) <= r.policy.pointExtentWorld;
			out.work += points.size();
		}
		out.emittingGeometryExact = r.fullSurfaceEmissionAttested;
		bool hood = Hood(r, base, cone, read, out.work);
		bool directional = small && (hood || r.coneCueKnown);
		if (r.coneCueKnown)
			cone = r.coneHalfAngleRadians;
		for (auto &v : out.ranked)
		{
			v.emitter = base;
			auto &g = v.emitter.geometry;
			g.kind = v.kind;
			v.status = Status::Approximation;
			if (v.kind == EmitterKind::Point || v.kind == EmitterKind::Spot)
			{
				g.angular.twoSided = false;
				g.angular.kind =
					v.kind == EmitterKind::Point ? AngularKind::Isotropic : AngularKind::SmoothCone;
				v.emitter.support.original = {};
				v.emitter.support.triangleSurfaces.clear();
				v.emitter.support.kind = v.kind == EmitterKind::Point
											 ? SourceSupport::Kind::MountedPoint
											 : SourceSupport::Kind::MountedSpot;
				if (r.tubeBodyKnown)
				{
					double d = Dot(V(base.support.outward),
								   V(g.position.world) - V(base.support.anchorWorld));
					v.emitter.support.maximumStandOffWorld =
						std::max(v.emitter.support.maximumStandOffWorld, d);
				}
				v.preference = small ? 0 : 4;
				v.reasons.push_back(small ? Reason::SmallAperture : Reason::PointCollapsesExtent);
				if (!small)
					v.uncertainties.push_back(
						"Ideal point collapses the measured visible emitting extent");
				if (v.kind == EmitterKind::Spot)
				{
					if (!directional)
					{
						v.status = Status::Unknown;
						v.preference = 100;
						v.reasons.push_back(Reason::NoDirectionalEvidence);
						continue;
					}
					g.angular.cosOuter = float(std::cos(cone));
					g.angular.cosInner = float(std::cos(cone * .8));
					v.emitter.spotTargetDistanceWorld = 64;
					v.emitter.spotRadiusWorld = 64 * std::tan(cone);
					v.preference = 0;
					v.reasons.push_back(hood ? Reason::HoodDirectional : Reason::ConeCue);
					v.reasons.push_back(Reason::ConeModelAssumed);
					v.uncertainties.push_back(
						"Smooth cone is a modern hypothesis, not measured photometry");
				}
				else if (directional)
					v.preference = 2;
			}
			else if (v.kind == EmitterKind::Rectangle)
			{
				if (r.tubeBodyKnown || !rectangle)
				{
					v.status = Status::Unknown;
					v.reasons.push_back(r.tubeBodyKnown ? Reason::MountOnly
														: Reason::NonRectangular);
					continue;
				}
				g.angular.kind = AngularKind::Lambertian;
				g.angular.twoSided = out.original.twoSidedAttested;
				v.status = Status::Supported;
				v.preference = small ? 2 : 0;
				v.reasons.push_back(out.aspect >= r.policy.stripAspect ? Reason::FlatStrip
																	   : Reason::ExtendedArea);
			}
			else
			{
				if (!r.tubeBodyKnown)
				{
					v.status = Status::Unknown;
					v.reasons.push_back(Reason::BodyUnavailable);
					continue;
				}
				g.angular.kind = AngularKind::Lambertian;
				g.angular.twoSided = false;
				v.preference = small ? 2 : 0;
				v.status = Status::Supported;
				v.reasons.push_back(Reason::CylindricalBody);
				v.uncertainties.push_back(r.tubeBodyProvenance);
			}
			if (!r.fullSurfaceEmissionAttested)
			{
				v.reasons.push_back(Reason::MaskUnknown);
				v.status = Status::Approximation;
				v.uncertainties.push_back(
					"Emitting geometry bounds are known; luminous mask is not attested");
			}
			v.uncertainties.push_back(
				v.kind == EmitterKind::Point
					? "Isotropic point proxy; no measured angular distribution"
					: v.kind == EmitterKind::Spot ? "Directional proxy; no calibrated photometry"
												  : "Lambertian surface hypothesis; radiance and "
													"texture modulation remain unknown");
			Must(SourceSupport::Check(r.scene, *placement, g, g, v.emitter.support, read,
									  v.placement));
			out.work += v.placement.work;
			Need(out.work <= r.policy.maxWork, Code::WorkLimit,
				 "Shape admission cumulative budget");
			if (!PointRefine::Admissible(v.placement.placement))
			{
				v.status = Status::Rejected;
				v.reasons.push_back(Reason::PlacementRejected);
				v.uncertainties.push_back(v.placement.reason);
			}
		}
		// An irregular connected sheet is retained as an exact offline reference,
		// but no point/broad rectangle is silently declared its modern primitive.
		if (!r.tubeBodyKnown && !rectangle)
		{
			for (auto &v : out.ranked)
			{
				v.status = Status::Unknown;
				v.reasons.push_back(Reason::NonRectangular);
			}
			out.limitation =
				"Exact original union retained; a supported primitive decomposition is unavailable";
		}
		OrderAndKey(r, out);
		return {};
	}
	catch (const Error &e)
	{
		out = {};
		return e;
	}
	catch (const std::bad_alloc &)
	{
		out = {};
		return Failure(Code::MemoryLimit, "Shape selection allocation");
	}
}
Error FitAndValidate(const ComparisonRequest &r, const ReadContext &read, Comparison &out)
{
	out = {};
	try
	{
		Domain(r);
		auto training =
			Inspect(r.fitting.training, r.fitting.count, Shape::Role::Training, r.material, read);
		auto validation =
			Inspect(r.validation, r.validationCount, Shape::Role::Validation, r.material, read);
		Disjoint(training, validation);
		Comparison c;
		c.prior = r.prior;
		c.material = r.material;
		c.targets = r.targets;
		c.frozenGeometry = r.fitting.geometry;
		c.frozenSampling = r.fitting.strictSampling;
		c.trainingIds.assign(training.ids.begin(), training.ids.end());
		c.trainingBlocks.assign(training.blocks.begin(), training.blocks.end());
		c.validationIds.assign(validation.ids.begin(), validation.ids.end());
		c.validationBlocks.assign(validation.blocks.begin(), validation.blocks.end());
		c.fits.resize(4);
		c.validation.resize(4);
		c.heldout.resize(4);
		double best = std::numeric_limits<double>::infinity();
		Disk::Digest trainingIdentity{}, validationIdentity{};
		bool haveIdentity = false;
		for (size_t i = 0; i < 4; ++i)
		{
			const auto &v = c.prior.ranked[i];
			if (!Usable(v))
				continue;
			auto fit = r.fitting;
			fit.training = training.rows;
			Shape::Candidate candidate;
			candidate.initial = v.emitter;
			candidate.fitPosition = candidate.fitFrame = candidate.fitDimensions =
				candidate.fitCone = false;
			candidate.centreBounds.known = true;
			candidate.centreBounds.minWorld = candidate.centreBounds.maxWorld =
				v.emitter.geometry.position.world;
			candidate.provenance = "ASR19 fixed fixture geometry; amplitude only";
			candidate.evidence = Shape::ShapeEvidence::VisibleFixture;
			candidate.geometryPriorWeight = 1;
			candidate.lowerRgb = Vec3();
			candidate.upperRgb = Vec3(100000, 100000, 100000);
			fit.candidates = {candidate};
			auto e = Shape::Fit(fit, read, c.fits[i]);
			if (!e)
			{
				if (e.code != Code::Unavailable)
					Must(e);
				c.limitations.push_back(e.message);
				continue;
			}
			if (!c.fits[i].fitted || !c.fits[i].strictReevaluated)
				continue;
			e = Shape::EvaluateFrozen(c.fits[i], validation.rows, r.validationCount,
									  Shape::Role::Validation, r.fitting.geometry,
									  r.fitting.strictSampling, 0, read, c.validation[i]);
			if (!e)
			{
				if (e.code != Code::Unavailable)
					Must(e);
				c.limitations.push_back(e.message);
				continue;
			}
			if (haveIdentity)
				Need(c.fits[i].trainingIdentity == trainingIdentity &&
						 c.validation[i].identity == validationIdentity,
					 Code::InvalidInput, "Rival shapes read different training/validation targets");
			else
			{
				trainingIdentity = c.fits[i].trainingIdentity;
				validationIdentity = c.validation[i].identity;
				haveIdentity = true;
			}
			if (c.validation[i].available && c.validation[i].positive &&
				c.validation[i].intervalMse < c.validation[i].blackMse)
				best = std::min(best, c.validation[i].intervalMse);
		}
		for (size_t i = 0; i < 4; ++i)
			if (c.validation[i].available && c.validation[i].positive &&
				c.validation[i].intervalMse < c.validation[i].blackMse &&
				c.validation[i].intervalMse <=
					best + r.equivalentMseSlack +
						c.validation[i].quadratureDifference * c.validation[i].quadratureDifference)
				c.photometricAlternatives.push_back(i);
		c.limitations.push_back("Geometry prior order is separate from conditional baked/relative "
								"error; equivalent predictions do not identify a historical shape");
		c.frozen = true;
		out = std::move(c);
		return {};
	}
	catch (const Error &e)
	{
		out = {};
		return e;
	}
	catch (const std::bad_alloc &)
	{
		out = {};
		return Failure(Code::MemoryLimit, "Shape comparison allocation");
	}
}
Error AuditHeldout(const ComparisonRequest &r,
				   std::shared_ptr<const ChunkSource<Shape::Observation>> source, uint64_t count,
				   const ReadContext &read, Comparison &out)
{
	try
	{
		Domain(r);
		Need(out.frozen && !out.heldoutAudited && out.prior.key == r.prior.key &&
				 out.material == r.material && out.targets == r.targets,
			 Code::InvalidState, "Held-out audit requires the matching frozen comparison");
		auto heldout = Inspect(source, count, Shape::Role::Heldout, r.material, read);
		Partition train, validation;
		train.ids.insert(out.trainingIds.begin(), out.trainingIds.end());
		train.blocks.insert(out.trainingBlocks.begin(), out.trainingBlocks.end());
		validation.ids.insert(out.validationIds.begin(), out.validationIds.end());
		validation.blocks.insert(out.validationBlocks.begin(), out.validationBlocks.end());
		Disjoint(heldout, train);
		Disjoint(heldout, validation);
		std::vector<Shape::Score> scores(4);
		for (size_t i = 0; i < 4; ++i)
			if (out.fits[i].fitted && out.fits[i].strictReevaluated)
				Must(Shape::EvaluateFrozen(out.fits[i], heldout.rows, count, Shape::Role::Heldout,
										   out.frozenGeometry, out.frozenSampling, 0, read,
										   scores[i]));
		out.heldout = std::move(scores);
		out.heldoutAudited = true;
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Held-out shape audit allocation");
	}
}
const char *StatusName(Status s)
{
	switch (s)
	{
	case Status::Supported:
		return "Supported";
	case Status::Approximation:
		return "Approximation";
	case Status::Unknown:
		return "Unknown";
	case Status::Rejected:
		return "Rejected";
	}
	return "Unknown";
}
const char *KindName(EmitterKind k)
{
	switch (k)
	{
	case EmitterKind::Point:
		return "Point";
	case EmitterKind::Spot:
		return "Spot";
	case EmitterKind::Rectangle:
		return "Rectangle";
	case EmitterKind::Tube:
		return "Tube";
	default:
		return "Unsupported";
	}
}
const char *ReasonName(Reason r)
{
	switch (r)
	{
#define R(x)                                                                                       \
	case Reason::x:                                                                                \
		return #x
		R(SmallAperture);
		R(ExtendedArea);
		R(FlatStrip);
		R(CylindricalBody);
		R(HoodDirectional);
		R(ConeCue);
		R(NoDirectionalEvidence);
		R(BodyUnavailable);
		R(NonRectangular);
		R(GeometryUnavailable);
		R(PlacementRejected);
		R(MaskUnknown);
		R(PointCollapsesExtent);
		R(ConeModelAssumed);
		R(HistoricalShapeAmbiguous);
		R(NotHistoricalTruth);
		R(MountOnly);
#undef R
	}
	return "Unknown";
}
} // namespace ShapeSelection
} // namespace Core
} // namespace Rend2LightRecon
