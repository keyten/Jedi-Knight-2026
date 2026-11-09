// ASR22 bounded offline discovery. No publication, renderer or historical solver changes.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lightrecon/joint.h"
namespace Rend2LightRecon { namespace Discovery {
using namespace Core;
enum class Classification { Unexplained, IndirectPossible, SunAmbiguous,
	UnsupportedGeometry, CandidateLocalDirect };
struct Evidence {
	Id region = 0;
	// Independent fixture/shadow/directional evidence, NEVER inferred from residual brightness.
	bool localDirectCue = false, sunSeparated = false, indirectSeparated = false;
	std::string provenance;
};
struct Options {
	size_t iterations = 4, maxCandidates = 64, fitsPerIteration = 8, proposalsPerIteration = 2;
	size_t maxSources = 8, minimumRows = 3, minimumBlocks = 2;
	double cellWorld = 128, minimumBaselineWorld = 1;
	double positiveThreshold = .01, minimumTrainingGain = 1e-6,
		minimumValidationGain = 1e-6, complexityPerSource = .001;
	double minimumPowerFraction = 1e-6; // fraction of declared RGB box; gauge invariant
	double maximumValidationBlockRegression = 0;
};
struct SearchVolume {
	// Geometry-only local fixture/search volume, not an illumination centroid.
	Joint::Candidate prototype;
	Bounds positions;
	unsigned divisions = 3; // 2..4, at most 64 fixed poses per volume
};
struct Request {
	Joint::Request fit; // one frozen modern photometric hypothesis; training reader only
	Joint::Model initial; // currently plausible sources; all amplitudes jointly refit
	// Bounded geometric hypotheses from Hidden, fixture providers or a declared lattice.
	// Not source truth. XYZ/kind/shape stay fixed; spot alternatives have distinct IDs.
	std::vector<Joint::Candidate> candidates;
	std::vector<SearchVolume> volumes;
	std::vector<Evidence> evidence;
	bool comparisonSupported = false, profileSeparated = false, directOnlyAttested = false;
	std::string provenance;
	Options options;
};
struct Region {
	Id region = 0;
	int64_t cell[3] = {};
	Classification classification = Classification::Unexplained;
	std::vector<Id> observations;
	size_t positiveRows = 0, blocks = 0;
	double positiveDiscrepancy = 0; // in the declared COMPARISON domain, never lamp power
	double receiverBaselineWorld = 0;
	std::string reason, provenance;
};
struct Attempt {
	Id candidate = 0;
	bool fitted = false, validated = false, accepted = false;
	double trainingGain = 0, validationGain = 0;
	std::string reason, provenance;
};
struct Iteration {
	size_t index = 0;
	Options limits; // frozen before seeing targets
	Joint::Options evaluationLimits; // per call; first round <= 4 + fits + proposals, later <= 1 + fits + proposals
	Disk::Digest incumbent{}, selected{};
	std::vector<Region> regions;
	std::vector<Attempt> attempts;
	bool candidateLimited = false;
	size_t calls = 0;
	Joint::Work work;
	std::string stop;
	std::string provenance;
};
struct Report {
	std::vector<Iteration> iterations;
	std::string stop, provenance;
	Options policy;
	std::vector<Evidence> evidence;
	size_t candidateCount = 0;
	bool comparisonSupported = false, profileSeparated = false, directOnlyAttested = false;
	bool frozen = false;
};
class Experiment {
public:
	const Report &Receipt() const;
	const Joint::Model &Initial() const;
	const Joint::Model &Selected() const;
private:
	struct Data;
	std::shared_ptr<const Data> data;
	friend Error Discover(const Request &, const std::vector<Joint::Observation> &,
		const Disk::Digest &, const ReadContext &, Experiment &);
	friend Error AuditHeldout(const Experiment &, const std::vector<Joint::Observation> &,
		const Disk::Digest &, const ReadContext &, Joint::Score &, Joint::Score &);
};
// Validation may select between training-frozen proposals each round. The fixed
// iteration cap bounds this adaptive selection; a third fold is audit-only.
Error Discover(const Request &, const std::vector<Joint::Observation> &validation,
	const Disk::Digest &validationKey, const ReadContext &, Experiment &);
Error AuditHeldout(const Experiment &, const std::vector<Joint::Observation> &heldout,
	const Disk::Digest &partition, const ReadContext &, Joint::Score &initial, Joint::Score &selected);
const char *Name(Classification);
} }
