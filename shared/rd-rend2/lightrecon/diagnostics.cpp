// SPDX-License-Identifier: GPL-2.0-or-later
#include "diagnostics.h"
#include "bake_math.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
namespace Rend2LightRecon
{
namespace Core
{
namespace Diagnostics
{
namespace
{
using namespace Bake::Detail;
double Component(Vec3 v, unsigned c)
{
	return c == 0 ? v.x : c == 1 ? v.y : v.z;
}
Record Base(Id id, Layer l, State s, const std::string &why, Id source = 0)
{
	Record r;
	r.id = id;
	r.layer = l;
	r.state = s;
	r.reason = why;
	r.source = source;
	r.quantity = "geometry-only";
	return r;
}
bool Valid(const Record &r)
{
	return r.id && unsigned(r.layer) < unsigned(Layer::Count) &&
		   unsigned(r.state) < unsigned(State::Count) &&
		   unsigned(r.primitive) <= unsigned(Primitive::Triangle) && Finite(r.a) && Finite(r.b) &&
		   Finite(r.c) && Finite(r.value) && Finite(r.lower) && Finite(r.upper);
}
State PlacementState(const PointRefine::PlacementResult &p)
{
	using F = PointRefine::Feasibility;
	auto s = PointRefine::Classify(p);
	return s == F::Proven
			   ? State::Accepted
			   : s == F::Conditional
					 ? State::Conditional
					 : s == F::Collision ? State::Rejected
										 : s == F::Invalid ? State::Invalid : State::Unknown;
}
} // namespace
const char *Name(Layer l)
{
	static const char *n[] = {"sources", "placement", "dynamic",	"receivers",  "direct",
							  "sun",	 "sky",		  "indirect",	"unresolved", "residual",
							  "probes",	 "field",	  "visibility", "transport"};
	return unsigned(l) < unsigned(Layer::Count) ? n[unsigned(l)] : "invalid";
}
const char *Name(State s)
{
	static const char *n[] = {"accepted",	 "conditional", "rejected", "deferred", "budget",
							  "unavailable", "unknown",		"visible",	"blocked",	"invalid"};
	return unsigned(s) < unsigned(State::Count) ? n[unsigned(s)] : "invalid";
}
Builder::Builder(Scene &s, Limits l) : scene_(s), limits_(l)
{
	for (auto &r : s.records)
		bytes_ += sizeof(r) + r.reason.size() + r.provenance.size() + r.quantity.size() +
				  r.domain.size() + r.profile.size();
}
bool Builder::Add(Record r)
{
	++scene_.requested;
	auto bytes = sizeof(r) + r.reason.size() + r.provenance.size() + r.quantity.size() +
				 r.domain.size() + r.profile.size();
	if (!Valid(r) || r.reason.size() > limits_.maxString ||
		r.provenance.size() > limits_.maxString || r.quantity.size() > limits_.maxString ||
		r.domain.size() > limits_.maxString || r.profile.size() > limits_.maxString ||
		scene_.records.size() >= limits_.maxRecords ||
		bytes > limits_.maxBytes - std::min(bytes_, limits_.maxBytes))
	{
		++scene_.omitted;
		scene_.complete = false;
		return false;
	}
	bytes_ += bytes;
	scene_.records.push_back(std::move(r));
	return true;
}
void Builder::Box(Id id, Layer l, State state, const Bounds &b, const std::string &why, Id source)
{
	if (!b.known)
	{
		Add(Base(id, l, State::Unknown, why + ";bounds=Unknown", source));
		return;
	}
	for (unsigned axis = 0; axis < 3; ++axis)
		for (unsigned mask = 0; mask < 4; ++mask)
		{
			V a(b.minWorld), z(b.maxWorld);
			unsigned p = (axis + 1) % 3, q = (axis + 2) % 3;
			a[p] = (mask & 1) ? z[p] : a[p];
			a[q] = (mask & 2) ? z[q] : a[q];
			V end = a;
			end[axis] = z[axis];
			auto r = Base(id, l, state, why, source);
			r.geometryKnown = true;
			r.primitive = Primitive::Line;
			r.a = a.Float();
			r.b = end.Float();
			Add(r);
		}
}
void Builder::Emitter(const PhysicalEmitter &e, State state, const std::string &profile,
					  const std::string &why, Vec3 anchor)
{
	auto base = Base(e.source.id, Layer::Sources, state, why, e.source.id);
	base.profile = profile;
	base.provenance = "canonical-source:" + std::to_string(e.source.provenance);
	base.quantity = "source-photometry";
	base.valueKnown = e.emission.intensity.known && e.emission.color.known;
	base.value = (V(e.emission.color.rgb) * e.emission.intensity.value).Float();
	base.unit = e.emission.unit;
	if (e.kind != EmitterKind::Directional && !e.position.known)
	{
		base.state = State::Unavailable;
		base.reason += ";position=Unknown";
		Add(base);
		return;
	}
	if (unsigned(e.kind) < unsigned(EmitterKind::Point) ||
		unsigned(e.kind) > unsigned(EmitterKind::Directional) || !Finite(e.position.world) ||
		!Finite(anchor))
	{
		base.state = State::Invalid;
		base.reason += ";invalid source kind/position";
		base.valueKnown = false;
		base.value = Vec3();
		Add(base);
		return;
	}
	V centre = e.kind == EmitterKind::Directional ? V(anchor) : V(e.position.world);
	base.a = centre.Float();
	base.geometryKnown = true;
	Add(base);
	if (e.kind != EmitterKind::Directional && e.rangeWorld.known &&
		std::isfinite(e.rangeWorld.value) && e.rangeWorld.value > 0)
	{
		Bounds bounds;
		bounds.known = true;
		V radius(e.rangeWorld.value, e.rangeWorld.value, e.rangeWorld.value);
		bounds.minWorld = (centre - radius).Float();
		bounds.maxWorld = (centre + radius).Float();
		Box(e.source.id, Layer::Sources, state, bounds,
			"declared finite culling range; NOT attenuation/visibility proof", e.source.id);
	}
	auto line = [&](V a, V b, const std::string &label, Layer layer = Layer::Sources) {
		auto r = base;
		r.a = a.Float();
		r.b = b.Float();
		r.primitive = Primitive::Line;
		r.reason = label;
		r.layer = layer;
		Add(r);
	};
	for (unsigned j = 0; j < 3; ++j)
	{
		V a = centre, b = centre;
		a[j] -= 3;
		b[j] += 3;
		line(a, b, "source marker (3 WU display size, not fixture geometry)");
	}
	if (!e.frame.known && e.kind != EmitterKind::Point)
	{
		base.geometryKnown = false;
		base.state = State::Unavailable;
		base.reason = "orientation=Unknown";
		Add(base);
		return;
	}
	if (e.kind != EmitterKind::Point &&
		(!Finite(e.frame.right) || !Finite(e.frame.up) || !Finite(e.frame.forward) ||
		 std::abs(Length(V(e.frame.right)) - 1) > 1e-3 ||
		 std::abs(Length(V(e.frame.up)) - 1) > 1e-3 ||
		 std::abs(Length(V(e.frame.forward)) - 1) > 1e-3 ||
		 Length(Cross(V(e.frame.right), V(e.frame.up)) - V(e.frame.forward)) > 1e-3))
	{
		base.state = State::Invalid;
		base.reason = "invalid source frame; shape unavailable";
		Add(base);
		return;
	}
	if ((e.kind == EmitterKind::Rectangle || e.kind == EmitterKind::Tube) &&
		(!e.geometry.known || !std::isfinite(e.geometry.halfWidthWorld) ||
		 !std::isfinite(e.geometry.halfHeightWorld) || !std::isfinite(e.geometry.halfLengthWorld) ||
		 !std::isfinite(e.geometry.radiusWorld) ||
		 (e.kind == EmitterKind::Rectangle &&
		  (e.geometry.halfWidthWorld <= 0 || e.geometry.halfHeightWorld <= 0)) ||
		 (e.kind == EmitterKind::Tube &&
		  (e.geometry.halfLengthWorld <= 0 || e.geometry.radiusWorld <= 0))))
	{
		base.state = e.geometry.known ? State::Invalid : State::Unavailable;
		base.reason = "unknown/invalid finite source extent";
		Add(base);
		return;
	}
	V right(e.frame.right), up(e.frame.up), forward(e.frame.forward);
	if (e.kind == EmitterKind::Directional)
	{
		line(centre, centre + forward * 48,
			 "photon travel direction; widget anchor is NOT finite source XYZ", Layer::Sun);
		line(centre, centre - forward * 32, "receiver-to-sun = minus photon travel", Layer::Sun);
		return;
	}
	if (e.kind == EmitterKind::Spot)
	{
		line(centre, centre + forward * 48, "spot photon travel axis");
		for (float cosine : {e.angular.cosInner, e.angular.cosOuter})
			if (cosine > 0 && cosine <= 1)
			{
				double radius = 48 * std::tan(std::acos(cosine));
				if (radius > 512)
					continue;
				for (unsigned j = 0; j < 16; ++j)
				{
					double a = 2 * Pi * j / 16, b = 2 * Pi * (j + 1) / 16;
					V x = centre + forward * 48 + (right * std::cos(a) + up * std::sin(a)) * radius,
					  y = centre + forward * 48 + (right * std::cos(b) + up * std::sin(b)) * radius;
					line(x, y,
						 "cone display at 48 WU; angular-law evidence remains profile-specific");
					if (j % 4 == 0)
						line(centre, x, "spot cone boundary");
				}
			}
	}
	if (e.kind == EmitterKind::Rectangle && e.geometry.known)
	{
		V corner[4];
		for (unsigned j = 0; j < 4; ++j)
			corner[j] = centre + right * ((j == 0 || j == 3 ? -1 : 1) * e.geometry.halfWidthWorld) +
						up * ((j < 2 ? -1 : 1) * e.geometry.halfHeightWorld);
		for (unsigned j = 0; j < 4; ++j)
			line(corner[j], corner[(j + 1) % 4], "declared emitting rectangle outline");
		line(centre, centre + forward * 16, "front emitting normal");
		if (e.angular.twoSided)
			line(centre, centre - forward * 16, "explicit two-sided back emission");
	}
	if (e.kind == EmitterKind::Tube && e.geometry.known)
	{
		V a = centre - right * e.geometry.halfLengthWorld,
		  b = centre + right * e.geometry.halfLengthWorld;
		line(a, b, "finite tube axis=frame.right");
		for (unsigned j = 0; j < 16; ++j)
		{
			double x = 2 * Pi * j / 16, y = 2 * Pi * (j + 1) / 16;
			V u = (up * std::cos(x) + forward * std::sin(x)) * e.geometry.radiusWorld,
			  v = (up * std::cos(y) + forward * std::sin(y)) * e.geometry.radiusWorld;
			line(a + u, a + v, "open cylindrical emitting support");
			line(b + u, b + v, "open cylindrical emitting support");
			if (j % 4 == 0)
				line(a + u, b + u, "tube side");
		}
	}
	if (e.position.sigmaWorld.bounded && e.position.sigmaWorld.upper > 0)
	{
		Bounds b;
		b.known = true;
		V d(e.position.sigmaWorld.upper, e.position.sigmaWorld.upper, e.position.sigmaWorld.upper);
		b.minWorld = (centre - d).Float();
		b.maxWorld = (centre + d).Float();
		Box(e.source.id, Layer::Placement, state, b,
			"declared position uncertainty bound; not calibrated unless provenance establishes it",
			e.source.id);
	}
}
void Builder::Placement(const Joint::Source &s, const PointRefine::PlacementResult &p,
						const char *predicate, Vec3 from, Vec3 to, double radius,
						const std::string &profile)
{
	auto state = PlacementState(p);
	auto r =
		Base(s.candidate.id, Layer::Placement, state,
			 std::string(predicate) + ":" + PointRefine::ConstraintName(p.state), s.candidate.id);
	r.geometryKnown = true;
	r.primitive = Primitive::Line;
	r.a = from;
	r.b = to;
	r.profile = profile;
	r.region = p.region;
	r.provenance = p.assumptions;
	r.domain = "source-feasibility, independent of receiver visibility";
	r.quantity = "clearance-radius-world-units";
	r.valueKnown = true;
	r.value = Vec3(float(radius), 0, 0);
	Add(r);
	r.reason = "support-kind=" + std::to_string(unsigned(s.value.support.kind)) +
			   " fixture=" + std::to_string(s.value.support.fixture) +
			   " mount-known=" + std::to_string(s.value.support.mountKnown) + "; " +
			   s.value.support.provenance;
	r.primitive = Primitive::Point;
	r.a = to;
	Add(r);
	if (s.value.support.mountKnown)
	{
		r.primitive = Primitive::Line;
		r.a = s.value.support.anchorWorld;
		r.b = (V(r.a) + V(s.value.support.outward) * 16).Float();
		r.reason += "; mounting outward side (display length 16 WU)";
		Add(r);
	}
	Bounds box;
	box.known = true;
	box.minWorld = (V(to) - V(radius, radius, radius)).Float();
	box.maxWorld = (V(to) + V(radius, radius, radius)).Float();
	Box(s.candidate.id, Layer::Placement, state, box, r.reason, s.candidate.id);
	for (auto &w : p.occupancy)
		Box(w.boundary, Layer::Dynamic, State::Conditional, w.scope,
			"occupancy assumption model=" + std::to_string(w.model) +
				" runtime-sweep=" + (w.runtimeExtentUnknown ? "Unknown" : "Known"),
			s.candidate.id);
}
void Builder::Dynamic(const DynamicBoundary &d)
{
	Box(d.id, Layer::Dynamic, State::Unknown, d.bounds,
		"BSP model=" + std::to_string(d.model) + "; " +
			(d.sweptExtentKnown ? "known swept bounds"
								: "captured original model bounds, NOT swept bounds") +
			"; bake occupancy=Unknown; runtime swept extent=" +
			(d.sweptExtentKnown ? "Known" : "Unknown"));
}
void Builder::Probes(const Probes::Topology &t)
{
	for (auto &p : t.Probes())
	{
		auto r =
			Base(p.id, Layer::Probes,
				 p.classification == Probes::Classification::Valid
					 ? State::Accepted
					 : p.classification == Probes::Classification::Relocated ? State::Conditional
																			 : State::Invalid,
				 "generated probe; lighting values independently available");
		r.source = p.source;
		r.region = p.region;
		r.geometryKnown = true;
		r.a = p.positionWorld;
		r.b = p.originalWorld;
		r.primitive = p.displacementWorld > 0 ? Primitive::Line : Primitive::Point;
		r.issues = p.issues;
		r.provenance = Disk::Hex(t.Key());
		Add(r);
	}
	for (auto &c : t.Cells())
		if (c.probe == UINT32_MAX)
		{
			auto r = Base(c.id, Layer::Probes,
						  c.space == SpaceClass::Solid ? State::Invalid : State::Unavailable,
						  "cell has no valid generated probe; not black illumination");
			r.geometryKnown = c.bounds.known;
			r.a = ((V(c.bounds.minWorld) + V(c.bounds.maxWorld)) * .5).Float();
			r.issues = c.issues;
			Add(r);
		}
}
void Builder::ProbeQuery(Id id, const Probes::Topology &t, Vec3 position,
						 const Probes::QueryPolicy &policy)
{
	auto support = t.Lookup(position, policy);
	auto r = Base(id, Layer::Probes,
				  support.status == Status::Ok ? State::Conditional : State::Unavailable,
				  "prepared interpolation support; not source visibility");
	r.a = position;
	r.geometryKnown = true;
	r.quantity = "linear-interpolation-weight";
	r.domain = "dimensionless";
	r.region = support.region;
	r.issues = support.issues;
	if (support.status != Status::Ok)
	{
		Add(r);
		++scene_.unknownQueries;
		return;
	}
	for (unsigned j = 0; j < support.count; ++j)
		for (auto &p : t.Probes())
			if (p.id == support.probes[j])
			{
				r.sample = p.id;
				r.b = p.positionWorld;
				r.primitive = Primitive::Line;
				r.valueKnown = true;
				r.value = Vec3(float(support.weights[j]), 0, 0);
				Add(r);
				break;
			}
}
void Builder::Field(const Fields::Artifact &a)
{
	if (a.topology)
		Probes(*a.topology);
	if (!a.descriptor)
		return;
	size_t lobes = 0;
	for (auto &v : a.values.records)
	{
		auto r = Base(v.probe, Layer::Field, State::Conditional,
					  "angular error diagnostic, not point RGB lighting");
		r.configuration = a.descriptor->configuration;
		r.alternative = a.descriptor->alternativeGroup;
		r.source = a.descriptor->field;
		r.geometryKnown = true;
		r.a = v.evaluatedPositionWorld;
		r.domain = a.descriptor->profile;
		r.quantity = "relativeRMSE,maxAbs,energyBias";
		r.profile = a.descriptor->encoding;
		r.provenance = a.descriptor->provenance;
		for (auto &d : a.diagnostics)
			if (d.probe == v.probe)
			{
				r.valueKnown = d.referenceKnown;
				r.value = Vec3(float(d.relativeRMSE), float(d.maximumError), float(d.energyBias));
				r.issues = d.issues;
			}
		Add(r);
		if (lobes++ >= 16)
			continue;
		for (auto direction : {Vec3(1, 0, 0), Vec3(-1, 0, 0), Vec3(0, 1, 0), Vec3(0, -1, 0),
							   Vec3(0, 0, 1), Vec3(0, 0, -1)})
		{
			Angular::V d{{direction.x, direction.y, direction.z}};
			auto value = Angular::Evaluate(v.angular, d, v.angular.quantity);
			r.primitive = Primitive::Line;
			r.b = (V(r.a) + V(direction) * 8).Float();
			r.unit = a.descriptor->unit;
			r.quantity = v.angular.quantity == Angular::Quantity::IncomingRadiance
							 ? "incoming-directional-radiance"
							 : "surface-normal-dependent-irradiance";
			r.reason = "basis evaluation at stated world direction; negative ringing is not "
					   "silently clamped";
			r.valueKnown = value.code == Angular::Code::Ok && !value.negative;
			r.value = r.valueKnown
						  ? Vec3(float(value.rgb[0]), float(value.rgb[1]), float(value.rgb[2]))
						  : Vec3();
			r.state = r.valueKnown ? State::Conditional : State::Unavailable;
			Add(r);
		}
	}
}
void Builder::Ray(Id id, Id source, Vec3 from, Vec3 to, const RayResult &hit, const char *scope)
{
	auto r = Base(id, Layer::Visibility,
				  hit.state == VisibilityState::Blocked
					  ? State::Blocked
					  : hit.state == VisibilityState::Visible
							? (hit.reliability == RayReliability::Geometric ? State::Visible
																			: State::Conditional)
							: State::Unknown,
				  scope, source);
	r.geometryKnown = true;
	r.primitive = Primitive::Line;
	r.a = from;
	r.b = to;
	r.issues = hit.issues;
	r.quantity = "geometric-ray (not PVS)";
	r.reason += ";reliability=" + std::to_string(unsigned(hit.reliability)) +
				";origin-domain=" + std::to_string(unsigned(hit.originDomain)) +
				";node-tests=" + std::to_string(hit.nodeTests) +
				";primitive-tests=" + std::to_string(hit.primitiveTests) +
				";bias-world=" + std::to_string(hit.appliedBiasWorld);
	r.reason += ";sky=" + std::to_string(unsigned(hit.sky));
	if (hit.terminalHit.positionKnown)
	{
		r.b = hit.terminalHit.positionWorld;
		r.reason += ";hit-surface=" + std::to_string(hit.terminalHit.surface) +
					";material=" + std::to_string(hit.terminalHit.material);
		r.valueKnown = true;
		r.quantity = "occluder-distance-world";
		r.value = Vec3(float(hit.terminalHit.distanceWorld), 0, 0);
	}
	Add(r);
	++scene_.rays;
	if (hit.state == VisibilityState::Unknown)
		++scene_.unknownQueries;
}
void Builder::Shadow(const ShadowCache::Artifact &a)
{
	for (auto &v : a.records)
	{
		auto r = Base(v.item.receiver.id, Layer::Visibility,
					  v.result.staticGeometry.state == ShadowState::Unknown
						  ? State::Unknown
						  : v.result.staticGeometry.state == ShadowState::Blocked ? State::Blocked
																				  : State::Visible,
					  "prepared static source visibility; finite sampling != exact penumbra",
					  v.item.source);
		r.sample = v.item.receiver.id;
		r.region = v.item.receiver.region;
		r.geometryKnown = true;
		r.a = v.item.receiver.positionWorld;
		r.quantity = "fractional-static-transmittance";
		r.valueKnown = v.result.staticGeometry.estimateKnown;
		r.value = Vec3(float(v.result.staticGeometry.value), 0, 0);
		r.lowerKnown = r.upperKnown = true;
		r.lower = Vec3(float(v.result.staticGeometry.lower), 0, 0);
		r.upper = Vec3(float(v.result.staticGeometry.upper), 0, 0);
		r.issues = v.result.issues;
		r.provenance = Disk::Hex(v.key);
		Add(r);
	}
}
bool Selection::Matches(const Record &r) const
{
	return (layers & (1u << unsigned(r.layer))) && (!source || source == r.source) &&
		   (!region || region == r.region) && (!configuration || configuration == r.configuration);
}
void Colour(State s, uint8_t c[3])
{
	static const uint8_t colours[][3] = {
		{40, 240, 80},	 {255, 165, 20}, {255, 40, 40},	 {80, 130, 255}, {255, 255, 20},
		{130, 130, 130}, {255, 40, 255}, {30, 240, 220}, {255, 60, 30},	 {255, 255, 255}};
	auto i = std::min(unsigned(s), unsigned(State::Count) - 1);
	std::copy_n(colours[i], 3, c);
}
Record Compare(Id id, Vec3 p, const Vec3 &prediction, const Vec3 &lo, const Vec3 &hi,
			   uint8_t present, uint8_t unbounded, const std::string &domain, PhotometricUnit unit,
			   bool comparable)
{
	auto r = Base(id, Layer::Residual, comparable ? State::Conditional : State::Unavailable,
				  "signed interval residual in explicitly common observation domain");
	r.sample = id;
	r.a = p;
	r.geometryKnown = true;
	r.quantity = "signed-observation-model-discrepancy";
	r.domain = domain;
	r.unit = unit;
	if (!comparable || domain.empty() || !Finite(prediction) || !Finite(lo) || !Finite(hi) ||
		!present)
		return r;
	V value;
	for (unsigned c = 0; c < 3; ++c)
		if (present & (1 << c))
		{
			if (!(unbounded & (1 << c)) && Component(hi, c) < Component(lo, c))
			{
				r.state = State::Invalid;
				return r;
			}
			value[c] = Component(prediction, c) < Component(lo, c)
						   ? Component(prediction, c) - Component(lo, c)
						   : (!(unbounded & (1 << c)) && Component(prediction, c) > Component(hi, c)
								  ? Component(prediction, c) - Component(hi, c)
								  : 0);
		}
	r.valueKnown = true;
	r.value = value.Float();
	r.lowerKnown = r.upperKnown = false;
	r.issues = unbounded;
	return r;
}
} // namespace Diagnostics
} // namespace Core
} // namespace Rend2LightRecon
