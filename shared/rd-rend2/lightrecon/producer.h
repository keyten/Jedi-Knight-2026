// S13 offline producer/publication boundary. No renderer adapter or GL dependency.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cache.h"
#include "candidate_pool.h"
#include "control.h"
#include "uncertainty.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace Producer
{
constexpr uint32_t kVersion = 1;
class SolarValidation
{
  public:
	virtual ~SolarValidation() = default;
	virtual Error Assess(const Solar::FitResult &, const ReadContext &, Solar::Result &) const = 0;
};
struct FinalReceipt
{
	Joint::Score score;
	std::string evidence; // aggregate trusted evaluation receipt, never answer XYZ/RGB
};
class FinalEvaluation
{
  public:
	virtual ~FinalEvaluation() = default;
	virtual Disk::Digest Key() const = 0;
	// Called only after all fitting/uncertainty probes. No later parameter updates.
	virtual Error Assess(const Joint::Model &, const ReadContext &, FinalReceipt &) const = 0;
};
struct Options
{
	uint64_t maxBytes = 128ull * 1024 * 1024, maxPayloadBytes = 64ull * 1024 * 1024;
	size_t maxPatches = 8192, maxSources = 128, maxChunks = 32768, chunkRows = 128;
	bool requireIndependentValidation = true, requireFinalEvaluation = true;
	bool retainUnsupportedDiagnostic = true; // completed diagnostic, NEVER a successful reconstruction
	Uncertainty::Options uncertainty;
	Candidates::Limits candidates;
	Candidates::Budget selection;
	Options()
	{
		uncertainty.maxAttempts = 256;
		uncertainty.maxRefits = 256;
		uncertainty.maxWitnesses = 128;
	}
};
struct Request
{
	InputManifest metadata;
	LightmapMapIdentity map;
	BuildCoverage coverage; // raw/map-wide denominators, never inferred from a small ROI
	std::string decoder, settingsIdentity;
	Disk::Digest validationKey{}, heldoutKey{}; // private partition identities, never withheld colours
	Disk::Fingerprint fingerprint;				// completeness is caller-attested, never guessed here
	Joint::Request fitting;
	std::vector<Candidates::Provider> providers;
	std::shared_ptr<const Hidden::Request> hidden;
	std::shared_ptr<const Solar::Request> solar;
	const SolarValidation *solarValidation = nullptr; // borrowed synchronous trusted controller
	const Uncertainty::CalibrationModel *calibration = nullptr;
	Options options;
};
enum class State
{
	NotStarted,
	DiagnosticOnly,
	Completed,
	Cancelled,
	Budget,
	Failed
};
struct Work
{
	Joint::Work solver, strict, decomposition;
	Uncertainty::Work uncertainty;
	Hidden::Report hidden;
	Solar::Work solar;
	uint64_t candidates = 0, deferred = 0, payloadBytes = 0;
};
struct Artifact
{
	std::shared_ptr<const ReconstructedLightingAsset> asset;
	std::vector<Disk::Chunk> chunks;
	Disk::Fingerprint fingerprint;
	Disk::Digest key{};
	Completeness completeness = Completeness::Partial;
};
struct Result
{
	State state = State::NotStarted;
	std::string reason;
	Artifact artifact;
	Joint::Result fitted;
	Uncertainty::Result uncertainty;
	Joint::Score training, validation;
	FinalReceipt final;
	Work work;
	std::vector<std::string> stages;
};
// No publication on cancellation/numerical/work failure. Unknown-profile inputs
// can return a diagnostic-only empty-resolved asset with explicit readiness.
Error Run(const Request &, const ReadContext &, Result &, const Joint::Validation * = nullptr,
		  const FinalEvaluation * = nullptr);

struct SampleRecord
{
	Id id = 0, patch = 0, region = 0, surface = 0, material = 0, block = 0;
	Vec3 world, normal;
	double area = 0;
	uint32_t style = 0;
};
struct ContributionRecord
{
	Id sample = 0, source = 0;
	TransportRootKind root = TransportRootKind::Unknown;
	uint32_t reflectionOrder = 0, issues = 0;
	bool available = false;
	Vec3 value, lower, upper;
};
struct ObservationRecord
{
	Id sample = 0, patch = 0;
	Joint::Role role = Joint::Role::Training;
	Joint::Channel channel = Joint::Channel::Surface;
	bool available = false, discrepancyKnown = false;
	uint32_t issues = 0;
	Vec3 prediction, direct, sun, sky, ambient, indirect, unresolved, signedIntervalDiscrepancy,
		directionToSource;
	bool directionKnown = false;
};
// Validates payload checksums, spans, IDs, domains, component sums and unit/
// membership cross-references. Structural Validate(asset) alone is insufficient.
Error ValidateArtifact(const Artifact &, const ReadContext &);
Error ReadSamples(const Artifact &, Id dataset, const ReadContext &, std::vector<SampleRecord> &);
Error ReadContributions(const Artifact &, Id dataset, const ReadContext &, std::vector<ContributionRecord> &);
Error ReadObservations(const Artifact &, Id dataset, const ReadContext &, std::vector<ObservationRecord> &);
// Ticket/generation protects against publishing work from a replaced world.
Status Publish(const Artifact &, Manager &, uint64_t expectedGeneration, const ReadContext &);
Error Load(const Disk::Cache &, const Disk::Fingerprint &, const ReadContext &, Artifact &);
Disk::Error Store(const Artifact &, const Disk::Cache &, const ReadContext &);
std::string ReportJson(const Result &);
Error DebugObj(const Artifact &, size_t maximumBytes, std::string &);
Error DebugPreview(const Artifact &, size_t maximumBytes, std::string &);
// Uses the existing independent build-policy rule (Disabled/CacheOnly/etc.).
// It is an OFFLINE backend. Production game fingerprint/preflight is unchanged.
class Backend final : public BuildBackend
{
	Request request_;
	ReadContext read_;
	const Joint::Validation *validation_;
	const FinalEvaluation *final_;
	std::shared_ptr<Disk::Cache> cache_;

  public:
	Result last;
	Disk::Code cacheWrite = Disk::Code::NotCacheable;
	Backend(Request, const ReadContext &, const Joint::Validation *, const FinalEvaluation *,
			std::shared_ptr<Disk::Cache>);
	Status LoadCache(BuildProduct &) override;
	Status Build(BuildProduct &) override;
};
const char *StateName(State);
} // namespace Producer
} // namespace Core
} // namespace Rend2LightRecon
