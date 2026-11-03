// Read-only CPU access to canonical reconstructed lighting (C++11).
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "tr_lightrecon_types.h"
#include <cstddef>
#include <memory>

namespace Rend2LightRecon {
namespace detail { struct SnapshotData; }
class Manager;
class WeakSnapshot;

enum class Availability { Disabled, Missing, Pending, Ready, Partial, Corrupt, Unloaded };
enum class Status { Ok, Disabled, Missing, Pending, Corrupt, Unloaded, Unavailable,
	Unsupported, InvalidArgument, NotFound, Ambiguous, StaleHandle, Expired };
template<typename T> struct Result
{
	Status status = Status::Unavailable;
	T value{};
	explicit operator bool() const { return status == Status::Ok; }
};
template<typename T> using ConstRecord = std::shared_ptr<const T>;

// Owns a lease on the immutable snapshot, not a copy of the vector. Iterators,
// references and pointers are borrowed and require this lease (or another lease
// on the same snapshot) to remain alive. At() returns an independently pinned record.
template<typename T> class ConstArray
{
public:
	typedef typename std::vector<T>::const_iterator const_iterator;
	ConstArray() = default;
	size_t size() const { return records_ ? records_->size() : 0; }
	bool empty() const { return size() == 0; }
	const_iterator begin() const { return Records().begin(); }
	const_iterator end() const { return Records().end(); }
	Result<ConstRecord<T>> At(size_t index) const
	{
		Result<ConstRecord<T>> r;
		r.status = index < size() ? Status::Ok : Status::NotFound;
		if (r) r.value = ConstRecord<T>(records_, &(*records_)[index]);
		return r;
	}
private:
	friend class Snapshot;
	explicit ConstArray(ConstRecord<std::vector<T>> records) : records_(records) {}
	const std::vector<T>& Records() const
	{
		static const std::vector<T> empty;
		return records_ ? *records_ : empty;
	}
	ConstRecord<std::vector<T>> records_;
};

// Metadata capabilities describe record presence, not decoded/resident payloads.
// Evaluation capabilities are separate and Unavailable without a CPU evaluator.
enum class Capability { AssetMetadata, PhysicalEmitters, SunEstimate, SkyEstimate,
	IndirectField, IncomingRadianceField, GeometryRegions, VisibilityData, SourceContributions,
	IndirectSampling, IncomingRadianceSampling, RegionLookup, VisibilityQueries, ContributionSampling, SkyVisibilityQueries, ConditionalFieldSampling, StaticShadowSupport, Count };
struct CapabilityInfo
{
	Status status = Status::Unavailable;
	size_t records = 0;
};

// IDs persist within an asset lineage. Handles additionally bind them to exactly
// one publication, preventing accidental rebinding after a rebuild/map change.
// Generation is process-local, never serialized. Zero ID/generation is invalid.
struct SourceHandle { Id id = 0; uint64_t generation = 0; };
struct RegionHandle { Id id = 0; uint64_t generation = 0; };

struct EmitterFilter
{
	bool byKind = false;
	EmitterKind kind = EmitterKind::Point; // exact kind, also supports extension kinds >=0x10000
	uint32_t states = 3; // bit 0 candidate, bit 1 resolved, bit 2 rejected
	Id region = 0; // zero = all regions
	bool exactCandidatesOnly = false;
	bool sunOnly = false;
	float minimumPhysicalConfidence = -1.0f; // -1 disabled; [0,1] requires known confidence
	float minimumTransportConfidence = -1.0f;
};
class EmitterCursor
{
public:
	EmitterCursor() = default;
	// Ok + pinned const record; NotFound at end (repeatable); Unavailable on default cursor.
	Result<ConstRecord<PhysicalEmitter>> Next();
private:
	friend class Snapshot;
	std::shared_ptr<const detail::SnapshotData> data_;
	EmitterFilter filter_;
	size_t next_ = 0;
};
struct ResolvedSun
{
	ConstRecord<PhysicalEmitter> emitter;
	Vec3 directionToSun; // unit, opposite the emitter's photon-travel forward axis
};
struct SourceQuality
{
	ConstRecord<Source> source; // physical/transport confidence and observation issue flags
	ConstRecord<PhysicalEmitter> emitter; // optional: position sigma and intensity interval
};

// All query positions are finite JA world coordinates. Normals/directions must
// already be unit world vectors (tolerance kUnitTolerance); no silent normalization.
// A zero field handle requests evaluator selection; explicit handles may select
// candidate hypotheses. Automatic selection must not silently resolve alternatives.
struct IrradianceQuery { Vec3 positionWorld, normalWorld; SourceHandle field; };
struct RadianceQuery { Vec3 positionWorld, directionToSource; SourceHandle field; };
struct VisibilityQuery { Vec3 receiverWorld, targetWorld; SourceHandle source; }; // nonzero segment
struct SkyVisibilityQuery { Vec3 positionWorld, directionToSky; SourceHandle source; }; // unit receiver -> sky ray
struct ContributionQuery
{
	Id sampleDomain = 0, sample = 0; // dataset ID + stable sample ID, NOT array index
	SourceHandle source;
	ContributionComponent component = ContributionComponent::Unresolved;
	ContributionQuantity quantity = ContributionQuantity::IrradianceRGB;
	Id dataset = 0, configuration = 0;		  // optional explicit alternative/path selection
	bool allowLegacyRelativeChannels = false; // offline typed channels; never LightingSample
};
struct LightingSample
{
	Id source = 0; // selected field/environment (or contribution source), scoped to this snapshot
	Vec3 rgb; // linear Rec.709 D65 RGB, Lux (irradiance), Nits (radiance), or Relative
	PhotometricUnit unit = PhotometricUnit::Relative;
	Id calibration = 0; // mandatory; no scene scale or exposure has been applied
};
enum class ConditionalQuantity { IncomingRadiance, NormalIrradiance, IrradianceOverPi, LegacyAppearance };
enum class FieldDomain { PhysicallyAuthored, SceneEquivalent, SourceLinearRelative, LegacyAppearance };
enum class FieldOutcome { Complete, Partial };
struct ConditionalFieldDescriptor
{
	Id field=0, dataset=0, calibration=0, configuration=0, alternativeGroup=0;
	ConditionalQuantity quantity=ConditionalQuantity::NormalIrradiance;
	FieldDomain domain=FieldDomain::SourceLinearRelative;
	PhotometricUnit unit=PhotometricUnit::Relative;
	ContributionColour colour=ContributionColour::Unknown;
	bool primariesKnown=false, linearTransferKnown=false;
	std::string profile, encoding, provenance, topology, geometry;
	TransportMembership membership;
	Estimate representationError; // numerical discrepancy, not a calibrated confidence interval
	uint32_t uncertainty=0;
	double geometryVolumeWorldCubed=0, lightingVolumeWorldCubed=0;
	uint64_t requestedProbes=0, availableProbes=0;
};
struct ConditionalFieldQuery
{
	SourceHandle field;
	Id configuration=0;
	Vec3 positionWorld, directionOrNormalWorld=Vec3(0,0,1);
	ConditionalQuantity quantity=ConditionalQuantity::NormalIrradiance;
	bool optIn=false, allowConditionalStaticGeometry=false;
};
struct ConditionalFieldSample
{
	ConstRecord<ConditionalFieldDescriptor> descriptor;
	Vec3 value, lower, upper;
	bool lowerKnown=false, upperKnown=false;
	FieldOutcome outcome=FieldOutcome::Partial;
	Id field=0, cell=0, region=0;
	uint64_t generation=0;
	uint32_t geometryIssues=0, supportCount=0;
	Id support[16]={};
	double weights[16]={};
};
struct VisibilitySample { float transmittance = 0.0f; }; // [0,1] along receiver -> target
enum class ShadowQueryKind { Segment, Source, SkyDirection };
enum class ShadowState { Visible, Blocked, Fractional, Unknown };
struct VisibilityEstimate {
	ShadowState state=ShadowState::Unknown;
	bool estimateKnown=false;
	double value=0,lower=0,upper=1; // bounds concern unresolved sampled rays, not quadrature confidence
};
struct ShadowSupportQuery {
	Id dataset=0,receiver=0,scenario=0;
	SourceHandle source;
	ShadowQueryKind kind=ShadowQueryKind::Source;
	Vec3 receiverWorld,targetOrDirection;
};
struct ShadowSupportSample {
	Id dataset=0,receiver=0,source=0,region=0,scenario=0;
	uint64_t generation=0;
	VisibilityEstimate staticGeometry,scenarioGeometry;
	VisibilityEstimate staticResponse,scenarioResponse; // incident/normal response weighted, not RGB
	bool responseApplicable=false,conditional=false,runtimeUpdateRequired=false;
	bool occluderDistanceKnown=false;
	double nearestStaticOccluderWorld=0,samplingDifference=0,quantizationBound=0;
	uint32_t issues=0,rayIssues=0,coarseSamples=0,referenceSamples=0;
};
struct ContributionSample
{
	LightingSample lighting; // RGB quantities
	float fraction = 0.0f; // dimensionless [0,1] when quantity == AttributionFraction
	Vec3 value, lower, upper; // conditional payload values/bounds, not calibrated confidence
	ContributionQuantity quantity = ContributionQuantity::IrradianceRGB;
	Id dataset = 0, configuration = 0;
	std::string domain;
	uint32_t issues = 0;
	bool lightingSampleAvailable = false; // Rec.709 adapter available; unit may still be Relative
};

class Snapshot
{
public:
	Snapshot() = default; // Missing; does not start any work
	Availability State() const;
	uint64_t Generation() const;
	const std::string& Message() const; // borrowed, valid while this snapshot is retained
	CapabilityInfo GetCapability(Capability capability) const;
	Result<ConstRecord<ReconstructedLightingAsset>> Asset() const;
	Result<EmitterCursor> Emitters(const EmitterFilter& filter = EmitterFilter()) const;
	Result<ResolvedSun> Sun() const; // unique resolved sun with known frame, otherwise Unavailable/Ambiguous
	Result<ConstRecord<EnvironmentLighting>> Sky() const; // unique resolved environment
	Result<ConstArray<EnvironmentLighting>> Environments() const;
	Result<ConstArray<TransportField>> TransportFields() const;
	Result<ConstArray<GeometryRegion>> Regions() const;
	Result<ConstArray<VisibilityReference>> VisibilityData() const;
	Result<ConstArray<ContributionReference>> Contributions() const;
	Result<ConstArray<Diagnostic>> Diagnostics() const;
	Result<SourceHandle> IdentifySource(Id id) const;
	Result<RegionHandle> IdentifyRegion(Id id) const;
	Result<ConstRecord<Source>> GetSource(SourceHandle source) const;
	Result<ConstRecord<PhysicalEmitter>> GetEmitter(SourceHandle source) const;
	Result<ConstRecord<GeometryRegion>> GetRegion(RegionHandle region) const;
	Result<SourceQuality> Quality(SourceHandle source) const;

