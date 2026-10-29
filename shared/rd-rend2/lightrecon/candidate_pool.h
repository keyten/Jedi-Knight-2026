// Candidate evidence consolidation and bounded OFFLINE work scheduling.
// Not a source-existence estimator or a renderer light list.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "hidden_candidates.h"
#include "sun_fit.h"

namespace Rend2LightRecon
{
namespace Core
{
namespace Candidates
{
constexpr uint32_t kVersion = 1;
enum class Class : uint32_t
{
	Point,
	Spot,
	Area,
	Tube,
	Sun,
	Sky,
	Transport,
	Unknown,
	Count
};
enum class Nature
{
	PhysicalCandidate,
	AmbiguousSourceOrTransport,
	TransportEvidence,
	Environment
};
enum class Tier
{
	Authored,
	GeometryAndVisibility,
	Geometry,
	SurfaceProposal,
	Unlocalized,
	Unsupported
};
struct Support
{
	Id observation = 0, surface = 0, region = 0;
	VisibilityState visibility = VisibilityState::Unknown;
};
struct Annotation
{
	Id candidate = 0;
	// Nonzero ONLY for an explicit same-source identity from the upstream
	// provider (authored record, connected single fixture). Never material/colour.
	Disk::Digest sourceIdentity{}, fragmentSet{}, scenario{};
	std::string profile; // conditional profile, not a recovered compiler identity
	Nature nature = Nature::PhysicalCandidate;
	Tier tier = Tier::Geometry;
	std::vector<Support> support;
	std::vector<Id> regions; // source/body memberships, NOT proof of optical LOS
	uint64_t geometryWork = 1;
	Id canonicalSource = 0; // original immutable asset source, when wrapped from Solar
	bool protectedAuthored = false, supported = true;
};
struct Provider
{
	std::string name;
	Disk::Digest generation{}, map{}, geometryKey{}, regionKey{};
	Chunk<EmitterEvidence> evidence;
	Chunk<EmitterCandidate> candidates;
	Chunk<Annotation> annotations; // same count/order as candidates
	std::shared_ptr<const ReconstructedLightingAsset> canonical;
	std::shared_ptr<const void> extraOwner; // e.g. Solar's environment payload
};
// Adapters retain immutable leases, not copies of whole evidence/observation data.
Error FromEvidence(const std::string &, Disk::Digest geometryKey, Disk::Digest regionKey,
				   std::shared_ptr<const EmitterEvidenceSet>, Provider &);
Error FromHidden(const std::string &, Disk::Digest map, Disk::Digest geometryKey, Disk::Digest regionKey,
				 std::shared_ptr<const Hidden::Result>, Provider &);
// Each joint Solar explanation has its own scenario. Never sum these alternatives.
Error FromSolar(const std::string &, Disk::Digest map, Disk::Digest geometryKey,
				std::shared_ptr<const Solar::Result>, std::vector<Provider> &);

struct Ref
{
	size_t provider = 0, index = 0; // valid while Catalog holds all provider leases
	Id original = 0, scoped = 0;
};
struct Model
{
	Id id = 0, bundle = 0;
	Class sourceClass = Class::Unknown;
	Nature nature = Nature::PhysicalCandidate;
	Tier tier = Tier::Unsupported;
	std::string profile;
	Disk::Digest scenario{};
	Ref primary;
	std::vector<Ref> evidence, parts; // parts = exact union when fragmentSet is nonzero
	std::vector<Support> support;
	std::vector<Id> regions;
	bool protectedAuthored = false, geometryKnown = false, fragmentUnion = false;
	bool contradictoryPrior = false;
};
struct Home
{
	enum class Kind
	{
		Region,
		SpatialCell,
		GlobalEnvironment,
		Unlocalized
	};
	Kind kind = Kind::Unlocalized;
	Id region = 0;
	int64_t cell[3] = {}; // analysis bins, never fabricated room IDs
};
struct Bundle
{
	Id id = 0, provenance = 0;
	std::vector<Ref> inputs, evidence;
	std::vector<Id> models, regions;
	Home home;
	bool sourceIdentityAttested = false, multiplicityUnknown = true;
};
enum class LinkKind
{
	PossibleSameSource,
	ShapeAlternative,
	ConflictingColour,
	Blocked,
	VisibilityUnknown
};
struct Link
{
	Id a = 0, b = 0;
	LinkKind kind = LinkKind::PossibleSameSource;
	double distanceWorld = 0;
	bool sharedSupport = false, sameRegion = false, directionCompatible = false;
	bool relationsComplete = true;
	VisibilityState visibility = VisibilityState::Unknown;
};
enum class InputAction
{
	Standalone,
	MergedEvidence,
	AlternativeModel,
	FragmentPart
};
struct Mapping
{
	Ref input;
	Id bundle = 0;
	InputAction action = InputAction::Standalone;
};
struct Limits
{
	size_t maxProviders = 32, maxCandidates = 100000, maxEvidence = 200000, maxSupport = 2000000;
	uint64_t maxBytes = 128ull * 1024 * 1024, maxTextBytes = 32ull * 1024 * 1024;
	uint64_t maxPairTests = 200000, maxRayWork = 16000000;
	uint64_t maxRelationWork = 16000000; // support/region intersections in local link proposals
	size_t maxNeighbours = 16, maxLinks = 100000;
	double localCellWorld = 512, possibleMatchWorld = 16, colourCosine = .98, directionCosine = .95;
};
struct Request
{
	Disk::Digest map{}, geometryKey{}, regionKey{};
	std::vector<Provider> providers;
	std::shared_ptr<const VisibilityProvider> visibility;
	std::shared_ptr<const LightingRegionGraph> regions;
	Limits limits;
};
struct Report
{
	uint64_t inputs = 0, evidence = 0, bundles = 0, models = 0, merged = 0, alternatives = 0,
			 fragmentParts = 0;
	uint64_t pairTests = 0, rays = 0, rayWork = 0, relationWork = 0, accountedBytes = 0, borrowedBytes = 0;
	bool linksComplete = true;
	std::vector<std::string> diagnostics;
};
struct Catalog
{
	Disk::Digest key{}, map{}, geometryKey{}, regionKey{};
	std::vector<Provider> providers;
	std::vector<Bundle> bundles;
	std::vector<Model> models;
	std::vector<Link> links; // proposals/negative witnesses; NEVER automatic weak unions
	std::vector<Mapping> mapping;
	std::vector<Provenance> provenance; // map -> original evidence -> consolidated bundle
	Report report;
};
Error Consolidate(const Request &, const ReadContext &, Catalog &);
const EmitterEvidence &Evidence(const Catalog &, const Ref &);
const Annotation &InputAnnotation(const Catalog &, const Ref &candidateRef);

enum class Deferred
{
	Context,
	UnsupportedGeometry,
	FeatureDisabled,
	LocalQuota,
	WaveBudget,
	ProtectedOverflow,
	Oversize,
	Alternative,
	PartialGeometry
};
struct Demand
{
	Home home;
	uint32_t classes = 0xffffffffu;
	size_t extraPerClass = 1;
	std::string reason; // geometry/region request only, no withheld RGB
};
struct Budget
{
	std::string profile;
	Disk::Digest scenario{}; // explicit competing-model context
	uint32_t classes = (1u << uint32_t(Class::Count)) - 1;
	size_t perClass[size_t(Class::Count)] = {4, 4, 4, 2, 2, 2, 2, 2};
	size_t perHome = 24, maxTickets = 256, maxPartsPerTicket = 64;
	size_t coverageBonusPerHome = 2; // bounded extra jobs with new certified-visible receiver support
	uint64_t maxGeometryWork = 4096, maxBytes = 32ull * 1024 * 1024;
	std::vector<Demand> more;
};
struct Cursor
{
	Disk::Digest catalog{}, policy{};
	size_t nextHome = 0;
	std::vector<uint64_t> nextPart; // model-indexed; caller owned, no global cache
};
struct Ticket
{
	Id model = 0, bundle = 0;
	size_t firstPart = 0, partCount = 0;
	uint64_t geometryWork = 0;
	bool completesModel = false, protectedAuthored = false;
};
struct Deferral
{
	Id model = 0, bundle = 0;
	Deferred reason = Deferred::WaveBudget;
};
struct Selection
{
	std::vector<Ticket> tickets; // optimisation jobs, NEVER a set of coexisting physical lights
	std::vector<Deferral> deferred;
	uint64_t geometryWork = 0, completedModels = 0;
	bool exhausted = false;
	std::vector<std::string> diagnostics;
};
struct Statistics
{
	Home home;
	Class sourceClass = Class::Unknown;
	std::string provider, reason;
	uint64_t models = 0, selected = 0, protectedModels = 0, parts = 0;
};
// Provider rows can refer to the same consolidated model: do not sum them as
// independent sources. "selected" is work admission, not physical detection.
std::vector<Statistics> Summarize(const Catalog &, const Selection &);
// Deterministic cooperative waves. At most one alternative/model per bundle in
// a wave. Fragment unions may span tickets/waves; do not evaluate a partial
// chunk as a complete source. Cursor changes only on successful publication.
// Catalog records and original photometry are never mutated by budgeting.
Error Select(const Catalog &, const Budget &, const ReadContext &, Cursor &, Selection &);
const char *ClassName(Class);
const char *DeferredName(Deferred);
} // namespace Candidates
} // namespace Core
} // namespace Rend2LightRecon
