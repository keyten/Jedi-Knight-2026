// ASR08 offline diagnostic API. Never linked into the renderer or asset codec.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lightrecon/sun_fit.h"

namespace LocalLightEvidence
{
using Rend2LightRecon::Id;
using Rend2LightRecon::Vec3;
using Rend2LightRecon::Core::Chunk;
using Rend2LightRecon::Core::Error;
using Rend2LightRecon::Core::ReadContext;
namespace Bake = Rend2LightRecon::Core::Bake;
namespace Decode = Rend2LightRecon::Core::Decode;
namespace Solar = Rend2LightRecon::Core::Solar;
enum class State
{
	Unavailable,
	Valid,
	Conditional
};
enum Mask : uint32_t
{
	Missing = 1u << 0,
	Ineligible = 1u << 1,
	UnknownEncoding = 1u << 2,
	UnsupportedObservation = 1u << 3,
	UnsupportedReceiver = 1u << 4,
	UnknownGlobal = 1u << 5,
	UnknownLocal = 1u << 6,
	Saturated = 1u << 7,
	GlobalOverprediction = 1u << 8,
	AttributionUnresolved = 1u << 9,
	AssumedProfile = 1u << 10,
	CompetingGlobals = 1u << 11,
	ObservationUncertainty = 1u << 12
};
struct Interval
{
	State state = State::Unavailable;
	double lower = 0, upper = 0; // encoded code values; NOT irradiance
	bool upperKnown = false;	 // 255 is censored, never a finite energy upper bound
	uint32_t mask = 0;
};
struct Observation
{
	Bake::Receiver receiver;
	Decode::Raw original; // original RGB8 lightmap only; grid/deluxe are refused
	bool eligible = false;
	double uncertaintyCodes = 0; // declared symmetric model/data allowance, not sigma
};
struct GlobalAlternative
{
	std::string id, provenance;
	Solar::Proposal candidate;	   // explicit frozen sun/sky, or a controlled fixture
	bool encodingAttested = false; // independently applies to THIS profile/dataset
};
struct LocalTrial
{
	std::string id;
	Chunk<Bake::SourceHypothesis> sources; // finite point/spot/polygon only; no fitting here
};
struct Options
{
	size_t maxSamples = 512, maxGlobals = 8, maxTrials = 32, maxSources = 32;
	uint64_t maxRays = 1000000, maxPairs = 2000000, maxTraversal = 64000000;
	uint64_t maxEncodingCalls = 100000, maxBytes = 64ull * 1024 * 1024;
	bool allowAssumedProfiles = false;
};
struct Request
{
	Chunk<Observation> observations;
	std::vector<GlobalAlternative> globals; // competing alternatives, NEVER summed
	std::vector<LocalTrial> trials;			// zero-local control is added automatically
	Bake::EvaluationContext geometry;
	Options options;
};
struct Row
{
	Id sample = 0;
	State state = State::Unavailable;
	uint32_t mask = AttributionUnresolved;
	Interval observed[3];
	// These two fields are forward predictions in CompilerLinearRelative255.
	// They are NOT inverted observations, calibrated energy or residual RGB.
	Vec3 globalPreencoding, trialPreencoding, predictedCodes;
	Vec3 underCodes, overCodes;	  // nonnegative distances to observation intervals
	double squaredErrorCodes = 0; // mean over RGB, equal receiver weighting
	std::string reason;
};
struct TrialResult
{
	std::string id;
	State state = State::Unavailable;
	std::vector<Row> rows; // original order; unavailable rows never become black
	size_t used = 0;
	double rmseCodes = 0, baselineRmseCodes = 0, improvementCodes = 0;
	// Baseline and trial use exactly the SAME supported subset. Scores from
	// different masks/global profiles are not automatically comparable.
};
struct AlternativeResult
{
	std::string id, profile, provenance, reason;
	State state = State::Unavailable;
	std::vector<TrialResult> trials; // first entry is the zero-local control
};
struct Result
{
	State state = State::Unavailable;
	std::vector<AlternativeResult> alternatives;
	Bake::Work work;
	uint64_t encodingCalls = 0;
};
// Encoded-space residual evidence is a conditional cost function:
// distance(Encode_profile(global + nonnegative local), original byte intervals).
// No inverse transfer, RGB8 subtraction, negative-light repair or lamp certification.
// Valid labels observation intervals only; every numerical decomposition is Conditional.
// Synchronous, bounded, no I/O, publication, runtime state or parameter refitting.
Error Evaluate(const Request &, const ReadContext &, Result &);
const char *StateName(State);
} // namespace LocalLightEvidence
