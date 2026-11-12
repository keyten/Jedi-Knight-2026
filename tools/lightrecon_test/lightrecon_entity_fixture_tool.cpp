// Offline authored input for C04 gameplay validation. No stock light inference.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/field_builder.h"
#include "lightrecon/geometry_key.h"
#include "lightrecon/offline.h"
#include "lightrecon/persistent.h"
#include "probe_fixtures.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;

namespace {
void Check(Error e)
{
	if (!e) throw std::runtime_error(e.message);
}

Disk::Fingerprint Manifest(const char *filename)
{
	std::ifstream in(filename);
	Disk::Fingerprint f;
	std::string expected, line;
	while (std::getline(in, line))
	{
		const auto manifest = line.find("dependency-manifest algorithm=");
		const auto dependency = line.find("dependency kind=");
		if (manifest == std::string::npos && dependency == std::string::npos)
			continue;
		const bool header = manifest != std::string::npos;
		if (header)
		{
			f = {};
			f.complete.fill(true);
		}
		std::istringstream row(line.substr(header ? manifest : dependency));
		std::string token;
		row >> token;
		Disk::InputContent input;
		while (row >> token)
		{
			const auto split = token.find('=');
			const auto k = token.substr(0, split), v = token.substr(split + 1);
			if (k == "algorithm") f.algorithm = v;
			else if (k == "version") f.algorithmVersion = uint32_t(std::stoul(v));
			else if (k == "world-units") f.units.worldUnitsPerMeter = std::stof(v);
			else if (k == "physical-to-scene") f.units.physicalToScene = std::stof(v);
			else if (k == "key") expected = v;
			else if (k == "kind") input.kind = Disk::InputKind(std::stoul(v));
			else if (k == "bytes") input.bytes = std::stoull(v);
			else if (k == "name") input.name = v;
			else if (k == "sha256")
			{
				if (v.size() != 64) throw std::runtime_error("Invalid input SHA256");
				for (size_t j = 0; j < 32; ++j)
					input.sha256[j] = uint8_t(std::stoul(v.substr(2*j, 2), nullptr, 16));
			}
		}
		if (!header) f.inputs.push_back(input);
	}
	Disk::Digest key;
	if (!Disk::MakeKey(f, key) || Disk::Hex(key) != expected)
		throw std::runtime_error("Invalid dependency manifest");
	return f;
}

enum class Stimulus { Height, SeparateRooms, DoorwayGradient };

Fields::Artifact Author(const Probes::Input &geometry, const Probes::Options &options,
	const Disk::Fingerprint &fingerprint, const ReadContext &read, Stimulus stimulus = Stimulus::Height)
{
	Fields::Artifact a;
	Check(Probes::Build(geometry, options, read, a.topology));
	a.asset.lineage = "c04-authored-reflected-validation";
	a.asset.units = fingerprint.units;
	a.asset.provenance = 1;
	for (const auto &input : fingerprint.inputs)
		if (input.kind == Disk::InputKind::Bsp)
		{
			if (geometry.map != input.sha256)
				throw std::runtime_error("Fixture geometry and runtime BSP differ");
			a.asset.assessment.map = input.name;
			a.asset.assessment.bspSha256 = Disk::Hex(input.sha256);
		}
	Provenance provenance;
	provenance.id = 1;
	provenance.kind = ProvenanceKind::Authored;
	provenance.producer = "lightrecon_entity_fixture_tool/1";
	provenance.method = "Analytic Rec.709 reflected radiance boundary condition";
	a.asset.provenanceRecords.push_back(provenance);
	Calibration calibration;
	calibration.id = calibration.provenance = 1;
	calibration.kind = CalibrationKind::PhysicallyAuthored;
	a.asset.calibrations.push_back(calibration);
	EnvironmentLighting sky;
	sky.source.id = 1;
	sky.source.provenance = 1;
	sky.source.state = HypothesisState::Resolved;
	sky.sunCoverage = SunCoverage::ExcludesExplicitSun;
	sky.meanLuminance.unit = PhotometricUnit::Nits;
	sky.meanLuminance.calibration = 1;
	sky.meanLuminance.intensity.known = true;
	sky.meanLuminance.intensity.value = 6000;
	sky.meanLuminance.color.known = true;
	sky.meanLuminance.color.rgb = Vec3(1, 1, 1);
	a.asset.environments.push_back(sky);
	for (const auto &r : geometry.regions->Regions())
	{
		GeometryRegion region;
		region.id = r.id;
		region.provenance = 1;
		region.key = "fixture/" + std::to_string(r.id);
		region.bounds = r.bounds;
		a.asset.regions.push_back(region);
	}

	a.sourceScene = Fields::SourceIdentity(a.asset);
	TransportField field;
	field.source.id = 2;
	field.source.provenance = 1;
	field.source.state = HypothesisState::Resolved;
	field.calibration = 1;
	field.dataset = 1;
	field.unit = PhotometricUnit::Lux;
	field.bounds = options.bounds;
	auto &m = field.membership;
	m.version = 2;
	m.model = "source-conditioned-field/1";
	m.domain = "generated-probes/irradiance/1";
	m.profile = "physical-emission/1";
	m.geometry = Disk::Hex(geometry.geometry);
	m.generation = Disk::Hex(a.sourceScene);
	m.rootsComplete = m.reflectionOrderKnown = true;
	TransportRoot root;
	root.kind = TransportRootKind::Sky;
	root.source = 1;
	m.roots = {root};
	m.reflectionOrders = {1};
	m.minimumReflectionOrder = m.maximumReflectionOrder = 1;
	m.directLocal = m.directSun = m.directSky = m.legacyAmbient = m.unresolved = FieldMembership::Excluded;
	m.reflected = FieldMembership::Included;
	m.colour = ContributionColour::LinearRec709D65;
	m.addition = CompositionRule::Permitted;
	m.replacement = CompositionRule::Forbidden;
	m.numericalError.known = true;
	a.asset.transport.push_back(field);
	for (Id id = 1; id <= 3; ++id)
	{
		Dataset d;
		d.id = id;
		d.encodingVersion = id == 1 ? Fields::kVersion : Probes::kVersion;
		d.chunkElements = id == 1 ? 64 : 128;
		d.purpose = id == 1 ? DatasetPurpose::Transport :
			id == 2 ? DatasetPurpose::RegionMembership : DatasetPurpose::Visibility;
		d.encoding = id == 1 ? "org.openjk.spatial-field" :
			id == 2 ? "org.openjk.probe-geometry" : "org.openjk.probe-topology";
		d.locator = id == 1 ? "prepared-field:2" : id == 2 ? "field-geometry:2" : "field-topology:2";
		a.asset.datasets.push_back(d);
	}

	a.values.field = field.source.id;
	a.values.topology = a.topology->Key();
	for (const auto &probe : a.topology->Probes())
	{
		Probes::Value v;
		v.probe = probe.id;
		v.evaluatedPositionWorld = probe.positionWorld;
		v.angular.present = true;
		v.angular.quantity = Angular::Quantity::DiffuseIrradiance;
		v.angular.degree = 1;
		// For a positive incident radiance L(w) = C + A dot w, the
		// cosine integral is pi*C + (2*pi/3)*A dot N. Use independent
		// red +X and blue -Y gradients and a constant green channel.
		const double height = std::max(0.0, std::min(1.0, double(probe.positionWorld.z) / 128));
		double red = 3800 + 4200 * height, blue = 7200 - 3400 * height;
		if (stimulus == Stimulus::SeparateRooms)
		{
			red = probe.positionWorld.x < 0 ? 8000 : 0;
			blue = probe.positionWorld.x > 0 ? 8000 : 0;
		}
		else if (stimulus == Stimulus::DoorwayGradient)
		{
			red = 4000 - 500 * probe.positionWorld.x;
			blue = 4000 + 500 * probe.positionWorld.x;
		}
		const double y00 = 1 / std::sqrt(4 * Angular::Pi);
		const double y1 = std::sqrt(3 / (4 * Angular::Pi));
		v.angular.sh = {{{red / y00, 1900 / y00, blue / y00}},
			{{0, 0, 0.6 * blue / y1}}, {{0, 0, 0}}, {{-0.6 * red / y1, 0, 0}}};
		a.values.records.push_back(v);
	}
	std::sort(a.values.records.begin(), a.values.records.end(),
		[](const Probes::Value &l, const Probes::Value &r) { return l.probe < r.probe; });

	auto d = std::make_shared<ConditionalFieldDescriptor>();
	d->field = field.source.id;
	d->dataset = d->calibration = 1;
	d->unit = PhotometricUnit::Lux;
	d->domain = FieldDomain::PhysicallyAuthored;
	d->colour = m.colour;
	d->primariesKnown = d->linearTransferKnown = true;
	d->profile = m.profile;
	d->encoding = "org.openjk.spatial-field/1";
	d->provenance = "C04 analytic gameplay stimulus; no recovered stock lighting";
	d->topology = Disk::Hex(a.topology->Key());
	d->geometry = m.geometry;
	d->membership = m;
	d->representationError = m.numericalError;
	d->requestedProbes = d->availableProbes = a.values.records.size();
	d->geometryVolumeWorldCubed = d->lightingVolumeWorldCubed = a.topology->Diagnostics().supportedStaticVolume;
	a.descriptor = d;
	Check(Fields::Encode(a, read));
	return a;
}

void CheckSpatialField()
{
	ProbeFixture::Scene room;
	room.Solid(ProbeFixture::Box(Vec3(-.2f, -4, 0), Vec3(.2f, -2.1f, 8)));
	room.Solid(ProbeFixture::Box(Vec3(-.2f, 2.1f, 0), Vec3(.2f, 4, 8)));
	room.options.coarseSpacingWorld = 2;
	room.options.minimumSpacingWorld = .25f;
	room.options.clearanceWorld = .03f;
	const auto input = room.Input(true);
	const auto a = Author(input, room.options, Disk::Fingerprint(), ReadContext(), Stimulus::SeparateRooms);
	std::shared_ptr<const Fields::Prepared> evaluator;
	Check(Fields::Prepare(a, ReadContext(), evaluator));
	Manager manager;
	if (manager.Publish(a.asset, Completeness::Partial, evaluator) != Status::Ok)
		throw std::runtime_error("Room field publication");
	const auto s = manager.Acquire();
	const auto left = s.SampleReflectedIrradianceSH(Vec3(-1.5f, -3, 2.25f));
	const auto right = s.SampleReflectedIrradianceSH(Vec3(1.5f, -3, 2.25f));
	const auto wall = s.SampleReflectedIrradianceSH(Vec3(0, -3, 2.25f));
	if (!left || !right || wall || left.value.coefficients[0].z != 0 ||
		right.value.coefficients[0].x != 0)
		throw std::runtime_error(std::string("Wall support: ") + StatusName(left.status) + "/" +
			StatusName(right.status) + "/" + StatusName(wall.status) + " wrong-side RGB " +
			std::to_string(left.value.coefficients[0].z) + "/" + std::to_string(right.value.coefficients[0].x));
	// A linear authored reference tests the path through the opening.
	// The disjoint-room stimulus above isolates through-wall contamination.
	const auto door = Author(input, room.options, Disk::Fingerprint(), ReadContext(), Stimulus::DoorwayGradient);
	Check(Fields::Prepare(door, ReadContext(), evaluator));
	Manager doorwayManager;
	if (doorwayManager.Publish(door.asset, Completeness::Partial, evaluator) != Status::Ok)
		throw std::runtime_error("Doorway field publication");
	const auto doorway = doorwayManager.Acquire();
	double previous = -1, maxStep = 0, errorSquared = 0;
	unsigned mixed = 0;
	for (int i = 0; i <= 16; ++i)
	{
		const float x = -1 + i / 8.0f;
		const auto p = doorway.SampleReflectedIrradianceSH(Vec3(x, .25f, 2.25f));
		if (!p || !p.value.region)
			throw std::runtime_error("Missing legitimate doorway support at step " + std::to_string(i));
		const auto &dc = p.value.coefficients[0];
		if (std::abs(dc.y / std::sqrt(4 * Angular::Pi) - 1900) > .001)
			throw std::runtime_error("Constant irradiance was not preserved");
		mixed += dc.x > 0 && dc.z > 0;
		const double blue = dc.z / std::sqrt(4 * Angular::Pi);
		errorSquared += std::pow(blue - (4000 + 500 * x), 2);
		if (previous >= 0)
			maxStep = std::max(maxStep, std::abs(blue - previous));
		previous = blue;
	}
	const double rmse = std::sqrt(errorSquared / 17);
	if (mixed != 17 || rmse > 600 || maxStep > 1500)
		throw std::runtime_error("Doorway gradient outside error budget");
	bool body = false;
	for (const auto &probe : a.topology->Probes())
	{
		const auto p = probe.positionWorld;
		if (p.x > -4 || probe.clearanceWorld <= .25 || !probe.region) continue;
		const auto centre = s.SampleReflectedIrradianceSH(p);
		const auto lower = s.SampleReflectedIrradianceSH(Vec3(p.x, p.y, p.z - .25f));
		const auto upper = s.SampleReflectedIrradianceSH(Vec3(p.x, p.y, p.z + .25f));
		if (centre && lower && upper && centre.value.cell == lower.value.cell &&
			centre.value.cell == upper.value.cell)
		{
			body = true;
			break;
		}
	}
	if (!body)
		throw std::runtime_error("Missing three anchors in certified body cell");
	IrradianceQuery q;
	q.positionWorld = Vec3(-1.5f, -3, 2.25f);
	q.normalWorld = Vec3(1, 0, 0);
	const auto facingRed = s.SampleIndirect(q);
	q.normalWorld = Vec3(-1, 0, 0);
	const auto facingAway = s.SampleIndirect(q);
	q.positionWorld.x = 1.5f;
	q.normalWorld = Vec3(0, -1, 0);
	const auto facingBlue = s.SampleIndirect(q);
	if (!facingRed || !facingAway || !facingBlue ||
		std::abs(facingRed.value.rgb.x - 12800) > .01f ||
		std::abs(facingAway.value.rgb.x - 3200) > .01f ||
		std::abs(facingBlue.value.rgb.z - 12800) > .01f)
		throw std::runtime_error("Independent red and blue normal response");

	auto sparse = a;
	const auto support = a.topology->Lookup(q.positionWorld);
	const auto missing = support.probes[0];
	sparse.values.records.erase(std::remove_if(sparse.values.records.begin(), sparse.values.records.end(),
		[missing](const Probes::Value &v) { return v.probe == missing; }), sparse.values.records.end());
	auto descriptor = std::make_shared<ConditionalFieldDescriptor>(*sparse.descriptor);
	descriptor->availableProbes = sparse.values.records.size();
	sparse.descriptor = descriptor;
	Check(Fields::Encode(sparse, ReadContext()));
	Check(Fields::Prepare(sparse, ReadContext(), evaluator));
	if (manager.Publish(sparse.asset, Completeness::Partial, evaluator) != Status::Ok ||
		manager.Acquire().SampleReflectedIrradianceSH(q.positionWorld).status != Status::Unavailable)
		throw std::runtime_error("Missing probe did not fall back");
	sparse.asset.regions.clear();
	Check(Fields::Prepare(sparse, ReadContext(), evaluator));
	if (evaluator->Supports(Capability::ReflectedIrradianceSH) != Status::Unavailable)
		throw std::runtime_error("Unknown region capability");

	const auto start = std::chrono::steady_clock::now();
	unsigned hits = 0;
	for (unsigned i = 0; i < 10000; ++i)
		hits += bool(doorway.SampleReflectedIrradianceSH(Vec3(-1 + (i % 17) / 8.0f, .25f, 2.25f)));
	const double microseconds = std::chrono::duration<double, std::micro>(
		std::chrono::steady_clock::now() - start).count() / 10000;
	if (hits != 10000)
		throw std::runtime_error("Doorway coverage changed during repeated read");
	std::cout << "Wall fallback, zero through-wall RGB, " << mixed <<
		" mixed doorway samples, gradient RMSE " << rmse << " lux, max doorway step " << maxStep <<
		" lux, constant 1900 lux, three certified body anchors, " <<
		microseconds << " us per uncached query\n";
}
} // namespace

