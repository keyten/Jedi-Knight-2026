// Camera-independent static shadow support, not GPU shadow resources.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "emitter_shape.h"
#include "probes.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace ShadowCache
{
constexpr uint32_t kVersion = 1;
enum class Storage
{
	Float64,
	UNorm16
};
enum Issue : uint32_t
{
	OpticalUncertain = 1,
	GeometryUncertain = 2,
	BakeStateConditional = 4,
	RuntimeMotionUnknown = 8,
	RuntimeMotionRelevant = 16,
	SourceUncertain = 32,
	FiniteSampling = 64,
	SamplingDisagreement = 128,
	Quantized = 256,
	UnsupportedSource = 512,
	OutsideEmission = 1024,
	MissingSky = 2048,
	ResolutionLimited = 4096
};
struct Receiver
{
	Id id = 0, region = 0, surface = 0;
	Vec3 positionWorld, geometricNormal;
	bool normalKnown = false;
	double footprintWorld = 0;
};
struct Item
{
	Receiver receiver;
	Id source = 0;
	ShadowQueryKind kind = ShadowQueryKind::Source;
	Vec3 targetOrDirection;
	uint32_t priority = 0; // caller-declared geometric/evidence work priority, not confidence
};
struct SourceSupport
{
	Id source = 0;
	Rend2LightRecon::Core::SourceSupport::Contract geometry;
};
struct Options
{
	Shape::Sampling sampling;
	RayPolicy optical;
	RayBias bias;
	double skyDistanceWorld = 65536, maximumSamplingDifference = .02, maximumWallSeconds = 300;
	uint64_t maxEntries = 100000, maxRays = 4000000, maxWork = 128000000, maxBytes = 128ull << 20;
	Storage storage = Storage::Float64;
};
struct Record
{
	Item item;
	ShadowSupportSample result;
	Disk::Digest key{}, sourceKey{}, supportKey{};
};
struct Work
{
	uint64_t requested = 0, built = 0, reused = 0, rays = 0, traversal = 0, bytes = 0;
	uint64_t staticKnown = 0, staticUnknown = 0, scenarioConditional = 0, unsupported = 0;
	double seconds = 0;
};
struct Descriptor
{
	Id dataset = 0, provenance = 0, scenario = 0;
	Disk::Digest map{}, geometry{}, policy{}, key{};
	std::string scope, occupancyProvenance;
	Storage storage = Storage::Float64;
	unsigned areaDivisions = 0, tubeSides = 0, tubeSegments = 0;
	RayPolicy optical;
	RayBias bias;
	double skyDistanceWorld = 0, maximumSamplingDifference = 0;
	Work coverage; // time is not serialized
};
struct Artifact
{
	ReconstructedLightingAsset asset;
	Descriptor descriptor;
	std::vector<Record> records;
	std::vector<Disk::Chunk> chunks;
	Work work;
};
struct Request
{
	std::shared_ptr<const ReconstructedLightingAsset> asset;
	PointRefine::SceneInput scene;
	Id dataset = 0, provenance = 0, scenario = 0;
	std::string scope;
	std::vector<Item> items;
	std::vector<SourceSupport> support;
	Options options;
	std::shared_ptr<const Artifact> previous; // only complete, exact dependency-key matches reused
};
// Identity excludes photometry: changing brightness cannot change geometric shadow support.
Disk::Digest SourceKey(const PhysicalEmitter &,
					   const Rend2LightRecon::Core::SourceSupport::Contract &);
Error Build(const Request &, const ReadContext &, Artifact &);
Error Encode(Artifact &, const ReadContext &);
Error Decode(const ReconstructedLightingAsset &, const std::vector<Disk::Chunk> &, Id dataset,
			 const Disk::Digest &expectedMap, const Disk::Digest &expectedGeometry,
			 const ReadContext &, Artifact &);
// Geometry-only helper. It neither reads probe coefficients nor certifies direct visibility.
Error ProbeItems(const Probes::Topology &, const std::vector<Id> &sources, size_t maxItems,
				 std::vector<Item> &);
struct Coverage
{
	Id id = 0;
	uint64_t records = 0, known = 0, unknown = 0, conditional = 0, unsupported = 0;
};
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
	Status SourceCoverage(Id, Coverage &) const;
	Status RegionCoverage(Id, Coverage &) const;
	Status ShadowSupport(const ReconstructedLightingAsset &, const ShadowSupportQuery &,
						 ShadowSupportSample &) const override;
	Status Visibility(const ReconstructedLightingAsset &, const VisibilityQuery &,
					  VisibilitySample &) const override;
	Status SkyVisibility(const ReconstructedLightingAsset &, const SkyVisibilityQuery &,
						 VisibilitySample &) const override;
};
Error Prepare(const Artifact &, const ReadContext &, std::shared_ptr<const Prepared> &);
} // namespace ShadowCache
} // namespace Core
} // namespace Rend2LightRecon