	// No fallback light evaluation, interpolation, tracing, loading or reconstruction.
	// Default production queries return Unavailable; only prepared CPU evaluators
	// installed by the manager may implement them. Ignore value unless status == Ok.
	// Physically authored Lux/Nits only. Relative/scene-equivalent fields use the
	// explicit conditional query; contribution compatibility semantics are separate.
	Result<LightingSample> SampleIndirect(const IrradianceQuery& query) const;
	Result<LightingSample> SampleIncomingRadiance(const RadianceQuery& query) const;
	Result<ConditionalFieldSample> SampleConditionalField(const ConditionalFieldQuery&) const;
	Result<RegionHandle> LookupRegion(const Vec3& positionWorld) const;
	Result<VisibilitySample> QueryVisibility(const VisibilityQuery& query) const;
	Result<ShadowSupportSample> QueryShadowSupport(const ShadowSupportQuery&) const;
	Result<VisibilitySample> QuerySkyVisibility(const SkyVisibilityQuery& query) const;
	Result<ContributionSample> SampleContribution(const ContributionQuery& query) const;
	WeakSnapshot Weak() const;
private:
	friend class Manager;
	friend class WeakSnapshot;
	explicit Snapshot(std::shared_ptr<const detail::SnapshotData> data) : data_(data) {}
	Status Access() const;
	Status Check(SourceHandle source) const;
	Status Check(RegionHandle region) const;
	template<typename T> Result<ConstArray<T>> Array(const std::vector<T> ReconstructedLightingAsset::*member) const;
	std::shared_ptr<const detail::SnapshotData> data_;
};
class WeakSnapshot
{
public:
	WeakSnapshot() = default;
	Result<Snapshot> Lock() const; // Expired when no strong snapshot/cursor/view/record lease remains
private:
	friend class Snapshot;
	std::weak_ptr<const detail::SnapshotData> data_;
};

} // namespace Rend2LightRecon