int main(int argc, char **argv)
{
	try
	{
		if (argc == 1)
		{
			CheckSpatialField();
			return 0;
		}
		if (argc != 10)
			throw std::runtime_error("usage: entity_fixture_tool cache-root manifest.log geometry.jkobs minx miny minz maxx maxy maxz");
		const auto fingerprint = Manifest(argv[2]);
		std::shared_ptr<const Offline::Dataset> dataset;
		Check(Offline::OpenFile(argv[3], Offline::Options(), dataset));
		Offline::Sources source;
		Offline::RegionSnapshot regionSnapshot;
		Check(dataset->Load(source, regionSnapshot));
		if (!source.domain || !source.geometry) throw std::runtime_error("Missing fixture BSP geometry");

		Probes::Options options;
		options.bounds.known = true;
		options.bounds.minWorld = Vec3(std::stof(argv[4]), std::stof(argv[5]), std::stof(argv[6]));
		options.bounds.maxWorld = Vec3(std::stof(argv[7]), std::stof(argv[8]), std::stof(argv[9]));
		options.coarseSpacingWorld = 64;
		options.minimumSpacingWorld = 8;
		ReadContext read;
		read.maxBytes = 128ull << 20;
		VisibilityBuildOptions visibilityOptions;
		visibilityOptions.patchToleranceWorld = source.metadata.visibilityPatchToleranceWorld;
		Probes::Input input;
		input.map = input.domainKey = source.metadata.map.sha256;
		input.domain = source.domain;
		input.dynamic = source.dynamic;
		Check(StaticVisibility::Build(source.geometry, visibilityOptions, read, input.visibility, source.domain));
		input.geometry = input.visibility->CertificationKey();
		RegionBuildInput regions;
		regions.identity = source.metadata.map;
		regions.domain = input.domain;
		regions.geometry = input.visibility;
		regions.dynamic = input.dynamic;
		RegionOptions regionOptions;
		regionOptions.bounds = options.bounds;
		regionOptions.coarseCellWorld = 64;
		regionOptions.minimumCellWorld = 8;
		regionOptions.inspectSky = false;
		Check(LightingRegionGraph::Build(regions, regionOptions, read, input.regions));
		const auto a = Author(input, options, fingerprint, read);
		Disk::Persistent::Selection selection;
		selection.discoverReflectedIrradiance = true;
		const Disk::Cache cache(argv[1]);
		const auto stored = Disk::Persistent::Store(cache, fingerprint, a.asset, a.chunks, selection);
		if (!stored) throw std::runtime_error(stored.message);
		BuildProduct result;
		Disk::Persistent::Work work;
		const auto loaded = Disk::Persistent::Load(cache, fingerprint, selection, result, work);
		if (!loaded) throw std::runtime_error(loaded.message);
		Manager manager;
		if (manager.Publish(*result.asset, Completeness::Partial, result.evaluator) != Status::Ok ||
			manager.Acquire().GetCapability(Capability::ReflectedIrradianceSH).status != Status::Ok)
			throw std::runtime_error("Authored field was not prepared");
		std::cout << a.values.records.size() << " authored probes, " << a.asset.regions.size() <<
			" regions, " << work.payloadBytes << " prepared payload bytes\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
