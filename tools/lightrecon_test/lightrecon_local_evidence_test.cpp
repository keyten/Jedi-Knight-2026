// Independent controlled forward fixtures. Assertions stay active in Release.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/bake_math.h"
#include "local_light_evidence.h"
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
using namespace LocalLightEvidence;
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
using namespace Bake::Detail;
using EvidenceResult = LocalLightEvidence::Result;
namespace B = Rend2LightRecon::Core::Bake;
int checks = 0, failures = 0;
#define CHECK(x)                                                                                             \
	do                                                                                                       \
	{                                                                                                        \
		++checks;                                                                                            \
		if (!(x))                                                                                            \
		{                                                                                                    \
			++failures;                                                                                      \
			std::printf("FAIL %d: %s\n", __LINE__, #x);                                                      \
		}                                                                                                    \
	} while (0)
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> p)
{
	return VectorChunk<T>(p, 0, p->size());
}
class FreeDomain final : public PointClassifier
{
  public:
	Error Classify(Vec3, double, PointDomain &out) const override
	{
		out = PointDomain::Free;
		return {};
	}
};
class Uncertain final : public VisibilityProvider
{
  public:
	std::shared_ptr<const VisibilityProvider> original;
	bool noSky = false, unknownLocal = false, partialLocal = false, optical = false;
	Error Query(const RayQuery &q, const ReadContext &read, RayResult &out) const override
	{
		auto e = original->Query(q, read, out);
		if (noSky)
			out.sky = SkyReach::NotReached;
		if (q.purpose == RayPurpose::Shadow && (unknownLocal || (partialLocal && q.startWorld.x > 0)))
			out.state = VisibilityState::Unknown;
		if (optical)
			out.issues |= OpticalUnknown;
		return e;
	}
};
class Cancel final : public Cancellation
{
  public:
	bool IsCancelled() const noexcept override { return true; }
};
struct Rig
{
	Request request;
	ReadContext read;
	B::Recipe recipe;
	std::shared_ptr<std::vector<Observation>> observations = std::make_shared<std::vector<Observation>>();
	std::shared_ptr<const StaticVisibility> scene;
	Rig()
	{
		read.maxBytes = 64ull * 1024 * 1024;
		recipe.fast = false;
		request.options.allowAssumedProfiles = true;
		SurfaceCapture capture;
		capture.Begin(BuildPolicy::ForceRebuild);
		LightmapMapIdentity map;
		map.path = "maps/asr08-owned.bsp";
		map.bytes = 1;
		map.sha256[0] = 8;
		CHECK(capture.SetMap(map));
		for (int face = 0; face < 2; ++face)
		{
			BakedSurface s;
			s.ordinal = face;
			s.kind = BakedSurfaceKind::Face;
			s.motion = SurfaceMotion::StaticWorld;
			s.material = "owned";
			s.materialFacts.available = true;
			s.materialFacts.cull = SurfaceCull::None;
			s.materialFacts.signals = face == 0 ? uint32_t(MaterialSky) : 0u;
			float z = face == 0 ? 2048.f : -16.f;
			for (Vec3 p : {Vec3(-20000, -20000, z), Vec3(20000, -20000, z), Vec3(20000, 20000, z),
						   Vec3(-20000, 20000, z)})
			{
				BakedVertex v;
				v.position = p;
				s.vertices.push_back(v);
			}
			s.indices = {0, 1, 2, 0, 2, 3};
			CHECK(capture.Add(s));
		}
		CHECK(capture.Freeze());
		CHECK(StaticVisibility::Build(capture.Acquire(), VisibilityBuildOptions(), read, scene,
									  std::make_shared<FreeDomain>()));
		request.geometry.visibility = scene;
		request.geometry.geometryKey = scene->CertificationKey();
		for (int y = -2; y <= 2; ++y)
			for (int x = -3; x <= 3; ++x)
			{
				Observation s;
				s.eligible = true;
				s.receiver.sample = observations->size() + 1;
				s.receiver.world = Vec3(float(x * 128), float(y * 128), 0);
				s.receiver.geometricNormal = s.receiver.shadingNormal = Vec3(0, 0, 1);
				s.receiver.mappedPlanar = s.receiver.compilerPlacementCertified = true;
				observations->push_back(s);
			}
		request.globals = {Global("candidate-sun", V(120, 120, 120))};
		request.trials = {Point("warm-local", V(-160, 0, 128), V(160, 30, 15)),
						  Point("wrong-location", V(160, 0, 128), V(160, 30, 15)),
						  Point("broad-white", V(0, 0, 1500), V(35952.016, 35952.016, 35952.016))};
	}
	GlobalAlternative Global(const char *id, V sun, V sky = V()) const
	{
		GlobalAlternative g;
		g.id = id;
		g.provenance = "explicit controlled fixture; not recovered historical truth";
		g.encodingAttested = true;
		g.candidate.profile = B::NetRadiantProfile(recipe);
		g.candidate.toSource = Vec3(0, 0, 1);
		g.candidate.sunRgb = sun.Float();
		g.candidate.skyRgb = sky.Float();
		g.candidate.model = Peak(sun) ? Solar::Model::SunAndSky : Solar::Model::SkyOnly;
		return g;
	}
	LocalTrial Point(const char *id, V xyz, V rgb) const
	{
		LocalTrial t;
		t.id = id;
		auto sources = std::make_shared<std::vector<B::SourceHypothesis>>(1);
		auto &s = sources->at(0);
		s.id = 1;
		s.kind = B::SourceKind::Point;
		s.position.known = s.amplitudeKnown = true;
		s.position.world = xyz.Float();
		s.compilerRgbAmplitude = rgb.Float();
		t.sources = Lease(sources);
		return t;
	}
	// Independent point/cosine, sky quadrature and encoding reference. No call
	// to Bake::Encode, CpuForwardEvaluator or the evidence API creates targets.
	void Illuminate(V sun, V local, V sky = V(), bool bleeding = false)
	{
		double skyFactor = (8 * std::sin(Pi / 12) + 8 * std::sin(Pi / 4) + 1) / 17;
		for (auto &s : *observations)
		{
			V delta = V(-160, 0, 128) - (V(s.receiver.world) + V(0, 0, 1));
			double d = Length(delta), coefficient = 7500 * delta.z / (d * d * d);
			V value = sun + sky * skyFactor + V(recipe.explicitSurfaceAmbient) + local * coefficient;
			if (bleeding && s.receiver.world.x < 0 && std::abs(s.receiver.world.y) <= 128)
				value = value + V(60, 8, 3); // outgoing red patch: attribution deliberately unknown
			for (int c = 0; c < 3; ++c)
				value[c] = 255 * std::pow(value[c] / 255, 1 / recipe.gamma);
			double peak = Peak(value),
				   gain = recipe.exposure == 0
							  ? (peak > 255 ? 255 / peak : 1)
							  : (peak > 0 ? 255 * (1 - std::exp(-peak / recipe.exposure)) / peak : 0);
			value = value * (gain / recipe.compensate);
			for (int c = 0; c < 3; ++c)
			{
				if (recipe.srgbOutput)
				{
					double v = value[c] / 255;
					value[c] = std::floor(
						255 * (v <= .0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - .055) + .5);
				}
				s.original.bytes[c] = uint8_t(std::min(255., value[c]));
			}
		}
	}
	EvidenceResult Run()
	{
		request.observations = Lease(observations);
		EvidenceResult out;
		auto e = Evaluate(request, read, out);
		CHECK(e);
		if (!e)
			std::printf("ERROR: %s\n", e.message.c_str());
		return out;
	}
};
struct Witness
{
	std::string name;
	EvidenceResult result;
};
std::vector<Witness> witnesses;
void CheckNonnegative(const EvidenceResult &out)
{
	for (const auto &g : out.alternatives)
		for (const auto &t : g.trials)
			for (const auto &row : t.rows)
				if (row.state == State::Conditional)
				{
					CHECK(Positive(V(row.globalPreencoding)) && Positive(V(row.trialPreencoding)));
					CHECK(Positive(V(row.underCodes)) && Positive(V(row.overCodes)));
					CHECK(row.mask & AttributionUnresolved);
				}
}
int main(int argc, char **argv)
{
	std::setbuf(stdout, nullptr);
	{
		Rig r;
		r.Illuminate(V(120, 120, 120), V(160, 30, 15));
		r.request.globals.push_back(r.Global("no-sun-alternative", V()));
		auto original = *r.observations;
		auto out = r.Run();
		CHECK(out.state == State::Conditional);
		CHECK(out.alternatives.size() == 2);
		for (size_t i = 0; i < original.size(); ++i)
			for (int c = 0; c < 4; ++c)
				CHECK(original[i].original.bytes[c] == r.observations->at(i).original.bytes[c]);
		const auto &with = out.alternatives[0].trials;
		const auto &without = out.alternatives[1].trials;
		CHECK(with[1].used == 35 && with[1].rmseCodes < .001 && with[1].improvementCodes > 3);
		CHECK(with[1].rmseCodes < with[2].rmseCodes && with[1].rmseCodes < with[3].rmseCodes);
		CHECK(without[3].rmseCodes < without[1].rmseCodes); // broad candidate absorbs unremoved global field
		for (const auto &row : with[1].rows)
			CHECK(row.mask & CompetingGlobals);
		CheckNonnegative(out);
		witnesses.push_back({"mixed_sun_local", out});
	}
	{
		Rig r;
		r.Illuminate(V(120, 120, 120), V());
		auto out = r.Run();
		CHECK(out.alternatives[0].trials[0].rmseCodes < .001);
		for (const auto &t : out.alternatives[0].trials)
			CHECK(t.improvementCodes <= .001);
		witnesses.push_back({"sun_only", out});
	}
	{
		Rig r;
		r.Illuminate(V(), V(160, 30, 15));
		r.request.globals = {r.Global("no-sun", V()), r.Global("excessive-sun", V(120, 120, 120))};
		auto out = r.Run();
		CHECK(out.alternatives[0].trials[1].rmseCodes < .001);
		CHECK(out.alternatives[1].trials[0].rmseCodes > 80);
		for (const auto &row : out.alternatives[1].trials[0].rows)
			CHECK(row.mask & GlobalOverprediction);
		CheckNonnegative(out);
		witnesses.push_back({"no_sun", out});
	}
	{
		Rig r;
		r.Illuminate(V(120, 120, 120), V(1600, 300, 150));
		r.request.trials = {r.Point("saturated-warm", V(-160, 0, 128), V(1600, 300, 150))};
		auto out = r.Run();
		const auto &g = out.alternatives[0];
		CHECK(g.trials[1].rmseCodes < .001);
		bool coupled = false;
		for (size_t i = 0; i < g.trials[1].rows.size(); ++i)
		{
			const auto &row = g.trials[1].rows[i];
			const auto &base = g.trials[0].rows[i];
			if (row.mask & Saturated)
				CHECK(!row.observed[0].upperKnown);
			if (base.overCodes.y > 5 && row.predictedCodes.y < base.predictedCodes.y &&
				row.trialPreencoding.y > 0)
				coupled = true; // positive red light lowers encoded green through shared-peak normalization
		}
		CHECK(coupled);
		CheckNonnegative(out);
		witnesses.push_back({"saturated_mixed", out});
	}
	{
		Rig r;
		r.Illuminate(V(600, 600, 600), V());
		r.request.trials.clear();
		r.request.globals = {r.Global("scale-300", V(300, 300, 300)),
							 r.Global("scale-600", V(600, 600, 600))};
		auto out = r.Run();
		CHECK(out.alternatives[0].trials[0].rmseCodes == 0);
		CHECK(out.alternatives[1].trials[0].rmseCodes == 0); // no unique saturated solar scale
		witnesses.push_back({"saturated_scale_ambiguity", out});
	}
	{
		Rig r;
		r.Illuminate(V(120, 120, 120), V(160, 30, 15), V(), true);
		r.request.trials.push_back(r.Point("grey-local", V(-160, 0, 128), V(160, 160, 160)));
		auto out = r.Run();
		const auto &g = out.alternatives[0];
		CHECK(g.trials[1].rmseCodes < g.trials.back().rmseCodes);
		CHECK(g.trials[1].improvementCodes > 3);
		bool preservedPatch = false;
		for (const auto &row : g.trials[1].rows)
			if (row.underCodes.x > 20 && row.underCodes.x > row.underCodes.y * 3)
				preservedPatch = true;
		CHECK(preservedPatch);
		CheckNonnegative(out);
		witnesses.push_back({"color_bleeding", out});
	}
	// Nonlinear recipes are explicit alternatives; RGB8 is never linearized.
	for (int variant = 0; variant < 3; ++variant)
	{
		Rig r;
		r.recipe.gamma = variant == 0 ? 2 : 1;
		r.recipe.srgbOutput = variant == 1;
		r.recipe.exposure = variant == 2 ? 300 : 0;
		r.recipe.explicitSurfaceAmbient = Vec3(5, 3, 1);
		r.request.globals = {r.Global("explicit-profile", V(120, 120, 120), V(12, 15, 20))};
		r.Illuminate(V(120, 120, 120), V(160, 30, 15), V(12, 15, 20));
		auto out = r.Run();
		CHECK(out.alternatives[0].trials[1].rmseCodes < .001);
		CheckNonnegative(out);
	}
	{
		Rig r;
		r.Illuminate(V(120, 120, 120), V(160, 30, 15));
		(*r.observations)[0].original.missing = true;
		(*r.observations)[1].eligible = false;
		(*r.observations)[2].receiver.compilerPlacementCertified = false;
		(*r.observations)[3].original.component = Decode::Component::GridDirected;
		(*r.observations)[4].original.directionData = true;
		(*r.observations)[5].original.encoding = Decode::Encoding::LinearRgb32F;
		(*r.observations)[6].uncertaintyCodes = 2;
		auto out = r.Run();
		CHECK(out.alternatives[0].trials[1].used == 29);
		const uint32_t masks[] = {Missing,
								  Ineligible,
								  UnsupportedReceiver,
								  UnsupportedObservation,
								  UnsupportedObservation,
								  UnsupportedObservation};
		for (size_t i = 0; i < 6; ++i)
		{
			const auto &row = out.alternatives[0].trials[1].rows[i];
			CHECK(row.state == State::Unavailable && (row.mask & masks[i]));
		}
		CHECK(out.alternatives[0].trials[1].rows[6].mask & ObservationUncertainty);
		CHECK(out.alternatives[0].trials[1].rows[6].observed[0].state == State::Valid);
		witnesses.push_back({"observation_masks", out});
	}
	for (int variant = 0; variant < 5; ++variant)
	{
		Rig r;
		r.Illuminate(V(120, 120, 120), V(160, 30, 15));
		if (variant == 0)
			r.request.globals[0].encodingAttested = false;
		if (variant == 1)
			r.request.globals[0].candidate.profile = B::ProfileHypothesis();
		if (variant == 2)
			r.request.geometry.visibility.reset();
		if (variant >= 3)
		{
			auto v = std::make_shared<Uncertain>();
			v->original = r.scene;
			v->noSky = variant == 3;
			v->optical = variant == 4;
			r.request.geometry.visibility = v;
		}
		auto out = r.Run();
		CHECK(out.state == State::Unavailable);
		for (const auto &t : out.alternatives[0].trials)
			CHECK(t.state == State::Unavailable && t.used == 0);
	}
	{
		Rig r;
		r.Illuminate(V(120, 120, 120), V(160, 30, 15));
		auto unknown = r.request.globals[0];
		unknown.id = "unidentified-profile";
		unknown.candidate.profile = B::ProfileHypothesis();
		r.request.globals.insert(r.request.globals.begin(), unknown);
		auto out = r.Run();
		CHECK(out.state == State::Conditional);
		CHECK(out.alternatives[0].state == State::Unavailable);
		CHECK(out.alternatives[1].trials[1].rmseCodes < .001);
		witnesses.push_back({"competing_unknown_profile", out});
	}
	for (bool partial : {false, true})
	{
		Rig r;
		r.Illuminate(V(120, 120, 120), V(160, 30, 15));
		auto v = std::make_shared<Uncertain>();
		v->original = r.scene;
		v->unknownLocal = !partial;
		v->partialLocal = partial;
		r.request.geometry.visibility = v;
		auto out = r.Run();
		const auto &g = out.alternatives[0];
		CHECK(g.trials[0].used == 35);
		CHECK(g.trials[1].used == (partial ? 20u : 0u));
		if (partial)
		{
			double sum = 0;
			for (size_t i = 0; i < g.trials[1].rows.size(); ++i)
				if (g.trials[1].rows[i].state == State::Conditional)
					sum += g.trials[0].rows[i].squaredErrorCodes;
			CHECK(std::abs(g.trials[1].baselineRmseCodes - std::sqrt(sum / g.trials[1].used)) < 1e-9);
		}
		witnesses.push_back({partial ? "partial_local_geometry" : "unknown_local_geometry", out});
	}
	// Invalid/negative/nonfinite source energy, duplicate IDs and bounded work
	// clear outputs on failure. No negative residual is clamped into a lamp.
	for (int variant = 0; variant < 10; ++variant)
	{
		Rig r;
		r.Illuminate(V(120, 120, 120), V(160, 30, 15));
		if (variant == 0)
			r.request.trials[0] = r.Point("negative", V(-160, 0, 128), V(-1, 0, 0));
		if (variant == 1)
			r.request.trials[0] =
				r.Point("nonfinite", V(-160, 0, 128), V(std::numeric_limits<double>::infinity(), 0, 0));
		if (variant == 2)
			r.request.globals[0].candidate.sunRgb.x = -1;
		if (variant == 3)
			r.observations->at(1).receiver.sample = 1;
		if (variant == 4)
			r.request.options.maxPairs = 1;
		if (variant == 5)
			r.request.options.maxEncodingCalls = 1;
		if (variant == 6)
			r.request.options.maxBytes = 1;
		if (variant == 7)
			r.request.options.maxRays = 1;
		if (variant == 8)
			r.request.options.maxTraversal = 1;
		Cancel cancel;
		if (variant == 9)
			r.read.cancellation = &cancel;
		r.request.observations = Lease(r.observations);
		EvidenceResult out;
		out.state = State::Conditional;
		auto e = Evaluate(r.request, r.read, out);
		CHECK(!e);
		CHECK(out.state == State::Unavailable && out.alternatives.empty());
		CHECK(e.code == (variant < 4 ? Code::InvalidInput
									 : variant == 6 ? Code::MemoryLimit
													: variant == 9 ? Code::Cancelled : Code::WorkLimit));
	}
	if (argc > 1)
	{
		std::ofstream file(argv[1]);
		if (!file)
		{
			std::printf("Cannot write witness\n");
			return 1;
		}
		file << std::setprecision(10);
		file << "{\"schema\":\"asr08-local-evidence-v1\",\"domain\":\"profile-specific encoded code "
				"intervals\","
				"\"prediction_unit\":\"compiler-relative-255 forward predictions "
				"only\",\"exact_historical_sources\":false,\"fixtures\":[";
		for (size_t k = 0; k < witnesses.size(); ++k)
		{
			if (k)
				file << ',';
			const auto &w = witnesses[k];
			file << "{\"name\":\"" << w.name << "\",\"state\":\"" << StateName(w.result.state)
				 << "\",\"alternatives\":[";
			for (size_t a = 0; a < w.result.alternatives.size(); ++a)
			{
				if (a)
					file << ',';
				const auto &g = w.result.alternatives[a];
				file << "{\"id\":\"" << g.id << "\",\"trials\":[";
				for (size_t j = 0; j < g.trials.size(); ++j)
				{
					if (j)
						file << ',';
					const auto &t = g.trials[j];
					file << "{\"id\":\"" << t.id << "\",\"state\":\"" << StateName(t.state)
						 << "\",\"used\":" << t.used << ",\"requested\":" << t.rows.size();
					if (t.used)
						file << ",\"rmse_codes\":" << t.rmseCodes
							 << ",\"baseline_same_mask_rmse_codes\":" << t.baselineRmseCodes
							 << ",\"improvement_codes\":" << t.improvementCodes;
					else
						file << ",\"rmse_codes\":null,\"baseline_same_mask_rmse_codes\":null,\"improvement_"
								"codes\":null";
					file << ",\"row_masks\":[";
					for (size_t i = 0; i < t.rows.size(); ++i)
					{
						if (i)
							file << ',';
						file << t.rows[i].mask;
					}
					file << "]}";
				}
				file << "]}";
			}
			file << "]}";
		}
		file << "],\"checks\":" << checks << ",\"failures\":" << failures << "}\n";
		if (!file)
		{
			std::printf("Witness write failed\n");
			return 1;
		}
	}
	std::printf("local evidence: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
