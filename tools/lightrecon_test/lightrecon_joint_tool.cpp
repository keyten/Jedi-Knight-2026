// Blind native S11 adapter: sanitized geometry + training values only.
// Validation RGB/answers stay in the trusted controller; stdin returns scores.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/bake_math.h"
#include "lightrecon/geometry_key.h"
#include "lightrecon/joint.h"
#include "lightrecon/offline.h"
#include "lightrecon/producer.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
using namespace Bake::Detail;
namespace J = Rend2LightRecon::Core::Joint;
namespace P = Rend2LightRecon::Core::Producer;
void Check(Error e)
{
	if (!e)
		throw std::runtime_error(e.message);
}
void Need(bool b, const char *s)
{
	if (!b)
		throw std::runtime_error(s);
}
Disk::Digest Key(const std::string &s) { return Disk::Hash(s.data(), s.size()); }
void Vec(std::istream &in, Vec3 &v)
{
	in >> v.x >> v.y >> v.z;
	Need(bool(in) && Finite(v), "Finite geometry vector");
}
template <class T> class Data final : public ChunkSource<T>
{
  public:
	std::shared_ptr<std::vector<T>> records = std::make_shared<std::vector<T>>();
	Error Read(uint64_t a, size_t n, const ReadContext &, Chunk<T> &out) const override
	{
		if (a > records->size() || n > records->size() - a)
			return Failure(Code::ProviderError, "Short protocol partition");
		out = VectorChunk<T>(records, size_t(a), n);
		return {};
	}
};
struct UVVertex
{
	double u = 0, v = 0;
	V p;
};
std::vector<UVVertex> Clip(std::vector<UVVertex> poly, unsigned axis, double edge, bool lower)
{
	std::vector<UVVertex> out;
	if (poly.empty())
		return out;
	auto coord = [&](const UVVertex &v) { return axis ? v.v : v.u; };
	for (size_t i = 0; i < poly.size(); ++i)
	{
		auto a = poly[i], b = poly[(i + 1) % poly.size()];
		double x = coord(a) - edge, y = coord(b) - edge;
		bool inside = lower ? x >= 0 : x <= 0, next = lower ? y >= 0 : y <= 0;
		if (inside)
			out.push_back(a);
		if (inside != next)
		{
			double t = x / (x - y);
			UVVertex v;
			v.u = a.u + t * (b.u - a.u);
			v.v = a.v + t * (b.v - a.v);
			v.p = a.p + (b.p - a.p) * t;
			out.push_back(v);
		}
	}
	return out;
}
Chunk<Bake::FootprintTriangle> Footprint(const BakedSurface &surface, unsigned slot, unsigned x, unsigned y,
										 Vec3 normal, double &area)
{
	Need(surface.kind == BakedSurfaceKind::Face && slot < 4, "Scoped planar source UV footprint required");
	auto triangles = std::make_shared<std::vector<Bake::FootprintTriangle>>();
	area = 0;
	for (size_t i = 0; i + 2 < surface.indices.size(); i += 3)
	{
		std::vector<UVVertex> p;
		for (unsigned k = 0; k < 3; ++k)
		{
			const auto &v = surface.vertices.at(surface.indices[i + k]);
			UVVertex q;
			q.u = v.lightmap[slot].u;
			q.v = v.lightmap[slot].v;
			q.p = V(v.position);
			p.push_back(q);
		}
		for (unsigned axis = 0; axis < 2; ++axis)
		{
			double v = axis ? y : x;
			p = Clip(p, axis, v / 128., true);
			p = Clip(p, axis, (v + 1) / 128., false);
		}
		for (size_t k = 1; k + 1 < p.size(); ++k)
		{
			Bake::FootprintTriangle t;
			t.world[0] = p[0].p.Float();
			t.world[1] = p[k].p.Float();
			t.world[2] = p[k + 1].p.Float();
			for (auto &n : t.normal)
				n = normal;
			double a = .5 * Length(Cross(p[k].p - p[0].p, p[k + 1].p - p[0].p));
			if (a > 1e-12)
			{
				area += a;
				triangles->push_back(t);
			}
		}
	}
	Need(area > 0 && triangles->size() <= 64, "Missing/oversized original clipped footprint");
	return VectorChunk<Bake::FootprintTriangle>(triangles, 0, triangles->size());
}
struct RowSpec
{
	J::Observation row;
	unsigned surface = 0, slot = 0, x = 0, y = 0;
	double area = 0;
};
class Controller final : public J::Validation
{
	const J::Request &request;
	std::shared_ptr<Data<J::Observation>> validation;
	Disk::Digest partition;

