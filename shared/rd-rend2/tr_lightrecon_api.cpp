// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "tr_lightrecon_manager.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace Rend2LightRecon {
namespace detail {
struct SnapshotData
{
	Availability state = Availability::Missing;
	uint64_t generation = 0;
	std::string message;
	ConstRecord<ReconstructedLightingAsset> asset;
	std::shared_ptr<const CpuEvaluator> evaluator;
	std::array<CapabilityInfo, static_cast<size_t>(Capability::Count)> capabilities;
	std::unordered_map<Id, const Source*> sources;
	std::unordered_map<Id, const PhysicalEmitter*> emitters;
	std::unordered_map<Id, const EnvironmentLighting*> environments;
	std::unordered_map<Id, const TransportField*> fields;
	std::unordered_map<Id, const GeometryRegion*> regions;
	std::unordered_map<Id, const Dataset*> datasets;
	std::unordered_map<Id, const Calibration*> calibrations;
	std::vector<const PhysicalEmitter*> resolvedSuns;
	std::vector<const EnvironmentLighting*> resolvedSkies;
};
} // namespace detail
namespace {

uint64_t NewGeneration()
{
	static std::atomic<uint64_t> last(0);
	uint64_t value = last.load(std::memory_order_relaxed);
	while (value != std::numeric_limits<uint64_t>::max())
		if (last.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) return value + 1;
	return 0; // never wrap/reuse a process-local generation
}
Status Access(const std::shared_ptr<const detail::SnapshotData>& data)
{
	if (!data) return Status::Missing;
	switch (data->state)
	{
	case Availability::Ready: case Availability::Partial: return Status::Ok;
	case Availability::Disabled: return Status::Disabled;
	case Availability::Missing: return Status::Missing;
	case Availability::Pending: return Status::Pending;
	case Availability::Corrupt: return Status::Corrupt;
	case Availability::Unloaded: return Status::Unloaded;
	}
	return Status::Corrupt;
}
bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool Unit(const Vec3& v)
{
	return Finite(v) && std::abs(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z - 1.0) <= kUnitTolerance;
}
bool Empty(SourceHandle h) { return h.id == 0 && h.generation == 0; }
bool Threshold(float value) { return value == -1.0f || (std::isfinite(value) && value >= 0.0f && value <= 1.0f); }
bool Matches(const Estimate& confidence, float minimum)
{
	return minimum < 0.0f || (confidence.known && confidence.value >= minimum);
}
template<typename T> Result<T> Failure(Status status) { Result<T> r; r.status = status; return r; }
template<typename T> Result<ConstRecord<T>> Record(const std::shared_ptr<const detail::SnapshotData>& data, const T* record)
{
	Result<ConstRecord<T>> r;
	r.status = record ? Status::Ok : Status::NotFound;
	if (record) r.value = ConstRecord<T>(data, record);
	return r;
}
bool ValidLighting(const detail::SnapshotData& data, const LightingSample& value, PhotometricUnit expected)
{
	if (!Finite(value.rgb) || value.rgb.x < 0.0f || value.rgb.y < 0.0f || value.rgb.z < 0.0f ||
		(value.unit != expected && value.unit != PhotometricUnit::Relative)) return false;
	const auto c = data.calibrations.find(value.calibration);
	return c != data.calibrations.end() && ((value.unit == PhotometricUnit::Relative) ==
		(c->second->kind == CalibrationKind::UncalibratedRelative));
}
bool ValidStatus(Status status)
{
	return status == Status::Ok || status == Status::Unavailable || status == Status::Unsupported ||
		status == Status::NotFound || status == Status::Ambiguous || status == Status::Corrupt;
}
Status EvaluationStatus(const detail::SnapshotData& data, Capability capability)
{
	return data.capabilities[static_cast<size_t>(capability)].status;
}
void Presence(detail::SnapshotData& data, Capability capability, size_t count)
{
	CapabilityInfo& info = data.capabilities[static_cast<size_t>(capability)];
	info.records = count;
	info.status = count ? Status::Ok : Status::Unavailable;
}
bool Index(detail::SnapshotData& data)
{
	const ReconstructedLightingAsset& a = *data.asset;
	for (const PhysicalEmitter& e : a.emitters) { data.sources[e.source.id] = &e.source; data.emitters[e.source.id] = &e; }
	for (const EnvironmentLighting& e : a.environments)
	{
		data.sources[e.source.id] = &e.source; data.environments[e.source.id] = &e;
		if (e.source.state == HypothesisState::Resolved) data.resolvedSkies.push_back(&e);
	}
	for (const TransportField& f : a.transport) { data.sources[f.source.id] = &f.source; data.fields[f.source.id] = &f; }
	for (const GeometryRegion& r : a.regions) data.regions[r.id] = &r;
	for (const Dataset& d : a.datasets) data.datasets[d.id] = &d;
	for (const Calibration& c : a.calibrations) data.calibrations[c.id] = &c;
	for (const SunSource& s : a.suns)
		if (data.emitters.at(s.emitter)->source.state == HypothesisState::Resolved) data.resolvedSuns.push_back(data.emitters.at(s.emitter));
	size_t indirect = 0, radiance = a.environments.size();
	for (const TransportField& f : a.transport)
	{
		if (f.component == TransportComponent::IndirectIrradiance) ++indirect;
		if (f.quantity == FieldQuantity::Radiance) ++radiance;
	}
	Presence(data, Capability::AssetMetadata, 1);
	Presence(data, Capability::PhysicalEmitters, a.emitters.size());
	Presence(data, Capability::SunEstimate, a.suns.size());
	Presence(data, Capability::SkyEstimate, a.environments.size());
	Presence(data, Capability::IndirectField, indirect);
	Presence(data, Capability::IncomingRadianceField, radiance);
	Presence(data, Capability::GeometryRegions, a.regions.size());
	Presence(data, Capability::VisibilityData, a.visibility.size());
	Presence(data, Capability::SourceContributions, a.contributions.size());
	const Capability queries[] = { Capability::IndirectSampling, Capability::IncomingRadianceSampling,
		Capability::RegionLookup, Capability::VisibilityQueries, Capability::ContributionSampling, Capability::SkyVisibilityQueries };
	const Capability metadata[] = { Capability::IndirectField, Capability::IncomingRadianceField,
		Capability::GeometryRegions, Capability::VisibilityData, Capability::SourceContributions, Capability::VisibilityData };
	for (size_t i = 0; i < 6; ++i)
	{
		CapabilityInfo& info = data.capabilities[static_cast<size_t>(queries[i])];
		if (!data.evaluator || !data.capabilities[static_cast<size_t>(metadata[i])].records) continue;
		if (queries[i] == Capability::VisibilityQueries || queries[i] == Capability::SkyVisibilityQueries)
		{
			const VisibilityKind kind = queries[i] == Capability::VisibilityQueries ? VisibilityKind::SegmentTransmittance : VisibilityKind::SkyTransmittance;
			if (std::find_if(a.visibility.begin(), a.visibility.end(), [kind](const VisibilityReference& v) { return v.kind == kind; }) == a.visibility.end()) continue;
		}
		info.status = data.evaluator->Supports(queries[i]);
		if (info.status != Status::Ok && info.status != Status::Unavailable && info.status != Status::Unsupported) return false;
	}
	return true;
}

} // namespace

