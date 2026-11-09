// SPDX-License-Identifier: GPL-2.0-or-later
#include "local_light_evidence.h"
#include "lightrecon/bake_math.h"
#include <set>

namespace LocalLightEvidence
{
namespace
{
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
using namespace Bake::Detail;
void Require(bool value, Code code, const char *reason)
{
	if (!value)
		throw Failure(code, reason);
}
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> p)
{
	return VectorChunk<T>(p, 0, p->size());
}
template <class T> bool Leased(const Chunk<T> &c) { return !c.count || (c.data && c.owner); }
// Preserve unknown optics/geometry and require a real sky hit for globals.
// A custom provider's clear segment alone is never evidence of sunlight.
class Guard final : public VisibilityProvider
{
  public:
	std::shared_ptr<const VisibilityProvider> original;
	Error Query(const RayQuery &q, const ReadContext &read, RayResult &out) const override
	{
		if (!original)
			return Failure(Code::MissingGeometry, "Missing static visibility");
		auto e = original->Query(q, read, out);
		if (e &&
			(out.reliability != RayReliability::Geometric ||
			 (out.issues & (OpticalUnknown | UnsupportedGeometry | IncompleteGeometry | PolicyAssumption)) ||
			 (q.purpose == RayPurpose::Sky && out.state == VisibilityState::Visible &&
			  out.sky != SkyReach::Reached)))
		{
			out.state = VisibilityState::Unknown;
			if (q.purpose == RayPurpose::Sky)
				out.issues |= SkyBoundaryMissing;
		}
		return e;
	}
};
Frame Orientation(Vec3 direction)
{
	V z = V(direction) * -1, x = Unit(Cross(std::abs(z.z) < .9 ? V(0, 0, 1) : V(0, 1, 0), z));
	Frame f;
	f.known = true;
	f.forward = z.Float();
	f.right = x.Float();
	f.up = Cross(z, x).Float();
	return f;
}
struct Engine
{
	const Request &request;
	const ReadContext &read;
	Result &result;
	Bake::EvaluationContext geometry;
	Engine(const Request &r, const ReadContext &c, Result &out)
		: request(r), read(c), result(out), geometry(r.geometry)
	{
		auto guard = std::make_shared<Guard>();
		guard->original = geometry.visibility;
		geometry.visibility = guard;
	}
	void Check() { Require(!Cancelled(read), Code::Cancelled, "Local evidence cancelled"); }
	Bake::Options ForwardOptions() const
	{
		Bake::Options o;
		o.output = Bake::Domain::CompilerLinearRelative255;
		o.allowAssumed = request.options.allowAssumedProfiles;
		o.maxPairs = request.options.maxPairs - result.work.pairs;
		o.maxRays = request.options.maxRays - result.work.rays;
		o.maxWork = request.options.maxTraversal - result.work.nodeTests - result.work.triangleTests;
		return o;
	}
	Bake::Result Forward(const Bake::ProfileHypothesis &profile, const Chunk<Bake::SourceHypothesis> &sources,
						 const Chunk<Bake::Receiver> &receivers)
	{
		Check();
		auto o = ForwardOptions();
		Require(o.maxPairs && o.maxRays && o.maxWork, Code::WorkLimit, "Local evidence forward budget");
		Bake::Result value;
		auto e = Bake::CpuForwardEvaluator().Evaluate(profile, sources, receivers, Decode::Profile(),
													  geometry, o, read, value);
		if (!e)
			throw e;
		result.work.pairs += value.work.pairs;
		result.work.rays += value.work.rays;
		result.work.nodeTests += value.work.nodeTests;
		result.work.triangleTests += value.work.triangleTests;
		return value;
	}
	Vec3 Encode(const Bake::Recipe &recipe, V value)
	{
		Check();
		Require(result.encodingCalls < request.options.maxEncodingCalls, Code::WorkLimit,
				"Local evidence encoding budget");
		++result.encodingCalls;
		Vec3 encoded;
		uint8_t packed[3];
		auto e = Bake::Encode(recipe, value.Float(), encoded, packed);
		if (!e)
			throw e;
		return encoded;
	}
};
void Score(Row &row)
{
	V p(row.predictedCodes), under, over;
	for (int c = 0; c < 3; ++c)
	{
		const auto &b = row.observed[c];
		under[c] = std::max(0., b.lower - p[c]);
		over[c] = b.upperKnown ? std::max(0., p[c] - b.upper) : 0;
		row.squaredErrorCodes += (under[c] * under[c] + over[c] * over[c]) / 3;
	}
	row.underCodes = under.Float();
	row.overCodes = over.Float();
}
void Validate(const Request &r, const ReadContext &read)
{
	const auto &o = r.options;
	Require(Leased(r.observations) && r.observations.count && r.observations.count <= o.maxSamples &&
				!r.globals.empty() && r.globals.size() <= o.maxGlobals && r.trials.size() <= o.maxTrials,
			Code::InvalidInput, "Invalid local evidence dimensions/lease");
	Require(o.maxSamples <= 65536 && o.maxGlobals <= 64 && o.maxTrials <= 256 && o.maxSources <= 256,
			Code::InvalidInput, "Unsupported local evidence dimensions");
	uint64_t bytes = uint64_t(r.observations.count) * r.globals.size() * (r.trials.size() + 1) *
					 (sizeof(Row) + sizeof(Bake::Prediction) * 2 + sizeof(Observation));
	Require(bytes <= o.maxBytes && (!read.maxBytes || bytes <= read.maxBytes), Code::MemoryLimit,
			"Local evidence result memory budget");
	std::set<Id> ids;
	for (size_t i = 0; i < r.observations.count; ++i)
	{
		const auto &s = r.observations.data[i];
		Require(s.receiver.sample && ids.insert(s.receiver.sample).second &&
					std::isfinite(s.uncertaintyCodes) && s.uncertaintyCodes >= 0 && s.uncertaintyCodes <= 255,
				Code::InvalidInput, "Invalid observation ID/uncertainty");
	}
	std::set<std::string> names;
	for (const auto &g : r.globals)
	{
		const auto &p = g.candidate;
		Require(!g.id.empty() && names.insert(g.id).second && !g.provenance.empty() &&
					(p.model == Solar::Model::SunAndSky || p.model == Solar::Model::SkyOnly) &&
					Positive(V(p.sunRgb)) && Positive(V(p.skyRgb)) &&
					(p.model != Solar::Model::SkyOnly || Peak(V(p.sunRgb)) == 0) &&
					(Peak(V(p.sunRgb)) == 0 || Normal(V(p.toSource))),
				Code::InvalidInput,
				"Invalid explicit global alternative (uniform-fill controls are not sun/sky)");
	}
	names.clear();
	names.insert("zero-local");
	for (const auto &t : r.trials)
	{
		Require(!t.id.empty() && names.insert(t.id).second && Leased(t.sources) && t.sources.count &&
					t.sources.count <= o.maxSources,
				Code::InvalidInput, "Invalid local trial");
		ids.clear();
		for (size_t i = 0; i < t.sources.count; ++i)
		{
			const auto &s = t.sources.data[i];
			Require(s.id && ids.insert(s.id).second &&
						(s.kind == Bake::SourceKind::Point || s.kind == Bake::SourceKind::Spot ||
						 s.kind == Bake::SourceKind::Polygon) &&
						s.amplitudeDomain == Bake::AmplitudeDomain::CompilerRelative && s.amplitudeKnown &&
						Positive(V(s.compilerRgbAmplitude)),
					Code::InvalidInput,
					"Local trials require finite nonnegative compiler-relative local sources");
		}
	}
}
AlternativeResult EvaluateGlobal(Engine &engine, const GlobalAlternative &g)
{
	const auto &r = engine.request;
	const auto &p = g.candidate;
	AlternativeResult out;
	out.id = g.id;
	out.profile = p.profile.id;
	out.provenance = g.provenance;
	TrialResult base;
	base.id = "zero-local";
	base.rows.resize(r.observations.count);
	Bake::Recipe recipe;
	auto recipeStatus = Bake::DescribeRecipe(p.profile, recipe);
	auto receivers = std::make_shared<std::vector<Bake::Receiver>>();
	std::vector<size_t> indices;
	for (size_t i = 0; i < r.observations.count; ++i)
	{
		engine.Check();
		const auto &s = r.observations.data[i];
		auto &row = base.rows[i];
		row.sample = s.receiver.sample;
		if (r.globals.size() > 1)
			row.mask |= CompetingGlobals;
		if (p.profile.applicability != Bake::Applicability::VerifiedFeatureSubset)
			row.mask |= AssumedProfile;
		if (s.original.missing)
			row.mask |= Missing;
		if (!s.eligible)
			row.mask |= Ineligible;
		if (s.original.encoding != Decode::Encoding::Rgb8 ||
			s.original.component != Decode::Component::Lightmap || s.original.directionData ||
			s.receiver.domain != Bake::Domain::EncodedRgb8BeforeQuantization)
			row.mask |= UnsupportedObservation;
		if (!g.encodingAttested || !recipeStatus)
			row.mask |= UnknownEncoding;
		if (recipeStatus && !(row.mask & (Missing | UnsupportedObservation | UnknownEncoding)))
			for (int c = 0; c < 3; ++c)
			{
				auto &b = row.observed[c];
				const auto code = s.original.bytes[c];
				b.state = State::Valid;
				b.lower = std::max(0., code - s.uncertaintyCodes);
				b.upperKnown = code < 255;
				// Encode's sRGB branch rounds internally; the other branch truncates.
				b.upper = code + (recipe.srgbOutput ? 0 : 1) + s.uncertaintyCodes;
				if (code == 255)
				{
					b.mask |= Saturated;
					row.mask |= Saturated;
				}
				if (s.uncertaintyCodes)
				{
					b.mask |= ObservationUncertainty;
					row.mask |= ObservationUncertainty;
				}
			}
		if (row.mask & (Missing | Ineligible | UnsupportedObservation | UnknownEncoding))
			continue;
		if (!Bake::Capability(p.profile, Bake::SourceKind::Directional, s.receiver, engine.ForwardOptions())
				 .implemented)
		{
			row.mask |= UnsupportedReceiver;
			continue;
		}
		receivers->push_back(s.receiver);
		indices.push_back(i);
	}
	if (!receivers->empty())
	{
		auto sources = std::make_shared<std::vector<Bake::SourceHypothesis>>();
		auto add = [&](Bake::SourceKind kind, Vec3 rgb) {
			if (Peak(V(rgb)) == 0)
				return;
			Bake::SourceHypothesis s;
			s.id = sources->size() + 1;
			s.kind = kind;
			s.orientation = Orientation(kind == Bake::SourceKind::Directional ? p.toSource : Vec3(0, 0, 1));
			s.amplitudeKnown = true;
			s.compilerRgbAmplitude = rgb;
			sources->push_back(s);
		};
		add(Bake::SourceKind::Directional, p.sunRgb);
		add(Bake::SourceKind::Sky, p.skyRgb);
		// Missing geometry is an unavailable comparison, never a zero field.
		if (!sources->empty() && !r.geometry.visibility)
			for (size_t i : indices)
				base.rows[i].mask |= UnknownGlobal;
		else
		{
			auto prediction = engine.Forward(p.profile, Lease(sources), Lease(receivers));
			for (size_t j = 0; j < indices.size(); ++j)
			{
				auto &row = base.rows[indices[j]];
				const auto &v = prediction.predictions.data[j];
				if (!v.available)
				{
					row.mask |= UnknownGlobal;
					row.reason = v.reason;
					continue;
				}
				row.globalPreencoding = v.compilerRelative;
				row.predictedCodes = engine.Encode(recipe, V(row.globalPreencoding));
				Score(row);
				if (Peak(V(row.overCodes)) > 0)
					row.mask |= GlobalOverprediction;
				row.state = State::Conditional;
				base.rmseCodes += row.squaredErrorCodes;
				++base.used;
			}
		}
	}
	if (base.used)
	{
		base.state = out.state = State::Conditional;
		base.rmseCodes = std::sqrt(base.rmseCodes / base.used);
		base.baselineRmseCodes = base.rmseCodes;
	}
	out.trials.push_back(base);
	for (const auto &trial : r.trials)
	{
		TrialResult value;
		value.id = trial.id;
		value.rows = base.rows;
		auto localReceivers = std::make_shared<std::vector<Bake::Receiver>>();
		std::vector<size_t> localIndices;
		for (size_t i = 0; i < base.rows.size(); ++i)
			if (base.rows[i].state == State::Conditional)
			{
				value.rows[i].state = State::Unavailable;
				value.rows[i].squaredErrorCodes = 0;
				localReceivers->push_back(r.observations.data[i].receiver);
				localIndices.push_back(i);
			}
		if (!localReceivers->empty())
		{
			bool supported = bool(r.geometry.visibility);
			for (const auto &receiver : *localReceivers)
				for (size_t j = 0; j < trial.sources.count; ++j)
					supported &= Bake::Capability(p.profile, trial.sources.data[j].kind, receiver,
												  engine.ForwardOptions())
									 .implemented;
			Bake::Result prediction;
			if (supported)
				prediction = engine.Forward(p.profile, trial.sources, Lease(localReceivers));
			for (size_t j = 0; j < localIndices.size(); ++j)
			{
				size_t i = localIndices[j];
				auto &row = value.rows[i];
				if (!supported || !prediction.predictions.data[j].available)
				{
					row.mask |= UnknownLocal;
					row.reason = supported ? prediction.predictions.data[j].reason
										   : "Unsupported local source/visibility";
					row.predictedCodes = row.underCodes = row.overCodes = Vec3();
					continue;
				}
				// Ambient belongs to the global baseline exactly once. Take only
				// the local evaluator's direct term; never subtract encoded images.
				row.trialPreencoding = prediction.predictions.data[j].direct;
				row.predictedCodes =
					engine.Encode(recipe, V(row.globalPreencoding) + V(row.trialPreencoding));
				Score(row);
				row.state = State::Conditional;
				value.rmseCodes += row.squaredErrorCodes;
				value.baselineRmseCodes += base.rows[i].squaredErrorCodes;
				++value.used;
			}
		}
		if (value.used)
		{
			value.state = State::Conditional;
			value.rmseCodes = std::sqrt(value.rmseCodes / value.used);
			value.baselineRmseCodes = std::sqrt(value.baselineRmseCodes / value.used);
			value.improvementCodes = value.baselineRmseCodes - value.rmseCodes;
		}
		out.trials.push_back(std::move(value));
	}
	if (out.state == State::Unavailable)
		out.reason = "No supported profile/observation/global comparison";
	return out;
}
} // namespace
Error Evaluate(const Request &r, const ReadContext &read, Result &out)
{
	out = Result();
	try
	{
		Validate(r, read);
		Result result;
		Engine engine(r, read, result);
		engine.Check();
		for (const auto &g : r.globals)
		{
			result.alternatives.push_back(EvaluateGlobal(engine, g));
			if (result.alternatives.back().state == State::Conditional)
				result.state = State::Conditional;
		}
		out = std::move(result);
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Local evidence allocation");
	}
}
const char *StateName(State s)
{
	switch (s)
	{
	case State::Unavailable:
		return "Unavailable";
	case State::Valid:
		return "Valid";
	case State::Conditional:
		return "Conditional";
	}
	return "Invalid";
}
} // namespace LocalLightEvidence