  public:
	Controller(const J::Request &r, std::shared_ptr<Data<J::Observation>> rows, Disk::Digest key)
		: request(r), validation(rows), partition(key)
	{
	}
	Disk::Digest Key() const override { return partition; }
	Error Evaluate(const J::Model &m, const ReadContext &read, J::Score &score) const override
	{
		std::vector<J::Prediction> predictions;
		auto error = J::EvaluateFrozen(request, m, validation, validation->records->size(),
									   J::Role::Validation, Key(), read, score, &predictions);
		if (!error)
			return error;
		std::cout << "{\"event\":\"validation\",\"model\":\"" << Disk::Hex(m.key) << "\",\"predictions\":[";
		for (size_t i = 0; i < predictions.size(); ++i)
		{
			if (i)
				std::cout << ',';
			const auto &p = predictions[i];
			std::cout << "{\"id\":" << p.id << ",\"available\":" << (p.available ? "true" : "false")
					  << ",\"rgb\":[" << p.rgb.x << ',' << p.rgb.y << ',' << p.rgb.z << "]}";
		}
		std::cout << "]}\n" << std::flush;
		std::string marker;
		unsigned available = 0, positive = 0, required = 0;
		std::cin >> marker >> available >> positive >> required >> score.intervalMse >> score.blackMse >>
			score.objective >> score.worstBlockRmse;
		if (!std::cin || marker != "SCORE" || available > 1 || positive > 1 || required > 1 ||
			!std::isfinite(score.objective) || score.objective < 0)
			return Failure(Code::ProviderError, "Invalid trusted validation response");
		score.available = available != 0;
		score.positive = positive != 0;
		score.requiredBlocksPass = required != 0;
		return {};
	}
};
class FinalController final : public P::FinalEvaluation
{
	const J::Request &request;
	std::shared_ptr<Data<J::Observation>> rows;
	Disk::Digest partition;