Status CpuEvaluator::Supports(Capability) const { return Status::Unavailable; }
Status CpuEvaluator::Indirect(const ReconstructedLightingAsset&, const IrradianceQuery&, LightingSample&) const { return Status::Unavailable; }
Status CpuEvaluator::IncomingRadiance(const ReconstructedLightingAsset&, const RadianceQuery&, LightingSample&) const { return Status::Unavailable; }
Status CpuEvaluator::RegionAt(const ReconstructedLightingAsset&, const Vec3&, Id&) const { return Status::Unavailable; }
Status CpuEvaluator::Visibility(const ReconstructedLightingAsset&, const VisibilityQuery&, VisibilitySample&) const { return Status::Unavailable; }
Status CpuEvaluator::SkyVisibility(const ReconstructedLightingAsset&, const SkyVisibilityQuery&, VisibilitySample&) const { return Status::Unavailable; }
Status CpuEvaluator::Contribution(const ReconstructedLightingAsset&, const ContributionQuery&, ContributionSample&) const { return Status::Unavailable; }

Manager::Manager() { SetUnavailable(Availability::Missing); }
Snapshot Manager::Acquire() const { return Snapshot(std::atomic_load_explicit(&current_, std::memory_order_acquire)); }
void Manager::Reset() noexcept
{
	std::shared_ptr<const detail::SnapshotData> empty;
	std::atomic_store_explicit(&current_, empty, std::memory_order_release);
}
Status Manager::SetUnavailable(Availability state, const std::string& message)
{
	if (state == Availability::Ready || state == Availability::Partial ||
		static_cast<unsigned>(state) > static_cast<unsigned>(Availability::Unloaded)) return Status::InvalidArgument;
	std::shared_ptr<detail::SnapshotData> data = std::make_shared<detail::SnapshotData>();
	data->generation = NewGeneration();
	if (!data->generation) return Status::Unavailable;
	data->state = state; data->message = message;
	std::shared_ptr<const detail::SnapshotData> published = data;
	std::atomic_store_explicit(&current_, published, std::memory_order_release);
	return Status::Ok;
}
Status Manager::Publish(const ReconstructedLightingAsset& asset, Completeness completeness, std::shared_ptr<const CpuEvaluator> evaluator)
{
	if (completeness != Completeness::Ready && completeness != Completeness::Partial) return Status::InvalidArgument;
	std::shared_ptr<detail::SnapshotData> data = std::make_shared<detail::SnapshotData>();
	data->asset = std::make_shared<const ReconstructedLightingAsset>(asset);
	if (!Validate(*data->asset, &data->message))
	{
		const Status s = SetUnavailable(Availability::Corrupt, data->message);
		return s == Status::Ok ? Status::Corrupt : s;
	}
	data->state = completeness == Completeness::Ready ? Availability::Ready : Availability::Partial;
	data->evaluator = evaluator;
	if (!Index(*data))
	{
		const Status s = SetUnavailable(Availability::Corrupt, "CPU evaluator declared an invalid capability status");
		return s == Status::Ok ? Status::Corrupt : s;
	}
	data->generation = NewGeneration();
	if (!data->generation) return Status::Unavailable;
	std::shared_ptr<const detail::SnapshotData> published = data;
	std::atomic_store_explicit(&current_, published, std::memory_order_release);
	return Status::Ok;
}

