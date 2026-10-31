// S12: conditional joint feasible witnesses and independently calibrated event rates.
// CPU/offline only. No renderer admission or automatic source publication.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "joint.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace Uncertainty
{
constexpr uint32_t kVersion = 1;
enum class Event
{
	Existence,
	CorrectType,
	ExactUse,
	FieldUseful
};
struct CalibrationTrial
{
	std::string family, scene, trial;
	Event event = Event::Existence;
	double ranking = 0; // bounded ranking ONLY until calibrated
	bool labelKnown = false, label = false;
};
struct CalibrationPolicy
{
	std::string population, protocol;
	std::vector<double> binEdges = {0, .05, .2, .5, .8, 1}; // frozen before final evaluation
	uint32_t minimumFamilies = 16;
	double alpha = .05;
	bool independentFamiliesAttested = false;
};
struct CalibrationBin
{
	Event event = Event::Existence;
	uint32_t bin = 0, families = 0, rows = 0, unlabelled = 0;
	double rate = 0, lower = 0, upper = 1;
};
struct CalibrationModel
{
	CalibrationPolicy policy;
	Disk::Digest key{};
	std::vector<CalibrationBin> bins;
	std::vector<std::string> families; // evaluation disjointness, not observed colours
};
Error Calibrate(const CalibrationPolicy &, const std::vector<CalibrationTrial> &, const ReadContext &,
				CalibrationModel &);
Error CalibratedRate(const CalibrationModel &, Event, double ranking, const std::string &population,
					 const std::string &evaluationFamily, CalibratedConfidence &);
enum class Method
{
	InputAlternative,
	Removal,
	PositionProfile,
	ColourProfile,
	GeometryProfile,
	DeleteBlock,
	SubsampleBlocks,
	Restart,
	MaterialProfile
};
struct Attempt
{
	Method method = Method::InputAlternative;
	Id source = 0;
	Disk::Digest model{};
	bool available = false, retained = false;
	Code code = Code::Ok;
	double training = 0, validation = 0;
	std::string reason;
};
struct Witness
{
	Joint::Model model;
	Method method = Method::InputAlternative;
	// Complete configuration is retained, never per-axis combinations of extrema.
	std::vector<Id> tracks; // source order in model
};
struct Track
{
	Id id = 0;
	std::vector<Id> candidateIds;
	std::vector<size_t> configurations;
	std::vector<UncertaintyFactor> factors;
	CalibratedConfidence existence, type, exactUse, fieldUse;
	CorrelatedPositionScatter scatter;
	Estimate relativePeak, directionSpreadRadians;
	bool peakColourBounded = false;
	Vec3 peakColourLower, peakColourUpper;
	uint32_t minimumSceneSources = 0, maximumSceneSources = 0, types = 0;
	uint32_t inputWitnesses = 0, resampledWitnesses = 0;
	double removalRanking = 0, typeRanking = 0, fieldRanking = 0;
	bool removalAvailable = false, associationAmbiguous = false, absoluteScaleKnown = false;
	bool positionDomainLimited = true;
	Suitability exactLight = Suitability::Unknown, approximateField = Suitability::Unknown;
};
struct Options
{
	size_t maxAttempts = 96, maxWitnesses = 64, maxRefits = 64, maxObservations = 100000;
	size_t positionLevels = 2, colourProfiles = 3, deleteBlocks = 3, subsampleReplicates = 3, restarts = 1;
	uint64_t maxEvaluations = 100000, maxRays = 4000000, maxWork = 400000000, maxBytes = 128ull * 1024 * 1024;
	uint64_t seed = 20261031;
	double trainingRegret = .01, validationRegret = .01;
	double positionStepWorld = .25;
	double minimumExactRateLower = .95, minimumFieldRateLower = .5;
	bool refitGeometry = false, materialProfiles = true;
	bool finiteUniverseAttested = false;
	std::string population, evaluationFamily;
};
struct Work
{
	uint64_t attempts = 0, refits = 0, assessments = 0, evaluations = 0, rays = 0, traversal = 0,
			 arithmetic = 0;
	uint64_t sampledBlocks = 0, accountedBytes = 0;
};
enum class State
{
	Unknown,
	ConditionalSet,
	Ambiguous,
	Partial,
	Cancelled,
	Budget,
	Unsupported
};
struct Result
{
	State state = State::Unknown;
	bool complete = false, globalSearchComplete = false;
	Disk::Digest key{}, trainingKey{}, geometryKey{};
	std::vector<Witness> witnesses;
	std::vector<Track> tracks;
	std::vector<Attempt> attempts;
	Work work;
	std::vector<std::string> limitations;
};
// Reads TRAINING only; optional oracle returns aggregate validation diagnostics.
// The calibration table is trained elsewhere on independent scene families.
Error Analyze(const Joint::Request &, const Joint::Result &, const Options &, const ReadContext &, Result &,
			  const Joint::Validation * = nullptr, const CalibrationModel * = nullptr);
struct SourceBinding
{
	Id track = 0, source = 0;
};
// Metadata-only atomic attachment to existing candidate sources. Parameters and
// authored sources are never overwritten. No Resolved state is assigned.
Error Attach(const Result &, const Joint::Request &, const std::vector<SourceBinding> &, Id provenance,
			 Id firstRecord, ReconstructedLightingAsset &);
const char *StateName(State);
const char *EventName(Event);
} // namespace Uncertainty
} // namespace Core
} // namespace Rend2LightRecon
