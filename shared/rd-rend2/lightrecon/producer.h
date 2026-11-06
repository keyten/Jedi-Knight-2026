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
constexpr uint32_t kVersion = 8; // X16: preserved spot law and receiver-vs-fixture provenance
struct PreparedProviders; // opaque immutable provider leases, created only by PrepareModelRequests
struct ProviderPolicy
{
	bool hiddenRequired = false, solarRequired = false;
	bool allowOptionalLocalBudget = true;
	size_t maxScopedRequests = 32;
};
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
	uint64_t maxContributionPairs = 2000000;
	size_t maxPatches = 8192, maxSources = 128, maxChunks = 32768, chunkRows = 128;
	bool requireIndependentValidation = true, requireFinalEvaluation = true;
	bool retainUnsupportedDiagnostic =
		true; // completed diagnostic, NEVER a successful reconstruction
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
	std::vector<std::string> contextHypotheses; // competing scoped models, never summed lighting
	Disk::Digest validationKey{},
		heldoutKey{};			   // private partition identities, never withheld colours
	Disk::Fingerprint fingerprint; // completeness is caller-attested, never guessed here
	Joint::Request fitting;
	std::vector<Candidates::Provider> providers;
	std::shared_ptr<const Hidden::Request> hidden;
	std::shared_ptr<const Solar::Request> solar;
	const SolarValidation *solarValidation = nullptr; // borrowed synchronous trusted controller
	const Uncertainty::CalibrationModel *calibration = nullptr;
	ProviderPolicy providerPolicy;
	std::shared_ptr<const PreparedProviders> prepared;
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
	std::vector<ProviderAssessment> providers;
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
	std::vector<ProviderScenario> scenarios;
	// No fitting/held-out targets added: each request has its own profile/scenario.
	// Rebind trusted validation/final callbacks to scopedRequest.fitting before Run.
	std::vector<std::shared_ptr<const Request>> scopedRequests;
};
// Execute providers once, preserve their leases/outcomes, construct separate
// model requests for mutually exclusive solar/profile scenarios. Does not fit.
Error PrepareModelRequests(const Request &, const ReadContext &, Result &);
// Reuse the canonical S05 -> Joint adapter in offline scheduling. False means
// unsupported geometry/transport evidence; never substitute a point source.
bool MakeCandidate(const Candidates::Catalog &, const Candidates::Model &,
				   const Joint::Request &, Joint::Candidate &);
Disk::Fingerprint DependencyFingerprint(const Request &); // same full CPU settings identity as Run
// Retain scoped provider outcomes after a CPU refit, with original binding and
// scene/profile/observation checks. Does not rerun or invent a provider result.
Error RebindProviders(const Request &original, Request &refitted);
// No publication on cancellation/numerical/work failure. Unknown-profile inputs
// can return a diagnostic-only empty-resolved asset with explicit readiness.
Error Run(const Request &, const ReadContext &, Result &, const Joint::Validation * = nullptr,
		  const FinalEvaluation * = nullptr);

enum class SampleKind
{
	TransportPatch,
	SurfaceLuxel,
	GridAmbient,
	GridDirected,
	GridDirection,
	DeluxeDirection
};
enum class ObservationDomain
{
	Unknown,
	EncodedSourceRGB,
	LinearRec709Irradiance,
	LegacyLinearChannels,
	Direction,
	LinearRec709Radiance
};
struct SampleRecord
{
	Id id = 0, patch = 0, region = 0, surface = 0, material = 0, block = 0;
	Vec3 world, normal;
	double area = 0;
	uint32_t style = 0;
	SampleKind kind = SampleKind::TransportPatch;
	Vec3 shadingNormal;
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
	Id sampleDomain = 0;
	ObservationDomain domain = ObservationDomain::Unknown;
	uint8_t presentMask = 0, unboundedUpperMask = 0;
	Vec3 observedLower, observedUpper; // finite endpoints; upper mask represents +infinity
	PhotometricUnit unit =
		PhotometricUnit::Relative; // TOTAL observation operator, not component unit
	Id calibration = 0;
};
// Validates payload checksums, spans, IDs, domains, component sums and unit/
// membership cross-references. Structural Validate(asset) alone is insufficient.
// attributionOnly validates all S13 sample/contribution/diagnostic data and
// passive records, leaving unrelated P01/P02/P05 payloads to their own codecs.
Error ValidateArtifact(const Artifact &, const ReadContext &, bool attributionOnly = false);
Error ReadSamples(const Artifact &, Id dataset, const ReadContext &, std::vector<SampleRecord> &);
Error ReadContributions(const Artifact &, Id dataset, const ReadContext &,
						std::vector<ContributionRecord> &);
Error ReadObservations(const Artifact &, Id dataset, const ReadContext &,
					   std::vector<ObservationRecord> &);
// Bounded reusable S13 codecs for trusted sparse producers/test adapters. The
// caller assigns a stable dataset ID; success atomically replaces descriptor/chunks.
Error EncodeSamples(Id, const std::vector<SampleRecord> &, size_t chunkRows, const ReadContext &,
					Dataset &, std::vector<Disk::Chunk> &);
Error EncodeContributions(Id, const std::vector<ContributionRecord> &, size_t chunkRows,
						  const ReadContext &, Dataset &, std::vector<Disk::Chunk> &);
Error EncodeObservations(Id, const std::vector<ObservationRecord> &, size_t chunkRows,
						 const ReadContext &, Dataset &, std::vector<Disk::Chunk> &);
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