Availability Snapshot::State() const { return data_ ? data_->state : Availability::Missing; }
uint64_t Snapshot::Generation() const { return data_ ? data_->generation : 0; }
const std::string& Snapshot::Message() const
{
	static const std::string empty;
	return data_ ? data_->message : empty;
}
Status Snapshot::Access() const { return Rend2LightRecon::Access(data_); }
CapabilityInfo Snapshot::GetCapability(Capability capability) const
{
	CapabilityInfo r;
	if (static_cast<unsigned>(capability) >= static_cast<unsigned>(Capability::Count)) r.status = Status::InvalidArgument;
	else if (Access() != Status::Ok) r.status = Access();
	else r = data_->capabilities[static_cast<size_t>(capability)];
	return r;
}
Result<ConstRecord<ReconstructedLightingAsset>> Snapshot::Asset() const
{
	if (Access() != Status::Ok) return Failure<ConstRecord<ReconstructedLightingAsset>>(Access());
	return Record(data_, data_->asset.get());
}
template<typename T> Result<ConstArray<T>> Snapshot::Array(const std::vector<T> ReconstructedLightingAsset::*member) const
{
	if (Access() != Status::Ok) return Failure<ConstArray<T>>(Access());
	Result<ConstArray<T>> r; r.status = Status::Ok;
	r.value = ConstArray<T>(ConstRecord<std::vector<T>>(data_, &(data_->asset.get()->*member)));
	return r;
}
Result<ConstArray<EnvironmentLighting>> Snapshot::Environments() const { return Array(&ReconstructedLightingAsset::environments); }
Result<ConstArray<TransportField>> Snapshot::TransportFields() const { return Array(&ReconstructedLightingAsset::transport); }
Result<ConstArray<GeometryRegion>> Snapshot::Regions() const { return Array(&ReconstructedLightingAsset::regions); }
Result<ConstArray<VisibilityReference>> Snapshot::VisibilityData() const { return Array(&ReconstructedLightingAsset::visibility); }
Result<ConstArray<ContributionReference>> Snapshot::Contributions() const { return Array(&ReconstructedLightingAsset::contributions); }
Result<ConstArray<Diagnostic>> Snapshot::Diagnostics() const { return Array(&ReconstructedLightingAsset::diagnostics); }

