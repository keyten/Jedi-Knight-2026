// Bounded offline directional-sun / diffuse-sky inference. No renderer state.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "bake_forward.h"
#include "evidence.h"
#include "lightgrid.h"

namespace Rend2LightRecon
{
namespace Core
{
namespace Solar
{
constexpr uint32_t kVersion = 1;
enum class State
{
	Unavailable,
	UnknownSun,
	UnresolvedEnvironment,
	SkyOnly,
	SunHypotheses,
	AmbiguousSun
};
enum class Model
{
	SkyOnly,
	UniformFillControl,
	SunAndSky
};
enum class Exclusion
{
	None,
	IneligibleReceiver,
	UnknownEncoding,
	UnsupportedGeometry,
	UnknownSkyVisibility
};
struct Sample
{
	Bake::Receiver receiver;
	uint8_t rgb[3] = {};
	double areaWorldSquared = 0, footprintRadiusWorld = 0;
	bool eligible = false, encodingVerifiedForProfile = false;
	// Fixed, independently provided local light/transport contribution, BEFORE
	// encoding in the selected compiler domain. Never inferred/subtracted RGB8.
	Vec3 otherPreencoding;
};
// Reuse original canonical decoder data. No renderer transform, albedo division
// or RGB re-encoding. Known profile applicability is an independent caller claim.
Error FromObservation(const Bake::Receiver &, const Decode::Observation &, double areaWorldSquared,
					  double footprintRadiusWorld, bool eligible, bool encodingVerifiedForProfile, Sample &);
enum class HintKind
{
	AuthoredSun,
	GridDominant,
	DeluxeDominant,
	ShadowDirection
};
struct DirectionHint
{
	Id id = 0;
	HintKind kind = HintKind::GridDominant;
	Vec3 toSource;
	std::string provenance;
	bool staticEligible = false;
	uint8_t style = 0;
	// Grid/deluxe mixtures remain seeds only, even if their quantizer is known.
	Estimate quantizationRadians;
};
std::vector<DirectionHint> AuthoredHints(const EmitterEvidenceSet &);
DirectionHint GridHint(const GridCell &, Id id);
DirectionHint DeluxeHint(const DeluxeDirection &, Id id);
struct Options
{
	uint32_t sphereDirections = 128, refineSeeds = 6, refineLevels = 4, maxAlternatives = 24;
	size_t maxSamples = 256, maxProfiles = 4;
	uint32_t minSamples = 12, minBlocks = 3, minValidationSamples = 8, minValidationBlocks = 2;
	double blockWorld = 128, partitionGapWorld = 1, amplitudeUpper = 2000;
	Vec3 partitionOriginWorld;
	double huberCodes = 4, acceptableRmseCodes = 1.5, maximumErrorCodes = 3;
	double sunImprovementCodes = 2, skyVsFillImprovementCodes = 1, alternativeDeltaCodes = .5;
	double minBlackRmseCodes = 4, positiveCodes = 8, minimumNormalEigenRatio = .015;
	double minimumCoverage = .75;
	uint64_t maxRays = 1000000, maxPairs = 2000000, maxTraversalWork = 64000000;
	uint64_t maxEncodingCalls = 1000000, maxBytes = 64ull * 1024 * 1024;
	bool allowAssumedProfiles = false, allowAssumedObservationEncoding = false;
};
struct Work
{
	uint64_t forwardCalls = 0, rays = 0, pairs = 0, traversal = 0, encodingCalls = 0, directions = 0;
	bool complete = true; // failed forward calls may omit their partial traversal counters
};
struct Coverage
{
	uint64_t requested = 0, used = 0, positive = 0, positiveBlocks = 0, blocks = 0, saturated = 0;
	uint64_t excludedIneligible = 0, excludedEncoding = 0, excludedGeometry = 0, excludedSky = 0;
	double normalEigenRatio = 0, blackRmse = 0;
};
struct ReceiverEvidence
{
	Id sample = 0;
	Exclusion exclusion = Exclusion::None;
	double skyResponse = 0, sunResponse = 0;
	bool sunFacing = false;
};
struct Score
{
	double intervalRmse = 0, huber = 0, maximumError = 0;
};
struct Proposal
{
	Model model = Model::SkyOnly;
	Bake::ProfileHypothesis profile;
	Vec3 toSource, sunRgb, skyRgb;
	double searchResolutionRadians = 0;
	Score training;
	Coverage coverage;
	bool authoredSeed = false, amplitudeDegenerate = false;
};
struct TrainingGeometry
{
	Id sample = 0;
	Vec3 world;
	double footprintRadius = 0;
};
struct FitResult
{
	State state = State::Unavailable;
	std::string reason;
	Disk::Digest mapKey{}, geometryKey{}, frozenKey{};
	std::string lineage;
	Units units;
	Options options;
	std::vector<Coverage> profileCoverage;
	std::vector<Proposal> proposals; // frozen before validation; no retained training RGB
	std::vector<TrainingGeometry> trainingGeometry;
	std::vector<DirectionHint> hints;
	std::vector<std::string> diagnostics;
	Work work;
};
struct Request
{
	Disk::Digest mapKey{};
	std::string lineage;
	Units units;
	Chunk<Sample> training;						   // caller may pass ONLY a blind training view
	std::vector<Bake::ProfileHypothesis> profiles; // UnknownProfile is NOT a numerical default
	std::vector<DirectionHint> hints;
	Bake::EvaluationContext geometry;
	Options options;
};
struct EnvironmentSample
{
	Vec3 toSource, relativeRgb;
	double responseWeight = 1.0 / 17; // compiler quadrature weight, NOT steradians
};
struct Hypothesis
{
	Proposal fitted;
	Score validation;
	Coverage coverage;
	bool skySupported = false; // removing sky must measurably worsen independent prediction
	std::vector<ReceiverEvidence> receivers;
	double validationSkyOnlyRmse = 0, validationFillRmse = 0;
	// Spread over retained tested alternatives + resolution floor. Not sigma/CI.
	double sampledAngularAmbiguityRadians = 0;
	Estimate sunIntensitySlice,
		skyIntensitySlice;						// conditional fixed-colour/fixed-other scale slices, NOT CI
	ReconstructedLightingAsset asset;			// one JOINT sun+sky explanation per asset
	std::vector<EnvironmentSample> environment; // owned payload for asset's dataset descriptor
	std::string acceptance;
};
struct Decision
{
	std::string profile, reason;
	Model model = Model::SkyOnly;
	Vec3 toSource;
	bool admitted = false;
	bool evaluated = false;
	Score validation;
};
struct Result
{
	State state = State::Unavailable;
	std::string reason;
	std::vector<Hypothesis> hypotheses; // alternatives, NEVER sum these assets
	std::vector<Decision> decisions;	// each tested frozen model's independent validation outcome
	std::vector<std::string> diagnostics;
	Work work;
};
struct Assessment
{
	Score score;
	Coverage coverage;
	std::vector<ReceiverEvidence> receivers;
	Work work;
};
// Value-only trusted scoring of an already frozen hypothesis; never refits or
// selects parameters. Useful for an untouched final held-out stage.
Error Assess(const Proposal &, const Options &, const Chunk<Sample> &, const Bake::EvaluationContext &,
			 const ReadContext &, Assessment &);
// Synchronous/bounded. Fit sees no validation/held-out RGB. On errors outputs
// diagnostics/state only, never an accepted partial asset. No I/O or global cache.
Error Fit(const Request &, const ReadContext &, FitResult &);
// Trusted evaluation boundary: no parameter refitting here. Requires disjoint
// world blocks/footprints and IDs; geometry/map identities must remain unchanged.
Error ValidateAndPublish(const FitResult &, Disk::Digest mapKey, const Chunk<Sample> &validation,
						 const Bake::EvaluationContext &, const ReadContext &, Result &);
const char *StateName(State);
} // namespace Solar
} // namespace Core
} // namespace Rend2LightRecon
