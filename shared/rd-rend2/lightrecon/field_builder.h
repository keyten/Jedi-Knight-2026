// P01 offline field production. No runtime resource/renderer dependencies.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "emitter_shape.h"
#include "indirect.h"
#include "probes.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace Fields
{
constexpr uint32_t kVersion = 2; // V01: payload binds frozen source geometry/photometry/configuration
enum class Route
{
	SourceConditioned,
	ObservationAppearance
};
struct Environment
{
	Id source = 0, dataset = 0;
	Angular::Field incoming; // decoded directional distribution, NEVER inferred from mean RGB
	Disk::Digest content{};
};
struct EmitterSupport
{
	Id source = 0;
	SourceSupport::Contract geometry;
};
struct AppearanceSample
{
	Id id = 0;
	Vec3 positionWorld, value;
	double weight = 1;
	AppearanceSample() = default;
	AppearanceSample(Id i, Vec3 p, Vec3 v, double w) : id(i), positionWorld(p), value(v), weight(w)
	{
	}
};
struct Passive
{
	std::shared_ptr<const Indirect::Transfer> transfer;
	std::shared_ptr<const Indirect::Result> solution;
	Disk::Digest sourceScene{}; // frozen source/configuration identity when E0 was prepared
};
struct Selection
{
	Route route = Route::SourceConditioned;
	Id field = 0, dataset = 0, geometryDataset = 0, topologyDataset = 0, provenance = 0,
	   calibration = 0;
	Id configuration = 0, alternativeGroup = 0;
	ConditionalQuantity quantity = ConditionalQuantity::NormalIrradiance;
	FieldDomain domain = FieldDomain::PhysicallyAuthored;
	ContributionColour colour = ContributionColour::LinearRec709D65;
	bool primariesKnown = true, linearTransferKnown = true;
	bool directLocal = false, sun = false, sky = false, firstBounce = false;
	bool allowConditionalSources = false, allowDirectProjection = false;
	std::vector<Id> sources; // explicit; empty does NOT mean all
	std::vector<Id> bounceRoots;
	std::string profile = "physical-emission/1", appearanceDomain, provenanceText;
};
struct Options
{
	unsigned degree = 2, angularOrder = 12, referenceOrder = 20;
	Shape::Sampling sourceSampling;
	uint64_t maxBytes = 128ull << 20, maxRays = 4000000, maxWork = 128000000, maxProbes = 20000;
	double skyRayLengthWorld = 65536, maximumRelativeAngularRMSE = .08, absoluteErrorFloor = 1e-6,
		   maximumWallSeconds = 300, appearanceRadiusWorld = 128;
	bool allowConditionalStatic = false;
};
struct Request
{
	std::shared_ptr<const ReconstructedLightingAsset> asset;
	std::shared_ptr<const Probes::Topology> topology;
	std::shared_ptr<const StaticVisibility> visibility;
	Selection selection;
	std::vector<Environment> environments;
	std::vector<EmitterSupport> emitterSupport; // attested emitting endpoint/triangle identity
	std::vector<AppearanceSample> appearance;
	std::vector<AppearanceSample>
		appearanceValidation; // independent, never used to place/fill probes
	Passive passive;
	Options options;
};
enum class ProbeState
{
	Available,
	GeometryUnknown,
	MissingInput,
	UnknownVisibility,
	AngularError,
	UnsupportedQuantity
};
struct ProbeDiagnostic
{
	Id probe = 0;
	ProbeState state = ProbeState::MissingInput;
	double relativeRMSE = 0, maximumError = 0, energyBias = 0, luminanceRMSE = 0;
	uint32_t issues = 0;
	bool referenceKnown = false;
};
struct Work
{
	uint64_t probes = 0, available = 0, rays = 0, traversal = 0, angularEvaluations = 0, bytes = 0,
			 coefficients = 0;
	double seconds = 0;
};
struct Artifact
{
	Disk::Digest sourceScene{};
	ReconstructedLightingAsset asset;
	std::shared_ptr<const Probes::Topology> topology;
	Probes::Values values;
	std::shared_ptr<const ConditionalFieldDescriptor> descriptor;
	std::vector<Disk::Chunk> chunks;
	std::vector<ProbeDiagnostic> diagnostics;
	Work work;
	bool partial = true;
};
Error Build(const Request &, const ReadContext &, Artifact &);
Disk::Digest SourceIdentity(const ReconstructedLightingAsset &);
// Actual passive first-bounce spatial lift uses rho*fixed-root E0/pi on original
// disjoint patch triangles; scalar E1 is never inverted into angular radiance.
class Prepared final : public CpuEvaluator
{
  public:
	struct Data;

  private:
	std::shared_ptr<const Data> data_;
	explicit Prepared(std::shared_ptr<const Data>);
	friend Error Prepare(const Artifact &, const ReadContext &, std::shared_ptr<const Prepared> &);

  public:
	Status AcceptsAsset(const ReconstructedLightingAsset &) const override;
	Status Supports(Capability) const override;
	Status Indirect(const ReconstructedLightingAsset &, const IrradianceQuery &,
					LightingSample &) const override;
	Status IncomingRadiance(const ReconstructedLightingAsset &, const RadianceQuery &,
							LightingSample &) const override;
	Status ConditionalField(const ReconstructedLightingAsset &, const ConditionalFieldQuery &,
							ConditionalFieldSample &) const override;
};
Error Prepare(const Artifact &, const ReadContext &, std::shared_ptr<const Prepared> &);
// Canonical datasets/chunks. Reload explicitly BEFORE Snapshot publication.
Error Encode(Artifact &, const ReadContext &);
Error Decode(const ReconstructedLightingAsset &, const std::vector<Disk::Chunk> &, Id field,
			 const Disk::Digest &expectedGeometry, const ReadContext &, Artifact &);
} // namespace Fields
} // namespace Core
} // namespace Rend2LightRecon