Result<EmitterCursor> Snapshot::Emitters(const EmitterFilter& filter) const
{
	const uint32_t kind = static_cast<uint32_t>(filter.kind);
	if ((filter.states & ~7u) || !Threshold(filter.minimumPhysicalConfidence) || !Threshold(filter.minimumTransportConfidence) ||
		(filter.byKind && (kind < 1 || (kind > 5 && kind < 0x10000)))) return Failure<EmitterCursor>(Status::InvalidArgument);
	if (Access() != Status::Ok) return Failure<EmitterCursor>(Access());
	if (filter.region && !data_->regions.count(filter.region)) return Failure<EmitterCursor>(Status::NotFound);
	Result<EmitterCursor> r; r.status = Status::Ok; r.value.data_ = data_; r.value.filter_ = filter;
	return r;
}
Result<ConstRecord<PhysicalEmitter>> EmitterCursor::Next()
{
	if (!data_) return Failure<ConstRecord<PhysicalEmitter>>(Status::Unavailable);
	const auto& a = *data_->asset;
	while (next_ < a.emitters.size())
	{
		const PhysicalEmitter& e = a.emitters[next_++];
		const Source& s = e.source;
		if ((filter_.byKind && e.kind != filter_.kind) || !(filter_.states & (1u << static_cast<unsigned>(s.state))) ||
			(filter_.region && std::find(s.regions.begin(), s.regions.end(), filter_.region) == s.regions.end()) ||
			(filter_.exactCandidatesOnly && s.evidence.exactLight != Suitability::Candidate) ||
			!Matches(s.evidence.physicalEmitterConfidence, filter_.minimumPhysicalConfidence) ||
			!Matches(s.evidence.transportConfidence, filter_.minimumTransportConfidence)) continue;
		if (filter_.sunOnly && std::find_if(a.suns.begin(), a.suns.end(), [&s](const SunSource& sun) { return sun.emitter == s.id; }) == a.suns.end()) continue;
		return Record(data_, &e);
	}
	return Failure<ConstRecord<PhysicalEmitter>>(Status::NotFound);
}
Result<ResolvedSun> Snapshot::Sun() const
{
	if (Access() != Status::Ok) return Failure<ResolvedSun>(Access());
	if (data_->resolvedSuns.empty()) return Failure<ResolvedSun>(Status::Unavailable);
	if (data_->resolvedSuns.size() != 1) return Failure<ResolvedSun>(Status::Ambiguous);
	const PhysicalEmitter& e = *data_->resolvedSuns[0];
	if (!e.frame.known) return Failure<ResolvedSun>(Status::Unavailable);
	Result<ResolvedSun> r; r.status = Status::Ok; r.value.emitter = ConstRecord<PhysicalEmitter>(data_, &e);
	r.value.directionToSun = Vec3(-e.frame.forward.x, -e.frame.forward.y, -e.frame.forward.z);
	return r;
}
Result<ConstRecord<EnvironmentLighting>> Snapshot::Sky() const
{
	if (Access() != Status::Ok) return Failure<ConstRecord<EnvironmentLighting>>(Access());
	if (data_->resolvedSkies.empty()) return Failure<ConstRecord<EnvironmentLighting>>(Status::Unavailable);
	if (data_->resolvedSkies.size() != 1) return Failure<ConstRecord<EnvironmentLighting>>(Status::Ambiguous);
	return Record(data_, data_->resolvedSkies[0]);
}
Result<SourceHandle> Snapshot::IdentifySource(Id id) const
{
	if (!id) return Failure<SourceHandle>(Status::InvalidArgument);
	if (Access() != Status::Ok) return Failure<SourceHandle>(Access());
	if (!data_->sources.count(id)) return Failure<SourceHandle>(Status::NotFound);
	Result<SourceHandle> r; r.status = Status::Ok; r.value.id = id; r.value.generation = Generation(); return r;
}
Result<RegionHandle> Snapshot::IdentifyRegion(Id id) const
{
	if (!id) return Failure<RegionHandle>(Status::InvalidArgument);
	if (Access() != Status::Ok) return Failure<RegionHandle>(Access());
	if (!data_->regions.count(id)) return Failure<RegionHandle>(Status::NotFound);
	Result<RegionHandle> r; r.status = Status::Ok; r.value.id = id; r.value.generation = Generation(); return r;
}
Status Snapshot::Check(SourceHandle source) const
{
	if (!source.id || !source.generation) return Status::InvalidArgument;
	if (Access() != Status::Ok) return Access();
	if (source.generation != Generation()) return Status::StaleHandle;
	return data_->sources.count(source.id) ? Status::Ok : Status::NotFound;
}
Status Snapshot::Check(RegionHandle region) const
{
	if (!region.id || !region.generation) return Status::InvalidArgument;
	if (Access() != Status::Ok) return Access();
	if (region.generation != Generation()) return Status::StaleHandle;
	return data_->regions.count(region.id) ? Status::Ok : Status::NotFound;
}
Result<ConstRecord<Source>> Snapshot::GetSource(SourceHandle source) const
{
	const Status s = Check(source); if (s != Status::Ok) return Failure<ConstRecord<Source>>(s);
	return Record(data_, data_->sources.at(source.id));
}
Result<ConstRecord<PhysicalEmitter>> Snapshot::GetEmitter(SourceHandle source) const
{
	const Status s = Check(source); if (s != Status::Ok) return Failure<ConstRecord<PhysicalEmitter>>(s);
	const auto e = data_->emitters.find(source.id); return Record(data_, e == data_->emitters.end() ? nullptr : e->second);
}
Result<ConstRecord<GeometryRegion>> Snapshot::GetRegion(RegionHandle region) const
{
	const Status s = Check(region); if (s != Status::Ok) return Failure<ConstRecord<GeometryRegion>>(s);
	return Record(data_, data_->regions.at(region.id));
}
Result<SourceQuality> Snapshot::Quality(SourceHandle source) const
{
	auto s = GetSource(source); if (!s) return Failure<SourceQuality>(s.status);
	Result<SourceQuality> r; r.status = Status::Ok; r.value.source = s.value;
	const auto e = data_->emitters.find(source.id);
	if (e != data_->emitters.end()) r.value.emitter = ConstRecord<PhysicalEmitter>(data_, e->second);
	return r;
}