  public:
	FinalController(const J::Request &q, std::shared_ptr<Data<J::Observation>> r, Disk::Digest k)
		: request(q), rows(r), partition(k)
	{
	}
	Disk::Digest Key() const override { return partition; }
	Error Assess(const J::Model &m, const ReadContext &read, P::FinalReceipt &receipt) const override
	{
		auto &score = receipt.score;
		std::vector<J::Prediction> values;
		auto error = J::EvaluateFrozen(request, m, rows, rows->records->size(), J::Role::Heldout, Key(), read,
									   score, &values);
		if (!error)
			return error;
		std::cout << std::setprecision(12) << "{\"event\":\"final\",\"predictions\":[";
		for (size_t i = 0; i < values.size(); ++i)
		{
			const auto &p = values[i];
			std::cout << (i ? "," : "") << "{\"id\":" << p.id
					  << ",\"available\":" << (p.available ? "true" : "false") << ",\"rgb\":[" << p.rgb.x
					  << ',' << p.rgb.y << ',' << p.rgb.z << "]}";
		}
		std::cout << "]}\n" << std::flush;
		std::string marker;
		unsigned available = 0, positive = 0, required = 0;
		std::cin >> marker >> available >> positive >> required >> score.intervalMse >> score.blackMse >>
			score.objective >> score.worstBlockRmse;
		if (!std::cin || marker != "FINAL" || available > 1 || positive > 1 || required > 1 ||
			!std::isfinite(score.objective) || score.objective < 0)
			return Failure(Code::ProviderError, "Invalid independent final receipt");
		score.available = available != 0;
		score.positive = positive != 0;
		score.requiredBlocksPass = required != 0;
		receipt.evidence = "Independent external held-out RGB controller, parameters frozen before receipt";
		return {};
	}
};
int main(int argc, char **argv)
{
	try
	{
		Need(argc == 3 || argc == 4 || argc == 5,
			 "usage: lightrecon_joint_tool geometry.public.jkobs training-and-geometry.txt "
			 "[producer-cache-root [--stock]]");
		ReadContext read;
		read.maxBytes = 128ull * 1024 * 1024;
		Offline::Options limits;
		limits.maxResidentBytes = read.maxBytes;
		std::shared_ptr<const Offline::Dataset> data;
		Check(Offline::OpenFile(argv[1], limits, data));
		const bool stockMode = argc == 5 && std::string(argv[4]) == "--stock";
		Need(data->Counts().rawImageBytes == 0 &&
				 (stockMode || (data->Counts().candidates == 0 && data->Counts().evidence == 0)),
			 "Blind geometry archive contains lighting/answer evidence");
		Offline::Sources source;
		Offline::RegionSnapshot graph;
		Check(data->Load(source, graph));
		Need(source.geometry && source.domain, "Public geometry/domain missing");
		Need(!source.grid || source.grid->Records().empty(), "Grid colours forbidden in geometry archive");
		Need(source.geometry->VertexHdr().empty(), "Vertex HDR colours forbidden");
		for (const auto &s : source.geometry->Surfaces())
			for (const auto &v : s.vertices)
				for (const auto &c : v.colour)
					Need(c[0] == 0 && c[1] == 0 && c[2] == 0, "Vertex lighting forbidden in blind geometry");
		J::Request request;
		request.mapKey = data->Info().map.sha256;
		request.policyKey = Key("S11 controlled planar direct native protocol v1");
		std::ifstream in(argv[2]);
		std::string magic;
		in >> magic;
		Need(magic == "JKJOINT1" || magic == "JKJOINT13", "Joint protocol version");
		Disk::Digest validationKey = Key("S11 external private validation partition v1"),
					 heldoutKey = Key("External final held-out geometry");
		BuildCoverage coverage;
		std::string trainIdentity, validationIdentity, heldoutIdentity;
		if (magic == "JKJOINT13")
		{
			in >> trainIdentity >> validationIdentity >> heldoutIdentity;
			Need(trainIdentity.size() == 64 && validationIdentity.size() == 64 &&
					 heldoutIdentity.size() == 64,
				 "Partition hashes required");
			validationKey = Key(validationIdentity);
			heldoutKey = Key(heldoutIdentity);
			unsigned complete = 0;
			in >> coverage.rawPages >> coverage.rawPixels >> coverage.derivedPixels >>
				coverage.mappedSamples >> complete;
			coverage.rawComplete = true;
			coverage.derivedComplete = complete != 0;
			coverage.sourceTransferKnown = coverage.primariesKnown = coverage.historicalProfileKnown = true;
		}
		Need(argc < 4 || magic == "JKJOINT13",
			 "Publication requires hashed partition manifests and full-map denominators");
		int linear = 0;
		in >> linear;
		Need(linear == 0 || linear == 1 || (stockMode && (linear == 2 || linear == 3)),
			 "Discrete supported/conditional profile selector");
		Bake::Recipe recipe;
		recipe.falloff = linear == 1 ? Bake::Falloff::Linear : Bake::Falloff::InverseSquare;
		J::Profile profile;
		profile.model.law = Shape::Law::HistoricalBake;
		profile.model.output = Shape::Output::RelativeIncident;
		profile.model.profile = Bake::NetRadiantProfile(recipe);
		profile.model.units = data->Info().units;
		profile.model.allowAssumed = true;
		profile.model.provenance = "Explicit controlled q3map2 profile, not inferred Raven identity";
		profile.contract.profile = profile.model.profile;
		profile.contract.operation = Indirect::ObservationOperator::NetRadiantEncoded;
		profile.contract.incidentDomain = Indirect::Root::Domain::CompilerRelative255;
		profile.contract.units = data->Info().units;
		profile.contract.transferAttested = profile.contract.primariesAttested = true;
		profile.contract.provenance = "Selected direct-only fixture recipe, relative Q only";
		profile.calibrationHypothesis = "Uncalibrated compiler Q, gain fixed at one";
		request.profiles = {profile};
		if (stockMode)
		{
			coverage.sourceTransferKnown = coverage.primariesKnown = coverage.historicalProfileKnown = false;
			coverage.limitations.push_back("UnknownProfile; numerical profiles/receiver recipe are explicit "
										   "conditional hypotheses, unknown source transfer and primaries");
			profile.model.provenance =
				"Explicit assumed NetRadiant direct surface hypothesis, not verified original JA bake";
			profile.contract.provenance =
				"Conditionally compare encoded bytes under assumed gamma1/profile; not decoded physical RGB";
			request.profiles = {profile};
			if (linear == 2)
			{
				auto other = profile;
				Bake::Recipe alt;
				alt.falloff = Bake::Falloff::Linear;
				other.model.profile = Bake::NetRadiantProfile(alt);
				other.contract.profile = other.model.profile;
				request.profiles.push_back(other);
			}
			if (linear == 3)
			{
				request.profiles[0].model.profile = {};
				request.profiles[0].contract.profile = {};
			}
		}
		size_t candidates = 0;
		in >> candidates;
		Need((candidates > 0 || stockMode) && candidates <= 8, "Native controlled candidate budget");
		for (size_t i = 0; i < candidates; ++i)
		{
			J::Candidate c;
			in >> c.id;
			Vec(in, c.shape.initial.geometry.position.world);
			c.shape.initial.geometry.source.id = c.id;
			c.shape.initial.geometry.position.known = true;
			c.shape.initial.geometry.kind = EmitterKind::Point;
			c.shape.initial.geometry.angular.kind = AngularKind::Isotropic;
			c.shape.initial.rgbAmplitude = Vec3(64, 64, 64);
			c.shape.upperRgb = Vec3(1200, 1200, 1200);
			c.shape.centreBounds = source.domain->Source().worldBounds;
			c.shape.fitPosition = c.shape.fitFrame = c.shape.fitDimensions = c.shape.fitCone = false;
			c.shape.maximumTranslationWorld = 0;
			c.provider = "explicit-controlled-fixed-XYZ-prior";
			c.provenance = "Allowed fixed geometry prior, no true amplitudes or count supplied as answers";
			request.candidates.push_back(c);
		}
		std::vector<RowSpec> specs;
		for (unsigned partition = 0; partition < 3; ++partition)
		{
			size_t count = 0;
			in >> count;
			Need(count >= 4 && count <= 128, "Native spatial partition budget");
			for (size_t i = 0; i < count; ++i)
			{
				RowSpec s;
				auto &o = s.row;
				in >> o.id >> s.surface >> s.slot >> s.x >> s.y;
				Vec(in, o.receiver.world);
				Vec(in, o.receiver.geometricNormal);
				in >> s.area >> o.block >> o.region;
				Need(s.surface < source.geometry->Surfaces().size() && s.x < 128 && s.y < 128 && s.slot < 4 &&
						 s.area > 0,
					 "Original chart geometry range");
				o.role = J::Role(partition);
				o.patch = o.id;
				o.receiver.sample = o.id;
				o.receiver.surface = s.surface + 1;
				o.receiver.shadingNormal = o.receiver.geometricNormal;
				o.receiver.mappedPlanar = o.receiver.compilerPlacementCertified = true;
				o.comparable = true;
				o.measure = s.area;
				for (unsigned c = 0; c < 3; ++c)
				{
					unsigned raw = 0;
					if (partition == 0)
					{
						in >> raw;
						Need(raw <= 255, "Raw source RGB8");
						o.rgb[c] = Amplitude::Rgb8(uint8_t(raw));
					}
					else
					{
						o.rgb[c].lower = 0;
						o.rgb[c].upper = std::numeric_limits<double>::infinity();
					}
				}
				specs.push_back(s);
			}
		}
		std::string extra;
		Need(bool(in) && !(in >> extra), "Joint protocol truncated/trailing fields");
		std::shared_ptr<const StaticVisibility> visibility;
		VisibilityBuildOptions visibilityOptions;
		visibilityOptions.patchToleranceWorld = data->Info().visibilityPatchToleranceWorld;
		Check(StaticVisibility::Build(source.geometry, visibilityOptions, read, visibility, source.domain));
		Indirect::BuildInput transfer;
		transfer.geometry = visibility;
		transfer.mapKey = request.mapKey;
		transfer.patchInterpretationKey = Key("Original planar triangle UV-clipped compiler footprint v1");
		transfer.completePatchSet = false;
		std::set<Id> ids, materials;
		for (const auto &s : specs)
		{
			Need(ids.insert(s.row.id).second, "Cross-partition duplicate/overlapping sample ID");
			Indirect::Patch p;
			p.id = s.row.id;
			p.receiver = s.row.receiver;
			const auto &surface = source.geometry->Surfaces()[s.surface];
			p.material = surface.materialIndex + 1;
			p.receiver.material = p.material;
			p.receiver.footprint =
				Footprint(surface, s.slot, s.x, s.y, p.receiver.geometricNormal, p.areaWorldSquared);
			Need(std::abs(p.areaWorldSquared - s.area) <= 1e-4 * std::max(1., s.area),
				 "Exported/imported original footprint area differs");
			transfer.patches.push_back(p);
			materials.insert(p.material);
		}
		Indirect::BuildOptions build;
		build.maximumLinkDistanceWorld = 1024;
		build.subdivisions = 0;
		Check(Indirect::Transfer::Build(transfer, build, read, request.transfer));
		for (Id id : materials)
		{
			Indirect::Reflectance m;
			m.material = id;
			m.provenance = "Reflectance unknown and unused by controlled bounce0 incident operator";
			request.materials.push_back(m);
		}
		request.geometry.visibility = visibility;
		request.geometry.geometryKey = visibility->CertificationKey();
		request.geometry.materialKey = Key("Original sanitized BSP material classification");
		request.geometry.observationPolicyKey = Key("Native source RGB8 gamma1 original sample UV mapping");
		request.placement.map = source.metadata.map;
		request.placement.domain = source.domain;
		request.placement.domainKey = Key("Original archived BSP domain");
		request.placement.geometry = visibility;
		request.placement.dynamic = source.dynamic;
		auto train = std::make_shared<Data<J::Observation>>(),
			 validation = std::make_shared<Data<J::Observation>>(),
			 held = std::make_shared<Data<J::Observation>>();
		for (const auto &s : specs)
		{
			auto out = s.row.role == J::Role::Training
						   ? train
						   : s.row.role == J::Role::Validation ? validation : held;
			out->records->push_back(s.row);
		}
		for (auto rows : {train, validation, held})
			std::sort(rows->records->begin(), rows->records->end(),
					  [](const J::Observation &a, const J::Observation &b) { return a.id < b.id; });
		request.training = train;
		request.count = train->records->size();
		GeometryKey key;
		for (const auto &x : *train->records)
		{
			key.U(x.id);
			for (const auto &c : x.rgb)
			{
				key.F(c.lower);
				key.F(c.upper);
			}
		}
		request.trainingKey = key.Finish();
		request.coarseSampling.compareRefined = request.strictSampling.compareRefined = false;
		auto &options = request.options;
		options.rounds = 2;
		options.beamWidth = 2;
		options.geometry = options.materials = options.residualSearch = options.mergeSplit = false;
		options.amplitudeSweeps = 5;
		options.amplitudeLineSteps = 24;
		options.jointSweeps = 1;
		options.maxModels = 32;
		options.maximumRequiredBlockRmse = 2;
		options.equivalentValidationSlack = .1;
		options.complexityPerSource = .001;
		options.complexityPerParameter = .0001;
		Controller controller(request, validation, validationKey);
		FinalController final(request, held, heldoutKey);
		J::Result result;
		Error error;
		P::Result produced;
		if (argc >= 4)
		{
			P::Request q;
			q.map = data->Info().map;
			q.coverage = coverage;
			q.metadata.units = data->Info().units;
			q.metadata.lineage = "controlled/" + Disk::Hex(q.map.sha256);
			q.metadata.revision = "sanitized-q3map2-bsp/1";
			q.metadata.provenance = 1;
			Provenance prov;
			prov.id = 1;
			prov.kind = ProvenanceKind::External;
			prov.producer = "Explicit controlled original BSP observation pipeline";
			prov.contentHash = Disk::Hex(q.map.sha256);
			q.metadata.provenanceRecords = {prov};
			q.fitting = request;
			q.validationKey = validationKey;
			q.heldoutKey = heldoutKey;
			q.settingsIdentity =
				stockMode ? "S13 predeclared training-local emissive hypotheses/1; no XYZ truth"
						  : "S13 native fixed-XYZ compiler fixture, bounded amplitude/count selection/1";
			q.decoder = "NetRadiant pinned direct-only RGB8 gamma1; relative compiler Q, no absolute scale";
			q.fingerprint.complete.fill(true);
			q.fingerprint.units = q.metadata.units;
			Disk::InputContent bsp;
			bsp.kind = Disk::InputKind::Bsp;
			bsp.name = q.map.path;
			bsp.bytes = q.map.bytes;
			bsp.sha256 = q.map.sha256;
			q.fingerprint.inputs.push_back(bsp);
			Disk::InputContent geometry;
			geometry.kind = Disk::InputKind::Materials;
			geometry.name = "sanitized-original-geometry-material-archive";
			std::unique_ptr<Disk::FileReader> geometryFile;
			auto opened = Disk::FileReader::OpenFile(argv[1], geometryFile);
			Need(bool(opened), "Original public geometry fingerprint read");
			auto hashed = Disk::HashContent(*geometryFile, geometry.sha256, 256ull * 1024 * 1024);
			Need(bool(hashed), "Public archive hash limit");
			geometry.bytes = geometryFile->Size();
			q.fingerprint.inputs.push_back(geometry);
			Disk::InputContent units;
			units.kind = Disk::InputKind::UnitCalibration;
			units.name = "uncalibrated-compiler-Q-no-absolute-photometry/1";
			units.bytes = units.name.size();
			units.sha256 = Key(units.name);
			q.fingerprint.inputs.push_back(units);
			q.options.uncertainty.positionLevels = 0;
			q.options.uncertainty.colourProfiles = 0;
			q.options.uncertainty.deleteBlocks = 0;
			q.options.uncertainty.subsampleReplicates = 0;
			q.options.uncertainty.restarts = 0;
			q.options.uncertainty.materialProfiles = false;
			error = P::Run(q, read, produced, &controller, &final);
			result = produced.fitted;
			if (error && produced.artifact.asset)
			{
				Disk::Cache cache(argv[3]);
				auto stored = P::Store(produced.artifact, cache, read);
				if (!stored)
					std::cerr << "Cache write unavailable: " << stored.message << '\n';
				P::Artifact loaded = produced.artifact;
				if (stored)
					Check(P::Load(cache, produced.artifact.fingerprint, read, loaded));
				Manager manager;
				Need(P::Publish(loaded, manager, manager.Acquire().Generation(), read) == Status::Ok,
					 "Immutable snapshot publication");
				// Cache::Store creates the output directory, including diagnostic-only archives.
				std::ofstream report(std::string(argv[3]) + "/report.json");
				report << P::ReportJson(produced) << '\n';
				std::string obj;
				Check(P::DebugObj(loaded, 128 * 1024, obj));
				std::ofstream debug(std::string(argv[3]) + "/sources.obj");
				debug << obj;
				std::string preview;
				Check(P::DebugPreview(loaded, 128 * 1024, preview));
				std::ofstream previewFile(std::string(argv[3]) + "/sources.preview");
				previewFile << preview;
			}
		}
		else
			error = J::Solve(request, read, result, &controller);
		for (const auto &attempt : result.attempts)
			if (!attempt.reason.empty())
				std::cerr << attempt.proposal << ": " << attempt.reason << '\n';
		std::cout << std::setprecision(12) << "{\"event\":\"result\",\"code\":" << unsigned(error.code)
				  << ",\"state\":\"" << J::StateName(result.state)
				  << "\",\"models\":" << result.work.attemptedModels
				  << ",\"evaluations\":" << result.work.evaluations << ",\"rays\":" << result.work.rays
				  << ",\"preferred_available\":" << (result.preferred != SIZE_MAX ? "true" : "false")
				  << ",\"sources\":[";
		if (result.preferred != SIZE_MAX)
		{
			const auto &m = result.alternatives[result.preferred];
			for (size_t i = 0; i < m.sources.size(); ++i)
			{
				if (i)
					std::cout << ',';
				const auto &s = m.sources[i];
				std::cout << "{\"id\":" << s.candidate.id << ",\"rgb\":[" << s.value.rgbAmplitude.x << ','
						  << s.value.rgbAmplitude.y << ',' << s.value.rgbAmplitude.z << "]}";
			}
		}
		std::cout << "],\"producer\":" << (argc >= 4 ? P::ReportJson(produced) : "null")
				  << ",\"heldout_predictions\":[";
		if (result.preferred != SIZE_MAX)
		{
			J::Score score;
			std::vector<J::Prediction> values;
			Check(J::EvaluateFrozen(request, result.alternatives[result.preferred], held,
									held->records->size(), J::Role::Heldout,
									Key("External final held-out geometry"), read, score, &values));
			for (size_t i = 0; i < values.size(); ++i)
			{
				if (i)
					std::cout << ',';
				const auto &p = values[i];
				std::cout << "{\"id\":" << p.id << ",\"available\":" << (p.available ? "true" : "false")
						  << ",\"rgb\":[" << p.rgb.x << ',' << p.rgb.y << ',' << p.rgb.z << "]}";
			}
		}
		std::cout << "]}\n" << std::flush;
		return error ? 0 : 1;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
