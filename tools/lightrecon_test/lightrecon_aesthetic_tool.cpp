// ASR20 trusted offline controller adapter. Never publishes reconstruction assets.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "aesthetic_fit.h"
#include "lightrecon/bake_math.h"
#include "lightrecon/offline.h"
#include "lightrecon/shape_selection.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace Rend2LightRecon;
using namespace Core;
using namespace Bake::Detail;
namespace A = Rend2LightRecon::Aesthetic;
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
Disk::Digest Key(const std::string &s)
{
	return Disk::Hash(s.data(), s.size());
}
void Vec(std::istream &in, Vec3 &p)
{
	in >> p.x >> p.y >> p.z;
	Need(bool(in) && Finite(p), "Finite protocol vector required");
}
void JsonVec(Vec3 v)
{
	std::cout << '[' << v.x << ',' << v.y << ',' << v.z << ']';
}
void Q(const std::string &s)
{
	std::cout << '"';
	for (unsigned char c : s)
	{
		if (c == '"' || c == '\\')
			std::cout << '\\';
		std::cout << (c >= 32 ? char(c) : ' ');
	}
	std::cout << '"';
}
std::vector<Shape::Observation> Rows(std::istream &in, Shape::Role role,
									 const std::shared_ptr<const SurfaceGeometry> &geometry,
									 std::string &material, bool sameMaterial = true)
{
	size_t n = 0;
	in >> n;
	Need(n > 0 && n <= 4096, "Bounded nonempty fold required");
	std::vector<Shape::Observation> rows;
	for (size_t i = 0; i < n; ++i)
	{
		Shape::Observation a;
		unsigned surface = 0;
		in >> a.id >> a.spatialBlock >> surface;
		Vec(in, a.receiver.world);
		Vec(in, a.receiver.geometricNormal);
		in >> a.area;
		Need(surface < geometry->Surfaces().size(), "Original receiver surface required");
		const auto &s = geometry->Surfaces()[surface];
		if (material.empty())
			material = s.material;
		Need(!sameMaterial || material == s.material, "Aesthetic subset must use one comparable original material");
		a.receiver.sample = a.id;
		a.receiver.surface = surface + 1;
		a.receiver.material = 1;
		a.receiver.shadingNormal = a.receiver.geometricNormal;
		a.receiver.mappedPlanar = true;
		a.receiver.compilerPlacementCertified = false; // modern trace, no historical certificate
		a.role = role;
		a.comparable = true;
		for (auto &c : a.channel)
		{
			unsigned b = 0;
			in >> b;
			Need(b <= 255, "Raw RGB8 interval required");
			c = Amplitude::Rgb8(uint8_t(b));
		}
		Need(bool(in), "Truncated fold");
		rows.push_back(a);
	}
	return rows;
}
void ScoreJson(const A::Score &s)
{
	std::cout << "{\"available\":" << s.available << ",\"robust\":" << s.robust
			  << ",\"mse\":" << s.mse << ",\"rows\":" << s.rows << ",\"blocks\":" << s.blocks
			  << ",\"normal_bins\":" << s.normalBins << ",\"maximum_error\":" << s.maximumError << '}';
}
void SparseJson(const A::SparseReport &s, const A::Request &r)
{
	const auto &p = s.policy;
	std::cout << ",\"sparse\":{\"input_candidates\":" << s.inputCandidates
		<< ",\"unique_candidates\":" << s.uniqueCandidates << ",\"duplicate_inputs_removed\":"
		<< s.duplicateInputs << ",\"models\":" << s.models << ",\"rationale\":";
	Q(s.rationale);
	std::cout << ",\"policy\":{\"max_candidates\":" << p.maxCandidates
		<< ",\"max_sources\":" << p.maxSources << ",\"max_models\":" << p.maxModels
		<< ",\"training_penalty_per_fixture\":" << p.trainingPenaltyPerFixture
		<< ",\"minimum_training_gain\":" << p.minimumTrainingGain << ",\"prune_slack\":" << p.pruneSlack
		<< ",\"minimum_contribution\":" << p.minimumContribution
		<< ",\"validation_penalty_per_fixture\":" << r.options.validationPenaltyPerFixture
		<< ",\"validation_equivalent_slack\":" << r.options.validationEquivalentSlack
		<< ",\"provenance\":";
	Q(p.provenance);
	std::cout << "},\"input_evidence\":[";
	for (size_t i = 0; i < p.evidence.size(); ++i)
	{
		std::cout << (i ? "," : "") << "{\"fixture\":";
		Q(std::to_string(r.fixtures[i].variants[0].geometry.source.id));
		std::cout << ",\"independently_visible\":" << p.evidence[i].independentlyVisible
			<< ",\"provenance\":";
		Q(p.evidence[i].provenance);
		std::cout << '}';
	}
	std::cout << "],\"assumptions\":[";
	for (size_t i = 0; i < s.assumptions.size(); ++i)
	{
		std::cout << (i ? "," : "");
		Q(s.assumptions[i]);
	}
	std::cout << "],\"training_path\":[";
	for (size_t i = 0; i < s.trainingPath.size(); ++i)
		std::cout << (i ? "," : "") << (s.trainingPath[i] == SIZE_MAX ? -1 : int(s.trainingPath[i]));
	std::cout << "],\"decisions\":[";
	for (size_t i = 0; i < s.decisions.size(); ++i)
	{
		const auto &d = s.decisions[i];
		std::cout << (i ? "," : "") << "{\"fixture\":";
		Q(std::to_string(d.fixture));
		std::cout << ",\"configuration\":" << (d.configuration == SIZE_MAX ? -1 : int(d.configuration))
			<< ",\"action\":";
		Q(d.action);
		std::cout << ",\"rationale\":";
		Q(d.rationale);
		std::cout << '}';
	}
	std::cout << "]}";
}
#include "aesthetic_region.h"
int main(int argc, char **argv)
{
	try
	{
		Need(argc == 4 || (argc == 6 && std::string(argv[2]) == "--export-region"),
			 "usage: lightrecon_aesthetic_tool geometry.jkobs train-validation.txt heldout.txt OR "
			 "archive --export-region surface folder radius OR archive geometry-only.txt --coverage");
		ReadContext read;
		read.maxBytes = 128ull * 1024 * 1024;
		Offline::Options limits;
		limits.maxResidentBytes = read.maxBytes;
		std::shared_ptr<const Offline::Dataset> data;
		Check(Offline::OpenFile(argv[1], limits, data));
		Offline::Sources source;
		Offline::RegionSnapshot graph;
		Check(data->Load(source, graph));
		Need(source.geometry && source.domain,
			 "Original geometry and retained BSP domain required");
		SurfaceLimits surfaceLimits;
		surfaceLimits.maxBytes = 128ull << 20;
		SurfaceCapture capture(surfaceLimits);
		capture.Begin(BuildPolicy::ForceRebuild);
		Check(capture.SetMap(source.metadata.map));
		for (auto s : source.geometry->Surfaces())
		{
			for (auto &v : s.vertices)
			{
				v.colourPresent = false;
				for (auto &c : v.colour)
					for (auto &b : c)
						b = 0;
			}
			Check(capture.Add(std::move(s)));
		}
		Check(capture.Freeze());
		source.geometry = capture.Acquire();
		VisibilityBuildOptions visibilityOptions;
		visibilityOptions.patchToleranceWorld = data->Info().visibilityPatchToleranceWorld;
		std::shared_ptr<const StaticVisibility> visibility;
		Check(StaticVisibility::Build(source.geometry, visibilityOptions, read, visibility,
									  source.domain));
		A::Request r;
		r.model.law = Shape::Law::DiffuseIntegral;
		r.model.output = Shape::Output::RelativeIncident;
		r.model.allowAssumed = true;
		r.model.units = data->Info().units;
		r.model.provenance = "ASR20 relative modern appearance hypothesis";
		r.geometry.visibility = visibility;
		r.geometry.geometryKey = visibility->CertificationKey();
		r.geometry.materialKey = Key("Original same-material bounded subset");
		r.scene.map = source.metadata.map;
		r.scene.geometry = visibility;
		r.scene.domain = source.domain;
		r.scene.originalSurfaces = source.geometry;
		r.scene.domainKey = Key("Original retained BSP domain");
		r.scene.dynamic = source.dynamic;
		r.scene.occupancyPolicy = PointRefine::OccupancyPolicy::ConditionalBake;
		r.scene.occupancyProvenance = "Static hypothesis conditional on unknown bake-state models";
		r.sampling.compareRefined = true;
		r.sampling.areaDivisions = 4;
		r.sampling.maxWork = 16000000;
		r.options.huber = 4;
		r.options.maxWork = 300000000;
		r.options.maxSweeps = 200;
		// Stock appearance targets are RGB8 intervals. Declare a practical relative
		// robust-loss stopping tolerance; do not imply exact parameter identification.
		r.options.tolerance = 1e-4;
		r.targets.domain = A::Domain::ForwardEncodedRgb8;
		r.targets.material = 1;
		// Caller explicitly supplies a conditional same-material appearance model.
		// Its gain absorbs unknown response; fitted RGB is never lamp energy in SI.
		r.targets.sameMaterialResponseAssumed = true;
		r.targets.provenance =
			"Declared conditional same-material encoded appearance comparison; unknown albedo/gain "
			"absorbed into relative powers; no calibrated energy claim";
		if (argc == 6)
			return ExportRegion(*data, source, r, uint32_t(std::stoul(argv[3])), std::stod(argv[5]),
								argv[4], read);
		Bake::Recipe recipe;
		r.targets.encoding = Bake::NetRadiantProfile(recipe);
		std::ifstream in(argv[2]);
		std::string magic;
		in >> magic;
		const bool coverageOnly = magic == "JKASR24-GEOMETRY-ONLY-1";
		Need(!coverageOnly || std::string(argv[3]) == "--coverage", "Geometry-only output flag required");
		bool sparse = magic == "JKASR21-SPARSE-ENCODED-ASSUMED-MATERIAL-1";
		Need(coverageOnly || sparse || magic == "JKASR20-ENCODED-ASSUMED-MATERIAL-1",
			 "Explicit conditional domain/response protocol required");
		in >> r.targets.gain;
		Vec(in, r.options.backgroundUpper);
		A::SparsePolicy policy;
		if (sparse)
		{
			in >> policy.maxSources >> policy.maxModels >> policy.trainingPenaltyPerFixture
				>> policy.minimumTrainingGain >> policy.pruneSlack >> policy.minimumContribution
				>> r.options.validationPenaltyPerFixture >> r.options.validationEquivalentSlack
				>> policy.provenance;
			r.options.maxBytes = read.maxBytes;
		}
		size_t fixtures = 0;
		in >> fixtures;
		Need(bool(in) && (sparse ? fixtures <= 128 : fixtures > 0 && fixtures <= (coverageOnly ? 16u : 4u)),
			 "Bounded fixture list required (ASR20: 1..4, ASR21: 0..128)");
		for (size_t i = 0; i < fixtures; ++i)
		{
			std::string kind;
			Id id = 0;
			in >> kind >> id;
			A::Fixture f;
			Shape::Emitter e;
			if (kind == "point" || kind == "mounted-point")
			{
				uint32_t ordinal = 0;
				if (kind == "mounted-point")
					in >> ordinal;
				e.geometry.source.id = id;
				e.geometry.source.state = HypothesisState::Candidate;
				Vec(in, e.geometry.position.world);
				e.geometry.position.known = true;
				e.geometry.kind = EmitterKind::Point;
				e.geometry.angular.kind = AngularKind::Isotropic;
				e.support.kind = SourceSupport::Kind::FreePoint;
				e.support.fixture = id;
				e.support.map = source.metadata.map.sha256;
				e.support.geometry = r.geometry.geometryKey;
				e.support.provenance = "Explicit supplied fixture point anchor";
				if (kind == "mounted-point")
				{
					Check(SourceSupport::FromMountedPointSurface(source.geometry, *visibility, ordinal,
												 id, e.geometry.position.world, 8, read, e.support));
				}
			}
			else if (kind == "surface" || kind == "surface-mesh")
			{
				uint32_t ordinal = 0;
				in >> ordinal;
				ShapeSelection::Request selection;
				selection.scene = r.scene;
				selection.fixture = id;
				selection.emittingSurfaces = {ordinal};
				// Original archived mask is not an exact luminous triangle lease.
				ShapeSelection::Result ranked;
				Check(ShapeSelection::Rank(selection, read, ranked));
				bool found = false;
				for (const auto &v : ranked.ranked)
					if (kind == "surface-mesh" && v.kind == EmitterKind::Rectangle &&
						ranked.original.original.count)
					{
						// The existing evaluator integrates this exact triangle lease.
						// The rectangle fields are only its API frame/bounds, not an
						// assertion that a nonrectangular face is a rectangle.
						e = v.emitter;
						e.support = ranked.original;
						e.geometry.angular.kind = AngularKind::Lambertian;
						e.geometry.angular.twoSided = e.support.twoSidedAttested;
						found = true;
						break;
					}
					else
					if (v.kind == EmitterKind::Rectangle &&
						(v.status == ShapeSelection::Status::Supported ||
						 v.status == ShapeSelection::Status::Approximation))
					{
						e = v.emitter;
						found = true;
						break;
					}
				Need(found, "Supplied fixture surface has no admissible rectangle bound");
			}
			else
				throw std::runtime_error(
					"Only explicit point anchors or supported original surfaces accepted");
			Vec(in, f.upperPower);
			if (sparse)
			{
				std::string evidence;
				A::FixtureEvidence prior;
				in >> evidence >> prior.provenance;
				Need(bool(in) && (evidence == "visible" || evidence == "hidden"),
					 "Explicit visible/hidden evidence and provenance reference required");
				prior.independentlyVisible = evidence == "visible";
				policy.evidence.push_back(prior);
			}
			e.rgbAmplitude = Vec3(1, 1, 1);
			f.variants = {e};
			f.provenance =
				"Supplied offline fixture anchor; whole-face emission remains conditional";
			r.fixtures.push_back(f);
		}
		for (size_t mask = 1; !sparse && mask < (size_t(1) << fixtures); ++mask)
		{
			A::Configuration c;
			c.name = "fixtures-" + std::to_string(mask);
			for (size_t j = 0; j < fixtures; ++j)
				if (mask & (size_t(1) << j))
					c.fixtures.push_back(j);
			r.configurations.push_back(c);
		}
		std::string material;
		auto train = Rows(in, Shape::Role::Training, source.geometry, material, !coverageOnly);
		if (coverageOnly)
		{
			Need(!r.fixtures.empty(), "Coverage adapter requires fixed fixtures");
			std::shared_ptr<const PointRefine::PlacementProvider> placement;
			Check(PointRefine::MakePlacement(r.scene, read, placement));
			r.geometry.visibility = PointRefine::WithOccupancy(r.geometry.visibility, placement);
			auto receivers = std::make_shared<std::vector<Bake::Receiver>>();
			for (const auto &row : train)
				receivers->push_back(row.receiver);
			std::shared_ptr<const Shape::Block> block;
			std::vector<Shape::Emitter> emitters;
			std::vector<Vec3> unit;
			for (const auto &fixture : r.fixtures)
			{
				emitters.push_back(fixture.variants[0]);
				unit.push_back(Vec3(1, 1, 1));
			}
			Check(Shape::Prepare(r.model, emitters,
				VectorChunk<Bake::Receiver>(receivers, 0, receivers->size()), r.geometry,
				r.sampling, read, block));
			std::vector<Shape::Prediction> predictions;
			Shape::Work work;
			Check(block->Evaluate(unit, read, predictions, work));
			std::cout << std::boolalpha << std::setprecision(17) << "{\"rows\":[";
			for (size_t i = 0; i < predictions.size(); ++i)
			{
				const auto &p = predictions[i];
				std::cout << (i ? "," : "") << "{\"id\":";
				Q(std::to_string(p.observation));
				std::cout << ",\"available\":" << p.available << ",\"visibility_issues\":"
					<< p.visibilityIssues << ",\"coefficient\":" << p.value.x << '}';
			}
			std::cout << "],\"geometry_only\":true,\"historical_source_certified\":false}\n";
			return 0;
		}
		auto validation = Rows(in, Shape::Role::Validation, source.geometry, material);
		std::string extra;
		Need(!(in >> extra), "Trailing fit fields");
		A::Experiment experiment;
		Check(sparse ? A::SelectSparse(r, policy, train, validation, read, experiment) :
			A::FitAndValidate(r, train, validation, read, experiment));
		// Held-out target file is opened only after training and validation have frozen.
		std::ifstream hidden(argv[3]);
		auto held = Rows(hidden, Shape::Role::Heldout, source.geometry, material);
		Need(!(hidden >> extra), "Trailing held-out fields");
		std::vector<A::Score> scores;
		A::Score baseline;
		Check(A::AuditHeldout(experiment, held, read, scores, baseline));
		std::cout << std::setprecision(12) << "{\"schema\":";
		Q(sparse ? "org.openjk.sparse-fit.v1" : "org.openjk.aesthetic-fit.v1");
		std::cout << ",\"map\":";
		Q(source.metadata.map.path);
		std::cout << ",\"material\":";
		Q(material);
		std::cout << ",\"selected\":"
				  << (experiment.Selected() == SIZE_MAX ? -1 : int(experiment.Selected()));
		if (sparse)
			SparseJson(experiment.Sparse(), r);
		std::cout << ",\"validation_equivalent\":[";
		for (size_t i = 0; i < experiment.ValidationAlternatives().size(); ++i)
			std::cout << (i ? "," : "") << experiment.ValidationAlternatives()[i];
		std::cout << "],\"work\":" << experiment.Work()
				  << ",\"absolute_scale_known\":false,\"historical_source_truth\":\"Unavailable\","
					 "\"material_response\":\"Assumed same-material encoded appearance "
					 "only\",\"baseline\":{";
		std::cout << "\"training\":";
		ScoreJson(experiment.Baseline().training);
		std::cout << ",\"validation\":";
		ScoreJson(experiment.Baseline().validation);
		std::cout << ",\"heldout\":";
		ScoreJson(baseline);
		std::cout << "},\"configurations\":[";
		for (size_t i = 0; i < scores.size(); ++i)
		{
			if (i)
				std::cout << ',';
			const auto &fit = experiment.Fits()[i];
			std::cout << "{\"name\":";
			Q(fit.name);
			std::cout << ",\"available\":" << fit.available << ",\"converged\":" << fit.converged
					  << ",\"limitation\":";
			Q(fit.limitation);
			std::cout << ",\"background\":";
			JsonVec(fit.background);
			std::cout << ",\"background_at_bound\":" << fit.backgroundAtBound << ",\"training\":";
			ScoreJson(fit.training);
			std::cout << ",\"validation\":";
			ScoreJson(fit.validation);
			std::cout << ",\"heldout\":";
			ScoreJson(scores[i]);
			std::cout << ",\"sources\":[";
			for (size_t j = 0; j < fit.sources.size(); ++j)
			{
				if (j)
					std::cout << ',';
				const auto &s = fit.sources[j];
				std::cout << "{\"fixture\":";
				Q(std::to_string(s.fixture));
				std::cout << ",\"state\":\"Candidate\",\"type\":";
				Q(s.emitter.support.kind == SourceSupport::Kind::OriginalSurface ?
					"OriginalSurface" : ShapeSelection::KindName(s.emitter.geometry.kind));
				std::cout << ",\"emitting_area_world_squared\":"
						  << s.placement.emittingAreaWorldSquared << ",\"position\":";
				JsonVec(s.emitter.geometry.position.world);
				std::cout << ",\"outward\":";
				JsonVec(s.emitter.support.outward);
				std::cout << ",\"half_width_world\":" << s.emitter.geometry.geometry.halfWidthWorld
					<< ",\"half_height_world\":" << s.emitter.geometry.geometry.halfHeightWorld;
				std::cout << ",\"relative_rgb\":";
				JsonVec(s.relativePower);
				std::cout << ",\"power_at_bound\":" << s.atPowerBound
						  << ",\"placement_conditional\":" << s.placement.conditional
						  << ",\"source_side_certified\":" << s.placement.sourceSideCertified
						  << ",\"column_rms\":" << s.columnRms
						  << ",\"maximum_correlation\":" << s.maximumColumnCorrelation
						  << ",\"quadrature_difference\":" << s.quadratureDifference
						  << ",\"placement\":";
				Q(PointRefine::ConstraintName(s.placement.placement.state));
				std::cout << ",\"placement_reason\":";
				Q(s.placement.reason);
				if (s.emitter.support.mountedPointPlane)
				{
					std::cout << ",\"support_key\":";
					Q(Disk::Hex(s.placement.key));
					std::cout << ",\"support_visual_issues\":" << s.placement.visualIssues;
				}
				std::cout << '}';
			}
			std::cout << "]}";
		}
		std::cout << "]}\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 2;
	}
}