Result<LightingSample> Snapshot::SampleIndirect(const IrradianceQuery& query) const
{
	if (!Finite(query.positionWorld) || !Unit(query.normalWorld)) return Failure<LightingSample>(Status::InvalidArgument);
	if (Access() != Status::Ok) return Failure<LightingSample>(Access());
	const TransportField* field = nullptr;
	if (!Empty(query.field))
	{
		const Status s = Check(query.field); if (s != Status::Ok) return Failure<LightingSample>(s);
		const auto f = data_->fields.find(query.field.id);
		if (f == data_->fields.end() || f->second->component != TransportComponent::IndirectIrradiance) return Failure<LightingSample>(Status::InvalidArgument);
		field = f->second;
	}
	const Status available = EvaluationStatus(*data_, Capability::IndirectSampling);
	if (available != Status::Ok) return Failure<LightingSample>(available);
	Result<LightingSample> r; r.status = data_->evaluator->Indirect(*data_->asset, query, r.value);
	if (r && !field)
	{
		const auto f = data_->fields.find(r.value.source);
		if (f == data_->fields.end() || f->second->component != TransportComponent::IndirectIrradiance ||
			f->second->source.state != HypothesisState::Resolved) return Failure<LightingSample>(Status::Corrupt);
		field = f->second;
	}
	if (!ValidStatus(r.status) || (r && (!ValidLighting(*data_, r.value, PhotometricUnit::Lux) ||
		field->source.id != r.value.source || field->unit != r.value.unit || field->calibration != r.value.calibration))) return Failure<LightingSample>(Status::Corrupt);
	return r ? r : Failure<LightingSample>(r.status);
}
Result<LightingSample> Snapshot::SampleIncomingRadiance(const RadianceQuery& query) const
{
	if (!Finite(query.positionWorld) || !Unit(query.directionToSource)) return Failure<LightingSample>(Status::InvalidArgument);
	if (Access() != Status::Ok) return Failure<LightingSample>(Access());
	PhotometricUnit unit = PhotometricUnit::Relative; Id calibration = 0;
	if (!Empty(query.field))
	{
		const Status s = Check(query.field); if (s != Status::Ok) return Failure<LightingSample>(s);
		const auto f = data_->fields.find(query.field.id); const auto e = data_->environments.find(query.field.id);
		if (f != data_->fields.end() && f->second->quantity == FieldQuantity::Radiance) { unit = f->second->unit; calibration = f->second->calibration; }
		else if (e != data_->environments.end()) { unit = e->second->meanLuminance.unit; calibration = e->second->meanLuminance.calibration; }
		else return Failure<LightingSample>(Status::InvalidArgument);
	}
	const Status available = EvaluationStatus(*data_, Capability::IncomingRadianceSampling);
	if (available != Status::Ok) return Failure<LightingSample>(available);
	Result<LightingSample> r; r.status = data_->evaluator->IncomingRadiance(*data_->asset, query, r.value);
	if (r && Empty(query.field))
	{
		const auto f = data_->fields.find(r.value.source); const auto e = data_->environments.find(r.value.source);
		if (f != data_->fields.end() && f->second->quantity == FieldQuantity::Radiance && f->second->source.state == HypothesisState::Resolved)
		{ unit = f->second->unit; calibration = f->second->calibration; }
		else if (e != data_->environments.end() && e->second->source.state == HypothesisState::Resolved)
		{ unit = e->second->meanLuminance.unit; calibration = e->second->meanLuminance.calibration; }
		else return Failure<LightingSample>(Status::Corrupt);
	}
	if (!ValidStatus(r.status) || (r && (!ValidLighting(*data_, r.value, PhotometricUnit::Nits) ||
		unit != r.value.unit || calibration != r.value.calibration ||
		(!Empty(query.field) && query.field.id != r.value.source)))) return Failure<LightingSample>(Status::Corrupt);
	return r ? r : Failure<LightingSample>(r.status);
}
Result<RegionHandle> Snapshot::LookupRegion(const Vec3& positionWorld) const
{
	if (!Finite(positionWorld)) return Failure<RegionHandle>(Status::InvalidArgument);
	if (Access() != Status::Ok) return Failure<RegionHandle>(Access());
	const Status available = EvaluationStatus(*data_, Capability::RegionLookup);
	if (available != Status::Ok) return Failure<RegionHandle>(available);
	Id id = 0; const Status s = data_->evaluator->RegionAt(*data_->asset, positionWorld, id);
	if (!ValidStatus(s)) return Failure<RegionHandle>(Status::Corrupt);
	if (s != Status::Ok) return Failure<RegionHandle>(s);
	const auto r = IdentifyRegion(id);
	return r ? r : Failure<RegionHandle>(Status::Corrupt);
}
Result<VisibilitySample> Snapshot::QueryVisibility(const VisibilityQuery& query) const
{
	if (!Finite(query.receiverWorld) || !Finite(query.targetWorld) ||
		(query.receiverWorld.x == query.targetWorld.x && query.receiverWorld.y == query.targetWorld.y && query.receiverWorld.z == query.targetWorld.z))
		return Failure<VisibilitySample>(Status::InvalidArgument);
	if (Access() != Status::Ok) return Failure<VisibilitySample>(Access());
	if (!Empty(query.source)) { const Status s = Check(query.source); if (s != Status::Ok) return Failure<VisibilitySample>(s); }
	const Status available = EvaluationStatus(*data_, Capability::VisibilityQueries);
	if (available != Status::Ok) return Failure<VisibilitySample>(available);
	Result<VisibilitySample> r; r.status = data_->evaluator->Visibility(*data_->asset, query, r.value);
	if (!ValidStatus(r.status) || (r && (!std::isfinite(r.value.transmittance) || r.value.transmittance < 0.0f || r.value.transmittance > 1.0f)))
		return Failure<VisibilitySample>(Status::Corrupt);
	return r ? r : Failure<VisibilitySample>(r.status);
}
Result<ContributionSample> Snapshot::SampleContribution(const ContributionQuery& query) const
{
	if (!query.sampleDomain || !query.sample || static_cast<unsigned>(query.component) > static_cast<unsigned>(ContributionComponent::Unresolved) ||
		static_cast<unsigned>(query.quantity) > static_cast<unsigned>(ContributionQuantity::LegacyRelativeChannels)) return Failure<ContributionSample>(Status::InvalidArgument);
	if(query.quantity==ContributionQuantity::LegacyRelativeChannels)return Failure<ContributionSample>(Status::Unsupported); // offline diagnostic channels, never linear Rec.709 LightingSample
	const Status s = Check(query.source); if (s != Status::Ok) return Failure<ContributionSample>(s);
	const auto d = data_->datasets.find(query.sampleDomain);
	if (d == data_->datasets.end() || d->second->purpose != DatasetPurpose::SampleDomain) return Failure<ContributionSample>(Status::InvalidArgument);
	const ContributionReference* reference = nullptr;
	for (const ContributionReference& c : data_->asset->contributions)
		if (c.sampleDomain == query.sampleDomain && c.component == query.component && c.quantity == query.quantity &&
			std::find(c.sources.begin(), c.sources.end(), query.source.id) != c.sources.end())
		{
			if (reference) return Failure<ContributionSample>(Status::Ambiguous);
			reference = &c;
		}
	if (!reference) return Failure<ContributionSample>(Status::Unavailable);
	const Status available = EvaluationStatus(*data_, Capability::ContributionSampling);
	if (available != Status::Ok) return Failure<ContributionSample>(available);
	Result<ContributionSample> r; r.status = data_->evaluator->Contribution(*data_->asset, query, r.value);
	bool valid = true;
	if (r && query.quantity == ContributionQuantity::AttributionFraction)
		valid = std::isfinite(r.value.fraction) && r.value.fraction >= 0.0f && r.value.fraction <= 1.0f;
	else if (r)
		valid = ValidLighting(*data_, r.value.lighting, query.quantity == ContributionQuantity::IrradianceRGB ? PhotometricUnit::Lux : PhotometricUnit::Nits) &&
			r.value.lighting.unit == reference->unit && r.value.lighting.calibration == reference->calibration && r.value.lighting.source == query.source.id;
	if (!ValidStatus(r.status) || !valid) return Failure<ContributionSample>(Status::Corrupt);
	return r ? r : Failure<ContributionSample>(r.status);
}
Result<VisibilitySample> Snapshot::QuerySkyVisibility(const SkyVisibilityQuery& query) const
{
	if (!Finite(query.positionWorld) || !Unit(query.directionToSky)) return Failure<VisibilitySample>(Status::InvalidArgument);
	if (Access() != Status::Ok) return Failure<VisibilitySample>(Access());
	if (!Empty(query.source))
	{
		const Status s = Check(query.source); if (s != Status::Ok) return Failure<VisibilitySample>(s);
		const auto e = data_->emitters.find(query.source.id);
		if (!data_->environments.count(query.source.id) && (e == data_->emitters.end() || e->second->kind != EmitterKind::Directional))
			return Failure<VisibilitySample>(Status::InvalidArgument);
	}
	const Status available = EvaluationStatus(*data_, Capability::SkyVisibilityQueries);
	if (available != Status::Ok) return Failure<VisibilitySample>(available);
	Result<VisibilitySample> r; r.status = data_->evaluator->SkyVisibility(*data_->asset, query, r.value);
	if (!ValidStatus(r.status) || (r && (!std::isfinite(r.value.transmittance) || r.value.transmittance < 0.0f || r.value.transmittance > 1.0f)))
		return Failure<VisibilitySample>(Status::Corrupt);
	return r ? r : Failure<VisibilitySample>(r.status);
}
WeakSnapshot Snapshot::Weak() const { WeakSnapshot r; r.data_ = data_; return r; }
Result<Snapshot> WeakSnapshot::Lock() const
{
	const auto data = data_.lock();
	if (!data) return Failure<Snapshot>(Status::Expired);
	Result<Snapshot> r; r.status = Status::Ok; r.value = Snapshot(data); return r;
}

} // namespace Rend2LightRecon
