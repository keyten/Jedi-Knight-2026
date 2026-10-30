// Owned passive transport, geometry and blind attribution fixtures. No runtime consumers.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/bake_math.h"
#include "lightrecon/emitter_shape.h"
#include "lightrecon/geometry_key.h"
#include "lightrecon/indirect.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
using namespace Bake::Detail;
namespace I = Rend2LightRecon::Core::Indirect;
namespace S = Rend2LightRecon::Core::Shape;
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
void Must(Error e)
{
	CHECK(e);
	if (!e)
		throw std::runtime_error(e.message);
}
ReadContext Read()
{
	ReadContext r;
	r.maxBytes = 128ull * 1024 * 1024;
	return r;
}
Disk::Digest Key(const std::string &s) { return Disk::Hash(s.data(), s.size()); }
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> v)
{
	return VectorChunk<T>(v, 0, v->size());
}
Bounds Box(V a, V b)
{
	Bounds r;
	r.known = true;
	r.minWorld = a.Float();
	r.maxWorld = b.Float();
	return r;
}
class Domain final : public RegionDomain
{
  public:
	Error Locate(Vec3, SpatialLocation &o, double = 1e-5) const override
	{
		o = {};
		o.work = 1;
		o.space = SpaceClass::Free;
		return {};
	}
	Error ClassifyBox(const Bounds &, SpaceClass &o, uint64_t &w) const override
	{
		o = SpaceClass::Free;
		w = 1;
		return {};
	}
};
I::Patch Patch(Id id, Id surface, Id material, V p, V normal, double side)
{
	I::Patch v;
	v.id = id;
	v.material = material;
	v.receiver.sample = id;
	v.receiver.surface = surface;
	v.receiver.material = material;
	v.receiver.world = p.Float();
	v.receiver.geometricNormal = v.receiver.shadingNormal = normal.Float();
	v.receiver.mappedPlanar = v.receiver.compilerPlacementCertified = true;
	V u = Unit(Cross(normal, std::abs(normal.z) > .9 ? V(0, 1, 0) : V(0, 0, 1))), w = Cross(normal, u);
	auto f = std::make_shared<std::vector<Bake::FootprintTriangle>>(2);
	V a = p - u * (side * .5) - w * (side * .5), b = p + u * (side * .5) - w * (side * .5),
	  c = p + u * (side * .5) + w * (side * .5), d = p - u * (side * .5) + w * (side * .5);
	(*f)[0].world[0] = a.Float();
	(*f)[0].world[1] = b.Float();
	(*f)[0].world[2] = c.Float();
	(*f)[1].world[0] = a.Float();
	(*f)[1].world[1] = c.Float();
	(*f)[1].world[2] = d.Float();
	for (auto &t : *f)
		for (auto &n : t.normal)
			n = normal.Float();
	v.receiver.footprint = Lease(f);
	v.areaWorldSquared = side * side;
	return v;
}
struct Scene
{
	std::shared_ptr<Domain> domain = std::make_shared<Domain>();
	std::shared_ptr<const StaticVisibility> visibility;
	LightmapMapIdentity map;
	std::vector<BakedSurface> surfaces;
	std::vector<I::Patch> patches;
	void Quad(V a, V b, V c, V d, uint32_t flags = 0)
	{
		BakedSurface s;
		s.ordinal = uint32_t(surfaces.size());
		s.kind = BakedSurfaceKind::Face;
		s.motion = SurfaceMotion::StaticWorld;
		s.material = "owned-s10";
		s.materialFacts.available = true;
		s.materialFacts.cull = SurfaceCull::None;
		s.materialFacts.signals = flags;
		for (auto p : {a, b, c, d})
		{
			BakedVertex v;
			v.position = p.Float();
			s.vertices.push_back(v);
		}
		s.indices = {0, 1, 2, 0, 2, 3};
		surfaces.push_back(s);
	}
	Scene(bool network = true, bool wall = false, bool opening = false, bool alpha = false,
		  bool missing = false, SurfaceCull wallCull = SurfaceCull::None)
	{
		if (network)
		{
			Quad(V(-4, -3, 0), V(4, -3, 0), V(4, 3, 0), V(-4, 3, 0));
			Quad(V(-.25, -.25, 1.6), V(.25, -.25, 1.6), V(.25, .25, 1.6), V(-.25, .25, 1.6));
			patches.push_back(Patch(1, 2, 1, V(0, 0, 1.6), V(0, 0, -1), .5));
			for (int y = -2; y <= 2; ++y)
				for (int x = -2; x <= 2; ++x)
					patches.push_back(
						Patch(patches.size() + 1, 1, 2, V(x * .45, y * .5, 0), V(0, 0, 1), .15));
		}
		else
		{
			Quad(V(-1.3, -.3, 2), V(-.7, -.3, 2), V(-.7, .3, 2), V(-1.3, .3, 2));
			Quad(V(.7, -.3, 0), V(1.3, -.3, 0), V(1.3, .3, 0), V(.7, .3, 0));
			patches = {Patch(1, 1, 1, V(-1, 0, 2), V(0, 0, -1), .2),
					   Patch(2, 2, 2, V(1, 0, 0), V(0, 0, 1), .2)};
		}
		if (wall)
		{
			size_t firstWall = surfaces.size();
			if (opening)
			{
				Quad(V(0, -2, -.1), V(0, -.4, -.1), V(0, -.4, 3), V(0, -2, 3));
				Quad(V(0, .4, -.1), V(0, 2, -.1), V(0, 2, 3), V(0, .4, 3));
			}
			else
				Quad(V(0, -2, -.1), V(0, 2, -.1), V(0, 2, 3), V(0, -2, 3),
					 alpha ? uint32_t(MaterialCutout) : 0u);
			for (size_t i = firstWall; i < surfaces.size(); ++i)
				surfaces[i].materialFacts.cull = wallCull;
		}
		if (missing)
		{
			BakedSurface s;
			s.ordinal = uint32_t(surfaces.size());
			s.kind = BakedSurfaceKind::Unsupported;
			s.motion = SurfaceMotion::StaticWorld;
			surfaces.push_back(s);
		}
		GeometryKey k;
		for (const auto &s : surfaces)
		{
			k.U(unsigned(s.kind));
			k.U(s.materialFacts.signals);
			k.U(unsigned(s.materialFacts.cull));
			for (const auto &v : s.vertices)
				k.V(v.position);
		}
		map.path = "maps/s10-owned.bsp";
		map.bytes = 1;
		map.sha256 = k.Finish();
		SurfaceCapture capture;
		capture.Begin(BuildPolicy::ForceRebuild);
		Must(capture.SetMap(map));
		for (const auto &s : surfaces)
			Must(capture.Add(s));
		Must(capture.Freeze());
		Must(StaticVisibility::Build(capture.Acquire(), {}, Read(), visibility, domain));
	}
	std::shared_ptr<const I::Transfer> Transfer(double range = 10, bool complete = true, bool moving = false)
	{
		I::BuildInput in;
		in.mapKey = map.sha256;
		in.patchInterpretationKey = Key("Owned exact planar receiving pieces v1");
		in.patches = patches;
		in.geometry = visibility;
		in.completePatchSet = complete;
		if (moving)
		{
			DynamicBoundary b;
			b.id = 1;
			b.sweptExtentKnown = true;
			b.bounds = Box(V(-.1, -2, 0), V(.1, 2, 3));
			in.moving.push_back(b);
		}
		I::BuildOptions o;
		o.maximumLinkDistanceWorld = range;
		o.subdivisions = 0;
		o.maxRays = 4000000;
		std::shared_ptr<const I::Transfer> out;
		Must(I::Transfer::Build(in, o, Read(), out));
		return out;
	}
};
class Stream final : public ChunkSource<I::Observation>
{
  public:
	std::shared_ptr<std::vector<I::Observation>> values = std::make_shared<std::vector<I::Observation>>();
	bool corrupt = false;
	Error Read(uint64_t first, size_t count, const ReadContext &r, Chunk<I::Observation> &out) const override
	{
		if (r.cancellation && r.cancellation->IsCancelled())
			return Failure(Code::Cancelled, "fixture cancel");
		if (corrupt)
			return Failure(Code::ProviderError, "fixture corrupt");
		if (first > values->size() || count > values->size() - first)
			return Failure(Code::ProviderError, "fixture range");
		out = VectorChunk<I::Observation>(values, size_t(first), count);
		return {};
	}
};
I::Root PointRoot(const Scene &scene, const I::Transfer &t, V position, V rgb, Id id = 50)
{
	S::Model model;
	model.law = S::Law::DiffuseIntegral;
	model.output = S::Output::RelativeIncident;
	model.allowAssumed = true;
	model.units.worldUnitsPerMeter = 1;
	model.provenance = "Owned scene-linear relative incident operator";
	S::Emitter e;
	e.geometry.source.id = id;
	e.geometry.kind = EmitterKind::Point;
	e.geometry.position.known = true;
	e.geometry.position.world = position.Float();
	e.geometry.angular.kind = AngularKind::Isotropic;
	e.rgbAmplitude = rgb.Float();
	auto receivers = std::make_shared<std::vector<Bake::Receiver>>();
	for (const auto &p : t.Patches())
		receivers->push_back(p.receiver);
	Bake::EvaluationContext context;
	context.visibility = scene.visibility;
	context.geometryKey = t.GeometryKey();
	context.materialKey = Key("owned known reflectance");
	context.observationPolicyKey = Key("point reference");
	S::Sampling sampling;
	sampling.compareRefined = false;
	std::shared_ptr<const S::Block> block;
	Must(S::Prepare(model, {e}, Lease(receivers), context, sampling, Read(), block));
	std::vector<S::Prediction> predictions;
	S::Work work;
	Must(block->Evaluate({rgb.Float()}, Read(), predictions, work));
	I::Root root;
	root.source = id;
	root.kind = TransportRootKind::LocalEmitter;
	root.generation = block->Key();
	root.provenance = "Shape::Prepare/Block::Evaluate fixed physical-kernel relative source";
	for (const auto &p : predictions)
	{
		root.incident.push_back(p.value);
		root.available.push_back(p.available);
	}
	return root;
}
I::Root Seed(const I::Transfer &t, TransportRootKind kind = TransportRootKind::LocalEmitter, Id id = 50)
{
	I::Root r;
	r.source = id;
	r.kind = kind;
	r.sunCoverage = kind == TransportRootKind::Sky ? SunCoverage::ExcludesExplicitSun : SunCoverage::Unknown;
	r.provenance = "Known finite boundary irradiance, no local lamp inferred";
	r.generation = Key("seed irradiance v1");
	r.incident.assign(t.Patches().size(), {});
	r.available.assign(t.Patches().size(), 1);
	r.incident[0] = Vec3(10, 10, 10);
	return r;
}
I::Reflectance MakeReflectance(Id id, V rho)
{
	I::Reflectance m;
	m.material = id;
	m.knowledge = I::MaterialKnowledge::Known;
	m.lower = m.upper = m.initial = rho.Float();
	m.provenance = "Owned bake-reflectance definition, independent of current PBR";
	return m;
}
std::vector<Vec3> Truth(const I::Transfer &t, const std::vector<I::Root> &roots, V reflectance)
{
	std::vector<V> v(t.Patches().size());
	for (const auto &root : roots)
	{
		for (size_t i = 0; i < v.size(); ++i)
			v[i] = v[i] + V(root.incident[i]);
		for (const auto &l : t.Links())
			for (unsigned direction = 0; direction < 2; ++direction)
			{
				size_t a = direction ? l.b : l.a, b = direction ? l.a : l.b;
				if (t.Patches()[a].material != 1)
					continue;
				for (int c = 0; c < 3; ++c)
					v[b][c] += l.conductance / t.Patches()[b].areaWorldSquared * reflectance[c] *
							   V(root.incident[a])[c];
			}
	}
	std::vector<Vec3> out;
	for (auto x : v)
		out.push_back(x.Float());
	return out;
}
std::shared_ptr<Stream> Observations(const I::Transfer &t, const std::vector<Vec3> &values, I::Role role)
{
	auto out = std::make_shared<Stream>();
	for (size_t i = 0; i < t.Patches().size(); ++i)
	{
		const auto &p = t.Patches()[i];
		if (i == 0)
			continue;
		bool train = p.receiver.world.y < -.1;
		if ((role == I::Role::Training && !train) || (role != I::Role::Training && p.receiver.world.y < .1))
			continue;
		I::Observation o;
		o.id = p.id + uint64_t(role) * 100000;
		o.patch = p.id;
		o.spatialBlock = 1 + uint64_t(i / 5);
		o.role = role;
		o.areaWorldSquared = p.areaWorldSquared;
		o.sigma = .001;
		for (int c = 0; c < 3; ++c)
			o.channels[c].lower = o.channels[c].upper = V(values[i])[c];
		out->values->push_back(o);
	}
	return out;
}
I::Request Request(std::shared_ptr<const I::Transfer> t, std::vector<I::Root> roots,
				   std::shared_ptr<Stream> train)
{
	I::Request r;
	r.transfer = t;
	r.roots = roots;
	r.materials = {MakeReflectance(1, V(.7, .18, .08)), MakeReflectance(2, V())};
	r.contract.operation = I::ObservationOperator::IncidentLinear;
	r.contract.transferAttested = r.contract.primariesAttested = r.contract.physicalTransportAssumed = true;
	r.contract.units.worldUnitsPerMeter = 1;
	r.contract.provenance = "Declared one-bounce Lambertian relative model, not Raven identity";
	r.training = train;
	r.count = train->values->size();
	r.trainingKey = Key("s10 blind training fold y<-.1; heldout y>.1");
	return r;
}
std::vector<std::string> rows;
double ReferenceBounce(const I::Patch &receiver, double exitance, unsigned order)
{
	// Independent dense rectangular area integral, no Transfer links/formulas read.
	V p = receiver.receiver.world;
	double sum = 0;
	for (unsigned x = 0; x < order; ++x)
		for (unsigned y = 0; y < order; ++y)
		{
			V q(-.25 + .5 * (x + .5) / order, -.25 + .5 * (y + .5) / order, 1.6), d = q - p;
			double rr = Dot(d, d), cosine = d.z / std::sqrt(rr);
			sum += exitance / Pi * cosine * cosine / rr * (.25 / (order * order));
		}
	return sum;
}
void NetworkTests(const std::string &prefix)
{
	Scene scene;
	auto t = scene.Transfer();
	auto root = PointRoot(scene, *t, V(-1, -.6, .8), V(3, 3, 3));
	CHECK(t->Report().maximumOutgoingFraction <= 1);
	CHECK(t->Report().allPairsRepresented);
	auto truth = Truth(*t, {root}, V(.7, .18, .08));
	auto train = Observations(*t, truth, I::Role::Training), held = Observations(*t, truth, I::Role::Heldout);
	auto request = Request(t, {root}, train);
	request.materials[0].knowledge = I::MaterialKnowledge::Bounded;
	request.materials[0].lower = Vec3(.2, .02, .01);
	request.materials[0].upper = Vec3(.9, .5, .4);
	request.materials[0].initial = Vec3(.3, .3, .3);
	I::Result result;
	Must(I::Fit(request, Read(), result));
	CHECK(result.fitted && result.converged);
	CHECK(!result.physicalBounceVerified && !result.absoluteScaleKnown);
	CHECK(result.maximumReflectedPowerFraction <= .7 + 1e-6);
	CHECK(Length(V(result.materials[0].initial) - V(.7, .18, .08)) < .001);
	for (size_t i = 0; i < root.incident.size(); ++i)
		CHECK(Length(V(result.fixedRoots[0].incident[i]) - V(root.incident[i])) == 0);
	I::Score score;
	Must(I::EvaluateFrozen(result, held, held->values->size(), I::Role::Heldout, Read(), score));
	CHECK(score.available && score.intervalMse < 1e-10);
	CHECK(score.intervalMse < score.directOnlyMse * .01);
	CHECK(score.blackMse > score.directOnlyMse);
	CHECK(result.values.back().indirect.x > result.values.back().indirect.z * 5);
	double maxReference = 0, referenceConvergence = 0;
	for (size_t i = 1; i < t->Patches().size(); ++i)
	{
		double a = ReferenceBounce(t->Patches()[i], root.incident[0].x * .7, 48),
			   b = ReferenceBounce(t->Patches()[i], root.incident[0].x * .7, 64);
		maxReference = std::max(maxReference, std::abs(result.values[i].indirect.x - b) / b);
		referenceConvergence = std::max(referenceConvergence, std::abs(a - b));
	}
	CHECK(maxReference < .02);
	std::ostringstream independent;
	independent << std::setprecision(12)
				<< "{\"scene\":\"independent_finite_reflector\",\"maximum_relative_difference\":"
				<< maxReference << ",\"reference48_64_difference\":" << referenceConvergence << "}";
	rows.push_back(independent.str());
	std::ostringstream row;
	row << std::setprecision(12) << "{\"scene\":\"red_reflector\",\"heldout_mse\":" << score.intervalMse
		<< ",\"direct_only_mse\":" << score.directOnlyMse << ",\"black_mse\":" << score.blackMse
		<< ",\"rho_r\":" << result.materials[0].initial.x << ",\"rho_g\":" << result.materials[0].initial.y
		<< ",\"rho_b\":" << result.materials[0].initial.z << ",\"links\":" << t->Links().size()
		<< ",\"rays\":" << t->Report().rays << ",\"evaluations\":" << result.work.evaluations
		<< ",\"false_emitters\":0}";
	rows.push_back(row.str());
	std::string text;
	Must(I::DebugTsv(result, 1024 * 1024, text));
	if (!prefix.empty())
		std::ofstream(prefix + "-decomposition.tsv") << text;
	auto wrongDirect = request;
	for (auto &v : wrongDirect.roots[0].incident)
		v = (V(v) * .5).Float();
	wrongDirect.roots[0].generation = Key("deliberately wrong direct-profile hypothesis");
	I::Result wrongProfile;
	Must(I::Fit(wrongDirect, Read(), wrongProfile));
	CHECK(wrongProfile.objective.data > result.objective.data + .001);
	CHECK(!wrongProfile.physicalBounceVerified);
	std::ostringstream discrepancy;
	discrepancy << "{\"scene\":\"wrong_direct_model\",\"training_objective\":" << wrongProfile.objective.data
				<< ",\"physical_bounce_verified\":false,\"false_emitters\":0}";
	rows.push_back(discrepancy.str());
	auto original = result.key;
	for (auto &o : *held->values)
		for (auto &c : o.channels)
			c.lower = c.upper = 1e8;
	I::Result repeat;
	Must(I::Fit(request, Read(), repeat));
	CHECK(repeat.key == original); // no held-out buffer accepted by Fit
	auto wrong = request;
	wrong.materials[0] = MakeReflectance(1, V(.35, .09, .04));
	I::Result mismatch;
	Must(I::Fit(wrong, Read(), mismatch));
	CHECK(mismatch.objective.data > result.objective.data);
	CHECK(mismatch.fixedRootsKey == result.fixedRootsKey);
	auto unknown = request;
	unknown.materials[0] = {};
	unknown.materials[0].material = 1;
	unknown.materials[0].upper = Vec3(.9, .9, .9);
	I::Result uncertain;
	Must(I::Fit(unknown, Read(), uncertain));
	CHECK(Peak(uncertain.values.back().indirect) == 0);
	CHECK(uncertain.values.back().indirectUpper.x >= truth.back().x - root.incident.back().x - 1e-6);
	CHECK(!uncertain.materialAllocationIdentifiable);
	CHECK(!uncertain.fitted && !uncertain.values.back().comparable);
	auto noBounce = request;
	noBounce.contract.physicalTransportAssumed = false;
	I::Result direct;
	Must(I::Fit(noBounce, Read(), direct));
	CHECK(Peak(direct.values.back().indirect) == 0);
	auto zero = Truth(*t, {root}, V());
	auto zeroTrain = Observations(*t, zero, I::Role::Training);
	auto zeroRequest = request;
	zeroRequest.training = zeroTrain;
	zeroRequest.count = zeroTrain->values->size();
	zeroRequest.materials[0].lower = Vec3();
	I::Result zeroFit;
	Must(I::Fit(zeroRequest, Read(), zeroFit));
	CHECK(Peak(zeroFit.materials[0].initial) < .0001);
	CHECK(Peak(zeroFit.values.back().indirect) < 1e-6);
	auto two = PointRoot(scene, *t, V(1, -.4, .6), V(.5, 1, 2), 51);
	auto combined = Truth(*t, {root, two}, V(.7, .18, .08));
	auto twoTrain = Observations(*t, combined, I::Role::Training);
	auto twoRequest = Request(t, {two, root}, twoTrain);
	I::Result overlap;
	Must(I::Fit(twoRequest, Read(), overlap));
	CHECK(overlap.byRoot.size() == 2);
	for (size_t i = 0; i < overlap.values.size(); ++i)
		CHECK(Length(V(overlap.byRoot[0].incident[i]) + V(overlap.byRoot[1].incident[i]) -
					 V(overlap.values[i].indirect)) < 1e-6);
}
void GeometryTests()
{
	for (auto cull : {SurfaceCull::Front, SurfaceCull::Back})
	{
		Scene oneSided(false, true, false, false, false, cull);
		auto transfer = oneSided.Transfer();
		CHECK(transfer->Links().size() == 1);
		const auto &link = transfer->Links().front();
		CHECK(link.conductance == 0 && link.possibleConductance > 0);
		CHECK(link.issues & I::UnknownOptics);
		CHECK(transfer->Report().unknownRays > 0);
	}
	for (unsigned mode = 0; mode < 5; ++mode)
	{
		Scene scene(false, mode <= 2, mode == 1, mode == 2, mode == 3);
		auto t = scene.Transfer(10, true, mode == 4);
		auto root = Seed(*t);
		auto truth = Truth(*t, {root}, V(.7, .18, .08));
		auto train = std::make_shared<Stream>();
		I::Observation obs;
		obs.id = 1;
		obs.patch = 2;
		obs.spatialBlock = 1;
		obs.areaWorldSquared = .04;
		for (int c = 0; c < 3; ++c)
			obs.channels[c].lower = obs.channels[c].upper = V(truth[1])[c];
		train->values->push_back(obs);
		auto request = Request(t, {root}, train);
		I::Result fit;
		Must(I::Fit(request, Read(), fit));
		if (mode == 0)
			CHECK(Peak(fit.values[1].indirect) == 0);
		if (mode == 1)
			CHECK(fit.values[1].indirect.x > 0);
		if (mode >= 2)
		{
			CHECK(Peak(fit.values[1].indirect) == 0);
			CHECK(fit.values[1].indirectUpper.x > 0);
		}
		CHECK(fit.fixedRoots.size() == 1);
		std::ostringstream row;
		row << std::setprecision(12) << "{\"scene\":\"geometry_mode_" << mode
			<< "\",\"receiver_indirect_r\":" << fit.values[1].indirect.x
			<< ",\"receiver_upper_r\":" << fit.values[1].indirectUpper.x
			<< ",\"receiver_comparable\":" << fit.values[1].comparable << ",\"rays\":" << t->Report().rays
			<< ",\"bytes\":" << t->Report().bytes << ",\"false_emitters\":0}";
		rows.push_back(row.str());
	}
	Scene open(false);
	auto t = open.Transfer();
	auto root = Seed(*t, TransportRootKind::Sky, 70);
	auto truth = Truth(*t, {root}, V(.7, .18, .08));
	auto train = std::make_shared<Stream>();
	I::Observation o;
	o.id = 1;
	o.patch = 2;
	o.spatialBlock = 1;
	o.areaWorldSquared = .04;
	for (int c = 0; c < 3; ++c)
		o.channels[c].lower = o.channels[c].upper = V(truth[1])[c];
	train->values->push_back(o);
	auto request = Request(t, {root}, train);
	I::Result sky;
	Must(I::Fit(request, Read(), sky));
	CHECK(Peak(sky.values[1].direct) == 0 && Peak(sky.values[1].sun) == 0);
	CHECK(sky.byRoot[0].kind == TransportRootKind::Sky && sky.values[1].indirect.x > 0);
	auto shortRange = open.Transfer(.5);
	CHECK(!shortRange->Report().allPairsRepresented);
	request.transfer = shortRange;
	I::Result omitted;
	Must(I::Fit(request, Read(), omitted));
	CHECK(!omitted.values[1].upperKnown && (omitted.values[1].issues & I::OmittedLinks));
	// Actual primary source is behind the doorway's side wall. Its direct ray
	// to the right receiver is blocked, while source->ceiling->opening->floor
	// is a valid one-reflection path. No aperture emitter is installed.
	Scene aperture(false, true, true);
	auto at = aperture.Transfer();
	auto physicalRoot = PointRoot(aperture, *at, V(-1, 1, 1), V(10, 10, 10));
	CHECK(physicalRoot.available[0] && physicalRoot.available[1]);
	CHECK(Peak(physicalRoot.incident[0]) > 0 && Peak(physicalRoot.incident[1]) == 0);
	auto reflected = Truth(*at, {physicalRoot}, V(.7, .18, .08));
	auto ar = std::make_shared<Stream>();
	I::Observation ao;
	ao.id = 100;
	ao.patch = 2;
	ao.spatialBlock = 1;
	ao.areaWorldSquared = .04;
	for (int c = 0; c < 3; ++c)
		ao.channels[c].lower = ao.channels[c].upper = V(reflected[1])[c];
	ar->values->push_back(ao);
	auto actualRequest = Request(at, {physicalRoot}, ar);
	I::Result actual;
	Must(I::Fit(actualRequest, Read(), actual));
	CHECK(Peak(actual.values[1].direct) == 0 && actual.values[1].indirect.x > 0 &&
		  actual.objective.data < 1e-10);
	std::ostringstream row;
	row << std::setprecision(12)
		<< "{\"scene\":\"hidden_source_doorway\",\"direct_receiver\":" << actual.values[1].direct.x
		<< ",\"indirect_receiver\":" << actual.values[1].indirect.x << ",\"new_aperture_emitters\":0}";
	rows.push_back(row.str());
}
void NegativeTests()
{
	Scene scene;
	auto t = scene.Transfer();
	auto root = PointRoot(scene, *t, V(-1, -.6, .8), V(3, 3, 3));
	auto truth = Truth(*t, {root}, V(.7, .18, .08));
	auto stream = Observations(*t, truth, I::Role::Training);
	auto request = Request(t, {root}, stream);
	I::Result out;
	request.contract.operation = I::ObservationOperator::Unknown;
	Must(I::Fit(request, Read(), out));
	CHECK(!out.fitted && !out.values[1].comparable);
	request.contract.operation = I::ObservationOperator::IncidentLinear;
	stream->values->front().role = I::Role::Heldout;
	CHECK(I::Fit(request, Read(), out).code == Code::InvalidInput);
	stream->values->front().role = I::Role::Training;
	stream->corrupt = true;
	CHECK(I::Fit(request, Read(), out).code == Code::ProviderError);
	stream->corrupt = false;
	request.options.maxWork = 1;
	CHECK(I::Fit(request, Read(), out).code == Code::WorkLimit);
	request.options.maxWork = 200000000;
	auto tiny = Read();
	tiny.maxBytes = 64;
	CHECK(I::Fit(request, tiny, out).code == Code::MemoryLimit);
	auto sceneEquivalent = request;
	sceneEquivalent.contract.unit = PhotometricUnit::Lux;
	sceneEquivalent.contract.incidentDomain = I::Root::Domain::SceneEquivalentLux;
	sceneEquivalent.contract.calibration.id = 9;
	sceneEquivalent.contract.calibration.kind = CalibrationKind::LegacySceneEquivalent;
	sceneEquivalent.contract.calibration.method =
		"Owned conditional unit mapping, no absolute luminous reference";
	sceneEquivalent.roots[0].domain = I::Root::Domain::SceneEquivalentLux;
	sceneEquivalent.roots[0].calibration = 9;
	Must(I::Fit(sceneEquivalent, Read(), out));
	CHECK(out.fitted && !out.absoluteScaleKnown);
	sceneEquivalent.roots[0].domain = I::Root::Domain::PhysicalLux;
	CHECK(I::Fit(sceneEquivalent, Read(), out).code == Code::InvalidInput);
	class Cancel final : public Cancellation
	{
	  public:
		bool IsCancelled() const noexcept override { return true; }
	} cancel;
	auto read = Read();
	read.cancellation = &cancel;
	CHECK(I::Fit(request, read, out).code == Code::Cancelled);
	auto duplicate = scene.patches;
	duplicate.push_back(duplicate[0]);
	I::BuildInput in;
	in.mapKey = scene.map.sha256;
	in.patchInterpretationKey = Key("negative");
	in.geometry = scene.visibility;
	in.patches = duplicate;
	std::shared_ptr<const I::Transfer> bad;
	CHECK(I::Transfer::Build(in, {}, Read(), bad).code == Code::InvalidInput);
	in.patches = {scene.patches[0], scene.patches[0]};
	in.patches[1].id = 2;
	CHECK(I::Transfer::Build(in, {}, Read(), bad).code == Code::Unavailable);
	in.patches = scene.patches;
	I::BuildOptions bounded;
	bounded.maxPairs = 1;
	CHECK(I::Transfer::Build(in, bounded, Read(), bad).code == Code::WorkLimit);
	CHECK(I::Transfer::Build(in, {}, tiny, bad).code == Code::MemoryLimit);
}
void UnresolvedTests()
{
	Scene scene;
	auto t = scene.Transfer();
	auto root = PointRoot(scene, *t, V(-1, -.6, .8), V(3, 3, 3));
	auto truth = Truth(*t, {root}, V());
	for (auto &v : truth)
		v = (V(v) + V(.2, .1, .05)).Float();
	auto train = Observations(*t, truth, I::Role::Training), held = Observations(*t, truth, I::Role::Heldout);
	auto request = Request(t, {root}, train);
	request.materials[0] = MakeReflectance(1, V());
	request.options.unresolved.enabled = true;
	request.options.unresolved.maximumValue = .4;
	request.options.unresolved.originWorld = Vec3(-5, -5, 0);
	request.options.unresolved.cellWorld = 10;
	request.options.unresolved.magnitudePenalty = .0001;
	I::Result fitted;
	Must(I::Fit(request, Read(), fitted));
	I::Score score;
	Must(I::EvaluateFrozen(fitted, held, held->values->size(), I::Role::Heldout, Read(), score));
	CHECK(score.available && score.intervalMse < score.directOnlyMse * .01);
	std::printf("unresolved held=%g baseline=%g last=%g rays=%llu objective=%g\n", score.intervalMse,
				score.directOnlyMse, fitted.values.back().unresolved.x, (unsigned long long)fitted.work.rays,
				fitted.objective.Total());
	CHECK(Peak(fitted.values.back().indirect) == 0 && fitted.values.back().unresolved.x > 0);
	CHECK(Length(V(fitted.fixedRoots[0].incident[0]) - V(root.incident[0])) == 0);
	std::ostringstream row;
	row << std::setprecision(12) << "{\"scene\":\"unresolved_fill\",\"heldout_mse\":" << score.intervalMse
		<< ",\"no_field_mse\":" << score.directOnlyMse << ",\"fixed_direct_amplitudes_changed\":false}";
	rows.push_back(row.str());
	// A deliberately unconstrained nonnegative per-sample residual can explain
	// all training RGB with zero direct source. Production has no such variable.
	double naiveSource = 0, naiveTrain = 0;
	for (const auto &o : *train->values)
		for (int c = 0; c < 3; ++c)
		{
			double residual = o.channels[c].lower;
			naiveTrain += std::pow(naiveSource + residual - o.channels[c].lower, 2);
		}
	CHECK(naiveSource == 0 && naiveTrain == 0);
	CHECK(fitted.fixedRootsKey != Disk::Digest{});
	// Pure authored/legacy ambient is its own boundary root, never a point light.
	auto ambient = Seed(*t, TransportRootKind::LegacyAmbient, 80);
	for (auto &v : ambient.incident)
		v = Vec3(.2, .3, .4);
	auto ambientTruth = ambient.incident;
	auto ambientTrain = Observations(*t, ambientTruth, I::Role::Training);
	auto ambientRequest = Request(t, {ambient}, ambientTrain);
	ambientRequest.materials[0] = MakeReflectance(1, V());
	I::Result fill;
	Must(I::Fit(ambientRequest, Read(), fill));
	CHECK(Peak(fill.values.back().direct) == 0 && fill.values.back().ambient.y == .3f);
	CHECK(Peak(fill.values.back().indirect) == 0);
	// Same surface/cell on both sides of an opaque or one-sided wall. Only
	// left-side training is supplied. No residual may interpolate across it.
	for (auto cull : {SurfaceCull::None, SurfaceCull::Front, SurfaceCull::Back})
	{
		Scene divided(true, true, false, false, false, cull);
		divided.patches.erase(divided.patches.begin()); // coplanar receivers only: no transport links
		auto transfer = divided.Transfer();
		auto boundary = Seed(*transfer, TransportRootKind::LegacyAmbient, 81);
		boundary.incident.assign(transfer->Patches().size(), Vec3(.1, .1, .1));
		std::vector<Vec3> measured(transfer->Patches().size(), Vec3(.2, .2, .2));
		auto left = Observations(*transfer, measured, I::Role::Training);
		left->values->erase(std::remove_if(left->values->begin(), left->values->end(),
										   [&](const I::Observation &sample) {
											   for (const auto &p : transfer->Patches())
												   if (p.id == sample.patch)
													   return p.receiver.world.x >= -.1;
											   return true;
										   }),
							left->values->end());
		auto separated = Request(transfer, {boundary}, left);
		separated.materials = {MakeReflectance(2, V())};
		separated.options.unresolved = request.options.unresolved;
		I::Result isolated;
		Must(I::Fit(separated, Read(), isolated));
		CHECK(isolated.values.front().unresolved.x > .09); // positive training support
		CHECK(isolated.values.back().unresolved.x == 0);   // right-side extrapolation forbidden
	}
}
void EncodingTests()
{
	Scene scene;
	auto t = scene.Transfer();
	auto root = PointRoot(scene, *t, V(-1, -.6, .8), V(3000, 2000, 1000));
	auto truth = Truth(*t, {root}, V(.7, .18, .08));
	auto train = Observations(*t, truth, I::Role::Training);
	auto request = Request(t, {root}, train);
	request.contract.operation = I::ObservationOperator::NetRadiantEncoded;
	request.contract.incidentDomain = I::Root::Domain::CompilerRelative255;
	request.roots[0].domain = I::Root::Domain::CompilerRelative255;
	Bake::Recipe recipe;
	recipe.fast = false;
	request.contract.profile = Bake::NetRadiantProfile(recipe);
	unsigned clipped = 0;
	for (auto &o : *train->values)
	{
		Vec3 encoded;
		uint8_t packed[3];
		Must(Bake::Encode(request.contract.profile, truth[size_t(o.patch - 1)], encoded, packed));
		for (int c = 0; c < 3; ++c)
		{
			o.channels[c] = Amplitude::Rgb8(packed[c]);
			clipped += packed[c] == 255;
		}
	}
	I::Result result;
	Must(I::Fit(request, Read(), result));
	CHECK(result.objective.data < 1e-8);
	CHECK(clipped > 0);
	// Shared-peak encoding is applied once to the complete sum. Component-wise
	// clipped RGB addition would produce a different observation.
	Vec3 a, b, sum;
	uint8_t packed[3];
	Must(Bake::Encode(request.contract.profile, Vec3(300, 0, 0), a, packed));
	Must(Bake::Encode(request.contract.profile, Vec3(0, 300, 0), b, packed));
	Must(Bake::Encode(request.contract.profile, Vec3(300, 300, 0), sum, packed));
	CHECK(Length(V(a) + V(b) - V(sum)) < 1e-5); // symmetric example is additive by coincidence
	Must(Bake::Encode(request.contract.profile, Vec3(300, 100, 0), a, packed));
	Must(Bake::Encode(request.contract.profile, Vec3(0, 300, 0), b, packed));
	Must(Bake::Encode(request.contract.profile, Vec3(300, 400, 0), sum, packed));
	CHECK(Length(V(a) + V(b) - V(sum)) > 10);
}
void CanonicalTests()
{
	Scene scene;
	auto t = scene.Transfer();
	auto root = PointRoot(scene, *t, V(-1, -.6, .8), V(3, 3, 3));
	auto truth = Truth(*t, {root}, V(.7, .18, .08));
	auto train = Observations(*t, truth, I::Role::Training);
	auto request = Request(t, {root}, train);
	Calibration calibration;
	calibration.id = 2;
	calibration.kind = CalibrationKind::UncalibratedRelative;
	calibration.provenance = 1;
	calibration.method = "Owned relative scene, no absolute photometry";
	request.contract.calibration = calibration;
	request.roots[0].calibration = calibration.id;
	I::Result result;
	Must(I::Fit(request, Read(), result));
	ReconstructedLightingAsset asset;
	asset.lineage = "s10-owned-conditional";
	asset.units.worldUnitsPerMeter = 1;
	asset.provenance = 1;
	Provenance p;
	p.id = 1;
	p.kind = ProvenanceKind::Reconstruction;
	p.producer = "s10 test";
	p.method = "Conditional one-bounce";
	asset.provenanceRecords.push_back(p);
	asset.calibrations.push_back(calibration);
	PhysicalEmitter e;
	e.source.id = 50;
	e.source.provenance = 1;
	e.kind = EmitterKind::Point;
	e.position.known = true;
	e.position.world = Vec3(-1, -.6, .8);
	e.angular.kind = AngularKind::Isotropic;
	asset.emitters.push_back(e);
	std::vector<Disk::Chunk> chunks;
	Must(I::AppendFields(*t, result, 100, 1, asset, chunks, Read()));
	CHECK(asset.emitters.size() == 1 && asset.transport.size() == 1);
	CHECK(asset.transport[0].membership.directSun == FieldMembership::Excluded);
	CHECK(asset.transport[0].membership.maximumReflectionOrder == 1);
	CHECK(asset.transport[0].membership.roots[0].source == 50);
	CHECK(asset.datasets[0].elementCount == t->Patches().size());
	CHECK(asset.datasets[0].encoding == "org.openjk.passive-transport-samples/source-linear-relative");
	Disk::Fingerprint fp;
	fp.algorithm = "org.openjk.s10-owned";
	fp.algorithmVersion = 1;
	fp.units = asset.units;
	fp.complete.fill(true);
	for (unsigned i = 0; i < unsigned(Disk::InputKind::Count); ++i)
	{
		Disk::InputContent c;
		c.kind = Disk::InputKind(i);
		c.name = "owned/" + std::to_string(i);
		c.sha256 = Key(c.name);
		fp.inputs.push_back(c);
	}
	std::vector<uint8_t> encoded;
	auto err = Disk::Encode(asset, fp, chunks, encoded);
	CHECK(bool(err));
	if (!err)
		std::printf("canonical %s\n", err.message.c_str());
	Disk::MemoryReader reader(encoded.data(), encoded.size());
	ReconstructedLightingAsset copy;
	Disk::Index index;
	CHECK(bool(Disk::Open(reader, fp, copy, index)));
	CHECK(copy.transport.size() == 1 &&
		  copy.transport[0].membership.model == asset.transport[0].membership.model);
	std::vector<uint8_t> payload;
	CHECK(bool(Disk::ReadChunk(reader, index, asset.datasets[0].id, 0, payload)));
	CHECK(payload == chunks[0].bytes);
	auto old = asset;
	old.version = 1;
	CHECK(!Validate(old));
	auto bad = asset;
	bad.transport[0].membership.roots[0].source = 999;
	CHECK(!Validate(bad));
	bad = asset;
	bad.transport[0].membership.minimumReflectionOrder = 2;
	CHECK(!Validate(bad));
	bad = asset;
	bad.transport[0].membership.roots[0].kind = TransportRootKind::Sky;
	CHECK(!Validate(bad));
	bad = asset;
	bad.transport[0].membership.reflected = FieldMembership::Excluded;
	CHECK(!Validate(bad));
	auto saved = asset.transport.size();
	CHECK(I::AppendFields(*t, result, 100, 1, asset, chunks, Read()).code == Code::InvalidInput);
	CHECK(asset.transport.size() == saved);
	auto malformed = result;
	malformed.byRoot[0].incident.clear();
	CHECK(I::AppendFields(*t, malformed, 200, 1, asset, chunks, Read()).code == Code::InvalidInput);
}
void ProductionRootTests()
{
	Scene scene;
	auto t = scene.Transfer();
	auto sources = std::make_shared<std::vector<Bake::SourceHypothesis>>(1);
	auto &s = (*sources)[0];
	s.id = 50;
	s.kind = Bake::SourceKind::Point;
	s.position.known = true;
	s.position.world = Vec3(-1, -.6, .8);
	s.amplitudeKnown = true;
	s.compilerRgbAmplitude = Vec3(300, 200, 100);
	s.angular.kind = AngularKind::Isotropic;
	Bake::Recipe recipe;
	recipe.fast = false;
	recipe.receiverPushWorld = .001;
	recipe.minimumDistanceWorld = .01;
	auto profile = Bake::NetRadiantProfile(recipe);
	Bake::EvaluationContext context;
	context.visibility = scene.visibility;
	context.geometryKey = t->GeometryKey();
	context.materialKey = Key("known mapping");
	context.observationPolicyKey = Key("explicit conditional sampling recipe");
	Bake::Options options;
	options.output = Bake::Domain::CompilerLinearRelative255;
	options.allowAssumed = true;
	I::Root root;
	Bake::Work work;
	Must(I::PredictRoot(*t, TransportRootKind::LocalEmitter, profile, Lease(sources), context, options,
						Read(), root, work));
	CHECK(root.incident.size() == t->Patches().size() && root.domain == I::Root::Domain::CompilerRelative255);
	CHECK(Peak(root.incident.back()) > 0 && work.rays > 0);
	CHECK(root.source == 50);
	options.output = Bake::Domain::GridAmbientDirected;
	CHECK(I::PredictRoot(*t, TransportRootKind::LocalEmitter, profile, Lease(sources), context, options,
						 Read(), root, work)
			  .code == Code::Unavailable);
	options.output = Bake::Domain::CompilerLinearRelative255;
	profile = {};
	CHECK(I::PredictRoot(*t, TransportRootKind::LocalEmitter, profile, Lease(sources), context, options,
						 Read(), root, work)
			  .code == Code::Unavailable);
}
int main(int argc, char **argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	auto start = std::chrono::steady_clock::now();
	try
	{
		NetworkTests(argc > 1 ? argv[1] : "");
		GeometryTests();
		NegativeTests();
		UnresolvedTests();
		EncodingTests();
		CanonicalTests();
		ProductionRootTests();
		if (argc > 1)
		{
			std::ofstream out(std::string(argv[1]) + "-results.json");
			out << "{\"schema\":\"org.openjk.indirect-s10.v1\",\"checks\":" << checks
				<< ",\"failures\":" << failures << ",\"scenes\":[";
			for (size_t i = 0; i < rows.size(); ++i)
				out << (i ? "," : "") << rows[i];
			out << "]}\n";
		}
	}
	catch (const std::exception &e)
	{
		std::printf("EXCEPTION %s\n", e.what());
		++failures;
	}
	std::printf("S10: %d checks, %d failures, %.3fs\n", checks, failures,
				std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
	return failures ? 1 : 0;
}
