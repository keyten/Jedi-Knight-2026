// Passive one-bounce diffuse transport and explicitly unresolved appearance.
// CPU/offline only. Never synthesizes a PhysicalEmitter or changes fixed roots.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "amplitude_fit.h"
#include "regions.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace Indirect
{
constexpr uint32_t kVersion = 1;
enum Issue : uint32_t
{
	UnknownOptics = 1u << 0,
	MovingPath = 1u << 1,
	ApproximateGeometry = 1u << 2,
	UnknownMaterial = 1u << 3,
	OmittedLinks = 1u << 4,
	QuadratureApproximation = 1u << 5,
	UnknownProfile = 1u << 6,
	RelativeScale = 1u << 7,
	MissingPrimary = 1u << 8,
	UnresolvedAppearance = 1u << 9,
	MeasurementCensored = 1u << 10,
	ModelDiscrepancy = 1u << 11
};
struct Patch
{
	Id id = 0, material = 0, region = 0;
	Bake::Receiver receiver; // actual footprint; surface-side/normal preserved
	double areaWorldSquared = 0;
};
struct BuildOptions
{
	double maximumLinkDistanceWorld = 256, receiverBiasWorld = .0001;
	unsigned subdivisions = 1; // triangle subdivision level; refined rule is level+1
	size_t maxPatches = 50000, maxPairs = 1600000;
	uint64_t maxBytes = 128ull * 1024 * 1024, maxWork = 128000000, maxRays = 4000000;
};
struct Link
{
	uint32_t a = 0, b = 0; // one reciprocal unordered pair
	double conductance = 0, possibleConductance = 0, quadratureDifference = 0;
	// F_ab=C/A_a and F_ba=C/A_b. Unknown visibility gives [0,C_possible], never .5.
	uint32_t issues = 0;
};
struct BuildReport
{
	// rays counts actual directional BVH queries. Reciprocal pairs enter blockedRays
	// only on agreement; disagreement/moving/approximate paths enter unknownRays.
	uint64_t candidatePairs = 0, rays = 0, work = 0, bytes = 0, blockedRays = 0, unknownRays = 0;
	double maximumOutgoingFraction = 0, maximumRuleDifference = 0;
	bool completePatchSet = false, allPairsRepresented = false;
};
struct BuildInput
{
	Disk::Digest mapKey{}, patchInterpretationKey{};
	std::vector<Patch> patches;
	std::shared_ptr<const StaticVisibility> geometry;
	std::shared_ptr<const LightingRegionGraph>
		regions; // optional acceleration/diagnostic, never visibility proof
	std::vector<DynamicBoundary> moving;
	bool completePatchSet = false;
};
class Transfer
{
  public:
	static Error Build(BuildInput, const BuildOptions &, const ReadContext &,
					   std::shared_ptr<const Transfer> &);
	const std::vector<Patch> &Patches() const;
	const std::vector<Link> &Links() const;
	const BuildReport &Report() const;
	Disk::Digest Key() const;
	Disk::Digest MapKey() const;
	Disk::Digest GeometryKey() const;
	const std::shared_ptr<const StaticVisibility> &Geometry() const;
	Error QueryPath(const RayQuery &, const ReadContext &, RayResult &) const;

  private:
	struct Data;
	std::shared_ptr<const Data> data;
};
struct Root
{
	enum class Domain
	{
		SourceLinearRelative,
		CompilerRelative255,
		PhysicalLux,
		SceneEquivalentLux // explicit calibration mapping; unknown absolute photometry
	};
	Domain domain = Domain::SourceLinearRelative;
	Id calibration = 0;
	Id source = 0;
	TransportRootKind kind = TransportRootKind::Unknown;
	SunCoverage sunCoverage = SunCoverage::Unknown; // sky roots must explicitly exclude direct sun
	std::vector<Vec3> incident; // fixed pre-encoding irradiance-like values, patch ID ordering in transfer
	std::vector<uint8_t> available;
	Disk::Digest generation{};
	std::string provenance;
};
// Production historical direct adapter. Keeps surface, sky, sun, ambient operators distinct.
// Physical/Shape callers may also provide immutable Root values with explicit contract.
Error PredictRoot(const Transfer &, TransportRootKind, const Bake::ProfileHypothesis &,
				  const Chunk<Bake::SourceHypothesis> &, const Bake::EvaluationContext &,
				  const Bake::Options &, const ReadContext &, Root &, Bake::Work &);
enum class MaterialKnowledge
{
	Known,
	Bounded,
	Unknown
};
struct Reflectance
{
	Id material = 0;
	MaterialKnowledge knowledge = MaterialKnowledge::Unknown;
	Vec3 lower, upper = Vec3(1, 1, 1), initial;
	double priorWeight = 0; // material-group prior, explicit and independently reported
	std::string provenance;
};
enum class ObservationOperator
{
	IncidentLinear,
	OutgoingLambertian,
	NetRadiantEncoded,
	Unknown
};
struct Contract
{
	Root::Domain incidentDomain = Root::Domain::SourceLinearRelative;
	ObservationOperator operation = ObservationOperator::Unknown;
	Bake::ProfileHypothesis profile;
	Units units;
	PhotometricUnit unit = PhotometricUnit::Relative;
	Calibration calibration;
	bool transferAttested = false, primariesAttested = false;
	bool physicalTransportAssumed = false; // NEVER implies empirical q3map bounce replay
	std::string provenance;
};
enum class Role
{
	Training,
	Validation,
	Heldout
};
struct Observation
{
	Id id = 0, patch = 0, spatialBlock = 0;
	Role role = Role::Training;
	Amplitude::Interval channels[3];
	double areaWorldSquared = 0, sigma = 1;
	bool eligible = true;
};
struct UnresolvedOptions
{
	bool enabled = false;
	Vec3 originWorld; // explicitly fixed coarse-cell origin, no target-dependent placement
	double cellWorld = 128, maximumValue = 0, magnitudePenalty = 1, smoothnessPenalty = 0;
	double normalOffsetWorld = .0001; // guarded endpoints for surface-tangent interpolation paths
	size_t maxCells = 4096;
	// Coarse surface-side/style cells; only certified straight paths connect training sites.
};
struct Options
{
	size_t maxIterations = 32, maxEvaluations = 10000, maxObservations = 1000000;
	uint64_t maxBytes = 128ull * 1024 * 1024, maxWork = 200000000;
	double huber = 4, objectiveTolerance = 1e-10, parameterTolerance = 1e-5, plausibilitySlack = 1e-5;
	UnresolvedOptions unresolved;
};
struct Request
{
	std::shared_ptr<const Transfer> transfer;
	std::vector<Root> roots;
	std::vector<Reflectance> materials;
	Contract contract;
	std::shared_ptr<const ChunkSource<Observation>> training;
	uint64_t count = 0;
	Disk::Digest trainingKey{};
	Options options;
};
struct PatchValue
{
	Id patch = 0;
	Vec3 direct, sun, sky, ambient, indirect, indirectLower, indirectUpper, unresolved;
	Vec3 totalPreencoding, totalPrediction, directOnlyPrediction;
	bool comparable = false, conditional = true, upperKnown = false;
	uint32_t issues = 0;
};
struct RootBounce
{
	Id source = 0;
	TransportRootKind kind = TransportRootKind::Unknown;
	std::vector<Vec3> incident;
	std::vector<Vec3> lower, upper;
};
struct Work
{
	uint64_t evaluations = 0, iterations = 0, linkOperations = 0, rowReads = 0, rays = 0, accountedBytes = 0;
};
struct Terms
{
	double data = 0, materialPrior = 0, unresolvedMagnitude = 0, smoothness = 0;
	double Total() const { return data + materialPrior + unresolvedMagnitude + smoothness; }
};
struct Alternative
{
	std::string name;
	double trainingObjective = 0;
	std::vector<Vec3> reflectance;
};
struct Result
{
	bool fitted = false, converged = false, absoluteScaleKnown = false, physicalBounceVerified = false;
	bool materialAllocationIdentifiable = false;
	std::string stop;
	Disk::Digest key{}, fixedRootsKey{}, trainingKey{}, transferKey{};
	Contract contract;
	std::vector<Reflectance> materials;
	std::vector<PatchValue> values;
	std::vector<RootBounce> byRoot;
	std::vector<Alternative> alternatives;
	std::vector<std::string> limitations;
	Work work;
	Terms objective;
	double directOnlyObjective = 0, maximumReflectedPowerFraction = 0;
	std::vector<Root> fixedRoots; // same frozen inputs, never fit source amplitudes here
};
Error Fit(const Request &, const ReadContext &, Result &);
struct Score
{
	bool available = false;
	uint64_t requested = 0, used = 0, unavailable = 0, censored = 0;
	double intervalMse = 0, directOnlyMse = 0, blackMse = 0, measure = 0, unresolvedPositive = 0,
		   overshoot = 0;
};
class DiagnosticSink
{
  public:
	virtual ~DiagnosticSink() = default;
	virtual Error Write(const Observation &, const PatchValue &, Vec3 signedIntervalResidual) = 0;
};
Error EvaluateFrozen(const Result &, std::shared_ptr<const ChunkSource<Observation>>, uint64_t, Role,
					 const ReadContext &, Score &, DiagnosticSink * = nullptr);
// Canonical descriptors + chunk payloads, never PhysicalEmitter records. Caller
// supplies existing roots/provenance/calibration in asset. Atomic on failure.
Error AppendFields(const Transfer &, const Result &, Id firstId, Id provenance, ReconstructedLightingAsset &,
				   std::vector<Disk::Chunk> &, const ReadContext &);
Error DebugTsv(const Result &, size_t maxBytes, std::string &);
} // namespace Indirect
} // namespace Core
} // namespace Rend2LightRecon
