// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "control.h"
#include <exception>

namespace Rend2LightRecon {
namespace {
bool SupportedFailure(Status s)
{
	return s == Status::Missing || s == Status::Unavailable || s == Status::Unsupported || s == Status::Corrupt;
}
Status Attempt(BuildBackend& backend, bool cache, Manager& owner, std::string& message)
{
	BuildProduct product;
	Status s;
	try { s = cache ? backend.LoadCache(product) : backend.Build(product); }
	catch (const std::exception& e) { message = e.what(); return Status::Corrupt; }
	catch (...) { message = "Build/load provider threw an exception"; return Status::Corrupt; }
	message = product.message;
	if (s == Status::Ok)
	{
		if (!product.asset) { message = "Provider returned Ok without an asset"; return Status::Corrupt; }
		if (product.completeness != Completeness::Ready && product.completeness != Completeness::Partial)
		{ message = "Invalid provider completeness"; return Status::Corrupt; }
		try { s = owner.Publish(*product.asset, product.completeness, product.evaluator); }
		catch (...) { message = "Asset validation/publication failed"; return Status::Corrupt; }
		if (s != Status::Ok) message = owner.Acquire().Message();
	}
	else if (!SupportedFailure(s)) { message = "Invalid build/load provider status"; s = Status::Corrupt; }
	return s;
}
bool UsableEmitter(const PhysicalEmitter& e)
{
	// A Snapshot is already validated. Exact-light suitability carries the
	// canonical geometry/photometry invariants; do not duplicate type-specific
	// validation here or infer a promotion/confidence threshold in a control.
	return e.source.state == HypothesisState::Resolved &&
		e.source.evidence.exactLight == Suitability::Candidate;
}
Status EmitterCapability(const Snapshot& snapshot, bool areaOnly)
{
	const Status s = snapshot.GetCapability(Capability::PhysicalEmitters).status;
	if (s != Status::Ok) return s;
	auto cursor = snapshot.Emitters();
	if (!cursor) return cursor.status;
	for (auto e = cursor.value.Next(); e; e = cursor.value.Next())
		if (UsableEmitter(*e.value) && (!areaOnly || e.value->kind == EmitterKind::Rectangle || e.value->kind == EmitterKind::Tube))
			return Status::Ok;
	return Status::Unavailable;
}
Status ConsumerCapability(const Snapshot& snapshot, Consumer consumer)
{
	switch (consumer)
	{
	case Consumer::Fog: case Consumer::Specular: case Consumer::Particles:
		return snapshot.GetCapability(Capability::IncomingRadianceSampling).status;
	case Consumer::Entities: case Consumer::Indirect:
		return snapshot.GetCapability(Capability::IndirectSampling).status;
	case Consumer::WorldDiffuse: return EmitterCapability(snapshot, false);
	case Consumer::Area: return EmitterCapability(snapshot, true);
	case Consumer::Shadows:
	{
		const Status s = EmitterCapability(snapshot, false);
		return s == Status::Ok ? snapshot.GetCapability(Capability::VisibilityQueries).status : s;
	}
	case Consumer::Sun:
	{
		const auto sun = snapshot.Sun();
		return sun ? (UsableEmitter(*sun.value.emitter) ? Status::Ok : Status::Unavailable) : sun.status;
	}
	case Consumer::Environment:
	{
		const Status s = snapshot.Sky().status;
		return s == Status::Ok ? snapshot.GetCapability(Capability::IncomingRadianceSampling).status : s;
	}
	default: return Status::InvalidArgument;
	}
}
} // namespace

BuildReport ApplyBuildPolicy(BuildPolicy policy, Manager& owner, BuildBackend* backend)
{
	BuildReport r; r.policy = policy;
	if (policy < BuildPolicy::Disabled || policy > BuildPolicy::ForceRebuild)
	{
		r.status = Status::InvalidArgument; r.message = "Invalid reconstruction build policy";
		owner.SetUnavailable(Availability::Corrupt, r.message); return r;
	}
	if (policy == BuildPolicy::Disabled)
	{
		r.status = Status::Disabled; r.message = "Canonical asset build/load disabled at map load";
		owner.SetUnavailable(Availability::Disabled, r.message); return r;
	}
	owner.SetUnavailable(Availability::Pending, "Applying canonical asset build/load policy");
	if (!backend)
	{
		r.status = Status::Unavailable;
		r.message = policy == BuildPolicy::CacheOnly ? "No canonical cache loader is installed; cache-only forbids building" :
			policy == BuildPolicy::ForceRebuild ? "No canonical reconstruction algorithm is installed; force rebuild cannot produce an asset" :
			"No canonical cache loader or reconstruction algorithm is installed";
	}
	else
	{
		if (policy != BuildPolicy::ForceRebuild)
		{
			r.cacheCalled = true; r.cacheStatus = Attempt(*backend, true, owner, r.message); r.status = r.cacheStatus;
			if (r.status == Status::Ok) return r;
		}
		if (policy == BuildPolicy::CacheOrBuild || policy == BuildPolicy::ForceRebuild)
		{
			r.buildCalled = true; r.buildStatus = Attempt(*backend, false, owner, r.message); r.status = r.buildStatus;
			if (r.status == Status::Ok) return r;
		}
	}
	if (r.message.empty()) r.message = "Canonical asset unavailable: " + std::string(StatusName(r.status));
	owner.SetUnavailable(r.status == Status::Corrupt ? Availability::Corrupt : Availability::Missing, r.message);
	return r;
}

ConsumerState EvaluateConsumer(const Snapshot& snapshot, const UseSettings& settings, Consumer consumer, FeatureSupport feature)
{
	ConsumerState r; r.master = settings.master;
	if (consumer < Consumer::Fog || consumer >= Consumer::Count) return r;
	r.requested = settings.requested[static_cast<size_t>(consumer)];
	r.assetStatus = snapshot.GetCapability(Capability::AssetMetadata).status;
	r.assetLoaded = r.assetStatus == Status::Ok;
	r.capabilityStatus = ConsumerCapability(snapshot, consumer);
	r.capabilityPresent = r.capabilityStatus == Status::Ok;
	r.featureAvailable = feature.available;
	if (!r.requested) r.reason = GateReason::NotRequested;
	else if (!r.master) r.reason = GateReason::MasterDisabled;
	else if (!r.assetLoaded) r.reason = GateReason::AssetUnavailable;
	else if (!r.capabilityPresent) r.reason = GateReason::CapabilityUnavailable;
	else if (!r.featureAvailable) r.reason = GateReason::FeatureUnavailable;
	else { r.reason = GateReason::Enabled; r.effective = true; }
	return r;
}

const char* ConsumerCvar(Consumer consumer)
{
	switch (consumer)
	{
#define LIGHTRECON_CONTROL(name, maximum, description)
#define LIGHTRECON_CONSUMER(name, kind, description) case Consumer::kind: return #name;
#include "../tr_lightrecon_cvars.inc"
#undef LIGHTRECON_CONSUMER
#undef LIGHTRECON_CONTROL
	default: return "invalid consumer";
	}
}
const char* GateReasonName(GateReason reason)
{
	switch (reason)
	{
	case GateReason::Enabled: return "enabled";
	case GateReason::InvalidConsumer: return "invalid consumer";
	case GateReason::NotRequested: return "consumer off";
	case GateReason::MasterDisabled: return "master off";
	case GateReason::AssetUnavailable: return "asset unavailable";
	case GateReason::CapabilityUnavailable: return "required lighting capability unavailable";
	case GateReason::FeatureUnavailable: return "renderer feature/adapter unavailable";
	default: return "invalid reason";
	}
}
const char* StatusName(Status s)
{
#define LIGHTRECON_STATUS(name) case Status::name: return #name;
	switch (s)
	{
	LIGHTRECON_STATUS(Ok) LIGHTRECON_STATUS(Disabled) LIGHTRECON_STATUS(Missing)
	LIGHTRECON_STATUS(Pending) LIGHTRECON_STATUS(Corrupt) LIGHTRECON_STATUS(Unloaded)
	LIGHTRECON_STATUS(Unavailable) LIGHTRECON_STATUS(Unsupported) LIGHTRECON_STATUS(InvalidArgument)
	LIGHTRECON_STATUS(NotFound) LIGHTRECON_STATUS(Ambiguous) LIGHTRECON_STATUS(StaleHandle) LIGHTRECON_STATUS(Expired)
	default: return "InvalidStatus";
	}
#undef LIGHTRECON_STATUS
}
const char* AvailabilityName(Availability s)
{
#define LIGHTRECON_AVAILABILITY(name) case Availability::name: return #name;
	switch (s)
	{
	LIGHTRECON_AVAILABILITY(Disabled) LIGHTRECON_AVAILABILITY(Missing) LIGHTRECON_AVAILABILITY(Pending)
	LIGHTRECON_AVAILABILITY(Ready) LIGHTRECON_AVAILABILITY(Partial) LIGHTRECON_AVAILABILITY(Corrupt) LIGHTRECON_AVAILABILITY(Unloaded)
	default: return "InvalidAvailability";
	}
#undef LIGHTRECON_AVAILABILITY
}
} // namespace Rend2LightRecon
