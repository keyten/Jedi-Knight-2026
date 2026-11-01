// X09: physical support is independent of baked RGB, observation IDs and folds.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "joint_test_scene.h"
#include "lightrecon/transport_mesh.h"
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
using namespace JointTest;
namespace T = Indirect::Topology;
int checks = 0, failures = 0;
#define CHECK(x)                                                                                   \
	do                                                                                             \
	{                                                                                              \
		++checks;                                                                                  \
		if (!(x))                                                                                  \
		{                                                                                          \
			++failures;                                                                            \
			std::printf("FAIL %d %s\n", __LINE__, #x);                                             \
		}                                                                                          \
	} while (0)
bool Close(double a, double b, double e = 1e-6)
{
	return std::abs(a - b) <= e * std::max(1., std::max(std::abs(a), std::abs(b)));
}
T::Support Tri(Id surface, Id tri, Id material, V a, V b, V c, V normal)
{
	T::Support s;
	s.surface = surface;
	s.triangle = tri;
	s.material = material;
	s.sideKnown = true;
	s.world[0] = a.Float();
	s.world[1] = b.Float();
	s.world[2] = c.Float();
	s.normal = normal.Float();
	return s;
}
std::vector<T::Support> Quad(Id surface, V p, V n, double side = 1, Id material = 1)
{
	auto patch = Patch(1, surface, material, p, n, side);
	std::vector<T::Support> s;
	for (size_t i = 0; i < patch.receiver.footprint.count; ++i)
	{
		const auto &t = patch.receiver.footprint.data[i];
		s.push_back(Tri(surface, i + 1, material, t.world[0], t.world[1], t.world[2], n));
	}
	return s;
}
std::shared_ptr<const T::Mesh> Mesh(std::vector<T::Support> s, T::Options o = {})
{
	std::shared_ptr<const T::Mesh> out;
	Must(T::Mesh::Build(Key("X09 owned map"), Key("X09 owned geometry"), s, o, Read(), out));
	return out;
}
std::vector<std::string> records;
void Record(const std::string &name, const T::Mesh &mesh)
{
	auto &r = mesh.Diagnostics();
	for (size_t i = 0; i < mesh.Elements().size(); ++i)
		for (size_t j = 0; j < i; ++j)
		{
			const auto &a = mesh.Elements()[i].patch, &b = mesh.Elements()[j].patch;
			for (size_t x = 0; x < a.receiver.footprint.count; ++x)
				for (size_t y = 0; y < b.receiver.footprint.count; ++y)
				{
					auto c = T::Inspect(a.receiver.footprint.data[x], a.receiver.geometricNormal,
										b.receiver.footprint.data[y], b.receiver.geometricNormal);
					CHECK(c.relation != T::Relation::SameSideOverlap ||
						  c.sharedArea <= 1e-10 * std::min(a.areaWorldSquared, b.areaWorldSquared));
				}
		}
	std::ostringstream out;
	out << std::setprecision(17) << "{\"case\":\"" << name << "\",\"mesh_key\":\""
		<< Disk::Hex(mesh.Key()) << "\",\"patches\":" << mesh.Elements().size()
		<< ",\"input_area\":" << r.scopedArea << ",\"physical_area\":" << r.outputArea
		<< ",\"deduplicated_area\":" << r.deduplicatedArea
		<< ",\"unsupported_area\":" << r.unsupportedArea << ",\"overlaps\":" << r.duplicateOverlaps
		<< ",\"conflicts\":" << r.conflictingOverlaps << ",\"crossings\":" << r.crossingPairs
		<< ",\"work\":" << r.work << ",\"bytes\":" << r.bytes << '}';
	records.push_back(out.str());
}
void TopologyTests()
{
	auto unit = Quad(1, V(), V(0, 0, 1));
	auto adjacent = Quad(2, V(1, 0, 0), V(0, 0, 1));
	auto s = unit;
	s.insert(s.end(), adjacent.begin(), adjacent.end());
	auto m = Mesh(s);
	CHECK(Close(m->Diagnostics().outputArea, 2));
	CHECK(m->Diagnostics().duplicateOverlaps == 0);
	Record("adjacent_coplanar", *m);
	auto duplicate = unit;
	for (auto &v : duplicate)
		v.surface = 2;
	s = unit;
	s.insert(s.end(), duplicate.begin(), duplicate.end());
	m = Mesh(s);
	CHECK(Close(m->Diagnostics().outputArea, 1));
	CHECK(Close(m->Diagnostics().deduplicatedArea, 1));
	CHECK(m->Diagnostics().duplicateOverlaps > 0);
	for (auto &e : m->Elements())
		CHECK(e.sources.size() == 2);
	Record("duplicate_source_triangles", *m);
	auto receiver = Patch(50, 2, 1, V(), V(0, 0, 1), 1).receiver;
	std::shared_ptr<const T::Mapping> map;
	Must(m->Map(receiver, Read(), map));
	CHECK(map->State() == T::Availability::Available);
	CHECK(Close(map->Area(), 1));
	CHECK(Close(map->CoveredArea(), 1));
	auto key = m->Key();
	for (unsigned role = 0; role < 3; ++role)
	{
		receiver.sample = 200 + role;
		Must(m->Map(receiver, Read(), map));
		CHECK(Close(map->CoveredArea(), 1));
		CHECK(m->Key() == key);
	} // no Role or target API exists in Mesh
	std::reverse(s.begin(), s.end());
	auto repeat = Mesh(s);
	CHECK(repeat->Key() == key);
	CHECK(repeat->Diagnostics().outputArea == m->Diagnostics().outputArea);
	// Partially overlapping independent charts: 1 + 1 - .5 = 1.5 physical units^2.
	auto shifted = Quad(2, V(.5, 0, 0), V(0, 0, 1));
	s = unit;
	s.insert(s.end(), shifted.begin(), shifted.end());
	m = Mesh(s);
	CHECK(Close(m->Diagnostics().outputArea, 1.5));
	CHECK(Close(m->Diagnostics().deduplicatedArea, .5));
	Record("overlapping_chart_geometry", *m);
	for (auto &v : shifted)
		v.material = 2;
	s = unit;
	s.insert(s.end(), shifted.begin(), shifted.end());
	m = Mesh(s);
	CHECK(Close(m->Diagnostics().outputArea, 1));
	CHECK(m->Diagnostics().conflictingOverlaps > 0);
	Record("conflicting_material_overlap", *m);
	for (auto &v : shifted)
	{
		v.material = 1;
		v.style = 1;
	}
	s = unit;
	s.insert(s.end(), shifted.begin(), shifted.end());
	m = Mesh(s);
	CHECK(Close(m->Diagnostics().outputArea, 1));
	CHECK(m->Diagnostics().conflictingOverlaps > 0);
	Record("conflicting_style_overlap", *m);
	for (auto &v : shifted)
	{
		v.material = 2;
		v.style = 0;
	}
	s = unit;
	s.insert(s.end(), shifted.begin(), shifted.end());
	auto rear = Quad(3, V(), V(0, 0, -1));
	s.insert(s.end(), rear.begin(), rear.end());
	m = Mesh(s);
	CHECK(Close(m->Diagnostics().outputArea, 2));
	CHECK(Close(m->Diagnostics().scopedArea, m->Diagnostics().outputArea +
												 m->Diagnostics().deduplicatedArea +
												 m->Diagnostics().unsupportedArea));
	Record("conflict_does_not_erase_opposite_side", *m);
	// Opposite material sides remain distinct; they do not become one reflector.
	auto back = Quad(2, V(), V(0, 0, -1));
	s = unit;
	s.insert(s.end(), back.begin(), back.end());
	m = Mesh(s);
	CHECK(Close(m->Diagnostics().outputArea, 2));
	CHECK(m->Diagnostics().opposingPairs > 0);
	Record("opposite_sheet_sides", *m);
	// A very thin but nonzero separation is not a coplanar merge tolerance.
	auto thin = Quad(2, V(0, 0, 1e-9), V(0, 0, 1));
	s = unit;
	s.insert(s.end(), thin.begin(), thin.end());
	m = Mesh(s);
	CHECK(Close(m->Diagnostics().outputArea, 2));
	CHECK(m->Diagnostics().duplicateOverlaps == 0);
	// True crossing interiors, plus unrelated geometry: retain only valid support.
	auto crossing = Quad(2, V(), V(1, 0, 0));
	auto far = Quad(3, V(5, 0, 0), V(0, 0, 1));
	s = unit;
	s.insert(s.end(), crossing.begin(), crossing.end());
	s.insert(s.end(), far.begin(), far.end());
	m = Mesh(s);
	CHECK(m->Diagnostics().crossingPairs > 0);
	CHECK(m->Diagnostics().unsupportedArea > 0);
	CHECK(m->Diagnostics().outputArea >= 1);
	CHECK(m->Diagnostics().outputArea < 3);
	Record("crossing_local_exclusion", *m);
	s = unit;
	s[0].sideKnown = false;
	m = Mesh(s);
	CHECK(Close(m->Diagnostics().outputArea, .5));
	CHECK(m->Diagnostics().issues & T::UnknownSide);
	Record("unknown_material_side", *m);
	s = unit;
	s.push_back(s.front());
	std::shared_ptr<const T::Mesh> bad;
	CHECK(T::Mesh::Build(Key("m"), Key("g"), s, {}, Read(), bad).code == Code::InvalidInput);
	CHECK(!bad);
	s = unit;
	s[0].normal = Vec3(1, 0, 0);
	CHECK(T::Mesh::Build(Key("m"), Key("g"), s, {}, Read(), bad).code == Code::InvalidInput);
	T::Options o;
	o.maxWork = 1;
	CHECK(T::Mesh::Build(Key("m"), Key("g"), unit, o, Read(), bad).code == Code::WorkLimit);
	class Stop : public Cancellation
	{
	  public:
		bool IsCancelled() const noexcept override
		{
			return true;
		}
	} stop;
	auto read = Read();
	read.cancellation = &stop;
	CHECK(T::Mesh::Build(Key("m"), Key("g"), unit, {}, read, bad).code == Code::Cancelled);
	// Source geometry mutation changes the mesh/cache identity.
	auto changed = unit;
	changed[0].world[0].x += .01f;
	CHECK(Mesh(changed)->Key() != Mesh(unit)->Key());
}
double ParallelSquareReference(double h)
{
	// Independent 2-D convolution of the four-dimensional finite-square integral.
	const unsigned n = 256;
	double sum = 0;
	for (unsigned i = 0; i < n; ++i)
		for (unsigned j = 0; j < n; ++j)
		{
			double x = (i + .5) / n, y = (j + .5) / n;
			sum += (1 - x) * (1 - y) * h * h / std::pow(h * h + x * x + y * y, 2);
		}
	return 4 * sum / (Pi * n * n);
}
void TransferTests()
{
	Scene scene(false);
	scene.patches = {Patch(1, 1, 1, V(0, 0, 1), V(0, 0, -1), 1),
					 Patch(2, 2, 2, V(0, 0, 0), V(0, 0, 1), 1)};
	// Use a clean visibility scene containing the actual finite patches, not moved
	// targets in the old fixture. Domain classification is independently explicit.
	scene.surfaces.clear();
	scene.Quad(V(-.5, -.5, 1), V(.5, -.5, 1), V(.5, .5, 1), V(-.5, .5, 1));
	scene.Quad(V(-.5, -.5, 0), V(.5, -.5, 0), V(.5, .5, 0), V(-.5, .5, 0));
	Rebuild(scene);
	I::BuildInput in;
	in.mapKey = scene.map.sha256;
	in.geometry = scene.visibility;
	in.patchInterpretationKey = Key("X09 disjoint finite-square reference");
	in.patches = scene.patches;
	in.completePatchSet = true;
	I::BuildOptions o;
	o.subdivisions = 0;
	o.relativeRuleTolerance = .005;
	o.maxQuadraturePairsPerLink = 1048576;
	std::shared_ptr<const I::Transfer> t;
	Must(I::Transfer::Build(in, o, Read(), t));
	CHECK(t->Links().size() == 1);
	double c = t->Links()[0].conductance, reference = ParallelSquareReference(1);
	CHECK(c > 0);
	CHECK(std::abs(c - reference) < .00025);
	CHECK(t->Report().maximumOutgoingFraction <= 1);
	CHECK(Close(c / in.patches[0].areaWorldSquared * in.patches[0].areaWorldSquared,
				c / in.patches[1].areaWorldSquared * in.patches[1].areaWorldSquared, 1e-12));
	std::ostringstream row;
	row << std::setprecision(17) << "{\"case\":\"finite_square_reference\",\"conductance\":" << c
		<< ",\"independent_reference\":" << reference
		<< ",\"absolute_error\":" << std::abs(c - reference) << ",\"rays\":" << t->Report().rays
		<< ",\"refined_pairs\":" << t->Report().refinedPairs
		<< ",\"maximum_outgoing\":" << t->Report().maximumOutgoingFraction << '}';
	records.push_back(row.str());
	// The reflectance-root test remains source linked and passive, including red bounce.
	I::Request q;
	q.transfer = t;
	q.materials = {KnownMaterial(1, V(.7, 0, 0)), KnownMaterial(2, V())};
	q.contract.incidentDomain = I::Root::Domain::SourceLinearRelative;
	q.contract.operation = I::ObservationOperator::IncidentLinear;
	q.contract.transferAttested = q.contract.primariesAttested =
		q.contract.physicalTransportAssumed = true;
	q.contract.provenance = "Owned X09 finite-area passive reference";
	I::Root root;
	root.source = 50;
	root.kind = TransportRootKind::LocalEmitter;
	root.incident = {Vec3(2, 2, 2), Vec3()};
	root.available = {1, 1};
	root.generation = Key("frozen source root");
	root.provenance = "known external source, no patch emission";
	q.roots = {root};
	auto data = std::make_shared<Stream<I::Observation>>();
	I::Observation obs;
	obs.id = 90;
	obs.patch = 2;
	obs.spatialBlock = 1;
	obs.areaWorldSquared = 1;
	data->data->push_back(obs);
	q.training = data;
	q.count = 1;
	q.trainingKey = Key("training RGB not mesh input");
	I::Result result;
	Must(I::Fit(q, Read(), result));
	CHECK(result.values[1].indirect.x > 0);
	CHECK(result.values[1].indirect.y == 0 && result.values[1].indirect.z == 0);
	CHECK(result.maximumReflectedPowerFraction <= .7 + 1e-7);
	// Insufficient near-field quadrature is a local uncertain link, no r epsilon.
	auto moveReflector=[&](double height){
		for(auto &v:scene.surfaces[0].vertices)v.position.z=float(height);
		Rebuild(scene);in.mapKey=scene.map.sha256;in.geometry=scene.visibility;
	};
	moveReflector(.001);
	in.patches[0] = Patch(1, 1, 1, V(0, 0, .001), V(0, 0, -1), 1);
	o.maxQuadraturePairsPerLink = 256;
	Must(I::Transfer::Build(in, o, Read(), t));
	CHECK(t->Report().unresolvedPairs > 0);
	CHECK(t->Links()[0].conductance == 0);
	CHECK(t->Links()[0].possibleConductance > 0);
	CHECK(t->Links()[0].issues & I::UnresolvedQuadrature);
	// Exact opposing sheet sides have zero inter-side diffuse exchange, not a singularity.
	moveReflector(0);
	in.patches[0] = Patch(1, 1, 1, V(), V(0, 0, -1), 1);
	Must(I::Transfer::Build(in, o, Read(), t));
	CHECK(t->Links().empty());
	CHECK(t->Report().coplanarPairs == 1);
	moveReflector(1);
	in.patches = scene.patches;
	o.maxRays = 1;
	CHECK(I::Transfer::Build(in, o, Read(), t).code == Code::WorkLimit);
	CHECK(!t);
	o.allowPartialWork = true;
	Must(I::Transfer::Build(in, o, Read(), t));
	CHECK(t->Report().localBudgetExceeded);
	CHECK(!t->Report().pairSearchComplete && !t->Report().completePatchSet);
	CHECK(t->Links().empty());
}
void JointMappingTests()
{
	Rig rig;
	auto &r = rig.request;
	std::vector<T::Support> supports;
	for (const auto &p : r.transfer->Patches())
		for (size_t i = 0; i < p.receiver.footprint.count; ++i)
		{
			auto &f = p.receiver.footprint.data[i];
			supports.push_back(Tri(p.receiver.surface, p.id * 100 + i + 1, p.material, f.world[0],
								   f.world[1], f.world[2], p.receiver.geometricNormal));
		}
	std::shared_ptr<const T::Mesh> mesh;
	Must(T::Mesh::Build(r.mapKey, r.geometry.geometryKey, supports, {}, Read(), mesh));
	I::BuildInput in;
	in.mapKey = r.mapKey;
	in.geometry = rig.scene.visibility;
	in.patchInterpretationKey = mesh->Key();
	for (auto &e : mesh->Elements())
		in.patches.push_back(e.patch);
	I::BuildOptions o;
	o.subdivisions = 0;
	Must(I::Transfer::Build(in, o, Read(), r.transfer));
	r.transportMesh = mesh;
	auto candidate = Point(50, V(0, 0, 1.1), true);
	candidate.fitAmplitude = false;
	candidate.shape.initial.rgbAmplitude = Vec3(2, 1, .5);
	r.candidates = {candidate};
	J::Model seed;
	seed.profile = 0;
	seed.geometryKey = r.geometry.geometryKey;
	seed.transferKey = r.transfer->Key();
	seed.materials = r.materials;
	J::Source source;
	source.candidate = candidate;
	source.value = candidate.shape.initial;
	seed.sources = {source};
	// The original receiver is not the transport element centroid. It must be
	// evaluated there, rather than copying a neighbouring patch's direct value.
	J::Observation obs;
	obs.id = 999;
	obs.block = 1;
	obs.measure = 1;
	obs.comparable = true;
	obs.receiver = rig.scene.patches[2].receiver;
	obs.receiver.sample = obs.id;
	Must(mesh->Map(obs.receiver, Read(), obs.transport));
	CHECK(obs.transport->State() == T::Availability::Available);
	rig.train->data->push_back(obs);
	r.training = rig.train;
	r.count = 1;
	J::Model evaluated;
	J::Score score;
	J::Work work;
	std::vector<J::Prediction> predictions;
	Must(J::AssessHypothesis(r, seed, Read(), evaluated, score, work, &predictions));
	CHECK(score.used == 1);
	CHECK(predictions[0].transportAvailable);
	auto receivers = std::make_shared<std::vector<Bake::Receiver>>(1, obs.receiver);
	std::shared_ptr<const S::Block> block;
	Must(S::Prepare(r.profiles[0].model, {source.value}, Lease(receivers), r.geometry,
					r.strictSampling, Read(), block));
	std::vector<S::Prediction> direct;
	S::Work w;
	Must(block->Evaluate({source.value.rgbAmplitude}, Read(), direct, w));
	CHECK(Length(V(predictions[0].rgb) - V(direct[0].value)) < 1e-6);
	r.profiles[0].contract.physicalTransportAssumed = true;
	r.materials[0] = KnownMaterial(1, V(.7, 0, 0));
	seed.materials = r.materials;
	Must(J::AssessHypothesis(r, seed, Read(), evaluated, score, work, &predictions));
	CHECK(predictions[0].indirect.x > 0);
	CHECK(predictions[0].indirect.y == 0 && predictions[0].indirect.z == 0);
	CHECK(predictions[0].rgb.x > direct[0].value.x);
	CHECK(evaluated.reflectedPowerFraction <= .7 + 1e-6);
	// Geometry-only mapping is reusable on held-out data; RGB is read only by the
	// trusted frozen scorer, never during mesh build or mapping construction.
	auto held = std::make_shared<Stream<J::Observation>>();
	obs.role = J::Role::Heldout;
	held->data->push_back(obs);
	J::Score heldScore;
	Must(J::EvaluateFrozen(r, evaluated, held, 1, J::Role::Heldout, Key("withheld targets"), Read(),
						   heldScore));
	CHECK(heldScore.used == 1);
	// Incomplete GI does not silently become a supported black indirect field.
	rig.train->data->front().receiver = Patch(1000, 1, 2, V(.25, .1, 0), V(0, 0, 1), .05).receiver;
	rig.train->data->front().id = 1000;
	rig.train->data->front().receiver.sample = 1000;
	Must(mesh->Map(rig.train->data->front().receiver, Read(), rig.train->data->front().transport));
	r.profiles[0].contract.physicalTransportAssumed = true;
	Must(J::AssessHypothesis(r, seed, Read(), evaluated, score, work, &predictions));
	CHECK(score.unavailable == 1);
	CHECK(score.transportUnavailable == 1);
	r.directOnlyConditional = true;
	Must(J::AssessHypothesis(r, seed, Read(), evaluated, score, work, &predictions));
	CHECK(score.used == 1);
	CHECK(score.directOnlyConditional == 1);
	CHECK(predictions[0].issues & I::MissingTransport);
	CHECK(predictions[0].rgb.x > 0);
	// A stale mapping cannot be used with moved observation geometry.
	rig.train->data->front().receiver.world.x += .1f;
	CHECK(J::AssessHypothesis(r, seed, Read(), evaluated, score, work).code == Code::InvalidInput);
}
int main(int argc, char **argv)
{
	try
	{
		TopologyTests();
		TransferTests();
		JointMappingTests();
	}
	catch (const std::exception &e)
	{
		++failures;
		std::printf("EXCEPTION %s\n", e.what());
	}
	if (argc > 1)
	{
		std::ofstream out(argv[1]);
		out << "{\"schema\":\"org.openjk.transport-topology-tests.v1\",\"checks\":" << checks
			<< ",\"failures\":" << failures << ",\"cases\":[";
		for (size_t i = 0; i < records.size(); ++i)
			out << (i ? "," : "") << records[i];
		out << "]}\n";
	}
	std::printf("X09: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
